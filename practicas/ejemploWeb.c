/* main.c: tu programa.
 *
 * Las librerias solo ponen en marcha cosas (GPIO, ADC, NVS, WiFi, servidor web,
 * sockets TCP, microrl). Todo lo demas esta aca, a la vista:
 *
 *   - task_tcp:    atiende un cliente TCP (MobaXterm, Telnet) con microrl.
 *   - execute():   los comandos que llegan por TCP.
 *   - handlers web: lo que hace cada direccion de la pagina (/wifi).
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
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe
#include "cJSON.h"				// JSON: armar respuestas y leer pedidos de la pagina

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar())

/* Configuracion que cambia la pagina (POST /config) y usa task_umbral.
 * Dos tareas la tocan (la del servidor web y task_umbral): se accede con mutex. */
typedef struct {
	int umbral;					// si el pote 1 pasa este valor, se prende el LED 3
} config_t;

config_t config = { 2000 };		// valor inicial
SemaphoreHandle_t mutexConfig;	// se crea en app_main, antes de las tareas

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void enviar(const char *texto);						// manda texto al cliente TCP
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi
esp_err_t toggle_led1(httpd_req_t *req);				// GET /led1: invierte el LED 1
esp_err_t estado_get(httpd_req_t *req);				// GET /estado: potes, LEDs y config en JSON
esp_err_t led_post(httpd_req_t *req);				// POST /led {"n":2,"v":true}
esp_err_t config_post(httpd_req_t *req);			// POST /config {"umbral":1500}
esp_err_t responder_json(httpd_req_t *req, cJSON *obj);	// manda un JSON y lo libera
cJSON *leer_json(httpd_req_t *req);					// lee el cuerpo del pedido como JSON
void task_umbral(void *pvParam);					// prende el LED 3 si el pote 1 pasa el umbral

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
	{ .uri = "/led1",   .method = HTTP_GET,  .handler = toggle_led1 },
	{ .uri = "/estado", .method = HTTP_GET,  .handler = estado_get },
	{ .uri = "/led",    .method = HTTP_POST, .handler = led_post },
	{ .uri = "/config", .method = HTTP_POST, .handler = config_post },
};

/* ======================= Arranque ======================= */

void app_main(void)
{
	/* Red guardada en NVS; si nunca se guardo, la de menuconfig */
	char ssid[33] = CONFIG_ESP_WIFI_SSID;		// 32 caracteres max + '\0'
	char pass[65] = CONFIG_ESP_WIFI_PASSWORD;	// 64 caracteres max + '\0'

	init_hardware();							// pines antes que nada
	mutexConfig = xSemaphoreCreateMutex();		// antes de web_init: los handlers lo usan apenas arranca el servidor

	memoria_init();								// NVS: tiene que ir antes que wifi_init
	memoria_get_str("ssid", ssid, sizeof(ssid));	// si la clave no existe, ssid no cambia
	memoria_get_str("pass", pass, sizeof(pass));
	wifi_init(CONFIG_ESP_AP_SSID, CONFIG_ESP_AP_PASSWORD, ssid, pass);	// AP propio + conexion al router

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/
													// sizeof/sizeof = cantidad de elementos de uris[]

	xTaskCreate(task_tcp,
				"task tcp",
				4096,			// microrl + snprintf: el minimo no alcanza
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);

	xTaskCreate(task_umbral,
				"task umbral",
				4096,
				NULL,
				tskIDLE_PRIORITY + 1,
				NULL);
	// app_main termina aca; las tareas, el WiFi y el servidor web siguen corriendo
}

/* Cada 100 ms compara el pote 1 con el umbral que puso la pagina */
void task_umbral(void *pvParam)
{
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while (1) {
		int pot = analogica_get(POT1_GPIO);			// una lectura, fuera del mutex

		xSemaphoreTake(mutexConfig, portMAX_DELAY);
		int umbral = config.umbral;					// copia rapida
		xSemaphoreGive(mutexConfig);

		salida_set(LED3_GPIO, pot > umbral);
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
	}
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
	char respuesta[64];			// para armar textos con numeros (snprintf)

	/* Tus comandos van aca, antes del mensaje de error:
	 *   if (strcmp(argv[0], "xxx") == 0) {		// strcmp devuelve 0 si son iguales
	 *       ...
	 *       return 0;
	 *   }
	 */

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

/* GET /led1: invierte el LED 1 y responde como quedo, {"led1":true} */
esp_err_t toggle_led1(httpd_req_t *req)
{
	bool nuevo = !salida_get(LED1_GPIO);
	salida_set(LED1_GPIO, nuevo);

	cJSON *raiz = cJSON_CreateObject();				// {}
	cJSON_AddBoolToObject(raiz, "led1", nuevo);		// {"led1":true}
	return responder_json(req, raiz);				// manda y libera
}

/* GET /estado: todo lo que muestra la pagina, en un solo JSON
 * {"pot1":1971,"pot2":3390,"led1":false,...,"led4":false,"umbral":2000} */
esp_err_t estado_get(httpd_req_t *req)
{
	cJSON *raiz = cJSON_CreateObject();
	cJSON_AddNumberToObject(raiz, "pot1", analogica_get(POT1_GPIO));
	cJSON_AddNumberToObject(raiz, "pot2", analogica_get(POT2_GPIO));

	char nombre[8];
	for (int i = 0; i < LED_COUNT; i++) {
		snprintf(nombre, sizeof(nombre), "led%d", i + 1);		// "led1", "led2"...
		cJSON_AddBoolToObject(raiz, nombre, salida_get(leds[i]));
	}

	xSemaphoreTake(mutexConfig, portMAX_DELAY);
	int umbral = config.umbral;						// copia rapida con el mutex
	xSemaphoreGive(mutexConfig);
	cJSON_AddNumberToObject(raiz, "umbral", umbral);

	return responder_json(req, raiz);
}

/* POST /led {"n":2,"v":true}: prende o apaga un LED */
esp_err_t led_post(httpd_req_t *req)
{
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *n = cJSON_GetObjectItem(raiz, "n");		// NULL si no vino
	cJSON *v = cJSON_GetObjectItem(raiz, "v");
	if (!cJSON_IsNumber(n) || !cJSON_IsBool(v) || n->valueint < 1 || n->valueint > LED_COUNT) {
		cJSON_Delete(raiz);							// liberar tambien al salir por error
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Faltan n (1-4) y v (true/false)");
	}
	salida_set(leds[n->valueint - 1], cJSON_IsTrue(v));	// LED 1 = leds[0]
	cJSON_Delete(raiz);
	return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* POST /config {"umbral":1500}: cambia el umbral que usa task_umbral */
esp_err_t config_post(httpd_req_t *req)
{
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *umbral = cJSON_GetObjectItem(raiz, "umbral");
	if (!cJSON_IsNumber(umbral) || umbral->valueint < 0 || umbral->valueint > 4095) {
		cJSON_Delete(raiz);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta umbral (0-4095)");
	}
	xSemaphoreTake(mutexConfig, portMAX_DELAY);		// config la lee task_umbral
	config.umbral = umbral->valueint;
	xSemaphoreGive(mutexConfig);

	cJSON_Delete(raiz);
	return httpd_resp_sendstr(req, "{\"ok\":true}");
}

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
