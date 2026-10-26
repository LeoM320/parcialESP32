/* ejemploNvs.c: guardar datos en la flash (NVS) para que sobrevivan a un reinicio o a
 * cortar la alimentacion. Es un main.c COMPLETO y probado (la plantilla con consola TCP +
 * los ejemplos marcados >>> NVS): se puede copiar entero a main/main.c o solo lo marcado.
 *
 * ---------------------------------------------------------------------------------------
 * LA LIBRERIA (main/memoria.h). Una clave = un valor. Claves de hasta 15 caracteres.
 *
 *   memoria_init()                                  una vez, al principio (ya esta en la plantilla)
 *
 *   tipo        guardar                              leer (si no existe, NO toca la variable)
 *   texto       memoria_set_str("nombre", nombre)    memoria_get_str("nombre", nombre, sizeof(nombre))
 *   0..65535    memoria_set_u16("puerto", p)         memoria_get_u16("puerto", &p)
 *   int         memoria_set_i32("entero", n)         memoria_get_i32("entero", &n)      (int32_t)
 *   float, struct, arreglo: los bytes tal cual
 *               memoria_set_blob("cfg", &cfg, sizeof(cfg))
 *                                                    memoria_get_blob("cfg", &cfg, sizeof(cfg))
 *   bool        como u16: memoria_set_u16("activo", activo) / uint16_t x; get; activo = x;
 *   borrar      memoria_borrar("clave")
 *
 * EL PATRON
 *   1. Variable con su valor POR DEFECTO:      int32_t entero = 0;
 *   2. Al arrancar, leer: si habia algo, lo pisa; si no, queda el default.
 *   3. Cuando el usuario cambia algo (comando, web): validar, cambiar la variable y guardar.
 *
 * OJO
 *   - Guardar tarda unos ms y la flash se gasta (~100.000 escrituras por sector): guardar
 *     cuando el usuario cambia algo, NUNCA en un lazo periodico ni desde una ISR.
 *   - Guardar solo si cambio (ver config_post: memcmp antes de guardar).
 *   - "idf.py flash" NO borra lo guardado; "idf.py erase-flash" borra todo (y tambien
 *     la red WiFi guardada).
 *   - Si cambias un struct que guardas con blob, lo viejo mide distinto y no se carga
 *     (queda el default): esta bien, es a proposito.
 *   - Las claves "ssid", "pass", "dest_ip", "dest_port" ya las usa la plantilla.
 * ---------------------------------------------------------------------------------------
 *
 * COMO PROBARLO (consola TCP, MobaXterm -> IP:1234)
 *   ver                      todo lo guardado y cuantas veces arranco la placa
 *   entero -25               int        -> guardado
 *   real 2.5                 float      (blob)
 *   activo on                bool       (u16 0/1)
 *   nombre hola mundo        texto
 *   config 20 90 70 auto     struct     min max vel auto|manual
 *   reiniciar                reinicia la placa: al volver, "ver" muestra lo mismo
 *   borrar entero            borra una clave (en RAM sigue hasta reiniciar)
 *   borrar todo              borra todo lo del ejemplo y vuelve a los defaults
 * Web (Postman o el navegador):
 *   GET  /config             {"min":20,"max":90,"vel":70,"auto":true}
 *   POST /config             {"vel":40}  (solo los campos que vienen; guarda si cambio algo)
 */

/* ======================= Includes ======================= */

#include <stdint.h>				// uint8_t, uint32_t...
#include <stdbool.h>			// bool, true, false
#include <inttypes.h>			// PRIi32 (printf de int32_t)
#include <stdio.h>				// printf(), snprintf()
#include <stdlib.h>				// atoi(), atof(), strtol(), strtof()
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
#include "hardware.h"			// pines: leds[], switches[], pots[], LED_COUNT...
#include "iodigital.h"			// salida_init/set/get, entrada_init/get, entrada_interrupcion
#include "ianalogica.h"			// analogica_init/get
#include "memoria.h"			// NVS: memoria_init, memoria_get/set_str/u16/i32/blob, memoria_borrar
#include "wifi.h"				// wifi_init, wifi_sta_conectar
#include "web.h"				// web_init, web_parametro, web_leer_cuerpo, web_campo
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe
#include "cJSON.h"				// JSON: armar respuestas y leer pedidos de la pagina

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

/* >>> NVS: valores por defecto (los que quedan si nunca se guardo nada) */
#define ENTERO_DEF		0
#define REAL_DEF		1.0f
#define ACTIVO_DEF		false
#define NOMBRE_DEF		"sin nombre"
#define CONFIG_DEF		{30, 80, 50, false}		// min, max, vel, automatico

/* ======================= Tipos ======================= */

/* >>> NVS: una configuracion completa, se guarda de una vez como blob */
typedef struct{
	uint8_t min;				// %
	uint8_t max;				// %
	uint8_t vel;				// %
	bool automatico;
}config_t;

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar())

/* >>> NVS: cada variable arranca con su default; cargar_nvs() la pisa si habia algo guardado */
int32_t entero = ENTERO_DEF;
float real = REAL_DEF;
bool activo = ACTIVO_DEF;
char nombre[17] = NOMBRE_DEF;			// 16 caracteres + '\0'
config_t config = CONFIG_DEF;
uint16_t arranques = 0;					// cuantas veces arranco la placa (se guarda en cada arranque)
SemaphoreHandle_t mutexConfig;			// config la usan la consola y el servidor web (tareas distintas)

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void enviar(const char *texto);						// manda texto al cliente TCP
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi
esp_err_t responder_json(httpd_req_t *req, cJSON *obj);	// manda un JSON y lo libera
cJSON *leer_json(httpd_req_t *req);					// lee el cuerpo del pedido como JSON

void cargar_nvs(void);								// >>> NVS: lee todo al arrancar
cJSON *config_json(void);							// >>> NVS: config como JSON
esp_err_t config_get(httpd_req_t *req);				// >>> NVS: GET /config
esp_err_t config_post(httpd_req_t *req);			// >>> NVS: POST /config

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
	{ .uri = "/config",   .method = HTTP_GET, .handler = config_get },	// >>> NVS
	{ .uri = "/config",   .method = HTTP_POST, .handler = config_post },	// >>> NVS
};

/* ======================= Arranque ======================= */

void app_main(void)
{
	/* Red guardada en NVS; si nunca se guardo, la de menuconfig */
	char ssid[33] = CONFIG_ESP_WIFI_SSID;		// 32 caracteres max + '\0'
	char pass[65] = CONFIG_ESP_WIFI_PASSWORD;	// 64 caracteres max + '\0'

	init_hardware();							// pines antes que nada

	memoria_init();								// NVS: tiene que ir antes que wifi_init y cargar_nvs
	memoria_get_str("ssid", ssid, sizeof(ssid));	// si la clave no existe, ssid no cambia
	memoria_get_str("pass", pass, sizeof(pass));
	wifi_init(CONFIG_ESP_AP_SSID, CONFIG_ESP_AP_PASSWORD, ssid, pass);	// AP propio + conexion al router

	mutexConfig = xSemaphoreCreateMutex();		// >>> NVS: antes de web_init (los handlers lo usan)
	cargar_nvs();								// >>> NVS: antes de crear las tareas que usan los datos

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/

	xTaskCreate(task_tcp,
				"task tcp",
				4096,			// microrl + snprintf: el minimo no alcanza
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);
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

/* ======================= NVS (>>> NVS) ======================= */

/* Al arrancar: cada get pisa la variable solo si habia algo guardado */
void cargar_nvs(void)
{
	memoria_get_i32("entero", &entero);
	memoria_get_blob("real", &real, sizeof(real));			// float: 4 bytes tal cual
	uint16_t a;
	if (memoria_get_u16("activo", &a)) {					// bool guardado como 0/1
		activo = (a != 0);
	}
	memoria_get_str("nombre", nombre, sizeof(nombre));
	memoria_get_blob("config", &config, sizeof(config));	// struct entero de una vez

	/* Contador de arranques: leer, sumar uno y guardar */
	memoria_get_u16("arranques", &arranques);
	arranques++;
	memoria_set_u16("arranques", arranques);
	ESP_LOGI("nvs", "Arranque numero %u", arranques);
}

/* ======================= Comandos (TCP + microrl) ======================= */

/* Microrl la llama al apretar Enter, con la linea ya separada en palabras:
 * argv[0] es el comando, argc cuantas palabras hay. */
int execute(int argc, const char *const *argv)
{
	char respuesta[96];			// para armar textos con numeros (snprintf)

	/* ver: lo que hay en RAM (que es lo guardado, salvo que se haya borrado sin reiniciar) */
	if (strcmp(argv[0], "ver") == 0) {
		config_t c;
		xSemaphoreTake(mutexConfig, portMAX_DELAY);
		c = config;
		xSemaphoreGive(mutexConfig);

		snprintf(respuesta, sizeof(respuesta), "arranques=%u\r\n", arranques);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "entero=%" PRIi32 " real=%.2f activo=%s\r\n",
				 entero, real, activo ? "on" : "off");
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "nombre=%s\r\n", nombre);
		enviar(respuesta);
		snprintf(respuesta, sizeof(respuesta), "config: min=%u max=%u vel=%u modo=%s\r\n",
				 c.min, c.max, c.vel, c.automatico ? "auto" : "manual");
		enviar(respuesta);
		return 0;
	}

	/* entero -25: int32 */
	if (strcmp(argv[0], "entero") == 0) {
		char *fin;
		long v = (argc == 2) ? strtol(argv[1], &fin, 10) : 0;
		if (argc != 2 || *fin != '\0' || fin == argv[1]) {
			enviar("ERROR: uso: entero <numero>\r\n");
			return 0;
		}
		entero = v;
		enviar(memoria_set_i32("entero", entero) ? "guardado\r\n" : "ERROR: no se pudo guardar\r\n");
		return 0;
	}

	/* real 2.5: float como blob (otra opcion: guardar entero * 100 con memoria_set_i32) */
	if (strcmp(argv[0], "real") == 0) {
		char *fin;
		float v = (argc == 2) ? strtof(argv[1], &fin) : 0;
		if (argc != 2 || *fin != '\0' || fin == argv[1]) {
			enviar("ERROR: uso: real <numero con punto>\r\n");
			return 0;
		}
		real = v;
		enviar(memoria_set_blob("real", &real, sizeof(real)) ? "guardado\r\n" : "ERROR: no se pudo guardar\r\n");
		return 0;
	}

	/* activo on|off: bool como u16 (0 o 1) */
	if (strcmp(argv[0], "activo") == 0) {
		if (argc != 2 || (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0)) {
			enviar("ERROR: uso: activo on|off\r\n");
			return 0;
		}
		activo = (strcmp(argv[1], "on") == 0);
		enviar(memoria_set_u16("activo", activo) ? "guardado\r\n" : "ERROR: no se pudo guardar\r\n");
		return 0;
	}

	/* nombre hola mundo: texto (se juntan las palabras con espacios) */
	if (strcmp(argv[0], "nombre") == 0) {
		char texto[sizeof(nombre)] = "";
		size_t largo = 0;
		for (int i = 1; i < argc; i++) {
			largo += strlen(argv[i]) + (i > 1 ? 1 : 0);
		}
		if (argc < 2 || largo >= sizeof(texto)) {
			enviar("ERROR: uso: nombre <1 a 16 caracteres>\r\n");
			return 0;
		}
		for (int i = 1; i < argc; i++) {
			if (i > 1) {
				strcat(texto, " ");
			}
			strcat(texto, argv[i]);
		}
		strcpy(nombre, texto);
		enviar(memoria_set_str("nombre", nombre) ? "guardado\r\n" : "ERROR: no se pudo guardar\r\n");
		return 0;
	}

	/* config 20 90 70 auto: struct entero como blob */
	if (strcmp(argv[0], "config") == 0) {
		config_t c;
		int mn = (argc == 5) ? atoi(argv[1]) : -1;
		int mx = (argc == 5) ? atoi(argv[2]) : -1;
		int vl = (argc == 5) ? atoi(argv[3]) : -1;
		bool modoOk = (argc == 5) && (strcmp(argv[4], "auto") == 0 || strcmp(argv[4], "manual") == 0);
		if (!modoOk || mn < 0 || mx > 100 || mn >= mx || vl < 0 || vl > 100) {
			enviar("ERROR: uso: config <min> <max> <vel> auto|manual  (0 <= min < max <= 100)\r\n");
			return 0;
		}
		c.min = mn;
		c.max = mx;
		c.vel = vl;
		c.automatico = (strcmp(argv[4], "auto") == 0);

		xSemaphoreTake(mutexConfig, portMAX_DELAY);
		config = c;
		xSemaphoreGive(mutexConfig);
		enviar(memoria_set_blob("config", &c, sizeof(c)) ? "guardado\r\n" : "ERROR: no se pudo guardar\r\n");
		return 0;
	}

	/* borrar <clave> | borrar todo */
	if (strcmp(argv[0], "borrar") == 0) {
		if (argc == 2 && strcmp(argv[1], "todo") == 0) {
			const char *claves[] = { "entero", "real", "activo", "nombre", "config", "arranques" };
			for (int i = 0; i < 6; i++) {
				memoria_borrar(claves[i]);
			}
			/* Lo guardado ya no esta; la RAM se vuelve a los defaults a mano */
			config_t def = CONFIG_DEF;
			entero = ENTERO_DEF;
			real = REAL_DEF;
			activo = ACTIVO_DEF;
			strcpy(nombre, NOMBRE_DEF);
			arranques = 0;
			xSemaphoreTake(mutexConfig, portMAX_DELAY);
			config = def;
			xSemaphoreGive(mutexConfig);
			enviar("borrado todo, valores por defecto\r\n");
		} else if (argc == 2) {
			enviar(memoria_borrar(argv[1]) ? "borrado (en RAM sigue hasta reiniciar)\r\n"
										   : "ERROR: esa clave no existe\r\n");
		} else {
			enviar("ERROR: uso: borrar <clave> | borrar todo\r\n");
		}
		return 0;
	}

	/* reiniciar: para comprobar que lo guardado vuelve */
	if (strcmp(argv[0], "reiniciar") == 0) {
		enviar("reiniciando...\r\n");
		vTaskDelay(pdMS_TO_TICKS(200));				// que llegue el texto antes de reiniciar
		esp_restart();
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

/* >>> NVS: copia bajo mutex, JSON afuera. Quien la llama hace cJSON_Delete */
cJSON *config_json(void)
{
	config_t c;
	xSemaphoreTake(mutexConfig, portMAX_DELAY);
	c = config;
	xSemaphoreGive(mutexConfig);

	cJSON *raiz = cJSON_CreateObject();
	cJSON_AddNumberToObject(raiz, "min", c.min);
	cJSON_AddNumberToObject(raiz, "max", c.max);
	cJSON_AddNumberToObject(raiz, "vel", c.vel);
	cJSON_AddBoolToObject(raiz, "auto", c.automatico);
	return raiz;
}

/* >>> NVS: GET /config */
esp_err_t config_get(httpd_req_t *req)
{
	return responder_json(req, config_json());
}

/* >>> NVS: POST /config {"min":20,"max":90,"vel":70,"auto":true} (los que vengan).
 * 1) copia de la config actual  2) aplicar lo que vino a la copia  3) validar la copia
 * 4) si cambio algo: guardar en RAM y en NVS (si no cambio, no gasta flash) */
esp_err_t config_post(httpd_req_t *req)
{
	cJSON *json = leer_json(req);
	if (json == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}

	config_t vieja, nueva;
	xSemaphoreTake(mutexConfig, portMAX_DELAY);
	vieja = config;
	xSemaphoreGive(mutexConfig);
	nueva = vieja;

	bool ok = true;
	cJSON *it;
	if ((it = cJSON_GetObjectItem(json, "min")) != NULL) {
		ok = ok && cJSON_IsNumber(it) && it->valueint >= 0 && it->valueint <= 100;
		nueva.min = it->valueint;
	}
	if ((it = cJSON_GetObjectItem(json, "max")) != NULL) {
		ok = ok && cJSON_IsNumber(it) && it->valueint >= 0 && it->valueint <= 100;
		nueva.max = it->valueint;
	}
	if ((it = cJSON_GetObjectItem(json, "vel")) != NULL) {
		ok = ok && cJSON_IsNumber(it) && it->valueint >= 0 && it->valueint <= 100;
		nueva.vel = it->valueint;
	}
	if ((it = cJSON_GetObjectItem(json, "auto")) != NULL) {
		ok = ok && cJSON_IsBool(it);
		nueva.automatico = cJSON_IsTrue(it);
	}
	cJSON_Delete(json);
	ok = ok && nueva.min < nueva.max;			// validar el conjunto, no solo cada campo

	cJSON *resp = cJSON_CreateObject();
	if (!ok) {
		cJSON_AddBoolToObject(resp, "ok", false);
		cJSON_AddStringToObject(resp, "error", "valores 0..100 y min < max");
		return responder_json(req, resp);
	}

	bool cambio = memcmp(&vieja, &nueva, sizeof(nueva)) != 0;	// byte a byte
	if (cambio) {
		xSemaphoreTake(mutexConfig, portMAX_DELAY);
		config = nueva;
		xSemaphoreGive(mutexConfig);
		ok = memoria_set_blob("config", &nueva, sizeof(nueva));	// la flash fuera del mutex
	}
	cJSON_AddBoolToObject(resp, "ok", ok);
	cJSON_AddBoolToObject(resp, "guardado", cambio);			// false = era igual, no se escribio
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
