/* implementacionTcpUner.c: la plantilla (main.c pelado) + el ESP como CLIENTE TCP que habla
 * protocolo UNER con otro equipo (ejemplo: la valvula remota de la Actividad 3)
 * + ejemplos de ENVIAR y RECIBIR cada tipo de dato dentro de una trama UNER.
 * Es un main.c COMPLETO y probado: se puede copiar entero a main/main.c, o solo lo marcado.
 *
 * Marcas:   >>> UNER      cliente TCP + UNER + valvula
 *           >>> EJEMPLO   los ejemplos de tipos de datos (sacar lo que no uses)
 *
 * Archivos:
 *   implementacionTcpUner.c      este archivo
 *   implementacionTcpUner.html   la seccion "Destino TCP" para pegar en www/wifi.html
 *   emulador_valvula.py          la valvula de mentira en la PC (contesta y hace eco de A0..AF)
 *   main/uner.c, main/uner.h     la libreria del protocolo (ya esta en la plantilla)
 *   main/tcp.c                   tcp_conectar() (ya esta en la plantilla)
 *
 * ---------------------------------------------------------------------------------------
 * TRAMA UNER (uner.h):   'U' 'N' 'E' 'R' | LEN | ':' | CARGA | CHK
 *   LEN = bytes de carga + 1,  CHK = XOR de todos los bytes anteriores.
 *   CARGA[0] = comando; despues sus datos. Mas de un byte: little-endian (el bajo primero).
 *
 *   ENVIAR: armar la carga y llamar a uner_mandar(carga, n). La manda task_valvula,
 *           que es la duenia del socket (nadie mas escribe en el).
 *       uint8_t carga[3];
 *       carga[0] = 0xA2;                      // comando
 *       uner_escribir16(&carga[1], 1500);     // uint16 en los bytes 1 y 2
 *       uner_mandar(carga, 3);
 *
 *   RECIBIR: task_valvula pasa cada byte por uner_rx_byte(); con cada trama valida
 *            llama a procesar_uner(carga, n). Ahi un case por comando:
 *       case 0xA2:
 *           if (n >= 3) { uint16_t v = uner_leer16(&carga[1]); ... }
 *
 *   Tipos:  uint8   carga[i]                         1 byte
 *           int8    (int8_t)carga[i]                 1 byte
 *           uint16  uner_escribir16 / uner_leer16    2 bytes
 *           int16   uner_escribir16(p, (uint16_t)x) / (int16_t)uner_leer16(p)
 *           uint32  uner_escribir32 / uner_leer32    4 bytes  (int32 igual que int16)
 *           float   uner_escribir_float / uner_leer_float   4 bytes
 *           texto   memcpy de los caracteres SIN el '\0'; el largo sale de n
 *
 * PROTOCOLO DE LA VALVULA
 *   ESP -> F0            valvula -> F0 estado      (0D controlable, FF no)
 *   ESP -> D0            valvula -> D0 pos         (0..100), se pide cada 100 ms
 *   ESP -> D1 pos        valvula -> D1 pos estado  (0D ok, FF error)
 *                        valvula -> D2 pos         llego a la posicion (sin pedirlo)
 *   3 s sin ninguna trama valida = falla: cierra, avisa y reintenta cada 1 s.
 *
 * ---------------------------------------------------------------------------------------
 * QUE AGREGAR AL MAIN.C PELADO (lo marcado >>> UNER)
 *   1. main/CMakeLists.txt: uner.c en SRCS (la plantilla ya lo trae).
 *   2. Includes, constantes, tipos, globales y prototipos marcados.
 *   3. Las tres entradas /destino y /valvula en uris[].
 *   4. app_main: mutex, cola y destino de NVS ANTES de web_init; crear task_valvula
 *      pasando &handleValvula.
 *   5. Los comandos destino y valvula en execute().
 *   6. Las funciones de "Cliente UNER" y los handlers de "Pagina web".
 *   7. Pegar implementacionTcpUner.html en www/wifi.html y flashear (idf.py -p COM6 flash
 *      sube la app y www/).
 * ---------------------------------------------------------------------------------------
 *
 * COMO PROBARLA (la PC hace de valvula)
 *   1. En la PC:  python practicas/emulador_valvula.py 5000
 *      (el firewall de Windows tiene que dejar entrar a Python en red privada)
 *   2. En http://IP-del-ESP/wifi.html: IP de la PC y 5000 -> Guardar destino.
 *      O por la consola TCP (MobaXterm, IP:1234):  destino 192.168.123.37 5000
 *   3. Consola:  valvula          {"ip":...,"conectado":true,"posicion":0,...}
 *                valvula 40       manda el setpoint; la posicion sube hasta 40
 *                destino borrar   deshabilita la valvula
 *                ejemplo          manda una trama de cada tipo (A1..A7); el emulador
 *                                 las devuelve iguales (eco)
 *                recibido         muestra lo que se decodifico de esas respuestas
 *      Web:      GET /valvula,  POST /valvula {"setpoint":40},  POST /destino {"ip":"","puerto":0}
 *   4. En el emulador, m + Enter lo deja mudo: a los 3 s el ESP marca falla y reconecta.
 *
 * Si la consigna pide otros nombres (GET /api/valve/status, comandos set-remote...),
 * cambiar solo las URIs de uris[] y los strcmp de execute().
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
#include "freertos/FreeRTOS.h"	// siempre primero de los de FreeRTOS
#include "freertos/task.h"		// xTaskCreate(), vTaskDelay(), task notify
#include "freertos/queue.h"		// colas
#include "freertos/semphr.h"		// mutex y semaforos
#include "freertos/event_groups.h"	// event groups
#include "freertos/timers.h"		// software timers
#include "hardware.h"			// pines: leds[], switches[], pots[], LED_COUNT...
#include "http_parser.h"
#include "iodigital.h"			// salida_init/set/get, entrada_init/get, entrada_interrupcion
#include "ianalogica.h"			// analogica_init/get
#include "pwm.h"				// pwm_init, pwm_set_duty, pwm_set_pulso_us
#include "rgb.h"				// rgb_init, rgb_set (LED RGB de la placa)
#include "temporizador.h"		// temporizador_init (interrupcion periodica)
#include "memoria.h"			// NVS: memoria_init, memoria_get/set_str
#include "wifi.h"				// wifi_init, wifi_sta_conectar
#include "web.h"				// web_init, web_parametro, web_leer_cuerpo, web_campo
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_conectar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe
#include "uner.h"				// >>> UNER: uner_armar, uner_rx_byte, uner_escribir16/leer16...
#include "lwip/sockets.h"		// >>> UNER: inet_pton(): validar la IP del destino
#include "cJSON.h"				// JSON: armar respuestas y leer pedidos de la pagina

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

/* >>> UNER: valvula remota (el ESP es el cliente) */
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

/* >>> EJEMPLO: un comando por tipo de dato (inventados: el emulador los devuelve iguales) */
#define CMD_EJ_U8			0xA1		// u8
#define CMD_EJ_U16			0xA2		// u16
#define CMD_EJ_I16			0xA3		// int16 (con signo)
#define CMD_EJ_U32			0xA4		// u32
#define CMD_EJ_FLOAT		0xA5		// float
#define CMD_EJ_TEXTO		0xA6		// texto (largo variable)
#define CMD_EJ_MIXTO		0xA7		// u8 + int16 + float en una misma trama

/* ======================= Tipos ======================= */

/* >>> UNER */
typedef struct{
	char ip[16];				// destino "255.255.255.255" + '\0'; "" = sin configurar
	uint16_t puerto;
	bool conectado;
	bool falla;					// se corto por latido (3 s sin respuesta)
	bool controlable;			// respuesta a 0xF0
	uint8_t posicion;			// 0..100, ultima informada
	uint8_t setpoint;			// 0..100, ultimo pedido
}valvula_t;

/* >>> UNER: una carga para mandar (la cola guarda copias de esto) */
typedef struct{
	uint8_t n;
	uint8_t carga[UNER_MAX_CARGA];
}mensajeUner_t;

/* >>> EJEMPLO: lo que se decodifico de las respuestas A1..A7 */
typedef struct{
	uint8_t u8;
	uint16_t u16;
	int16_t i16;
	uint32_t u32;
	float real;
	char texto[UNER_MAX_CARGA];		// + '\0' que se agrega al recibir
	uint8_t mixtoU8;
	int16_t mixtoI16;
	float mixtoReal;
	int tramas;						// cuantas respuestas de ejemplo llegaron
}ejemploUner_t;

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar())

/* >>> UNER */
valvula_t valvula = {"", 0, false, false, false, 0, 0};
ejemploUner_t recibido;						// >>> EJEMPLO (global: arranca en 0)
SemaphoreHandle_t mutexValvula;				// protege valvula y recibido
QueueHandle_t colaUner;						// cualquiera -> task_valvula: cargas para mandar
TaskHandle_t handleValvula;					// para avisarle que cambio el destino (task notify)

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void enviar(const char *texto);						// manda texto al cliente TCP
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi
esp_err_t responder_json(httpd_req_t *req, cJSON *obj);	// manda un JSON y lo libera
cJSON *leer_json(httpd_req_t *req);					// lee el cuerpo del pedido como JSON

void task_valvula(void *pvParam);					// >>> UNER: cliente TCP + UNER
bool uner_mandar(const uint8_t *carga, uint8_t n);	// >>> UNER: encola una carga para mandar
bool enviar_uner(int sock, const uint8_t *carga, uint8_t n);	// >>> UNER: arma la trama y la manda
void procesar_uner(const uint8_t *carga, uint8_t n);	// >>> UNER: interpreta una trama recibida
bool destino_guardar(const char *ip, int puerto);	// >>> UNER: valida, guarda en NVS y reconecta
void pedir_setpoint(uint8_t pos);					// >>> UNER: manda D1 pos
cJSON *valvula_json(void);							// >>> UNER: estado como JSON
esp_err_t destino_post(httpd_req_t *req);			// >>> UNER: POST /destino
esp_err_t valvula_get(httpd_req_t *req);			// >>> UNER: GET /valvula
esp_err_t valvula_post(httpd_req_t *req);			// >>> UNER: POST /valvula
void ejemplo_mandar(void);							// >>> EJEMPLO: una trama de cada tipo

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
	{ .uri = "/destino",   .method = HTTP_POST, .handler = destino_post },	// >>> UNER: {"ip":..,"puerto":..}
	{ .uri = "/valvula",   .method = HTTP_GET, .handler = valvula_get },		// >>> UNER: estado + destino
	{ .uri = "/valvula",   .method = HTTP_POST, .handler = valvula_post },	// >>> UNER: {"setpoint":45}
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

	/* >>> UNER: objetos y destino guardado, antes que web_init (los handlers los usan) */
	mutexValvula = xSemaphoreCreateMutex();
	colaUner = xQueueCreate(10, sizeof(mensajeUner_t));
	memoria_get_str("dest_ip", valvula.ip, sizeof(valvula.ip));	// si no hay, queda "" y la tarea espera
	memoria_get_u16("dest_port", &valvula.puerto);

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/
													// sizeof/sizeof = cantidad de elementos de uris[]

	xTaskCreate(task_tcp,
				"task tcp",
				4096,			// microrl + snprintf: el minimo no alcanza
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

	xTaskCreate(task_valvula,	// >>> UNER
				"task valvula",
				4096,
				NULL,
				tskIDLE_PRIORITY + 3,
				&handleValvula);	// el handle: para el task notify de destino_guardar()
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

/* ======================= Comandos (TCP + microrl) ======================= */

/* Microrl la llama al apretar Enter, con la linea ya separada en palabras:
 * argv[0] es el comando, argc cuantas palabras hay. */
int execute(int argc, const char *const *argv)
{
	char respuesta[96];			// para armar textos con numeros (snprintf)

	/* Tus comandos van aca, antes del mensaje de error:
	 *   if (strcmp(argv[0], "xxx") == 0) {		// strcmp devuelve 0 si son iguales
	 *       ...
	 *       return 0;
	 *   }
	 */

	/* >>> UNER: destino 192.168.1.50 5000 | destino borrar */
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

	/* >>> UNER: valvula (muestra el estado) | valvula 45 (pide ir al 45 %) */
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

	/* >>> EJEMPLO: ejemplo (manda A1..A7) | recibido (muestra lo decodificado) */
	if (strcmp(argv[0], "ejemplo") == 0) {
		ejemplo_mandar();
		enviar("mandadas A1..A7 (ver con: recibido)\r\n");
		return 0;
	}
	if (strcmp(argv[0], "recibido") == 0) {
		ejemploUner_t r;
		xSemaphoreTake(mutexValvula, portMAX_DELAY);
		r = recibido;									// copia bajo mutex, imprimir afuera
		xSemaphoreGive(mutexValvula);

		snprintf(respuesta, sizeof(respuesta), "tramas=%d u8=%u u16=%u i16=%d u32=%" PRIu32 "\r\n",
				 r.tramas, r.u8, r.u16, r.i16, r.u32);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "float=%.3f texto=%s\r\n", r.real, r.texto);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "mixto: u8=%u i16=%d float=%.2f\r\n",
				 r.mixtoU8, r.mixtoI16, r.mixtoReal);
		enviar(respuesta);
		return 0;
	}

	snprintf(respuesta, sizeof(respuesta), "ERROR: comando desconocido: %s\r\n", argv[0]);
	enviar(respuesta);
	return 0;					// microrl pide que devuelva int
}

/* Manda texto al cliente TCP (si hay uno). Lo usa microrl para el eco y el
 * prompt, y vos para responder. */
void enviar(const char *texto)
{
	if (sockTcp >= 0) {								// hay alguien conectado
		tcp_enviar(sockTcp, texto, strlen(texto));	// strlen: largo sin el '\0'
	}
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
		sockTcp = sock;								// desde ahora enviar() le manda a el
		telnet_inicio(&telnet, sock);				// MobaXterm: el eco lo hace microrl
		microrl_init(&rl, enviar);					// microrl escribe con enviar()
		microrl_set_execute_callback(&rl, execute);	// al Enter llama a execute()
		enviar("\r\nConsola ESP32\r\n");			// mensaje de bienvenida
		microrl_insert_char(&rl, KEY_CR);			// muestra el prompt

		/* Mientras siga conectado: lo que llega va a microrl */
		while (1) {
			int n = tcp_recibir(sock, buf, sizeof(buf));	// 0 = vencio la espera
			if (n < 0) {
				break;								// se desconecto
			}
			n = telnet_filtrar(&telnet, buf, n);	// saca los comandos Telnet
			for (int i = 0; i < n; i++) {
				microrl_insert_char(&rl, buf[i]);	// eco, edicion; al Enter llama a execute
			}
		}

		/* Se fue el cliente: liberar y esperar otro */
		sockTcp = -1;								// enviar() ya no manda nada
		close(sock);								// cierra el socket de ese cliente
	}
}

/* ======================= Cliente UNER (>>> UNER) ======================= */

/* Se conecta al destino, pregunta si es controlable (F0), pide la posicion cada
 * 100 ms (D0) y manda lo que llegue por colaUner. Si en 3 s no llega ninguna trama
 * valida, marca falla, cierra y reintenta. Si cambian el destino (task notify),
 * cierra y se conecta al nuevo. */
void task_valvula(void *pvParam)
{
	uint8_t buf[64];
	uner_rx_t rx;					// estado del receptor: una trama puede llegar partida
	char ip[16];
	uint16_t puerto;
	mensajeUner_t msj;

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

		int sock = tcp_conectar(ip, puerto, VALVULA_ESPERA_MS);	// bloquea hasta 3 s
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

			/* ENVIAR: todo lo que este en la cola (setpoints, ejemplos...) */
			bool ok = true;
			while (ok && xQueueReceive(colaUner, &msj, 0) == pdTRUE) {
				ok = enviar_uner(sock, msj.carga, msj.n);
			}
			if (!ok) {
				break;
			}

			if (ahora - ultimoPedido >= VALVULA_PERIODO_MS) {
				ultimoPedido = ahora;
				uint8_t carga = CMD_POSICION;
				if (!enviar_uner(sock, &carga, 1)) {
					break;
				}
			}

			/* RECIBIR: los bytes que lleguen, de a uno, al receptor */
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

/* Para mandar desde cualquier tarea o handler: copia la carga a la cola y la manda
 * task_valvula. false si la carga es muy larga o la cola esta llena. */
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

/* RECIBIR: carga[0] = comando, despues sus datos. Siempre chequear n antes de leer. */
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

	/* >>> EJEMPLO: leer cada tipo */
	case CMD_EJ_U8:							// A1 u8
		if (n >= 2) {
			recibido.u8 = carga[1];
			recibido.tramas++;
		}
		break;
	case CMD_EJ_U16:						// A2 u16 (2 bytes)
		if (n >= 3) {
			recibido.u16 = uner_leer16(&carga[1]);
			recibido.tramas++;
		}
		break;
	case CMD_EJ_I16:						// A3 int16: se lee como u16 y se convierte
		if (n >= 3) {
			recibido.i16 = (int16_t)uner_leer16(&carga[1]);
			recibido.tramas++;
		}
		break;
	case CMD_EJ_U32:						// A4 u32 (4 bytes)
		if (n >= 5) {
			recibido.u32 = uner_leer32(&carga[1]);
			recibido.tramas++;
		}
		break;
	case CMD_EJ_FLOAT:						// A5 float (4 bytes)
		if (n >= 5) {
			recibido.real = uner_leer_float(&carga[1]);
			recibido.tramas++;
		}
		break;
	case CMD_EJ_TEXTO:						// A6 texto: n - 1 caracteres, sin '\0'
		memcpy(recibido.texto, &carga[1], n - 1);
		recibido.texto[n - 1] = '\0';		// se lo agrega para usarlo como string
		recibido.tramas++;
		break;
	case CMD_EJ_MIXTO:						// A7 u8 (byte 1) + int16 (2..3) + float (4..7)
		if (n >= 8) {
			recibido.mixtoU8 = carga[1];
			recibido.mixtoI16 = (int16_t)uner_leer16(&carga[2]);
			recibido.mixtoReal = uner_leer_float(&carga[4]);
			recibido.tramas++;
		}
		break;

	default:
		ESP_LOGW("valvula", "Comando desconocido 0x%02X", carga[0]);
		break;
	}
	xSemaphoreGive(mutexValvula);
}

/* >>> EJEMPLO: ENVIAR cada tipo. Cada carga: [comando][datos...] */
void ejemplo_mandar(void)
{
	uint8_t carga[UNER_MAX_CARGA];

	carga[0] = CMD_EJ_U8;					// A1 200
	carga[1] = 200;
	uner_mandar(carga, 2);

	carga[0] = CMD_EJ_U16;					// A2 1500 -> bytes DC 05
	uner_escribir16(&carga[1], 1500);
	uner_mandar(carga, 3);

	carga[0] = CMD_EJ_I16;					// A3 -300 -> bytes D4 FE
	uner_escribir16(&carga[1], (uint16_t)(int16_t)-300);
	uner_mandar(carga, 3);

	carga[0] = CMD_EJ_U32;					// A4 123456 -> bytes 40 E2 01 00
	uner_escribir32(&carga[1], 123456);
	uner_mandar(carga, 5);

	carga[0] = CMD_EJ_FLOAT;				// A5 3.25 -> bytes 00 00 50 40
	uner_escribir_float(&carga[1], 3.25f);
	uner_mandar(carga, 5);

	const char *texto = "hola UNER";		// A6 + los 9 caracteres, sin el '\0'
	carga[0] = CMD_EJ_TEXTO;
	memcpy(&carga[1], texto, strlen(texto));
	uner_mandar(carga, 1 + strlen(texto));

	carga[0] = CMD_EJ_MIXTO;				// A7 7, -12, 98.6 -> 8 bytes de carga
	carga[1] = 7;
	uner_escribir16(&carga[2], (uint16_t)(int16_t)-12);
	uner_escribir_float(&carga[4], 98.6f);
	uner_mandar(carga, 8);
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

/* D1 pos: lo manda task_valvula */
void pedir_setpoint(uint8_t pos)
{
	xSemaphoreTake(mutexValvula, portMAX_DELAY);
	valvula.setpoint = pos;
	xSemaphoreGive(mutexValvula);

	uint8_t carga[] = { CMD_SETPOINT, pos };
	uner_mandar(carga, sizeof(carga));
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

/* >>> UNER: POST /destino {"ip":"192.168.1.50","puerto":5000}; {"ip":"","puerto":0} la deshabilita */
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

/* >>> UNER: GET /valvula: estado y destino actual */
esp_err_t valvula_get(httpd_req_t *req)
{
	return responder_json(req, valvula_json());
}

/* >>> UNER: POST /valvula {"setpoint":45} */
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
