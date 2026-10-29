/* ejemploMicrorl.c: ejemplo de consola TCP con microrl (para estudiar).
 *
 * Para probarlo: copiarlo sobre main/main.c, flashear y conectarse con MobaXterm
 * (Telnet, IP del ESP, puerto 1234). Escribir "help".
 *
 * Comandos (todos en execute()):
 *   hola                   sin argumentos
 *   help                   lista de comandos
 *   led <1-4> <on|off>     dos argumentos, con validacion
 *   toggle <1-4>           un argumento
 *   pote                   consulta: responde los dos potes
 *   rgb <r> <g> <b>        tres numeros (LED RGB de la placa)
 *   estado                 responde todo en JSON (cJSON, igual que la web)
 *   umbral [valor]         con valor lo cambia, sin valor lo muestra (mutex con task_umbral)
 *   parpadeo <1-4> <ms>    le manda la orden a task_parpadeo por una cola y contesta al toque
 *   parar                  frena el parpadeo
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

/* Umbral: lo cambia el comando "umbral" (task_tcp) y lo lee task_umbral -> mutex */
typedef struct {
	int umbral;					// si el pote 1 pasa este valor, se prende el LED 3
} config_t;

config_t config = { 2000 };
SemaphoreHandle_t mutexConfig;

/* Orden de parpadeo: la manda el comando "parpadeo" y la recibe task_parpadeo */
typedef struct {
	int led;					// indice 0..3 (LED 1 = 0); -1 = parar
	int periodo_ms;
} orden_parpadeo_t;

QueueHandle_t colaParpadeo;

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void enviar(const char *texto);						// manda texto al cliente TCP
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi
esp_err_t responder_json(httpd_req_t *req, cJSON *obj);	// manda un JSON y lo libera
cJSON *leer_json(httpd_req_t *req);					// lee el cuerpo del pedido como JSON
void task_umbral(void *pvParam);					// LED 3 = pote 1 > umbral
void task_parpadeo(void *pvParam);					// hace parpadear el LED que le pidan

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
	rgb_init(RGB_GPIO);							// LED RGB de la placa (comando "rgb")

	/* Antes de crear las tareas que los usan */
	mutexConfig = xSemaphoreCreateMutex();
	colaParpadeo = xQueueCreate(4, sizeof(orden_parpadeo_t));	// hasta 4 ordenes en fila

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

	xTaskCreate(task_parpadeo,
				"task parpadeo",
				4096,
				NULL,
				tskIDLE_PRIORITY + 1,
				NULL);
	// app_main termina aca; las tareas, el WiFi y el servidor web siguen corriendo
}

/* Cada 100 ms compara el pote 1 con el umbral */
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

/* Espera ordenes por la cola. Mientras tiene una, invierte ese LED cada periodo_ms.
 * La espera de la cola ES el delay: si llega una orden nueva, la atiende enseguida. */
void task_parpadeo(void *pvParam)
{
	orden_parpadeo_t orden = { -1, 0 };				// arranca sin parpadear
	while (1) {
		TickType_t espera = (orden.led < 0) ? portMAX_DELAY : pdMS_TO_TICKS(orden.periodo_ms);
		orden_parpadeo_t nueva;
		if (xQueueReceive(colaParpadeo, &nueva, espera) == pdTRUE) {
			if (orden.led >= 0) {
				salida_set(leds[orden.led], 0);		// apagar el LED anterior
			}
			orden = nueva;							// llego una orden: cambiar
		} else if (orden.led >= 0) {
			salida_set(leds[orden.led], !salida_get(leds[orden.led]));	// timeout = toca invertir
		}
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

	/* hola: sin argumentos */
	if (strcmp(argv[0], "hola") == 0) {			// strcmp devuelve 0 si son iguales
		enviar("Hola! Soy el ESP32\r\n");
		return 0;
	}

	/* help: lista de comandos */
	if (strcmp(argv[0], "help") == 0) {
		enviar("hola\r\n"
			   "led <1-4> <on|off>\r\n"
			   "toggle <1-4>\r\n"
			   "pote\r\n"
			   "rgb <r> <g> <b>      (0-255)\r\n"
			   "estado               (JSON)\r\n"
			   "umbral [0-4095]      (LED 3 = pote 1 > umbral)\r\n"
			   "parpadeo <1-4> <ms>\r\n"
			   "parar\r\n");					// textos seguidos: C los pega en uno solo
		return 0;
	}

	/* led <1-4> <on|off>: dos argumentos -> argc tiene que ser 3 */
	if (strcmp(argv[0], "led") == 0) {
		if (argc != 3) {
			enviar("Uso: led <1-4> <on|off>\r\n");
			return 0;
		}
		int n = atoi(argv[1]);					// "2" -> 2 (si no es numero da 0)
		if (n < 1 || n > LED_COUNT) {
			enviar("ERROR: el LED va de 1 a 4\r\n");
			return 0;
		}
		if (strcmp(argv[2], "on") == 0) {
			salida_set(leds[n - 1], 1);			// LED 1 = leds[0]
		} else if (strcmp(argv[2], "off") == 0) {
			salida_set(leds[n - 1], 0);
		} else {
			enviar("ERROR: tiene que ser on u off\r\n");
			return 0;
		}
		snprintf(respuesta, sizeof(respuesta), "LED %d %s\r\n", n, argv[2]);
		enviar(respuesta);
		return 0;
	}

	/* toggle <1-4>: un argumento -> argc 2 */
	if (strcmp(argv[0], "toggle") == 0) {
		int n = (argc == 2) ? atoi(argv[1]) : 0;	// sin argumento: 0 = invalido
		if (n < 1 || n > LED_COUNT) {
			enviar("Uso: toggle <1-4>\r\n");
			return 0;
		}
		bool nuevo = !salida_get(leds[n - 1]);
		salida_set(leds[n - 1], nuevo);
		snprintf(respuesta, sizeof(respuesta), "LED %d %s\r\n", n, nuevo ? "on" : "off");
		enviar(respuesta);
		return 0;
	}

	/* pote: consulta, responde con numeros */
	if (strcmp(argv[0], "pote") == 0) {
		snprintf(respuesta, sizeof(respuesta), "pote1=%d pote2=%d\r\n",
				 analogica_get(POT1_GPIO), analogica_get(POT2_GPIO));
		enviar(respuesta);
		return 0;
	}

	/* rgb <r> <g> <b>: tres numeros -> argc 4 */
	if (strcmp(argv[0], "rgb") == 0) {
		if (argc != 4) {
			enviar("Uso: rgb <r> <g> <b>   (0-255)\r\n");
			return 0;
		}
		int r = atoi(argv[1]);
		int g = atoi(argv[2]);
		int b = atoi(argv[3]);
		if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
			enviar("ERROR: cada color va de 0 a 255\r\n");
			return 0;
		}
		rgb_set(r, g, b);
		snprintf(respuesta, sizeof(respuesta), "RGB %d %d %d\r\n", r, g, b);
		enviar(respuesta);
		return 0;
	}

	/* estado: responde todo en una linea JSON (lo mismo que haria GET /estado) */
	if (strcmp(argv[0], "estado") == 0) {
		xSemaphoreTake(mutexConfig, portMAX_DELAY);
		int umbral = config.umbral;				// copia rapida
		xSemaphoreGive(mutexConfig);

		cJSON *obj = cJSON_CreateObject();
		cJSON_AddNumberToObject(obj, "pot1", analogica_get(POT1_GPIO));
		cJSON_AddNumberToObject(obj, "pot2", analogica_get(POT2_GPIO));
		cJSON *arr = cJSON_AddArrayToObject(obj, "leds");	// [true,false,...]
		for (int i = 0; i < LED_COUNT; i++) {
			cJSON_AddItemToArray(arr, cJSON_CreateBool(salida_get(leds[i])));
		}
		cJSON_AddNumberToObject(obj, "umbral", umbral);

		char *texto = cJSON_PrintUnformatted(obj);	// objeto -> texto (reserva memoria)
		cJSON_Delete(obj);
		if (texto != NULL) {
			enviar(texto);						// puede ser largo: no pasa por respuesta[64]
			enviar("\r\n");
			free(texto);
		}
		return 0;
	}

	/* umbral [valor]: sin valor lo muestra, con valor lo cambia */
	if (strcmp(argv[0], "umbral") == 0) {
		if (argc == 2) {
			int u = atoi(argv[1]);
			if (u < 0 || u > 4095) {
				enviar("ERROR: el umbral va de 0 a 4095\r\n");
				return 0;
			}
			xSemaphoreTake(mutexConfig, portMAX_DELAY);
			config.umbral = u;					// task_umbral lo usa en la proxima vuelta
			xSemaphoreGive(mutexConfig);
		}
		xSemaphoreTake(mutexConfig, portMAX_DELAY);
		int u = config.umbral;
		xSemaphoreGive(mutexConfig);
		snprintf(respuesta, sizeof(respuesta), "umbral=%d\r\n", u);
		enviar(respuesta);
		return 0;
	}

	/* parpadeo <1-4> <ms>: el trabajo largo lo hace otra tarea; aca solo se manda la orden */
	if (strcmp(argv[0], "parpadeo") == 0) {
		if (argc != 3) {
			enviar("Uso: parpadeo <1-4> <ms>\r\n");
			return 0;
		}
		int n = atoi(argv[1]);
		int ms = atoi(argv[2]);
		if (n < 1 || n > LED_COUNT || ms < 20 || ms > 5000) {
			enviar("ERROR: LED 1-4, ms entre 20 y 5000\r\n");
			return 0;
		}
		orden_parpadeo_t orden = { n - 1, ms };
		if (xQueueSend(colaParpadeo, &orden, 0) != pdTRUE) {	// 0 = no esperar si esta llena
			enviar("ERROR: cola llena\r\n");
			return 0;
		}
		snprintf(respuesta, sizeof(respuesta), "LED %d parpadea cada %d ms\r\n", n, ms);
		enviar(respuesta);
		return 0;
	}

	/* parar: misma cola, led = -1 */
	if (strcmp(argv[0], "parar") == 0) {
		orden_parpadeo_t orden = { -1, 0 };
		xQueueSend(colaParpadeo, &orden, 0);
		enviar("Parpadeo detenido\r\n");
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
