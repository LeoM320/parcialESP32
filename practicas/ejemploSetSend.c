/* ejemploSetSend.c: recibir (set) y enviar (send) variables de distintos tipos por la web,
 * con y sin JSON, por GET y por POST.
 *
 * Archivos del ejemplo:
 *   ejemploSetSend.c                          handlers (este archivo)
 *   ejemploSetSend.html                       la pagina: www/setsend.html -> http://IP/setsend.html
 *   ejemploSetSend.postman_collection.json    todas las peticiones para importar en Postman
 *
 * ---------------------------------------------------------------------------------------
 * QUE AGREGAR AL MAIN.C PELADO
 *   1. Tipos, globales, prototipos y ayudas de este archivo (secciones de abajo).
 *   2. Las entradas de uris_setsend[] dentro de tu uris[].
 *   3. En app_main, ANTES de web_init():   mutexDatos = xSemaphoreCreateMutex();
 *   4. El pelado ya trae responder_json() y leer_json(): no copiarlas de nuevo.
 *   5. Copiar el html a www/setsend.html y subirlo: idf.py -p COM6 storage-flash
 * ---------------------------------------------------------------------------------------
 *
 * Las variables de ejemplo (una de cada tipo):
 *   entero   int          -1000..1000
 *   real     float        0.0..100.0       (con PUNTO: "2.5")
 *   activo   bool         true/false, en texto acepta 1/0, true/false, on/off
 *   nombre   char[17]     1..16 caracteres
 *   color    enum         "rojo" | "verde" | "azul"
 *   valores  uint8_t[4]   cuatro numeros 0..255; en texto "1,2,3,4", en JSON [1,2,3,4]
 *
 * PETICIONES
 *
 *   ENVIAR (el ESP manda, el cliente procesa)
 *     GET  /datos                 -> JSON  {"entero":5,"real":2.5,"activo":true,"nombre":"hola",
 *                                           "color":"rojo","valores":[1,2,3,4]}
 *     GET  /texto                 -> texto "entero=5\nreal=2.50\nactivo=1\n..." (una linea por variable)
 *     GET  /valor?campo=real      -> texto "2.50"  (una sola variable, sin nada alrededor)
 *
 *   RECIBIR (el cliente manda, el ESP procesa). Solo se cambian los campos que vienen;
 *   se valida TODO antes de aplicar: si uno esta mal no se cambia nada.
 *     GET  /set?entero=5&real=2.5         sin JSON, datos en la URL        -> texto "OK" / "ERROR: ..."
 *     POST /form   cuerpo: entero=5&real=2.5   sin JSON, como un <form>   -> texto "OK" / "ERROR: ..."
 *     POST /datos  cuerpo: {"entero":5,"real":2.5}   con JSON             -> {"ok":true} / {"ok":false,"error":...}
 *                  JSON mal formado o con tipos incorrectos -> 400
 *
 * EL PATRON (igual para las 3 formas de recibir)
 *   texto o JSON  ->  cambios_t (valores + "hay" por campo)  ->  validar_cambios()  ->  aplicar_cambios() bajo mutex
 *   Asi la validacion y el mutex se escriben UNA vez y sirven para GET, form y JSON.
 */

/* ======================= Includes (el pelado ya los tiene) ======================= */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>				// snprintf(), sscanf()
#include <stdlib.h>				// strtol(), strtof()
#include <string.h>				// strcmp(), strlen()
#include <strings.h>			// strcasecmp(): comparar sin mayusculas
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "web.h"				// web_leer_cuerpo, web_campo, web_parametro
#include "cJSON.h"

/* ======================= Constantes ======================= */

#define NOMBRE_MAX		16		// caracteres utiles de nombre (+1 por el '\0')
#define CANT_VALORES	4

/* ======================= Tipos ======================= */

typedef enum{
	E_COLOR_ROJO,
	E_COLOR_VERDE,
	E_COLOR_AZUL
}color_e;

typedef struct{
	int entero;
	float real;
	bool activo;
	char nombre[NOMBRE_MAX + 1];
	color_e color;
	uint8_t valores[CANT_VALORES];
}datos_t;

/* Lo que llego en un pedido: cada campo con su "hay" (true = vino y hay que cambiarlo) */
typedef struct{
	bool hayEntero;		int entero;
	bool hayReal;		float real;
	bool hayActivo;		bool activo;
	bool hayNombre;		char nombre[NOMBRE_MAX + 1];
	bool hayColor;		color_e color;
	bool hayValores;	int valores[CANT_VALORES];	// int para poder detectar 300 o -1 antes de pasar a uint8_t
}cambios_t;

/* ======================= Globales ======================= */

datos_t datos = { 0, 0.0f, false, "esp32", E_COLOR_VERDE, { 0, 0, 0, 0 } };
SemaphoreHandle_t mutexDatos;					// protege datos (crearlo en app_main)

const char *colores[] = { "rojo", "verde", "azul" };	// mismo orden que color_e

/* ======================= Prototipos ======================= */

esp_err_t responder_json(httpd_req_t *req, cJSON *obj);		// ya esta en el pelado
cJSON *leer_json(httpd_req_t *req);							// ya esta en el pelado

/* Ayudas: texto -> tipo (devuelven false si el texto no sirve) */
bool texto_a_int(const char *s, int *valor);
bool texto_a_float(const char *s, float *valor);
bool texto_a_bool(const char *s, bool *valor);
bool texto_a_color(const char *s, color_e *valor);
bool texto_a_valores(const char *s, int valores[CANT_VALORES]);

/* El patron comun */
bool cambios_desde_texto(const char *texto, cambios_t *c, char *error, size_t tam);
bool cambios_desde_json(const cJSON *raiz, cambios_t *c, char *error, size_t tam);
bool validar_cambios(const cambios_t *c, char *error, size_t tam);
void aplicar_cambios(const cambios_t *c);
esp_err_t responder_texto(httpd_req_t *req, const char *texto);

/* Handlers */
esp_err_t datos_get(httpd_req_t *req);		// GET  /datos  -> JSON
esp_err_t texto_get(httpd_req_t *req);		// GET  /texto  -> texto
esp_err_t valor_get(httpd_req_t *req);		// GET  /valor?campo=x -> texto
esp_err_t set_get(httpd_req_t *req);		// GET  /set?x=..   (sin JSON)
esp_err_t form_post(httpd_req_t *req);		// POST /form x=..  (sin JSON)
esp_err_t datos_post(httpd_req_t *req);		// POST /datos {..} (con JSON)

/* Agregar estas lineas a tu uris[] */
const httpd_uri_t uris_setsend[] = {
	{ .uri = "/datos", .method = HTTP_GET,  .handler = datos_get },
	{ .uri = "/texto", .method = HTTP_GET,  .handler = texto_get },
	{ .uri = "/valor", .method = HTTP_GET,  .handler = valor_get },
	{ .uri = "/set",   .method = HTTP_GET,  .handler = set_get },
	{ .uri = "/form",  .method = HTTP_POST, .handler = form_post },
	{ .uri = "/datos", .method = HTTP_POST, .handler = datos_post },	// misma URI que el GET: otro metodo
};

/* =======================================================================================
 *  AYUDAS: TEXTO -> TIPO
 * ======================================================================================= */

/* "25" ok; "25x", "", "abc" no */
bool texto_a_int(const char *s, int *valor)
{
	char *fin;
	long n = strtol(s, &fin, 10);
	if (fin == s || *fin != '\0') {
		return false;
	}
	*valor = (int)n;
	return true;
}

/* "2.5" ok; "2,5" no (en C el decimal va con punto) */
bool texto_a_float(const char *s, float *valor)
{
	char *fin;
	float n = strtof(s, &fin);
	if (fin == s || *fin != '\0') {
		return false;
	}
	*valor = n;
	return true;
}

/* Acepta lo que suele mandar cada cliente: 1/0 (a mano), true/false (JS), on (checkbox de un form) */
bool texto_a_bool(const char *s, bool *valor)
{
	if (strcmp(s, "1") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "on") == 0) {
		*valor = true;
		return true;
	}
	if (strcmp(s, "0") == 0 || strcasecmp(s, "false") == 0 || strcasecmp(s, "off") == 0) {
		*valor = false;
		return true;
	}
	return false;
}

/* "rojo" -> E_COLOR_ROJO, recorriendo la tabla colores[] */
bool texto_a_color(const char *s, color_e *valor)
{
	for (int i = 0; i < sizeof(colores) / sizeof(colores[0]); i++) {
		if (strcasecmp(s, colores[i]) == 0) {
			*valor = (color_e)i;
			return true;
		}
	}
	return false;
}

/* "1,2,3,4" -> {1,2,3,4}. %n guarda cuantos caracteres leyo: si no llego al final, sobra algo */
bool texto_a_valores(const char *s, int valores[CANT_VALORES])
{
	int leidos = 0;
	if (sscanf(s, "%d,%d,%d,%d%n", &valores[0], &valores[1], &valores[2], &valores[3], &leidos) != CANT_VALORES) {
		return false;
	}
	return s[leidos] == '\0';
}

/* =======================================================================================
 *  EL PATRON COMUN
 * ======================================================================================= */

/* Texto urlencoded "entero=5&real=2.5&...": sirve igual para la URL de un GET
 * (lo que va despues del ?) y para el cuerpo de un POST de formulario.
 * Los campos que no vienen, o vienen vacios (\"entero=\" de un input sin llenar), quedan con hay = false. */
bool cambios_desde_texto(const char *texto, cambios_t *c, char *error, size_t tam)
{
	char v[64];									// valor de un campo, ya decodificado
	memset(c, 0, sizeof(*c));					// todo en 0: ningun "hay"

	if (web_campo(texto, "entero", v, sizeof(v)) && v[0] != '\0') {
		if (!texto_a_int(v, &c->entero)) {
			snprintf(error, tam, "entero no es un numero: %s", v);
			return false;
		}
		c->hayEntero = true;
	}
	if (web_campo(texto, "real", v, sizeof(v)) && v[0] != '\0') {
		if (!texto_a_float(v, &c->real)) {
			snprintf(error, tam, "real no es un numero (usar punto): %s", v);
			return false;
		}
		c->hayReal = true;
	}
	if (web_campo(texto, "activo", v, sizeof(v)) && v[0] != '\0') {
		if (!texto_a_bool(v, &c->activo)) {
			snprintf(error, tam, "activo tiene que ser 1/0, true/false u on/off");
			return false;
		}
		c->hayActivo = true;
	}
	if (web_campo(texto, "nombre", c->nombre, sizeof(c->nombre)) && c->nombre[0] != '\0') {	// false tambien si es muy largo
		c->hayNombre = true;
	} else if (web_campo(texto, "nombre", v, sizeof(v)) && strlen(v) > NOMBRE_MAX) {
		snprintf(error, tam, "nombre: maximo %d caracteres", NOMBRE_MAX);
		return false;
	}
	if (web_campo(texto, "color", v, sizeof(v)) && v[0] != '\0') {
		if (!texto_a_color(v, &c->color)) {
			snprintf(error, tam, "color tiene que ser rojo, verde o azul");
			return false;
		}
		c->hayColor = true;
	}
	if (web_campo(texto, "valores", v, sizeof(v)) && v[0] != '\0') {
		if (!texto_a_valores(v, c->valores)) {
			snprintf(error, tam, "valores: formato 1,2,3,4");
			return false;
		}
		c->hayValores = true;
	}
	return true;
}

/* JSON {"entero":5,...}: aca el TIPO lo dice el JSON, asi que se chequea con cJSON_IsXxx.
 * Devuelve false si un campo vino con el tipo equivocado (el handler responde 400). */
bool cambios_desde_json(const cJSON *raiz, cambios_t *c, char *error, size_t tam)
{
	memset(c, 0, sizeof(*c));
	cJSON *j;

	if ((j = cJSON_GetObjectItem(raiz, "entero")) != NULL) {	// vino el campo
		if (!cJSON_IsNumber(j) || j->valuedouble != (int)j->valuedouble) {	// 2.5 no es entero
			snprintf(error, tam, "entero tiene que ser un numero entero");
			return false;
		}
		c->entero = j->valueint;
		c->hayEntero = true;
	}
	if ((j = cJSON_GetObjectItem(raiz, "real")) != NULL) {
		if (!cJSON_IsNumber(j)) {
			snprintf(error, tam, "real tiene que ser un numero");
			return false;
		}
		c->real = (float)j->valuedouble;		// valuedouble: con decimales (valueint los corta)
		c->hayReal = true;
	}
	if ((j = cJSON_GetObjectItem(raiz, "activo")) != NULL) {
		if (!cJSON_IsBool(j)) {					// true/false sin comillas
			snprintf(error, tam, "activo tiene que ser true o false");
			return false;
		}
		c->activo = cJSON_IsTrue(j);
		c->hayActivo = true;
	}
	if ((j = cJSON_GetObjectItem(raiz, "nombre")) != NULL) {
		if (!cJSON_IsString(j)) {
			snprintf(error, tam, "nombre tiene que ser texto");
			return false;
		}
		if (strlen(j->valuestring) > NOMBRE_MAX) {
			snprintf(error, tam, "nombre: maximo %d caracteres", NOMBRE_MAX);
			return false;
		}
		snprintf(c->nombre, sizeof(c->nombre), "%s", j->valuestring);	// copia segura
		c->hayNombre = true;
	}
	if ((j = cJSON_GetObjectItem(raiz, "color")) != NULL) {
		if (!cJSON_IsString(j) || !texto_a_color(j->valuestring, &c->color)) {
			snprintf(error, tam, "color tiene que ser \"rojo\", \"verde\" o \"azul\"");
			return false;
		}
		c->hayColor = true;
	}
	if ((j = cJSON_GetObjectItem(raiz, "valores")) != NULL) {
		if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) != CANT_VALORES) {
			snprintf(error, tam, "valores tiene que ser un arreglo de %d numeros", CANT_VALORES);
			return false;
		}
		for (int i = 0; i < CANT_VALORES; i++) {
			cJSON *e = cJSON_GetArrayItem(j, i);	// elemento i del arreglo
			if (!cJSON_IsNumber(e)) {
				snprintf(error, tam, "valores[%d] no es un numero", i);
				return false;
			}
			c->valores[i] = e->valueint;
		}
		c->hayValores = true;
	}
	return true;
}

/* Rangos. Se valida todo junto ANTES de tocar datos: si algo esta mal no cambia nada */
bool validar_cambios(const cambios_t *c, char *error, size_t tam)
{
	if (!c->hayEntero && !c->hayReal && !c->hayActivo && !c->hayNombre && !c->hayColor && !c->hayValores) {
		snprintf(error, tam, "no vino ningun campo");
		return false;
	}
	if (c->hayEntero && (c->entero < -1000 || c->entero > 1000)) {
		snprintf(error, tam, "entero fuera de rango (-1000..1000)");
		return false;
	}
	if (c->hayReal && (c->real < 0.0f || c->real > 100.0f)) {
		snprintf(error, tam, "real fuera de rango (0..100)");
		return false;
	}
	if (c->hayNombre && strlen(c->nombre) == 0) {
		snprintf(error, tam, "nombre vacio");
		return false;
	}
	if (c->hayValores) {
		for (int i = 0; i < CANT_VALORES; i++) {
			if (c->valores[i] < 0 || c->valores[i] > 255) {
				snprintf(error, tam, "valores[%d] fuera de rango (0..255)", i);
				return false;
			}
		}
	}
	return true;
}

/* Solo asignaciones dentro del mutex, y solo de los campos que vinieron */
void aplicar_cambios(const cambios_t *c)
{
	xSemaphoreTake(mutexDatos, portMAX_DELAY);
	if (c->hayEntero)  datos.entero = c->entero;
	if (c->hayReal)    datos.real = c->real;
	if (c->hayActivo)  datos.activo = c->activo;
	if (c->hayNombre)  memcpy(datos.nombre, c->nombre, sizeof(datos.nombre));
	if (c->hayColor)   datos.color = c->color;
	if (c->hayValores) {
		for (int i = 0; i < CANT_VALORES; i++) {
			datos.valores[i] = (uint8_t)c->valores[i];	// ya validado 0..255
		}
	}
	xSemaphoreGive(mutexDatos);
}

/* Respuesta sin JSON: texto plano */
esp_err_t responder_texto(httpd_req_t *req, const char *texto)
{
	httpd_resp_set_type(req, "text/plain");
	return httpd_resp_sendstr(req, texto);
}

/* =======================================================================================
 *  ENVIAR (el ESP manda)
 * ======================================================================================= */

/* GET /datos: todo en JSON. Cada tipo con su cJSON_Add... */
esp_err_t datos_get(httpd_req_t *req)
{
	datos_t d;
	xSemaphoreTake(mutexDatos, portMAX_DELAY);
	d = datos;									// copia y afuera
	xSemaphoreGive(mutexDatos);

	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "entero", d.entero);		// int    -> numero
	cJSON_AddNumberToObject(obj, "real", d.real);			// float  -> numero con decimales
	cJSON_AddBoolToObject(obj, "activo", d.activo);			// bool   -> true/false
	cJSON_AddStringToObject(obj, "nombre", d.nombre);		// char[] -> "texto"
	cJSON_AddStringToObject(obj, "color", colores[d.color]);	// enum   -> "texto" (por tabla)

	cJSON *arr = cJSON_AddArrayToObject(obj, "valores");	// arreglo -> [1,2,3,4]
	for (int i = 0; i < CANT_VALORES; i++) {
		cJSON_AddItemToArray(arr, cJSON_CreateNumber(d.valores[i]));
	}

	cJSON *extra = cJSON_AddObjectToObject(obj, "extra");	// objeto anidado -> {"uptime_ms":..}
	cJSON_AddNumberToObject(extra, "uptime_ms", (double)(xTaskGetTickCount() * portTICK_PERIOD_MS));

	return responder_json(req, obj);			// imprime, libera y responde
}

/* GET /texto: lo mismo sin JSON, una linea "clave=valor" por variable */
esp_err_t texto_get(httpd_req_t *req)
{
	datos_t d;
	xSemaphoreTake(mutexDatos, portMAX_DELAY);
	d = datos;
	xSemaphoreGive(mutexDatos);

	char buf[200];
	snprintf(buf, sizeof(buf),
			 "entero=%d\nreal=%.2f\nactivo=%d\nnombre=%s\ncolor=%s\nvalores=%u,%u,%u,%u\n",
			 d.entero, d.real, d.activo, d.nombre, colores[d.color],
			 d.valores[0], d.valores[1], d.valores[2], d.valores[3]);
	return responder_texto(req, buf);
}

/* GET /valor?campo=real: una sola variable, sin nada alrededor ("2.50") */
esp_err_t valor_get(httpd_req_t *req)
{
	char campo[16];
	if (!web_parametro(req, "campo", campo, sizeof(campo))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta ?campo=");
	}

	datos_t d;
	xSemaphoreTake(mutexDatos, portMAX_DELAY);
	d = datos;
	xSemaphoreGive(mutexDatos);

	char buf[32];
	if (strcmp(campo, "entero") == 0) {
		snprintf(buf, sizeof(buf), "%d", d.entero);
	} else if (strcmp(campo, "real") == 0) {
		snprintf(buf, sizeof(buf), "%.2f", d.real);
	} else if (strcmp(campo, "activo") == 0) {
		snprintf(buf, sizeof(buf), "%d", d.activo);
	} else if (strcmp(campo, "nombre") == 0) {
		snprintf(buf, sizeof(buf), "%s", d.nombre);
	} else if (strcmp(campo, "color") == 0) {
		snprintf(buf, sizeof(buf), "%s", colores[d.color]);
	} else {
		return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "campo desconocido");
	}
	return responder_texto(req, buf);
}

/* =======================================================================================
 *  RECIBIR (el cliente manda)
 * ======================================================================================= */

/* GET /set?entero=5&real=2.5: sin JSON, los datos en la URL. Para probar desde el navegador */
esp_err_t set_get(httpd_req_t *req)
{
	char query[256];							// lo que va despues del '?'
	if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
		return responder_texto(req, "ERROR: falta ?campo=valor");
	}

	cambios_t c;
	char error[80];
	if (!cambios_desde_texto(query, &c, error, sizeof(error)) || !validar_cambios(&c, error, sizeof(error))) {
		char resp[100];
		snprintf(resp, sizeof(resp), "ERROR: %s", error);
		return responder_texto(req, resp);
	}
	aplicar_cambios(&c);
	return responder_texto(req, "OK");
}

/* POST /form, cuerpo "entero=5&real=2.5": sin JSON, lo que manda un <form method="POST">.
 * Es el mismo formato que la URL del GET, por eso usa la misma cambios_desde_texto */
esp_err_t form_post(httpd_req_t *req)
{
	char cuerpo[256];
	if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0) {
		return responder_texto(req, "ERROR: cuerpo muy largo o vacio");
	}

	cambios_t c;
	char error[80];
	if (!cambios_desde_texto(cuerpo, &c, error, sizeof(error)) || !validar_cambios(&c, error, sizeof(error))) {
		char resp[100];
		snprintf(resp, sizeof(resp), "ERROR: %s", error);
		return responder_texto(req, resp);
	}
	aplicar_cambios(&c);
	return responder_texto(req, "OK");
}

/* POST /datos, cuerpo {"entero":5,"real":2.5,...}: con JSON.
 * Tipo incorrecto o JSON roto -> 400. Rango invalido -> {"ok":false,"error":"..."} */
esp_err_t datos_post(httpd_req_t *req)
{
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}

	cambios_t c;
	char error[80];
	bool tiposOk = cambios_desde_json(raiz, &c, error, sizeof(error));
	cJSON_Delete(raiz);							// ya se copio todo a c: liberar antes de seguir
	if (!tiposOk) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
	}

	cJSON *resp = cJSON_CreateObject();
	if (!validar_cambios(&c, error, sizeof(error))) {
		cJSON_AddBoolToObject(resp, "ok", false);
		cJSON_AddStringToObject(resp, "error", error);
		return responder_json(req, resp);
	}
	aplicar_cambios(&c);
	cJSON_AddBoolToObject(resp, "ok", true);
	return responder_json(req, resp);
}
