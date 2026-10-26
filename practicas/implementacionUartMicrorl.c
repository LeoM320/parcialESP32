/* implementacionUartMicrorl.c: la plantilla (main.c pelado) + la consola microrl por UART
 * + ejemplos de RECIBIR y ENVIAR datos por consola. Es un main.c COMPLETO y probado:
 * se puede copiar entero a main/main.c, o copiar solo las partes marcadas.
 *
 * Marcas:   >>> UART      lo que hace falta para la consola por UART
 *           >>> EJEMPLO   los ejemplos de datos (sacar lo que no uses)
 *
 * ---------------------------------------------------------------------------------------
 * QUE AGREGAR AL MAIN.C PELADO (solo UART)
 *   1. main/CMakeLists.txt: "esp_driver_uart" en PRIV_REQUIRES (la plantilla ya lo trae).
 *   2. Include driver/uart.h, constantes UART_*, globales rlUart, mutexConsola, salida.
 *   3. Prototipos task_uart, enviar_tcp, enviar_uart.
 *   4. app_main: mutexConsola = xSemaphoreCreateMutex() ANTES de crear las tareas,
 *      y xTaskCreate(task_uart, ...).
 *   5. REEMPLAZAR la enviar() del pelado por enviar(), enviar_tcp() y enviar_uart().
 *   6. task_tcp: microrl_init(&rl, enviar_tcp), la bienvenida con enviar_tcp, y el
 *      Take/salida/Give alrededor del for de microrl_insert_char.
 *   7. Copiar task_uart entera.
 * ---------------------------------------------------------------------------------------
 *
 * Como funciona:
 *   - Una instancia de microrl por consola (rl = TCP, rlUart = UART), cada una con su
 *     linea a medio escribir y su historial. Las dos llaman a la MISMA execute().
 *   - enviar() (la que usas en execute) llama a "salida", un puntero a funcion que
 *     apunta a enviar_tcp o enviar_uart: la respuesta vuelve a la consola que pregunto.
 *   - mutexConsola: un comando a la vez (execute nunca corre en las dos tareas juntas).
 *
 * COMO PROBARLA
 *   A) UART0 (COM6, el USB de la placa): MobaXterm -> Session -> Serial -> COM6, 115200.
 *      Tambien con idf.py -p COM6 monitor. Los ESP_LOG salen mezclados con la consola.
 *   B) UART1 con un adaptador CP2102 (otro COM, la consola sola):
 *        ESP GPIO17 (TX) -> RXD del CP2102
 *        ESP GPIO18 (RX) <- TXD del CP2102
 *        GND             -- GND        (NO conectar el 5V ni el 3V3 del CP2102)
 *      Comentar el bloque A y descomentar el B en las constantes.
 *
 * COMANDOS DE EJEMPLO (andan igual por TCP y por UART)
 *   Recibir (el usuario manda un dato, el ESP lo valida y lo guarda):
 *     entero -25          int con rango          -> entero=-25        | ERROR: ...
 *     real 2.5            float (punto o coma)   -> real=2.50
 *     activo on           bool: on/off 1/0 true/false si/no
 *     nombre hola mundo   texto (con espacios)   -> nombre=hola mundo
 *     color verde         enum por nombre        -> color=verde
 *     valores 1 2 3 4     varios numeros         -> valores=1,2,3,4
 *     led 2 on            accion sobre hardware  -> led2=on
 *   Enviar (el ESP manda datos):
 *     ver                 todo en texto, una linea por dato
 *     json                todo en un JSON  {"entero":-25,"real":2.5,...}
 *     pote                los potes: crudo 0..4095 y en %
 *     sw                  los switches: 1 = apretado
 *   Sin que pregunten (aviso espontaneo a las dos consolas):
 *     apretar SW1..SW4    -> "AVISO: SW1 apretado"
 *     avisar              -> lo dispara un comando (mismo camino que el switch)
 */

/* ======================= Includes ======================= */

#include <stdint.h>				// uint8_t, uint32_t...
#include <stdbool.h>			// bool, true, false
#include <inttypes.h>			// PRIu32 (printf de uint32_t)
#include <stdio.h>				// printf(), snprintf()
#include <stdlib.h>				// atoi(), atof(), strtol(), strtof()
#include <string.h>				// strcmp(), strlen(), memcpy(), memcmp()
#include <unistd.h>				// close()
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"			// ESP_LOGI/W/E: mensajes con nombre y color en el monitor
#include "esp_timer.h"			// esp_timer_get_time(): microsegundos desde el arranque
#include "esp_system.h"			// esp_restart(): reiniciar la placa
#include "driver/uart.h"		// >>> UART: uart_driver_install, uart_read_bytes, uart_write_bytes
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
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe
#include "cJSON.h"				// JSON: armar respuestas y leer pedidos de la pagina

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

/* >>> UART: elegir UNA de las dos opciones (comentar la otra) */

/* A) UART0 por el USB de la placa (COM6): la misma del monitor y de los printf */
#define UART_CONSOLA	UART_NUM_0
#define UART_TX_PIN		UART_PIN_NO_CHANGE	// UART0 ya tiene sus pines (43 TX, 44 RX)
#define UART_RX_PIN		UART_PIN_NO_CHANGE

/* B) UART1 con un adaptador CP2102 aparte (otro COM, sin printf mezclados) */
//#define UART_CONSOLA	UART_NUM_1
//#define UART_TX_PIN		17
//#define UART_RX_PIN		18

#define UART_BAUDIOS	115200		// el mismo en la terminal (MobaXterm -> Serial)
#define UART_BUF_RX		256			// buffer del driver (tiene que ser mayor a 128, el FIFO)

#define AVISOS_PERIODO_MS	50		// >>> EJEMPLO: cada cuanto task_avisos mira los switches

/* ======================= Tipos ======================= */

/* >>> EJEMPLO: un dato de cada tipo para recibir y enviar */
typedef enum{
	E_COLOR_ROJO,
	E_COLOR_VERDE,
	E_COLOR_AZUL
}color_e;

typedef struct{
	int entero;					// -1000..1000
	float real;					// 0.0..100.0
	bool activo;
	char nombre[17];			// 16 caracteres + '\0'
	color_e color;
	uint8_t valores[4];			// 0..255 cada uno
}datos_t;

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar_tcp())

microrl_t rlUart;				// >>> UART: otra instancia: cada consola con su linea e historial
SemaphoreHandle_t mutexConsola;			// >>> UART: un comando a la vez (task_tcp o task_uart)
void (*salida)(const char *texto);		// >>> UART: a que consola responde enviar()

/* >>> EJEMPLO: solo los toca execute(), que ya corre de a uno (mutexConsola): no
 * necesitan mutex propio. Si otra tarea los usa, agregarle uno. */
datos_t datos = {0, 0.0f, false, "", E_COLOR_ROJO, {0, 0, 0, 0}};
const char *colores[] = { "rojo", "verde", "azul" };	// mismo orden que color_e
TaskHandle_t handleAvisos;				// para disparar un aviso desde un comando

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void task_uart(void *pvParam);						// >>> UART: tarea de la consola UART
void enviar(const char *texto);						// responde a la consola que mando el comando
void enviar_tcp(const char *texto);					// >>> UART: manda texto al cliente TCP
void enviar_uart(const char *texto);				// >>> UART: manda texto por la UART
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi
esp_err_t responder_json(httpd_req_t *req, cJSON *obj);	// manda un JSON y lo libera
cJSON *leer_json(httpd_req_t *req);					// lee el cuerpo del pedido como JSON

bool leer_entero(const char *texto, int min, int max, int *valor);		// >>> EJEMPLO
bool leer_real(const char *texto, float min, float max, float *valor);	// >>> EJEMPLO
bool leer_bool(const char *texto, bool *valor);							// >>> EJEMPLO
void consola_avisar(const char *texto);				// >>> EJEMPLO: manda a las dos consolas
void task_avisos(void *pvParam);					// >>> EJEMPLO: avisa al apretar un switch

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
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

	mutexConsola = xSemaphoreCreateMutex();		// >>> UART: antes de crear las tareas que lo usan

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/
													// sizeof/sizeof = cantidad de elementos de uris[]

	xTaskCreate(task_tcp,
				"task tcp",
				4096,			// microrl + snprintf: el minimo no alcanza
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

	xTaskCreate(task_uart,		// >>> UART
				"task uart",
				4096,
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

	xTaskCreate(task_avisos,	// >>> EJEMPLO
				"task avisos",
				4096,
				NULL,
				tskIDLE_PRIORITY + 1,
				&handleAvisos);
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
 * argv[0] es el comando, argc cuantas palabras hay (argv[1] es el primer dato). */
int execute(int argc, const char *const *argv)
{
	char respuesta[64];			// para armar textos con numeros (snprintf)

	/* Tus comandos van aca, antes del mensaje de error:
	 *   if (strcmp(argv[0], "xxx") == 0) {		// strcmp devuelve 0 si son iguales
	 *       ...
	 *       return 0;
	 *   }
	 */

	/* ---------- >>> EJEMPLO: RECIBIR datos ----------
	 * Patron: 1) chequear argc  2) convertir y validar  3) guardar  4) confirmar.
	 * Si algo esta mal se responde "ERROR: ..." y NO se cambia nada. */

	/* entero -25 */
	if (strcmp(argv[0], "entero") == 0) {
		int v;
		if (argc != 2 || !leer_entero(argv[1], -1000, 1000, &v)) {
			enviar("ERROR: uso: entero <-1000..1000>\r\n");
			return 0;
		}
		datos.entero = v;
		snprintf(respuesta, sizeof(respuesta), "entero=%d\r\n", datos.entero);
		enviar(respuesta);
		return 0;
	}

	/* real 2.5 (tambien acepta 2,5) */
	if (strcmp(argv[0], "real") == 0) {
		float v;
		if (argc != 2 || !leer_real(argv[1], 0.0f, 100.0f, &v)) {
			enviar("ERROR: uso: real <0.0..100.0>\r\n");
			return 0;
		}
		datos.real = v;
		snprintf(respuesta, sizeof(respuesta), "real=%.2f\r\n", datos.real);	// %.2f: 2 decimales
		enviar(respuesta);
		return 0;
	}

	/* activo on | off | 1 | 0 | true | false | si | no */
	if (strcmp(argv[0], "activo") == 0) {
		bool v;
		if (argc != 2 || !leer_bool(argv[1], &v)) {
			enviar("ERROR: uso: activo on|off\r\n");
			return 0;
		}
		datos.activo = v;
		enviar(datos.activo ? "activo=on\r\n" : "activo=off\r\n");
		return 0;
	}

	/* nombre hola mundo: microrl separo "hola" y "mundo"; se vuelven a juntar con espacios */
	if (strcmp(argv[0], "nombre") == 0) {
		char texto[sizeof(datos.nombre)] = "";
		size_t largo = 0;
		for (int i = 1; i < argc; i++) {
			largo += strlen(argv[i]) + (i > 1 ? 1 : 0);		// +1 por el espacio
		}
		if (argc < 2 || largo >= sizeof(texto)) {
			enviar("ERROR: uso: nombre <texto de 1 a 16 caracteres>\r\n");
			return 0;
		}
		for (int i = 1; i < argc; i++) {
			if (i > 1) {
				strcat(texto, " ");
			}
			strcat(texto, argv[i]);
		}
		strcpy(datos.nombre, texto);
		snprintf(respuesta, sizeof(respuesta), "nombre=%s\r\n", datos.nombre);
		enviar(respuesta);
		return 0;
	}

	/* color verde: se busca el texto en la tabla y la posicion es el valor del enum */
	if (strcmp(argv[0], "color") == 0) {
		int encontrado = -1;
		for (int i = 0; argc == 2 && i < 3; i++) {
			if (strcmp(argv[1], colores[i]) == 0) {
				encontrado = i;
			}
		}
		if (encontrado < 0) {
			enviar("ERROR: uso: color rojo|verde|azul\r\n");
			return 0;
		}
		datos.color = (color_e)encontrado;
		snprintf(respuesta, sizeof(respuesta), "color=%s\r\n", colores[datos.color]);
		enviar(respuesta);
		return 0;
	}

	/* valores 1 2 3 4: se validan TODOS antes de guardar */
	if (strcmp(argv[0], "valores") == 0) {
		int v[4];
		bool ok = (argc == 5);
		for (int i = 0; ok && i < 4; i++) {
			ok = leer_entero(argv[i + 1], 0, 255, &v[i]);
		}
		if (!ok) {
			enviar("ERROR: uso: valores <0..255> <0..255> <0..255> <0..255>\r\n");
			return 0;
		}
		for (int i = 0; i < 4; i++) {
			datos.valores[i] = v[i];
		}
		snprintf(respuesta, sizeof(respuesta), "valores=%u,%u,%u,%u\r\n",
				 datos.valores[0], datos.valores[1], datos.valores[2], datos.valores[3]);
		enviar(respuesta);
		return 0;
	}

	/* led 2 on: numero + estado, y se actua sobre el hardware */
	if (strcmp(argv[0], "led") == 0) {
		int n;
		bool prender;
		if (argc != 3 || !leer_entero(argv[1], 1, LED_COUNT, &n) || !leer_bool(argv[2], &prender)) {
			enviar("ERROR: uso: led <1..4> on|off\r\n");
			return 0;
		}
		salida_set(leds[n - 1], prender);				// leds[0] es el LED 1
		snprintf(respuesta, sizeof(respuesta), "led%d=%s\r\n", n, prender ? "on" : "off");
		enviar(respuesta);
		return 0;
	}

	/* ---------- >>> EJEMPLO: ENVIAR datos ---------- */

	/* ver: todo en texto, una linea por dato (facil de leer en MobaXterm) */
	if (strcmp(argv[0], "ver") == 0) {
		snprintf(respuesta, sizeof(respuesta), "entero=%d\r\n", datos.entero);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "real=%.2f\r\n", datos.real);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "activo=%s\r\n", datos.activo ? "on" : "off");
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "nombre=%s\r\n", datos.nombre);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "color=%s\r\n", colores[datos.color]);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "valores=%u,%u,%u,%u\r\n",
				 datos.valores[0], datos.valores[1], datos.valores[2], datos.valores[3]);
		enviar(respuesta);
		return 0;
	}

	/* json: todo en un JSON (facil de leer desde un programa) */
	if (strcmp(argv[0], "json") == 0) {
		cJSON *raiz = cJSON_CreateObject();
		cJSON_AddNumberToObject(raiz, "entero", datos.entero);
		cJSON_AddNumberToObject(raiz, "real", datos.real);
		cJSON_AddBoolToObject(raiz, "activo", datos.activo);
		cJSON_AddStringToObject(raiz, "nombre", datos.nombre);
		cJSON_AddStringToObject(raiz, "color", colores[datos.color]);
		cJSON *lista = cJSON_AddArrayToObject(raiz, "valores");	// [1,2,3,4]
		for (int i = 0; i < 4; i++) {
			cJSON_AddItemToArray(lista, cJSON_CreateNumber(datos.valores[i]));
		}

		char *texto = cJSON_PrintUnformatted(raiz);	// objeto -> texto (reserva memoria)
		cJSON_Delete(raiz);							// borra el objeto y todo lo de adentro
		if (texto != NULL) {
			enviar(texto);
			enviar("\r\n");
			free(texto);							// el texto tambien se libera
		}
		return 0;
	}

	/* pote: lectura del ADC, cruda y en % */
	if (strcmp(argv[0], "pote") == 0) {
		for (int i = 0; i < POT_COUNT; i++) {
			int crudo = analogica_get(pots[i]);		// 0..4095 (-1 si falla)
			snprintf(respuesta, sizeof(respuesta), "pot%d=%d (%d %%)\r\n",	// %% imprime un %
					 i + 1, crudo, crudo * 100 / 4095);
			enviar(respuesta);
		}
		return 0;
	}

	/* sw: estado de los switches (1 = apretado) */
	if (strcmp(argv[0], "sw") == 0) {
		snprintf(respuesta, sizeof(respuesta), "sw=%d%d%d%d\r\n",
				 !entrada_get(SW1_GPIO), !entrada_get(SW2_GPIO),	// apretado = 0: se invierte
				 !entrada_get(SW3_GPIO), !entrada_get(SW4_GPIO));
		enviar(respuesta);
		return 0;
	}

	/* avisar: dispara el mismo aviso que un switch. No se llama a consola_avisar() desde
	 * aca porque execute ya tiene mutexConsola (se trabaria): se le avisa a la tarea. */
	if (strcmp(argv[0], "avisar") == 0) {
		xTaskNotifyGive(handleAvisos);
		enviar("ok\r\n");
		return 0;
	}

	snprintf(respuesta, sizeof(respuesta), "ERROR: comando desconocido: %s\r\n", argv[0]);
	enviar(respuesta);
	return 0;					// microrl pide que devuelva int
}

/* ---------- >>> EJEMPLO: convertir texto a numero, validando ----------
 * atoi("hola") devuelve 0 sin avisar; strtol/strtof dicen hasta donde pudieron leer:
 * si no llegaron al final del texto, habia algo que no era numero. */

bool leer_entero(const char *texto, int min, int max, int *valor)
{
	char *fin;
	long v = strtol(texto, &fin, 10);			// base 10
	if (fin == texto || *fin != '\0' || v < min || v > max) {
		return false;							// vacio, con letras o fuera de rango
	}
	*valor = (int)v;
	return true;
}

bool leer_real(const char *texto, float min, float max, float *valor)
{
	char copia[16];
	if (strlen(texto) >= sizeof(copia)) {
		return false;
	}
	strcpy(copia, texto);
	for (char *p = copia; *p != '\0'; p++) {	// acepta coma decimal: "2,5" -> "2.5"
		if (*p == ',') {
			*p = '.';
		}
	}
	char *fin;
	float v = strtof(copia, &fin);
	if (fin == copia || *fin != '\0' || v < min || v > max) {
		return false;
	}
	*valor = v;
	return true;
}

bool leer_bool(const char *texto, bool *valor)
{
	if (strcmp(texto, "on") == 0 || strcmp(texto, "1") == 0 ||
		strcmp(texto, "true") == 0 || strcmp(texto, "si") == 0) {
		*valor = true;
		return true;
	}
	if (strcmp(texto, "off") == 0 || strcmp(texto, "0") == 0 ||
		strcmp(texto, "false") == 0 || strcmp(texto, "no") == 0) {
		*valor = false;
		return true;
	}
	return false;
}

/* ---------- >>> EJEMPLO: ENVIAR sin que pregunten ----------
 * Desde una tarea que NO es la consola: tomar mutexConsola (para no mezclarse con
 * un comando a medio responder) y escribir en las dos consolas. */
void consola_avisar(const char *texto)
{
	xSemaphoreTake(mutexConsola, portMAX_DELAY);
	enviar_uart("\r\n");
	enviar_uart(texto);
	enviar_uart("\r\n");
	enviar_tcp("\r\n");
	enviar_tcp(texto);
	enviar_tcp("\r\n");
	xSemaphoreGive(mutexConsola);
}

/* Mira los switches cada 50 ms; al apretar uno (paso de suelto a apretado) avisa.
 * Tambien avisa si un comando le manda un task notify (comando "avisar"). */
void task_avisos(void *pvParam)
{
	bool antes[SW_COUNT] = {false};
	char texto[48];

	while (1) {
		if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AVISOS_PERIODO_MS)) > 0) {	// espera 50 ms o un aviso
			consola_avisar("AVISO: pedido por consola");
		}
		for (int i = 0; i < SW_COUNT; i++) {
			bool apretado = !entrada_get(switches[i]);	// apretado = 0
			if (apretado && !antes[i]) {				// flanco: recien apretado
				snprintf(texto, sizeof(texto), "AVISO: SW%d apretado", i + 1);
				consola_avisar(texto);
			}
			antes[i] = apretado;
		}
	}
}

/* ======================= Salida de las consolas (>>> UART) ======================= */

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
		microrl_init(&rl, enviar_tcp);				// >>> UART: microrl escribe con enviar_tcp()
		microrl_set_execute_callback(&rl, execute);	// al Enter llama a execute()
		enviar_tcp("\r\nConsola ESP32 (TCP)\r\n");	// >>> UART: enviar_tcp en vez de enviar
		microrl_insert_char(&rl, KEY_CR);			// muestra el prompt

		/* Mientras siga conectado: lo que llega va a microrl */
		while (1) {
			int n = tcp_recibir(sock, buf, sizeof(buf));	// 0 = vencio la espera
			if (n < 0) {
				break;								// se desconecto
			}
			n = telnet_filtrar(&telnet, buf, n);	// saca los comandos Telnet

			xSemaphoreTake(mutexConsola, portMAX_DELAY);	// >>> UART: si la UART esta ejecutando, espera
			salida = enviar_tcp;					// >>> UART: lo que responda execute() va al cliente TCP
			for (int i = 0; i < n; i++) {
				microrl_insert_char(&rl, buf[i]);	// eco, edicion; al Enter llama a execute
			}
			xSemaphoreGive(mutexConsola);			// >>> UART
		}

		/* Se fue el cliente: liberar y esperar otro */
		sockTcp = -1;								// enviar_tcp() ya no manda nada
		close(sock);								// cierra el socket de ese cliente
	}
}

/* >>> UART: la misma consola por UART (UART0 del USB o UART1 con CP2102, ver UART_CONSOLA).
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
