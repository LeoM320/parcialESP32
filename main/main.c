/* main.c: tu programa.
 *
 * Las librerias solo ponen en marcha cosas (GPIO, ADC, NVS, WiFi, servidor web,
 * sockets TCP, microrl, UNER). Todo lo demas esta aca, a la vista:
 *
 *   - task_tcp:    atiende un cliente TCP (MobaXterm, Telnet) con microrl.
 *   - task_uart:   la misma consola por UART            (si CONSOLA_UART es 1).
 *   - task_uner:   cliente TCP que habla protocolo UNER  (si CLIENTE_UNER es 1).
 *   - execute():   los comandos que llegan por TCP (y UART).
 *   - handlers web: lo que hace cada direccion de la pagina (/wifi, /destino).
 *
 * Ejemplos completos de cada modulo, con datos de todos los tipos:
 *   practicas/implementacionUartMicrorl.c y practicas/implementacionTcpUner.c
 *   practicas/ejemploNvs.c (guardar datos)   practicas/ejemploDoxygen.h (comentarios)
 *
 * CONSOLA TCP y UNER NO SE MEZCLAN: son dos sockets distintos.
 *   - Consola: el ESP es SERVIDOR, escucha en SU puerto PUERTO_TCP (1234). MobaXterm se
 *     conecta a IP_del_ESP:1234 y lo que llega va a microrl.
 *   - UNER: el ESP es CLIENTE, se conecta a destino IP:puerto (la PC, el emulador). Lo que
 *     llega por ESE socket va a uner_rx_byte(), nunca a microrl.
 *   Aunque el destino use tambien el 1234 no choca: una conexion es "PC -> ESP:1234" y la
 *   otra "ESP -> PC:1234"; cada una tiene su socket, su tarea y su buffer.
 *
 * ======================= PRIORIDADES DE LAS TAREAS =======================
 * Numero mas alto = se ejecuta primero. Si dos estan listas, gana la de mas prioridad; si
 * empatan, se turnan. Regla: lo que tiene plazo (tiempo real) arriba, lo humano abajo.
 *
 *   +6  Eventos urgentes avisados desde una ISR (parada de emergencia, fin de carrera,
 *       alarma): la ISR solo hace xTaskNotifyFromISR / xQueueSendFromISR y la tarea actua.
 *   +5  Control con periodo fijo (leer sensor -> decidir -> actuar, vTaskDelayUntil),
 *       PWM/motor/valvula que siguen un setpoint.
 *       (el servidor web de ESP-IDF corre en +5: un control en +5 se turna con el)
 *   +4  Adquisicion / muestreo periodico (potes, ADC, promedios) que alimenta una cola.
 *   +3  Protocolo con otro equipo (task_uner): tiene latido y timeouts que cumplir.
 *   +2  Consolas (task_tcp, task_uart): las usa una persona, 100 ms de demora no se notan.
 *   +1  Lo que no apura: parpadeo de un LED de "vivo", estadisticas, logs periodicos.
 *
 * Cuidados:
 *   - TODA tarea tiene que bloquearse en su while(1) (vTaskDelay, xQueueReceive, recv con
 *     timeout, ulTaskNotifyTake). Una que gira sin bloquearse deja sin CPU a las de menor
 *     prioridad (y salta el watchdog si es la de mas baja).
 *   - No pasar de +10: lwIP (18), esp_timer (22) y WiFi (23) son del sistema y tienen que ganar.
 *   - Subir la prioridad no arregla un problema de datos compartidos: eso es con mutex.
 *   - Stack: 2048 alcanza para tareas simples; 4096 si usa snprintf, cJSON o microrl.
 */

/* ======================= Modulos (0 = apagado, 1 = prendido) ======================= */

#define CONSOLA_UART	1	// 1: la consola microrl tambien por UART (ver UART_CONSOLA abajo)
#define CLIENTE_UNER	1	// 1: el ESP se conecta a destino IP:puerto y habla UNER
							//    (destino en wifi.html, por consola o guardado en NVS)

/* ======================= Includes ======================= */

#include <stdint.h>				// uint8_t, uint32_t...
#include <stdbool.h>			// bool, true, false
#include <inttypes.h>			// PRIu32 (printf de uint32_t)
#include <stdio.h>				// printf(), snprintf()
#include <stdlib.h>				// atoi(), atof(), strtol(), strtof()
#include <string.h>				// strcmp(), strlen(), memcpy(), memcmp()
#include <ctype.h>				// isdigit(), toupper()...
#include <math.h>				// fabsf(), roundf(), sqrtf()...
#include <unistd.h>				// close()
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"			// ESP_LOGI/W/E: mensajes con nombre y color en el monitor
#include "esp_timer.h"			// esp_timer_get_time(): microsegundos desde el arranque
#include "esp_system.h"			// esp_restart(): reiniciar la placa
#include "esp_netif.h"			// esp_netif_get_ip_info(): la IP actual
#include "driver/uart.h"		// uart_driver_install, uart_read_bytes, uart_write_bytes
#include "freertos/FreeRTOS.h"	// siempre primero de los de FreeRTOS
#include "freertos/task.h"		// xTaskCreate(), vTaskDelay(), task notify
#include "freertos/queue.h"		// colas
#include "freertos/semphr.h"		// mutex y semaforos
#include "freertos/event_groups.h"	// event groups
#include "freertos/timers.h"		// software timers
#include "lwip/sockets.h"		// inet_pton(): validar una IP en texto
#include "hardware.h"			// pines: leds[], switches[], pots[], LED_COUNT...
#include "http_parser.h"
#include "iodigital.h"			// salida_init/set/get, entrada_init/get, entrada_interrupcion
#include "ianalogica.h"			// analogica_init/get
#include "pwm.h"				// pwm_init, pwm_set_duty, pwm_set_pulso_us
#include "rgb.h"				// rgb_init, rgb_set (LED RGB de la placa)
#include "temporizador.h"		// temporizador_init (interrupcion periodica)
#include "memoria.h"			// NVS: memoria_init, memoria_get/set_str/u16/i32/blob, memoria_borrar
#include "wifi.h"				// wifi_init, wifi_sta_conectar
#include "web.h"				// web_init, web_parametro, web_leer_cuerpo, web_campo
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_conectar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe
#include "uner.h"				// uner_armar, uner_rx_byte, uner_escribir16/leer16...
#include "cJSON.h"				// JSON: armar respuestas y leer pedidos de la pagina

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

/* Consola UART (CONSOLA_UART 1): elegir UNA de las dos opciones (comentar la otra) */

/* A) UART0 por el USB de la placa (COM6): la misma del monitor y de los printf */
#define UART_CONSOLA	UART_NUM_0
#define UART_TX_PIN		UART_PIN_NO_CHANGE	// UART0 ya tiene sus pines (43 TX, 44 RX)
#define UART_RX_PIN		UART_PIN_NO_CHANGE

/* B) UART1 con un adaptador CP2102 aparte (otro COM, sin printf mezclados).
 *    Cableado: ESP GPIO17 (TX) -> RXD del CP2102
 *              ESP GPIO18 (RX) <- TXD del CP2102
 *              GND             -- GND   (NO conectar el 5V ni el 3V3 del CP2102) */
//#define UART_CONSOLA	UART_NUM_1
//#define UART_TX_PIN		17
//#define UART_RX_PIN		18

#define UART_BAUDIOS	115200		// el mismo en la terminal (MobaXterm -> Serial)
#define UART_BUF_RX		256			// buffer del driver (tiene que ser mayor a 128, el FIFO)

/* Cliente UNER (CLIENTE_UNER 1) */
#define UNER_ESPERA_MS		50			// timeout de tcp_recibir(): cada cuanto da vueltas el lazo
#define UNER_REINTENTO_MS	1000		// espera antes de volver a conectar
#define UNER_PERIODO_MS		0			// cada cuanto manda UNER_CMD_PERIODICO; 0 = nunca
#define UNER_CMD_PERIODICO	0xD0		// lo que se pide periodicamente (sin datos)
#define UNER_LATIDO_MS		0			// sin trama valida en este tiempo = falla; 0 = no controla
										// (valvula de la Actividad 3: PERIODO 100, CMD D0, LATIDO 3000)

/* ======================= Tipos ======================= */

typedef struct{					// cliente UNER: a donde y como esta
	char ip[16];				// "255.255.255.255" + '\0'; "" = sin destino
	uint16_t puerto;
	bool conectado;
	bool falla;					// se corto por latido
	char ultima[3 * UNER_MAX_CARGA + 1];	// ultima carga recibida en hex "D0 2A"
}destino_t;

typedef struct{					// una carga para mandar (la cola guarda copias)
	uint8_t n;
	uint8_t carga[UNER_MAX_CARGA];
}mensajeUner_t;

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar_tcp())

SemaphoreHandle_t mutexConsola;			// un comando a la vez: execute() corre en task_tcp o en task_uart
void (*salida)(const char *texto);		// a que consola responde enviar(): enviar_tcp o enviar_uart

#if CONSOLA_UART
microrl_t rlUart;				// otra instancia para la UART: cada consola con su linea e historial
#endif

#if CLIENTE_UNER
destino_t destino = {"", 0, false, false, ""};
SemaphoreHandle_t mutexDestino;				// protege destino
QueueHandle_t colaUner;						// cualquiera -> task_uner: cargas para mandar
TaskHandle_t handleUner;					// para avisarle que cambio el destino (task notify)
#endif

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void task_uart(void *pvParam);						// tarea de la consola UART
void enviar(const char *texto);						// responde a la consola que mando el comando
void enviar_tcp(const char *texto);					// manda texto al cliente TCP
void enviar_uart(const char *texto);				// manda texto por la UART
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi
esp_err_t responder_json(httpd_req_t *req, cJSON *obj);	// manda un JSON y lo libera
cJSON *leer_json(httpd_req_t *req);					// lee el cuerpo del pedido como JSON

void task_uner(void *pvParam);						// cliente TCP + UNER
bool uner_mandar(const uint8_t *carga, uint8_t n);	// encola una carga; la manda task_uner
bool enviar_uner(int sock, const uint8_t *carga, uint8_t n);	// arma la trama y la manda
void procesar_uner(const uint8_t *carga, uint8_t n);	// TUS comandos recibidos
bool destino_guardar(const char *ip, int puerto);	// valida, guarda en NVS y reconecta
cJSON *destino_json(void);							// estado del cliente como JSON
esp_err_t destino_get(httpd_req_t *req);			// handler de GET /destino
esp_err_t destino_post(httpd_req_t *req);			// handler de POST /destino

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
#if CLIENTE_UNER
	{ .uri = "/destino",   .method = HTTP_GET, .handler = destino_get },	// seccion de wifi.html
	{ .uri = "/destino",   .method = HTTP_POST, .handler = destino_post },	// {"ip":..,"puerto":..}
#endif
};

/* ======================= Arranque ======================= */

void app_main(void)
{
	/* Red guardada en NVS; si nunca se guardo, la de menuconfig */
	char ssid[33] = CONFIG_ESP_WIFI_SSID;		// 32 caracteres max + '\0'
	char pass[65] = CONFIG_ESP_WIFI_PASSWORD;	// 64 caracteres max + '\0'

	init_hardware();							// pines antes que nada

	memoria_init();								// NVS: tiene que ir antes que wifi_init
	memoria_get_str("ssid", ssid, sizeof(ssid));	// si la clave no existe, ssid no cambia
	memoria_get_str("pass", pass, sizeof(pass));
	wifi_init(CONFIG_ESP_AP_SSID, CONFIG_ESP_AP_PASSWORD, ssid, pass);	// AP propio + conexion al router

	/* Objetos de FreeRTOS antes que cualquier cosa que los use (tareas, handlers) */
	mutexConsola = xSemaphoreCreateMutex();
#if CLIENTE_UNER
	mutexDestino = xSemaphoreCreateMutex();
	colaUner = xQueueCreate(10, sizeof(mensajeUner_t));
	memoria_get_str("dest_ip", destino.ip, sizeof(destino.ip));	// si no hay, queda "" y espera
	memoria_get_u16("dest_port", &destino.puerto);
#endif

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/
													// sizeof/sizeof = cantidad de elementos de uris[]

	xTaskCreate(task_tcp,
				"task tcp",
				4096,			// microrl + snprintf: el minimo no alcanza
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

#if CONSOLA_UART
	xTaskCreate(task_uart,
				"task uart",
				4096,
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);
#endif

#if CLIENTE_UNER
	xTaskCreate(task_uner,
				"task uner",
				4096,
				NULL,
				tskIDLE_PRIORITY + 3,
				&handleUner);	// el handle: para el task notify de destino_guardar()
#endif
	// app_main termina aca; las tareas, el WiFi y el servidor web siguen corriendo
}

void init_hardware(void)
{
	for (int i = 0; i < LED_COUNT; i++) {
		salida_init(leds[i]);							// LED como salida, arranca apagado
	}
	for (int i = 0; i < SW_COUNT; i++) {
		entrada_init(switches[i], GPIO_PULLUP_ONLY);	// apretado = 0
	}
	for (int i = 0; i < POT_COUNT; i++) {
		analogica_init(pots[i]);						// canal del ADC1 de ese pin
	}
}

/* ======================= Comandos (TCP y UART + microrl) ======================= */

/* Microrl la llama al apretar Enter, con la linea ya separada en palabras:
 * argv[0] es el comando, argc cuantas palabras hay. */
int execute(int argc, const char *const *argv)
{
	char respuesta[64];			// para armar textos con numeros (snprintf)

	/* Tus comandos van aca, antes del mensaje de error:
	 *   if (strcmp(argv[0], "xxx") == 0) {		// strcmp devuelve 0 si son iguales
	 *       ...
	 *       return 0;
	 *   }
	 */

#if CLIENTE_UNER
	/* destino 192.168.1.50 5000 | destino borrar | destino (muestra el estado) */
	if (strcmp(argv[0], "destino") == 0) {
		if (argc == 2 && strcmp(argv[1], "borrar") == 0) {
			destino_guardar("", 0);
			enviar("destino borrado\r\n");
		} else if (argc == 3) {
			enviar(destino_guardar(argv[1], atoi(argv[2])) ? "destino guardado, conectando...\r\n"
														   : "ERROR: ip o puerto invalido\r\n");
		} else if (argc == 1) {
			cJSON *raiz = destino_json();
			char *texto = cJSON_PrintUnformatted(raiz);
			cJSON_Delete(raiz);
			if (texto != NULL) {
				enviar(texto);
				enviar("\r\n");
				free(texto);
			}
		} else {
			enviar("ERROR: uso: destino [<ip> <puerto> | borrar]\r\n");
		}
		return 0;
	}

	/* uner D1 2A: manda una trama con esa carga (bytes en hex) */
	if (strcmp(argv[0], "uner") == 0) {
		uint8_t carga[UNER_MAX_CARGA];
		bool ok = (argc >= 2);
		for (int i = 1; ok && i < argc; i++) {
			char *fin;
			long b = strtol(argv[i], &fin, 16);		// base 16: "2A" -> 42
			ok = (*fin == '\0' && fin != argv[i] && b >= 0 && b <= 255);
			carga[i - 1] = b;
		}
		if (!ok) {
			enviar("ERROR: uso: uner <byte hex> [<byte hex>...]  ej: uner D1 2A\r\n");
		} else {
			enviar(uner_mandar(carga, argc - 1) ? "ok\r\n" : "ERROR: cola llena\r\n");
		}
		return 0;
	}
#endif

	snprintf(respuesta, sizeof(respuesta), "ERROR: comando desconocido: %s\r\n", argv[0]);
	enviar(respuesta);
	return 0;					// microrl pide que devuelva int
}

/* La que usas en execute(): responde a la consola de donde vino el comando.
 * Cada tarea pone "salida" antes de pasarle caracteres a su microrl (con mutexConsola). */
void enviar(const char *texto)
{
	if (salida != NULL) {
		salida(texto);
	}
}

/* Manda texto al cliente TCP (si hay uno). Lo usa microrl de TCP para el eco y el prompt */
void enviar_tcp(const char *texto)
{
	if (sockTcp >= 0) {								// hay alguien conectado
		tcp_enviar(sockTcp, texto, strlen(texto));	// strlen: largo sin el '\0'
	}
}

/* Manda texto por la UART. Lo usa microrl de UART para el eco y el prompt */
void enviar_uart(const char *texto)
{
#if CONSOLA_UART
	uart_write_bytes(UART_CONSOLA, texto, strlen(texto));
#endif
}

void task_tcp(void *pvParam)
{
	char buf[64];				// aca deja tcp_recibir los bytes que llegan
	telnet_t telnet;			// estado del filtro Telnet (un comando puede llegar partido)

	int servidor = tcp_servidor(PUERTO_TCP, TCP_ESPERA_MS);	// abre el puerto y escucha
	if (servidor < 0) {
		vTaskDelete(NULL);		// no se pudo abrir: la tarea se borra (nunca return)
	}

	while (1) {											// atiende un cliente tras otro
		int sock = tcp_aceptar(servidor);			// -1 = nadie se conecto en TCP_ESPERA_MS
		if (sock < 0) {
			continue;								// vuelve a esperar
		}

		/* Se conecto un cliente: preparar la consola */
		sockTcp = sock;								// desde ahora enviar_tcp() le manda a el
		telnet_inicio(&telnet, sock);				// MobaXterm: el eco lo hace microrl
		microrl_init(&rl, enviar_tcp);				// microrl escribe con enviar_tcp()
		microrl_set_execute_callback(&rl, execute);	// al Enter llama a execute()
		enviar_tcp("\r\nConsola ESP32\r\n");		// mensaje de bienvenida
		microrl_insert_char(&rl, KEY_CR);			// muestra el prompt

		/* Mientras siga conectado: lo que llega va a microrl */
		while (1) {
			int n = tcp_recibir(sock, buf, sizeof(buf));	// 0 = vencio la espera
			if (n < 0) {
				break;								// se desconecto
			}
			n = telnet_filtrar(&telnet, buf, n);	// saca los comandos Telnet

			xSemaphoreTake(mutexConsola, portMAX_DELAY);	// si la UART esta ejecutando, espera
			salida = enviar_tcp;					// lo que responda execute() va al cliente TCP
			for (int i = 0; i < n; i++) {
				microrl_insert_char(&rl, buf[i]);	// eco, edicion; al Enter llama a execute
			}
			xSemaphoreGive(mutexConsola);
		}

		/* Se fue el cliente: liberar y esperar otro */
		sockTcp = -1;								// enviar_tcp() ya no manda nada
		close(sock);								// cierra el socket de ese cliente
	}
}

#if CONSOLA_UART
/* La misma consola por UART (UART0 del USB o UART1 con CP2102, ver UART_CONSOLA).
 * Ojo: con UART0 los printf y ESP_LOG tambien salen por aca */
void task_uart(void *pvParam)
{
	uint8_t buf[64];

	uart_config_t cfg = {
		.baud_rate  = UART_BAUDIOS,
		.data_bits  = UART_DATA_8_BITS,
		.parity     = UART_PARITY_DISABLE,
		.stop_bits  = UART_STOP_BITS_1,
		.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	uart_driver_install(UART_CONSOLA, UART_BUF_RX, 0, 0, NULL, 0);	// rx 256, tx sin buffer, sin cola de eventos
	uart_param_config(UART_CONSOLA, &cfg);
	uart_set_pin(UART_CONSOLA, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);	// sin RTS/CTS

	microrl_init(&rlUart, enviar_uart);
	microrl_set_execute_callback(&rlUart, execute);
	enviar_uart("\r\nConsola ESP32 (UART)\r\n");
	microrl_insert_char(&rlUart, KEY_CR);			// muestra el prompt

	while (1) {
		int n = uart_read_bytes(UART_CONSOLA, buf, 1, portMAX_DELAY);	// duerme hasta el primer byte
		if (n <= 0) {
			continue;
		}
		n += uart_read_bytes(UART_CONSOLA, buf + 1, sizeof(buf) - 1, 0);	// y lo que ya haya llegado

		xSemaphoreTake(mutexConsola, portMAX_DELAY);	// si TCP esta ejecutando, espera
		salida = enviar_uart;						// lo que responda execute() va a la UART
		for (int i = 0; i < n; i++) {
			microrl_insert_char(&rlUart, buf[i]);
		}
		xSemaphoreGive(mutexConsola);
	}
}
#endif

/* ======================= Cliente UNER ======================= */

#if CLIENTE_UNER
/* Se conecta al destino y queda: mandando lo que llegue por colaUner (uner_mandar),
 * pidiendo UNER_CMD_PERIODICO cada UNER_PERIODO_MS (si no es 0) y pasando cada trama
 * valida a procesar_uner(). Si se corta, o no llega nada en UNER_LATIDO_MS (si no
 * es 0), cierra y reintenta. Si cambian el destino (task notify), se conecta al nuevo. */
void task_uner(void *pvParam)
{
	uint8_t buf[64];
	uner_rx_t rx;					// estado del receptor: una trama puede llegar partida
	char ip[16];
	uint16_t puerto;
	mensajeUner_t msj;

	while (1) {
		ulTaskNotifyTake(pdTRUE, 0);				// borra avisos viejos: ya se lee el destino actual

		xSemaphoreTake(mutexDestino, portMAX_DELAY);
		strcpy(ip, destino.ip);
		puerto = destino.puerto;
		xSemaphoreGive(mutexDestino);

		if (ip[0] == '\0' || puerto == 0) {			// sin destino: esperar a que lo configuren
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			continue;
		}

		int sock = tcp_conectar(ip, puerto, UNER_ESPERA_MS);	// bloquea hasta 3 s
		if (sock < 0) {
			ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(UNER_REINTENTO_MS));	// espera, salvo que cambien el destino
			continue;
		}

		xSemaphoreTake(mutexDestino, portMAX_DELAY);
		destino.conectado = true;
		destino.falla = false;
		xSemaphoreGive(mutexDestino);

		uner_rx_init(&rx);
		/* Si hay que mandar algo apenas conecta, aca. Ej: uint8_t c = 0xF0; enviar_uner(sock, &c, 1); */
		int64_t ultimaRespuesta = esp_timer_get_time() / 1000;	// en ms
		int64_t ultimoPedido = 0;
		bool falla = false;

		while (1) {
			int64_t ahora = esp_timer_get_time() / 1000;

			if (ulTaskNotifyTake(pdTRUE, 0) > 0) {	// cambiaron el destino
				break;
			}

			/* ENVIAR: todo lo que este en la cola */
			bool ok = true;
			while (ok && xQueueReceive(colaUner, &msj, 0) == pdTRUE) {
				ok = enviar_uner(sock, msj.carga, msj.n);
			}
			if (!ok) {
				break;
			}

			if (UNER_PERIODO_MS > 0 && ahora - ultimoPedido >= UNER_PERIODO_MS) {
				ultimoPedido = ahora;
				uint8_t carga = UNER_CMD_PERIODICO;
				if (!enviar_uner(sock, &carga, 1)) {
					break;
				}
			}

			/* RECIBIR: los bytes que lleguen, de a uno, al receptor */
			int n = tcp_recibir(sock, buf, sizeof(buf));	// 0 = vencio UNER_ESPERA_MS
			if (n < 0) {
				break;								// el otro cerro
			}
			for (int i = 0; i < n; i++) {
				if (uner_rx_byte(&rx, buf[i])) {	// trama completa y con checksum bien
					xSemaphoreTake(mutexDestino, portMAX_DELAY);	// guardar en hex para verla: "D0 2A"
					int k = 0;
					destino.ultima[0] = '\0';
					for (int j = 0; j < rx.n; j++) {
						k += sprintf(&destino.ultima[k], j ? " %02X" : "%02X", rx.carga[j]);
					}
					xSemaphoreGive(mutexDestino);
					procesar_uner(rx.carga, rx.n);
					ultimaRespuesta = esp_timer_get_time() / 1000;
				}
			}

			if (UNER_LATIDO_MS > 0 && esp_timer_get_time() / 1000 - ultimaRespuesta > UNER_LATIDO_MS) {
				ESP_LOGW("uner", "Sin respuesta en %d ms: falla", UNER_LATIDO_MS);
				falla = true;
				break;
			}
		}

		close(sock);
		xSemaphoreTake(mutexDestino, portMAX_DELAY);
		destino.conectado = false;
		destino.falla = falla;
		xSemaphoreGive(mutexDestino);
		ESP_LOGW("uner", "Desconectado de %s:%u", ip, puerto);
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(UNER_REINTENTO_MS));
	}
}

/* TUS comandos recibidos: carga[0] = comando, despues sus datos. Chequear n antes de leer.
 * Corre en task_uner: si guardas algo que leen otras tareas, protegerlo con un mutex. */
void procesar_uner(const uint8_t *carga, uint8_t n)
{
	switch (carga[0]) {
	/*
	case 0xD0:								// ej: D0 pos (1 byte)
		if (n >= 2) {
			... = carga[1];
		}
		break;
	case 0xA2:								// ej: A2 + uint16 (2 bytes)
		if (n >= 3) {
			... = uner_leer16(&carga[1]);
		}
		break;
	*/
	default:
		ESP_LOGI("uner", "Recibido comando 0x%02X (%u bytes)", carga[0], n);
		break;
	}
}

/* Para mandar desde cualquier tarea o handler: copia la carga a la cola y la manda
 * task_uner (la unica que escribe en el socket). false si no entra o la cola esta llena.
 *   uint8_t carga[] = { 0xD1, 45 };   uner_mandar(carga, sizeof(carga)); */
bool uner_mandar(const uint8_t *carga, uint8_t n)
{
	mensajeUner_t msj;
	if (n == 0 || n > UNER_MAX_CARGA) {
		return false;
	}
	msj.n = n;
	memcpy(msj.carga, carga, n);
	return xQueueSend(colaUner, &msj, 0) == pdTRUE;
}

/* Arma la trama UNER con la carga y la manda por el socket */
bool enviar_uner(int sock, const uint8_t *carga, uint8_t n)
{
	uint8_t trama[UNER_MAX_TRAMA];
	size_t largo = uner_armar(trama, carga, n);
	return largo > 0 && tcp_enviar(sock, trama, largo);
}

/* Valida, guarda en NVS y le avisa a task_uner que se reconecte.
 * ip "" = sin destino: la tarea corta y se queda esperando */
bool destino_guardar(const char *ip, int puerto)
{
	struct in_addr dir;
	int puntos = 0;
	for (const char *p = ip; *p != '\0'; p++) {	// lwIP acepta "1.2.3" (abreviada): pedir 4 numeros
		puntos += (*p == '.');
	}
	if (ip[0] == '\0') {
		puerto = 0;
	} else if (strlen(ip) >= sizeof(destino.ip) || puntos != 3 || inet_pton(AF_INET, ip, &dir) != 1 ||
			   puerto < 1 || puerto > 65535) {
		return false;
	}
	xSemaphoreTake(mutexDestino, portMAX_DELAY);
	strcpy(destino.ip, ip);
	destino.puerto = puerto;
	xSemaphoreGive(mutexDestino);

	memoria_set_str("dest_ip", ip);			// queda para el proximo arranque
	memoria_set_u16("dest_port", puerto);
	xTaskNotifyGive(handleUner);			// que cierre y se conecte al nuevo
	return true;
}

/* Copia bajo mutex y arma el JSON afuera. Quien la llama hace cJSON_Delete */
cJSON *destino_json(void)
{
	destino_t d;
	xSemaphoreTake(mutexDestino, portMAX_DELAY);
	d = destino;
	xSemaphoreGive(mutexDestino);

	cJSON *raiz = cJSON_CreateObject();
	cJSON_AddStringToObject(raiz, "ip", d.ip);
	cJSON_AddNumberToObject(raiz, "puerto", d.puerto);
	cJSON_AddBoolToObject(raiz, "conectado", d.conectado);
	cJSON_AddBoolToObject(raiz, "falla", d.falla);
	cJSON_AddStringToObject(raiz, "ultima", d.ultima);
	return raiz;
}
#endif

/* ======================= Pagina web ======================= */

/* POST /wifi (formulario de wifi.html): guarda la red en NVS y se conecta */
esp_err_t wifi_post(httpd_req_t *req)
{
	char cuerpo[200], ssid[33], pass[65];

	/* El cuerpo llega como "ssid=MiRed&pass=clave": se lee y se separa en campos */
	if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0 ||
		!web_campo(cuerpo, "ssid", ssid, sizeof(ssid)) ||
		!web_campo(cuerpo, "pass", pass, sizeof(pass))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Datos invalidos");
	}
	memoria_set_str("ssid", ssid);		// queda guardado para el proximo arranque
	memoria_set_str("pass", pass);
	wifi_sta_conectar(ssid, pass);		// se conecta ahora, sin reiniciar
	return httpd_resp_sendstr(req, "Guardado. Conectando... (la IP nueva sale en el monitor)");
}

#if CLIENTE_UNER
/* GET /destino: destino guardado y estado de la conexion */
esp_err_t destino_get(httpd_req_t *req)
{
	return responder_json(req, destino_json());
}

/* POST /destino {"ip":"192.168.1.50","puerto":5000}; {"ip":"","puerto":0} lo borra */
esp_err_t destino_post(httpd_req_t *req)
{
	cJSON *json = leer_json(req);
	if (json == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *ip = cJSON_GetObjectItem(json, "ip");
	cJSON *puerto = cJSON_GetObjectItem(json, "puerto");
	bool ok = cJSON_IsString(ip) && cJSON_IsNumber(puerto) &&
			  destino_guardar(ip->valuestring, puerto->valueint);
	cJSON_Delete(json);							// ip y puerto eran parte de json: no usarlos mas

	cJSON *resp = cJSON_CreateObject();
	cJSON_AddBoolToObject(resp, "ok", ok);
	if (!ok) {
		cJSON_AddStringToObject(resp, "error", "ip o puerto invalido");
	}
	return responder_json(req, resp);
}
#endif

/* Ayudas para tus handlers con JSON (ejemplos completos en GUIA.md, seccion 5) */

/* Manda el objeto como respuesta JSON y libera todo */
esp_err_t responder_json(httpd_req_t *req, cJSON *obj)
{
	char *texto = cJSON_PrintUnformatted(obj);		// objeto -> texto {"a":1,...} (reserva memoria)
	cJSON_Delete(obj);								// el objeto ya no hace falta
	if (texto == NULL) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sin memoria");
	}
	httpd_resp_set_type(req, "application/json");
	esp_err_t err = httpd_resp_sendstr(req, texto);
	free(texto);									// el texto tambien se libera
	return err;
}

/* Lee el cuerpo del pedido y lo convierte en objeto (NULL si no es JSON valido).
 * Quien la llama tiene que hacer cJSON_Delete del resultado. */
cJSON *leer_json(httpd_req_t *req)
{
	char cuerpo[256];
	if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0) {
		return NULL;								// no entra o se corto
	}
	return cJSON_Parse(cuerpo);						// NULL si esta mal escrito
}
