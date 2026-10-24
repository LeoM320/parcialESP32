/* main.c: tu programa.
 *
 * Las librerias solo ponen en marcha cosas (GPIO, ADC, NVS, WiFi, servidor web,
 * sockets TCP, microrl). Todo lo demas esta aca, a la vista:
 *
 *   - task_tcp:    atiende un cliente TCP (MobaXterm, Telnet) con microrl.
 *   - execute():   los comandos que llegan por TCP.
 *   - handlers web: lo que hace cada direccion de la pagina (/led, /estado, /wifi).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>				// close()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware.h"
#include "iodigital.h"
#include "ianalogica.h"
#include "memoria.h"
#include "wifi.h"
#include "web.h"
#include "tcp.h"
#include "telnet.h"
#include "microrl.h"

#define PUERTO_TCP		1234
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

microrl_t rl;
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno

void init_hardware(void);
void task_tcp(void *pvParam);
void enviar(const char *texto);
int execute(int argc, const char *const *argv);
esp_err_t led_get(httpd_req_t *req);
esp_err_t estado_get(httpd_req_t *req);
esp_err_t wifi_post(httpd_req_t *req);

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/led",    .method = HTTP_GET,  .handler = led_get },
	{ .uri = "/estado", .method = HTTP_GET,  .handler = estado_get },
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },
};

void app_main(void)
{
	/* Red guardada en NVS; si nunca se guardo, la de menuconfig */
	char ssid[33] = CONFIG_ESP_WIFI_SSID;
	char pass[65] = CONFIG_ESP_WIFI_PASSWORD;

	init_hardware();

	memoria_init();
	memoria_get_str("ssid", ssid, sizeof(ssid));
	memoria_get_str("pass", pass, sizeof(pass));
	wifi_init(CONFIG_ESP_AP_SSID, CONFIG_ESP_AP_PASSWORD, ssid, pass);

	web_init(uris, sizeof(uris) / sizeof(uris[0]));

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
		salida_init(leds[i]);
	}
	for (int i = 0; i < SW_COUNT; i++) {
		entrada_init(switches[i], GPIO_PULLUP_ONLY);	// apretado = 0
	}
	for (int i = 0; i < POT_COUNT; i++) {
		analogica_init(pots[i]);
	}
}

/* ======================= Comandos (TCP + microrl) ======================= */

/* Microrl la llama al apretar Enter, con la linea ya separada en palabras:
 * argv[0] es el comando, argc cuantas palabras hay. */
int execute(int argc, const char *const *argv)
{
	char respuesta[64];

	if (strcmp(argv[0], "hola") == 0) {
		enviar("Hola\r\n");
	} else {
		snprintf(respuesta, sizeof(respuesta), "ERROR: comando desconocido: %s\r\n", argv[0]);
		enviar(respuesta);
	}
	return 0;
}

/* Manda texto al cliente TCP (si hay uno). Lo usa microrl para el eco y el
 * prompt, y vos para responder. */
void enviar(const char *texto)
{
	if (sockTcp >= 0) {
		tcp_enviar(sockTcp, texto, strlen(texto));
	}
}

void task_tcp(void *pvParam)
{
	char buf[64];
	telnet_t telnet;

	int servidor = tcp_servidor(PUERTO_TCP, TCP_ESPERA_MS);
	if (servidor < 0) {
		vTaskDelete(NULL);
	}

	while (1) {
		int sock = tcp_aceptar(servidor);			// -1 = nadie se conecto en TCP_ESPERA_MS
		if (sock < 0) {
			continue;
		}

		sockTcp = sock;
		telnet_inicio(&telnet, sock);				// MobaXterm: el eco lo hace microrl
		microrl_init(&rl, enviar);
		microrl_set_execute_callback(&rl, execute);
		enviar("\r\nConsola ESP32\r\n");
		microrl_insert_char(&rl, KEY_CR);			// muestra el prompt

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

		sockTcp = -1;
		close(sock);
	}
}

/* ======================= Pagina web ======================= */

/* GET /led?n=2&v=1   n: 1..4   v: 1 prende, 0 apaga */
esp_err_t led_get(httpd_req_t *req)
{
	char n[4], v[4];

	if (!web_parametro(req, "n", n, sizeof(n)) || !web_parametro(req, "v", v, sizeof(v))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Faltan n y v");
	}
	int i = atoi(n) - 1;
	if (i < 0 || i >= LED_COUNT) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No existe ese LED");
	}
	salida_set(leds[i], strcmp(v, "1") == 0);
	return httpd_resp_sendstr(req, "OK");
}

/* GET /estado: texto con lo que quieras ver en la pagina */
esp_err_t estado_get(httpd_req_t *req)
{
	char txt[256];
	int largo = 0;

	for (int i = 0; i < LED_COUNT; i++) {
		largo += snprintf(txt + largo, sizeof(txt) - largo, "LED %d: %s\n",
						  i + 1, salida_get(leds[i]) ? "prendido" : "apagado");
	}
	for (int i = 0; i < SW_COUNT; i++) {
		largo += snprintf(txt + largo, sizeof(txt) - largo, "SW %d: %s\n",
						  i + 1, entrada_get(switches[i]) ? "suelto" : "apretado");
	}
	for (int i = 0; i < POT_COUNT; i++) {
		largo += snprintf(txt + largo, sizeof(txt) - largo, "Pote %d: %d\n",
						  i + 1, analogica_get(pots[i]));
	}
	httpd_resp_set_type(req, "text/plain");
	return httpd_resp_sendstr(req, txt);
}

/* POST /wifi (formulario de wifi.html): guarda la red en NVS y se conecta */
esp_err_t wifi_post(httpd_req_t *req)
{
	char cuerpo[200], ssid[33], pass[65];

	if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0 ||
		!web_campo(cuerpo, "ssid", ssid, sizeof(ssid)) ||
		!web_campo(cuerpo, "pass", pass, sizeof(pass))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Datos invalidos");
	}
	memoria_set_str("ssid", ssid);
	memoria_set_str("pass", pass);
	wifi_sta_conectar(ssid, pass);
	return httpd_resp_sendstr(req, "Guardado. Conectando... (la IP nueva sale en el monitor)");
}
