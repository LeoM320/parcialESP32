/* main.c: control de nivel de un tanque (simulacro 1).
 *
 *   - task_controlProtegido: cada 100 ms mide el nivel y decide alarma y bomba (bajo mutex).
 *   - task_controlCopia:     recibe una copia del estado y maneja LED1 (PWM), LED2 y el RGB.
 *   - task_antirrebote:      filtra SW1..SW3 y pasa solo las pulsaciones validas al control.
 *   - timers:                heartbeat (LED3) y alarma (LED4).
 *   - task_tcp:              consola TCP (MobaXterm, Telnet) con microrl.
 *   - task_uart:             la misma consola por UART0 (el COM del USB, idf.py monitor).
 *   - task_valvula:          cliente TCP que habla protocolo UNER con una valvula remota
 *                            (destino IP:puerto configurable en wifi.html y guardado en NVS).
 */

/* ======================= Includes ======================= */

#include <stdint.h>				// uint8_t, uint32_t...
#include <stdbool.h>			// bool, true, false
#include <inttypes.h>			// PRIu32 (printf de uint32_t)
#include <stdio.h>				// printf(), snprintf()
#include <stdlib.h>				// atoi(), atof()
#include <string.h>				// strcmp(), strlen(), memcpy(), memcmp()
#include <unistd.h>				// close()
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"			// ESP_LOGI/W/E: mensajes con nombre y color en el monitor
#include "esp_timer.h"			// esp_timer_get_time(): microsegundos desde el arranque
#include "esp_system.h"			// esp_restart(): reiniciar la placa
#include "driver/uart.h"		// uart_driver_install, uart_read_bytes, uart_write_bytes
#include "freertos/FreeRTOS.h"	// siempre primero de los de FreeRTOS
#include "freertos/task.h"		// xTaskCreate(), vTaskDelay(), task notify
#include "freertos/queue.h"		// colas
#include "freertos/semphr.h"		// mutex y semaforos
#include "freertos/event_groups.h"	// event groups
#include "freertos/timers.h"		// software timers
#include "hardware.h"			// pines: leds[], switches[], pots[], LED_COUNT...
#include "iodigital.h"			// salida_init/set/get, entrada_init/get, entrada_interrupcion
#include "ianalogica.h"			// analogica_init/get
#include "portmacro.h"
#include "pwm.h"				// pwm_init, pwm_set_duty, pwm_set_pulso_us
#include "rgb.h"				// rgb_init, rgb_set (LED RGB de la placa)
#include "temporizador.h"		// temporizador_init (interrupcion periodica)
#include "memoria.h"			// NVS: memoria_init, memoria_get/set_str
#include "wifi.h"				// wifi_init, wifi_sta_conectar
#include "web.h"				// web_init, web_parametro, web_leer_cuerpo, web_campo
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe
#include "uner.h"				// uner_armar, uner_rx_init, uner_rx_byte
#include "lwip/sockets.h"		// inet_pton(): validar la IP del destino
#include "cJSON.h"				// JSON: armar respuestas y leer pedidos de la pagina

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

/* Consola UART: elegir UNA de las dos opciones (comentar la otra) */

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

/* Valvula remota por TCP + UNER (el ESP es el cliente) */
#define VALVULA_ESPERA_MS		50			// timeout de tcp_recibir(): cada cuanto da vueltas el lazo
#define VALVULA_PERIODO_MS		100			// cada cuanto se pide la posicion (0xD0)
#define VALVULA_LATIDO_MS		3000		// sin respuesta valida en este tiempo = falla
#define VALVULA_REINTENTO_MS	1000		// espera antes de volver a conectar

#define CMD_CONTROLABLE		0xF0		// -> F0            <- F0 estado
#define CMD_POSICION		0xD0		// -> D0            <- D0 pos
#define CMD_SETPOINT		0xD1		// -> D1 pos        <- D1 pos estado
#define CMD_LLEGO			0xD2		//                  <- D2 pos (sin pedirlo)
#define VALVULA_OK			0x0D
#define VALVULA_ERROR		0xFF

#define MIN_DEF			30			// configuracion de fabrica (en %)
#define MAX_DEF			80
#define ALARMA_DEF		90
#define VEL_DEF			80

#define PERIODO_CONTROL_MS	100
#define ANTIRREBOTE_MS		20
#define HISTERESIS_ALARMA	5		// la alarma vuelve a normal por debajo de alarma - 5

/* ======================= Tipos ======================= */

typedef struct{
	uint8_t min;
	uint8_t max;
	uint8_t alarma;
	uint8_t vel;
}configBomba_t;

typedef enum{
	E_MODO_MANUAL,
	E_MODO_AUTO
}modoBomba_e;

typedef enum{
	E_ESTADO_OFF,
	E_ESTADO_ON
}estadoBomba_e;

typedef enum{
	E_ALARMA_APAGADA,
	E_ALARMA_ACTIVA,
	E_ALARMA_RECONOCIDA
}alarma_e;

typedef struct{
	uint8_t nivel;				// en %
	modoBomba_e modo;
	estadoBomba_e estado;
	alarma_e alarma;
	configBomba_t config;
}bomba_t;

typedef struct{
	char ip[16];				// destino "255.255.255.255" + '\0'; "" = sin configurar
	uint16_t puerto;
	bool conectado;
	bool falla;					// se corto por latido (3 s sin respuesta)
	bool controlable;			// respuesta a 0xF0
	uint8_t posicion;			// 0..100, ultima informada
	uint8_t setpoint;			// 0..100, ultimo pedido
}valvula_t;

typedef enum{					// mismo orden que switches[] de hardware.h
	E_SW1,
	E_SW2,
	E_SW3,
	E_SW4
}pulsadores_e;

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
microrl_t rlUart;				// otra instancia para la UART: cada consola con su linea e historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar_tcp())

SemaphoreHandle_t mutexConsola;			// un comando a la vez: execute() corre en task_tcp o en task_uart
void (*salida)(const char *texto);		// a que consola responde enviar(): enviar_tcp o enviar_uart

bomba_t bomba = {0, E_MODO_MANUAL, E_ESTADO_OFF, E_ALARMA_APAGADA, {MIN_DEF, MAX_DEF, ALARMA_DEF, VEL_DEF}};

SemaphoreHandle_t mutexBomba;				// protege bomba (estado y config)
QueueHandle_t colaBomba;					// control -> salidas (copia de bomba, 1 lugar)
QueueHandle_t colaPulsadores;				// ISR -> antirrebote (todos los flancos)
QueueHandle_t colaPulsacionesFiltradas;		// antirrebote -> control (solo pulsaciones validas)

valvula_t valvula = {"", 0, false, false, false, 0, 0};
SemaphoreHandle_t mutexValvula;				// protege valvula
QueueHandle_t colaSetpoint;					// web/consola -> task_valvula (1 lugar, gana el ultimo)
TaskHandle_t handleValvula;					// para avisarle que cambio el destino (task notify)

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
esp_err_t estado_get(httpd_req_t *req); // handler de GET /estado
esp_err_t modo_post(httpd_req_t *req); // handler de POST /modo
//esp_err_t modo_get(httpd_req_t *req); // handler de GET /modo?modo=auto (version sin POST)
esp_err_t actualizar_get(httpd_req_t *req); // handler de GET /actualizar

void task_valvula(void *pvParam);					// cliente TCP + UNER de la valvula
bool enviar_uner(int sock, const uint8_t *carga, uint8_t n);	// arma la trama y la manda
void procesar_uner(const uint8_t *carga, uint8_t n);	// interpreta una respuesta de la valvula
bool destino_guardar(const char *ip, int puerto);	// valida, guarda en NVS y reconecta
void pedir_setpoint(uint8_t pos);					// encola un setpoint para la valvula
cJSON *valvula_json(void);							// estado de la valvula como JSON
esp_err_t destino_post(httpd_req_t *req);			// handler de POST /destino
esp_err_t valvula_get(httpd_req_t *req);			// handler de GET /valvula
esp_err_t valvula_post(httpd_req_t *req);			// handler de POST /valvula

void task_controlProtegido(void *pvParam);			// mide y decide (maquina de estados)
void task_controlCopia(void *pvParam);				// maneja las salidas con la copia
void task_antirrebote(void *pvParam);				// filtra los pulsadores
void callback_heartbit(TimerHandle_t xTimer);		// LED3 cada 500 ms
void callback_ledReconocida(TimerHandle_t xTimer);	// LED4 segun la alarma, cada 250 ms
void isr_pulsadores(void *arg);						// una sola ISR para SW1..SW3

const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
	{ .uri = "/estado",   .method = HTTP_GET, .handler = estado_get },
	{ .uri = "/modo",   .method = HTTP_POST, .handler = modo_post },
	//{ .uri = "/modo",   .method = HTTP_GET, .handler = modo_get },	// misma URI, otro metodo: pueden convivir
	{ .uri = "/actualizar",   .method = HTTP_GET, .handler = actualizar_get },
	{ .uri = "/destino",   .method = HTTP_POST, .handler = destino_post },	// seccion destino de wifi.html
	{ .uri = "/valvula",   .method = HTTP_GET, .handler = valvula_get },		// estado + destino actual
	{ .uri = "/valvula",   .method = HTTP_POST, .handler = valvula_post }	// {"setpoint":45}
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

	/* Objetos de FreeRTOS antes que cualquier cosa que los use */
	mutexBomba = xSemaphoreCreateMutex();
	mutexConsola = xSemaphoreCreateMutex();
	colaBomba = xQueueCreate(1, sizeof(bomba_t));
	colaPulsadores = xQueueCreate(10, sizeof(pulsadores_e));
	colaPulsacionesFiltradas = xQueueCreate(5, sizeof(pulsadores_e));
	mutexValvula = xSemaphoreCreateMutex();
	colaSetpoint = xQueueCreate(1, sizeof(uint8_t));

	/* Destino de la valvula guardado en NVS (si no hay, queda "" y la tarea espera) */
	memoria_get_str("dest_ip", valvula.ip, sizeof(valvula.ip));
	memoria_get_u16("dest_port", &valvula.puerto);

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/

	xTaskCreate(task_tcp,
				"task tcp",
				4096,
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

	xTaskCreate(task_uart,
				"task uart",
				4096,
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

	xTaskCreate(task_valvula,
				"task valvula",
				4096,
				NULL,
				tskIDLE_PRIORITY + 3,
				&handleValvula);

	xTaskCreate(task_controlProtegido,
				"control protegido",
				4096,
				NULL,
				tskIDLE_PRIORITY + 10,
				NULL);

	xTaskCreate(task_controlCopia,
				"control copia",
				4096,
				NULL,
				tskIDLE_PRIORITY + 10,
				NULL);

	xTaskCreate(task_antirrebote,
				"antirrebote",
				4096,
				NULL,
				tskIDLE_PRIORITY + 11,
				NULL);

	TimerHandle_t hbHandle = xTimerCreate("heartbit",
										  pdMS_TO_TICKS(500),
										  pdTRUE,
										  NULL,
										  callback_heartbit);
	xTimerStart(hbHandle, 0);

	TimerHandle_t ledReconocidaHandle = xTimerCreate("led alarma",
													 pdMS_TO_TICKS(250),
													 pdTRUE,
													 NULL,
													 callback_ledReconocida);
	xTimerStart(ledReconocidaHandle, 0);

	entrada_interrupcion(SW1_GPIO, GPIO_INTR_ANYEDGE, isr_pulsadores, (void *)E_SW1);
	entrada_interrupcion(SW2_GPIO, GPIO_INTR_ANYEDGE, isr_pulsadores, (void *)E_SW2);
	entrada_interrupcion(SW3_GPIO, GPIO_INTR_ANYEDGE, isr_pulsadores, (void *)E_SW3);
}

void init_hardware(void)
{
	pwm_init(LED_PWM_GPIO, LED_PWM_FREC_HZ);			// LED1 = bomba
	rgb_init(RGB_GPIO);

	for (int i = 1; i < LED_COUNT; i++) {				// desde 1: leds[0] es el de la PWM
		salida_init(leds[i]);
	}
	for (int i = 0; i < SW_COUNT; i++) {
		entrada_init(switches[i], GPIO_PULLUP_ONLY);	// apretado = 0
	}
	for (int i = 0; i < POT_COUNT; i++) {
		analogica_init(pots[i]);
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
	
	if (strcmp(argv[0], "modo") == 0) {
		if (argc != 2) {
			enviar("ERROR: uso: modo auto | modo manual\r\n");
		} else if (strcmp(argv[1], "auto") == 0) {
			xSemaphoreTake(mutexBomba, portMAX_DELAY);
			bomba.modo = E_MODO_AUTO;
			xSemaphoreGive(mutexBomba);
			enviar("modo=auto\r\n");
		} else if (strcmp(argv[1], "manual") == 0) {
			xSemaphoreTake(mutexBomba, portMAX_DELAY);
			bomba.modo = E_MODO_MANUAL;
			xSemaphoreGive(mutexBomba);
			enviar("modo=manual\r\n");
		} else {
			enviar("ERROR: modo invalido, usar auto o manual\r\n");
		}
		return 0;
	}
	
	if (strcmp(argv[0], "estado") == 0) {
		bomba_t b;
		xSemaphoreTake(mutexBomba, portMAX_DELAY);
		b = bomba;									// solo la copia adentro
		xSemaphoreGive(mutexBomba);

		const char *alarmas[] = { "normal", "activa", "reconocida" };	// mismo orden que alarma_e

		cJSON *raiz = cJSON_CreateObject();
		cJSON_AddNumberToObject(raiz, "nivel", b.nivel);
		cJSON_AddBoolToObject(raiz, "bomba", b.estado == E_ESTADO_ON);
		cJSON_AddNumberToObject(raiz, "vel", b.config.vel);
		cJSON_AddStringToObject(raiz, "modo", b.modo == E_MODO_AUTO ? "auto" : "manual");
		cJSON_AddStringToObject(raiz, "alarma", alarmas[b.alarma]);
		cJSON_AddNumberToObject(raiz, "min", b.config.min);
		cJSON_AddNumberToObject(raiz, "max", b.config.max);
		cJSON_AddNumberToObject(raiz, "lim", b.config.alarma);

		char *texto = cJSON_PrintUnformatted(raiz);	// objeto -> texto (reserva memoria)
		cJSON_Delete(raiz);
		if (texto != NULL) {
			enviar(texto);
			enviar("\r\n");
			free(texto);
		}
		return 0;
	}

	/* destino 192.168.1.50 5000: a donde se conecta task_valvula; destino borrar: ninguno */
	if (strcmp(argv[0], "destino") == 0) {
		if (argc == 2 && strcmp(argv[1], "borrar") == 0) {
			destino_guardar("", 0);
			enviar("destino borrado, valvula deshabilitada\r\n");
		} else if (argc != 3) {
			enviar("ERROR: uso: destino <ip> <puerto> | destino borrar\r\n");
		} else if (!destino_guardar(argv[1], atoi(argv[2]))) {
			enviar("ERROR: ip o puerto invalido\r\n");
		} else {
			enviar("destino guardado, conectando...\r\n");
		}
		return 0;
	}

	/* valvula: muestra el estado; valvula 45: pide ir al 45 % */
	if (strcmp(argv[0], "valvula") == 0) {
		if (argc == 2) {
			int pos = atoi(argv[1]);
			if (pos < 0 || pos > 100) {
				enviar("ERROR: posicion 0..100\r\n");
				return 0;
			}
			pedir_setpoint(pos);
		}
		cJSON *raiz = valvula_json();
		char *texto = cJSON_PrintUnformatted(raiz);
		cJSON_Delete(raiz);
		if (texto != NULL) {
			enviar(texto);
			enviar("\r\n");
			free(texto);
		}
		return 0;
	}

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
	uart_write_bytes(UART_CONSOLA, texto, strlen(texto));
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
		enviar_tcp("\r\nConsola ESP32 (TCP)\r\n");	// mensaje de bienvenida
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

/* ======================= Valvula remota (TCP + UNER) ======================= */

/* Cliente: se conecta al destino, pregunta si es controlable (F0), pide la
 * posicion cada 100 ms (D0) y manda los setpoints que lleguen por colaSetpoint (D1).
 * Si en 3 s no llega ninguna trama valida, marca falla, cierra y reintenta.
 * Si cambian el destino (task notify), cierra y se conecta al nuevo. */
void task_valvula(void *pvParam)
{
	uint8_t buf[64];
	uner_rx_t rx;
	char ip[16];
	uint16_t puerto;

	while (1) {
		ulTaskNotifyTake(pdTRUE, 0);				// borra avisos viejos: ya se lee el destino actual

		xSemaphoreTake(mutexValvula, portMAX_DELAY);
		strcpy(ip, valvula.ip);
		puerto = valvula.puerto;
		xSemaphoreGive(mutexValvula);

		if (ip[0] == '\0' || puerto == 0) {			// sin destino: esperar a que lo configuren
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			continue;
		}

		int sock = tcp_conectar(ip, puerto, VALVULA_ESPERA_MS);
		if (sock < 0) {
			ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VALVULA_REINTENTO_MS));	// espera, salvo que cambien el destino
			continue;
		}

		xSemaphoreTake(mutexValvula, portMAX_DELAY);
		valvula.conectado = true;
		valvula.falla = false;
		xSemaphoreGive(mutexValvula);

		uner_rx_init(&rx);
		uint8_t pregunta = CMD_CONTROLABLE;
		enviar_uner(sock, &pregunta, 1);
		int64_t ultimaRespuesta = esp_timer_get_time() / 1000;	// en ms
		int64_t ultimoPedido = 0;
		bool falla = false;

		while (1) {
			int64_t ahora = esp_timer_get_time() / 1000;

			if (ulTaskNotifyTake(pdTRUE, 0) > 0) {	// cambiaron el destino
				break;
			}

			uint8_t sp;
			if (xQueueReceive(colaSetpoint, &sp, 0) == pdTRUE) {
				uint8_t carga[] = { CMD_SETPOINT, sp };
				if (!enviar_uner(sock, carga, sizeof(carga))) {
					break;
				}
			}

			if (ahora - ultimoPedido >= VALVULA_PERIODO_MS) {
				ultimoPedido = ahora;
				uint8_t carga = CMD_POSICION;
				if (!enviar_uner(sock, &carga, 1)) {
					break;
				}
			}

			int n = tcp_recibir(sock, buf, sizeof(buf));	// 0 = vencio VALVULA_ESPERA_MS
			if (n < 0) {
				break;								// el servidor cerro
			}
			for (int i = 0; i < n; i++) {
				if (uner_rx_byte(&rx, buf[i])) {	// trama completa y con checksum bien
					procesar_uner(rx.carga, rx.n);
					ultimaRespuesta = esp_timer_get_time() / 1000;
				}
			}

			if (esp_timer_get_time() / 1000 - ultimaRespuesta > VALVULA_LATIDO_MS) {
				ESP_LOGW("valvula", "Sin respuesta en %d ms: falla", VALVULA_LATIDO_MS);
				falla = true;
				break;
			}
		}

		close(sock);
		xSemaphoreTake(mutexValvula, portMAX_DELAY);
		valvula.conectado = false;
		valvula.falla = falla;
		xSemaphoreGive(mutexValvula);
		ESP_LOGW("valvula", "Desconectada de %s:%u", ip, puerto);
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VALVULA_REINTENTO_MS));
	}
}

/* Arma la trama UNER con la carga y la manda por el socket */
bool enviar_uner(int sock, const uint8_t *carga, uint8_t n)
{
	uint8_t trama[UNER_MAX_TRAMA];
	size_t largo = uner_armar(trama, carga, n);
	return largo > 0 && tcp_enviar(sock, trama, largo);
}

/* carga[0] = comando, despues sus datos */
void procesar_uner(const uint8_t *carga, uint8_t n)
{
	xSemaphoreTake(mutexValvula, portMAX_DELAY);
	switch (carga[0]) {
	case CMD_CONTROLABLE:					// F0 estado
		if (n >= 2) {
			valvula.controlable = (carga[1] == VALVULA_OK);
		}
		break;
	case CMD_POSICION:						// D0 pos
		if (n >= 2) {
			valvula.posicion = carga[1];
		}
		break;
	case CMD_SETPOINT:						// D1 pos estado
		if (n >= 3 && carga[2] != VALVULA_OK) {
			ESP_LOGW("valvula", "Rechazo el setpoint %u", carga[1]);
		}
		break;
	case CMD_LLEGO:							// D2 pos
		if (n >= 2) {
			valvula.posicion = carga[1];
			ESP_LOGI("valvula", "Llego a %u %%", carga[1]);
		}
		break;
	default:
		ESP_LOGW("valvula", "Comando desconocido 0x%02X", carga[0]);
		break;
	}
	xSemaphoreGive(mutexValvula);
}

/* Valida, guarda en NVS y le avisa a task_valvula que se reconecte.
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
	} else if (strlen(ip) >= sizeof(valvula.ip) || puntos != 3 || inet_pton(AF_INET, ip, &dir) != 1 ||
			   puerto < 1 || puerto > 65535) {
		return false;
	}
	xSemaphoreTake(mutexValvula, portMAX_DELAY);
	strcpy(valvula.ip, ip);
	valvula.puerto = puerto;
	xSemaphoreGive(mutexValvula);

	memoria_set_str("dest_ip", ip);			// queda para el proximo arranque
	memoria_set_u16("dest_port", puerto);
	xTaskNotifyGive(handleValvula);			// que cierre y se conecte al nuevo
	return true;
}

/* El setpoint lo manda task_valvula; si habia uno sin mandar, gana el nuevo */
void pedir_setpoint(uint8_t pos)
{
	xSemaphoreTake(mutexValvula, portMAX_DELAY);
	valvula.setpoint = pos;
	xSemaphoreGive(mutexValvula);
	xQueueOverwrite(colaSetpoint, &pos);
}

/* Copia bajo mutex y arma el JSON afuera. Quien la llama hace cJSON_Delete */
cJSON *valvula_json(void)
{
	valvula_t v;
	xSemaphoreTake(mutexValvula, portMAX_DELAY);
	v = valvula;
	xSemaphoreGive(mutexValvula);

	cJSON *raiz = cJSON_CreateObject();
	cJSON_AddStringToObject(raiz, "ip", v.ip);
	cJSON_AddNumberToObject(raiz, "puerto", v.puerto);
	cJSON_AddBoolToObject(raiz, "conectado", v.conectado);
	cJSON_AddBoolToObject(raiz, "falla", v.falla);
	cJSON_AddBoolToObject(raiz, "controlable", v.controlable);
	cJSON_AddNumberToObject(raiz, "posicion", v.posicion);
	cJSON_AddNumberToObject(raiz, "setpoint", v.setpoint);
	return raiz;
}

/* ======================= Pagina web ======================= */

/* POST /destino {"ip":"192.168.1.50","puerto":5000} (seccion de wifi.html) */
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

/* GET /valvula: estado y destino actual */
esp_err_t valvula_get(httpd_req_t *req)
{
	return responder_json(req, valvula_json());
}

/* POST /valvula {"setpoint":45} */
esp_err_t valvula_post(httpd_req_t *req)
{
	cJSON *json = leer_json(req);
	if (json == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *sp = cJSON_GetObjectItem(json, "setpoint");
	bool ok = cJSON_IsNumber(sp) && sp->valuedouble >= 0 && sp->valuedouble <= 100;
	if (ok) {
		pedir_setpoint((uint8_t)sp->valuedouble);
	}
	cJSON_Delete(json);

	cJSON *resp = cJSON_CreateObject();
	cJSON_AddBoolToObject(resp, "ok", ok);
	if (!ok) {
		cJSON_AddStringToObject(resp, "error", "setpoint 0..100");
	}
	return responder_json(req, resp);
}

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

/* ======================= Control ======================= */

/* Cada vuelta: leer entradas -> Take -> maquina de estados -> copia -> Give -> mandar la copia */
void task_controlProtegido(void *pvParam)
{
	TickType_t xLastWakeTime = xTaskGetTickCount();
	bomba_t bombaTemp;
	while (1) {
		uint8_t nivelTemp = (analogica_get(POT1_GPIO) * 100) / 4095;

		/* Pulsaciones que llegaron desde la vuelta anterior (sin esperar) */
		bool sw1 = false, sw2 = false, sw3 = false;
		pulsadores_e ev;
		while (xQueueReceive(colaPulsacionesFiltradas, &ev, 0) == pdTRUE) {
			if (ev == E_SW1) sw1 = true;
			if (ev == E_SW2) sw2 = true;
			if (ev == E_SW3) sw3 = true;
		}

		xSemaphoreTake(mutexBomba, portMAX_DELAY);
		bomba.nivel = nivelTemp;

		if (sw1) {
			bomba.modo = (bomba.modo == E_MODO_AUTO) ? E_MODO_MANUAL : E_MODO_AUTO;
		}

		/* Alarma */
		if (bomba.alarma == E_ALARMA_APAGADA && bomba.nivel >= bomba.config.alarma) {
			bomba.alarma = E_ALARMA_ACTIVA;
		}
		if (bomba.alarma == E_ALARMA_ACTIVA && sw3) {
			bomba.alarma = E_ALARMA_RECONOCIDA;
		}
		if (bomba.alarma != E_ALARMA_APAGADA && bomba.nivel < bomba.config.alarma - HISTERESIS_ALARMA) {
			bomba.alarma = E_ALARMA_APAGADA;
		}

		/* Bomba: con alarma siempre apagada */
		if (bomba.alarma != E_ALARMA_APAGADA) {
			bomba.estado = E_ESTADO_OFF;
		} else if (bomba.modo == E_MODO_AUTO) {
			if (bomba.nivel <= bomba.config.min) {
				bomba.estado = E_ESTADO_ON;
			} else if (bomba.nivel >= bomba.config.max) {
				bomba.estado = E_ESTADO_OFF;
			}
		} else if (sw2) {
			bomba.estado = (bomba.estado == E_ESTADO_ON) ? E_ESTADO_OFF : E_ESTADO_ON;
		}

		bombaTemp = bomba;
		xSemaphoreGive(mutexBomba);

		xQueueOverwrite(colaBomba, &bombaTemp);
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PERIODO_CONTROL_MS));
	}
}

/* Salidas: solo trabaja con la copia, nunca toca el mutex */
void task_controlCopia(void *pvParam)
{
	bomba_t bombaTemp;
	while (1) {
		xQueueReceive(colaBomba, &bombaTemp, portMAX_DELAY);

		pwm_set_duty(LED_PWM_GPIO, (bombaTemp.estado == E_ESTADO_ON) ? bombaTemp.config.vel : 0);
		salida_set(LED2_GPIO, bombaTemp.modo == E_MODO_MANUAL);

		if (bombaTemp.alarma == E_ALARMA_ACTIVA) {
			rgb_set(10, 0, 0);							// rojo
		} else if (bombaTemp.alarma == E_ALARMA_RECONOCIDA) {
			rgb_set(10, 10, 0);							// amarillo
		} else if (bombaTemp.modo == E_MODO_MANUAL) {
			rgb_set(0, 0, 10);							// azul
		} else {
			rgb_set(0, 10, 0);							// verde
		}
	}
}

/* ======================= Timers ======================= */

void callback_heartbit(TimerHandle_t xTimer)
{
	salida_set(LED3_GPIO, !salida_get(LED3_GPIO));
}

/* Unico dueño de LED4. No se bloquea: si el mutex esta ocupado usa el estado anterior */
void callback_ledReconocida(TimerHandle_t xTimer)
{
	static alarma_e estadoAlarma = E_ALARMA_APAGADA;
	if (xSemaphoreTake(mutexBomba, 0) == pdTRUE) {
		estadoAlarma = bomba.alarma;
		xSemaphoreGive(mutexBomba);
	}

	if (estadoAlarma == E_ALARMA_ACTIVA) {
		salida_set(LED4_GPIO, !salida_get(LED4_GPIO));	// parpadea
	} else {
		salida_set(LED4_GPIO, estadoAlarma == E_ALARMA_RECONOCIDA);	// fijo o apagado
	}
}

/* ======================= Pulsadores ======================= */

void IRAM_ATTR isr_pulsadores(void *arg)
{
	pulsadores_e ev = (pulsadores_e)(int)arg;
	BaseType_t despertar = pdFALSE;
	xQueueSendFromISR(colaPulsadores, &ev, &despertar);
	portYIELD_FROM_ISR(despertar);
}

void task_antirrebote(void *pvParam)
{
	pulsadores_e ev;
	bool apretado[3] = { false, false, false };
	while (1) {
		xQueueReceive(colaPulsadores, &ev, portMAX_DELAY);
		vTaskDelay(pdMS_TO_TICKS(ANTIRREBOTE_MS));
		xQueueReset(colaPulsadores);

		for (int i = 0; i < 3; i++) {
			bool ahora = (entrada_get(switches[i]) == 0);	// pull-up: apretado = 0
			if (ahora && !apretado[i]) {
				pulsadores_e p = (pulsadores_e)i;
				xQueueSend(colaPulsacionesFiltradas, &p, 0);
			}
			apretado[i] = ahora;
		}
	}
}

esp_err_t estado_get(httpd_req_t *req){
	bomba_t b;
	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	b = bomba;									// solo la copia adentro
	xSemaphoreGive(mutexBomba);

	const char *alarmas[] = { "normal", "activa", "reconocida" };	// mismo orden que alarma_e

	cJSON *raiz = cJSON_CreateObject();
	cJSON_AddNumberToObject(raiz, "nivel", b.nivel);
	cJSON_AddBoolToObject(raiz, "bomba", b.estado == E_ESTADO_ON);
	cJSON_AddNumberToObject(raiz, "vel", b.config.vel);
	cJSON_AddStringToObject(raiz, "modo", b.modo == E_MODO_AUTO ? "auto" : "manual");
	cJSON_AddStringToObject(raiz, "alarma", alarmas[b.alarma]);
	cJSON_AddNumberToObject(raiz, "min", b.config.min);
	cJSON_AddNumberToObject(raiz, "max", b.config.max);
	cJSON_AddNumberToObject(raiz, "lim", b.config.alarma);

	return responder_json(req, raiz);			// imprime, libera y responde
}

esp_err_t modo_post(httpd_req_t *req){
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	
	cJSON *modo = cJSON_GetObjectItem(raiz, "modo");
	if (!cJSON_IsString(modo)) {					// falta o no es texto -> 400
		cJSON_Delete(raiz);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta modo");
	}

	modoBomba_e modoE;
	if (strcmp(modo->valuestring, "auto") == 0) {	// el texto esta en valuestring
		modoE = E_MODO_AUTO;
	} else if (strcmp(modo->valuestring, "manual") == 0) {
		modoE = E_MODO_MANUAL;
	} else {
		cJSON_Delete(raiz);
		cJSON *resp = cJSON_CreateObject();
		cJSON_AddBoolToObject(resp, "ok", false);
		cJSON_AddStringToObject(resp, "error", "modo invalido, usar auto o manual");
		return responder_json(req, resp);
	}
	cJSON_Delete(raiz);								// ya no hace falta el pedido

	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	bomba.modo = modoE;
	xSemaphoreGive(mutexBomba);

	cJSON *resp = cJSON_CreateObject();
	cJSON_AddBoolToObject(resp, "ok", true);
	return responder_json(req, resp);
}

/* Version con GET (sin JSON de entrada): los datos vienen en la URL.
 * Para usarla: descomentar esto, el prototipo y la linea de uris[].
 *   Navegador:  http://192.168.123.38/modo?modo=auto
 *   Pagina:     fetch('/modo?modo=' + document.getElementById('modo').value)
 * Diferencia con modo_post: no hay cuerpo, asi que no hay leer_json ni cJSON_Delete del pedido;
 * el valor se lee con web_parametro() a un buffer y se compara directo con strcmp. */
/*esp_err_t modo_get(httpd_req_t *req){
	char valor[10];									// "manual" + '\0' entra de sobra

	if (!web_parametro(req, "modo", valor, sizeof(valor))) {	// falta ?modo= o es muy largo
		cJSON *resp = cJSON_CreateObject();
		cJSON_AddBoolToObject(resp, "ok", false);
		cJSON_AddStringToObject(resp, "error", "falta modo: /modo?modo=auto");
		return responder_json(req, resp);
	}

	modoBomba_e modoE;
	if (strcmp(valor, "auto") == 0) {
		modoE = E_MODO_AUTO;
	} else if (strcmp(valor, "manual") == 0) {
		modoE = E_MODO_MANUAL;
	} else {
		cJSON *resp = cJSON_CreateObject();
		cJSON_AddBoolToObject(resp, "ok", false);
		cJSON_AddStringToObject(resp, "error", "modo invalido, usar auto o manual");
		return responder_json(req, resp);
	}

	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	bomba.modo = modoE;
	xSemaphoreGive(mutexBomba);

	cJSON *resp = cJSON_CreateObject();
	cJSON_AddBoolToObject(resp, "ok", true);
	return responder_json(req, resp);				// la respuesta igual puede ser JSON
}*/

esp_err_t actualizar_get(httpd_req_t *req)
{
	//Debo enviar en json el valor del pot1
	
	uint8_t nivel;
	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	nivel = bomba.nivel;
	xSemaphoreGive(mutexBomba);

	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "pot1", nivel);		// int    -> numero

	return responder_json(req, obj);			// imprime, libera y responde
}