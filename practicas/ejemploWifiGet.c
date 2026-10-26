/* ejemploWifiGet.c: el ejemplo de wifi del template, pero todo con GET.
 *
 * No es un main completo: son las piezas para copiar a main.c.
 * La pagina esta en ejemploWifiGet.html (va en www/wifiget.html).
 *
 * Con GET los datos viajan en la URL, asi que se prueba escribiendo en el navegador:
 *   http://192.168.123.38/wifi/ver                         -> {"ssid":"MiRed","ip":"192.168..."}
 *   http://192.168.123.38/wifi/guardar?ssid=MiRed&pass=123 -> {"ok":true}
 *   http://192.168.123.38/wifi/guardar?pass=123            -> {"ok":false,"error":"falta ssid"}
 *
 * Diferencias con el POST original (wifi_post en main.c):
 *   - POST: los datos van en el cuerpo -> web_leer_cuerpo() + web_campo().
 *   - GET:  los datos van en la URL    -> web_parametro() (ya decodifica "mi+red" -> "mi red").
 *
 * OJO: es para depurar. Con GET la clave queda en el historial del navegador y un F5
 * repite el pedido. En el parcial usa el metodo que pida la consigna.
 */

/* ======================= Includes ======================= */

#include <stdbool.h>
#include <string.h>				// strlen()
#include "esp_http_server.h"
#include "esp_netif.h"			// esp_netif_get_ip_info(): la IP actual
#include "memoria.h"			// memoria_get_str, memoria_set_str
#include "wifi.h"				// wifi_sta_conectar
#include "web.h"				// web_parametro
#include "cJSON.h"

/* ======================= Prototipos (copiar arriba en main.c) ======================= */

esp_err_t responder_json(httpd_req_t *req, cJSON *obj);		// ya esta en main.c

esp_err_t wifi_ver_get(httpd_req_t *req);		// GET /wifi/ver: red guardada e IP actual
esp_err_t wifi_guardar_get(httpd_req_t *req);	// GET /wifi/guardar?ssid=..&pass=..: guarda y conecta

/* Asi quedan las entradas en uris[] de main.c */
const httpd_uri_t uris_wifi_get[] = {
	{ .uri = "/wifi/ver",     .method = HTTP_GET, .handler = wifi_ver_get },
	{ .uri = "/wifi/guardar", .method = HTTP_GET, .handler = wifi_guardar_get },
};

/* ======================= Handlers ======================= */

/* Muestra la red guardada en NVS (sin la clave) y la IP que tiene ahora */
esp_err_t wifi_ver_get(httpd_req_t *req)
{
	char ssid[33] = "";							// si nunca se guardo queda vacio
	memoria_get_str("ssid", ssid, sizeof(ssid));

	char ip[16] = "sin conexion";				// "255.255.255.255" + '\0'
	esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	esp_netif_ip_info_t info;
	if (sta != NULL && esp_netif_get_ip_info(sta, &info) == ESP_OK && info.ip.addr != 0) {
		esp_ip4addr_ntoa(&info.ip, ip, sizeof(ip));	// IP en numero -> texto
	}

	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "ssid", ssid);
	cJSON_AddStringToObject(obj, "ip", ip);
	return responder_json(req, obj);
}

/* Lee ssid y pass de la URL, valida, guarda en NVS y se conecta */
esp_err_t wifi_guardar_get(httpd_req_t *req)
{
	char ssid[33];
	char pass[65];
	cJSON *obj = cJSON_CreateObject();

	/* web_parametro da false si falta o si no entra (ssid de mas de 32) */
	if (!web_parametro(req, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
		cJSON_AddBoolToObject(obj, "ok", false);
		cJSON_AddStringToObject(obj, "error", "falta ssid (o mas de 32 caracteres)");
		return responder_json(req, obj);
	}
	if (!web_parametro(req, "pass", pass, sizeof(pass))) {
		pass[0] = '\0';							// sin pass: red abierta
	}
	if (strlen(pass) > 0 && strlen(pass) < 8) {
		cJSON_AddBoolToObject(obj, "ok", false);
		cJSON_AddStringToObject(obj, "error", "la clave WPA tiene minimo 8 caracteres");
		return responder_json(req, obj);
	}

	memoria_set_str("ssid", ssid);				// queda para el proximo arranque
	memoria_set_str("pass", pass);
	wifi_sta_conectar(ssid, pass);				// se conecta ahora, sin reiniciar

	cJSON_AddBoolToObject(obj, "ok", true);
	cJSON_AddStringToObject(obj, "ssid", ssid);
	return responder_json(req, obj);
}
