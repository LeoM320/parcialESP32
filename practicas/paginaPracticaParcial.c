/* paginaPracticaParcial.c: handlers web del simulacro 1 (consigna 5).
 *
 * No es un main completo: son las piezas para copiar a main.c.
 * Los tipos y globales de arriba son los que ya tenes en main.c (bomba_t, mutexBomba...).
 * La pagina que usa estos handlers esta en paginaPracticaParcial.html (va en www/index.html).
 *
 *   GET  /estado     -> {"nivel":57,"bomba":true,"vel":80,"modo":"auto","alarma":"normal","min":30,"max":80,"lim":90}
 *   POST /modo       {"modo":"manual"}                        -> {"ok":true}
 *   POST /bomba      {"on":true}                              -> {"ok":true} o {"ok":false,"error":"..."}
 *   POST /config     {"min":30,"max":80,"alarma":90,"vel":80} -> {"ok":true} o {"ok":false,"error":"..."}
 *   POST /reconocer  {}                                       -> {"ok":true} o {"ok":false,"error":"..."}
 *
 * Reglas:
 *   - JSON mal formado o con tipos incorrectos -> 400.
 *   - Orden que no se puede cumplir -> {"ok":false,"error":"..."}.
 *   - En el mutex solo se copia o se asigna; el JSON, la red y la NVS van afuera.
 *   - Cada leer_json() con su cJSON_Delete() antes de CADA return.
 */

/* ======================= Includes ======================= */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>				// free()
#include <string.h>				// strcmp()
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "web.h"				// web_leer_cuerpo
#include "cJSON.h"

/* ======================= Lo que ya esta en main.c ======================= */

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
	uint8_t nivel;
	modoBomba_e modo;
	estadoBomba_e estado;
	alarma_e alarma;
	configBomba_t config;
}bomba_t;

extern bomba_t bomba;					// en main.c sin extern
extern SemaphoreHandle_t mutexBomba;

void enviar(const char *texto);			// la de la consola TCP

/* ======================= Prototipos (copiar arriba en main.c) ======================= */

esp_err_t responder_json(httpd_req_t *req, cJSON *obj);		// ya esta en main.c
cJSON *leer_json(httpd_req_t *req);							// ya esta en main.c

esp_err_t responder_ok(httpd_req_t *req, bool ok, const char *error);	// {"ok":true} / {"ok":false,"error":...}
cJSON *estado_json(void);									// el JSON de /estado (lo usa tambien la consola)
bool config_valida(int min, int max, int alarma, int vel);	// misma validacion para web y consola

esp_err_t estado_get(httpd_req_t *req);
esp_err_t modo_post(httpd_req_t *req);
esp_err_t bomba_post(httpd_req_t *req);
esp_err_t config_post(httpd_req_t *req);
esp_err_t reconocer_post(httpd_req_t *req);
void comando_estado(void);									// comando "estado" de la consola

/* Asi queda uris[] en main.c */
const httpd_uri_t uris_ejemplo[] = {
	{ .uri = "/estado",    .method = HTTP_GET,  .handler = estado_get },
	{ .uri = "/modo",      .method = HTTP_POST, .handler = modo_post },
	{ .uri = "/bomba",     .method = HTTP_POST, .handler = bomba_post },
	{ .uri = "/config",    .method = HTTP_POST, .handler = config_post },
	{ .uri = "/reconocer", .method = HTTP_POST, .handler = reconocer_post },
};

/* ======================= Ayudas ======================= */

esp_err_t responder_ok(httpd_req_t *req, bool ok, const char *error)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddBoolToObject(obj, "ok", ok);
	if (!ok) {
		cJSON_AddStringToObject(obj, "error", error);
	}
	return responder_json(req, obj);			// responder_json hace el Delete
}

/* Copia bajo mutex y arma el JSON afuera. Quien la llama hace el Delete (o responder_json) */
cJSON *estado_json(void)
{
	bomba_t b;
	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	b = bomba;
	xSemaphoreGive(mutexBomba);

	const char *alarmas[] = { "normal", "activa", "reconocida" };	// mismo orden que alarma_e

	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "nivel", b.nivel);
	cJSON_AddBoolToObject(obj, "bomba", b.estado == E_ESTADO_ON);
	cJSON_AddNumberToObject(obj, "vel", b.config.vel);
	cJSON_AddStringToObject(obj, "modo", b.modo == E_MODO_AUTO ? "auto" : "manual");
	cJSON_AddStringToObject(obj, "alarma", alarmas[b.alarma]);
	cJSON_AddNumberToObject(obj, "min", b.config.min);
	cJSON_AddNumberToObject(obj, "max", b.config.max);
	cJSON_AddNumberToObject(obj, "lim", b.config.alarma);		// la consigna lo llama "lim"
	return obj;
}

bool config_valida(int min, int max, int alarma, int vel)
{
	return 0 <= min && min < max && max < alarma && alarma <= 100 && vel >= 10 && vel <= 100;
}

/* ======================= Handlers ======================= */

esp_err_t estado_get(httpd_req_t *req)
{
	return responder_json(req, estado_json());
}

/* El molde de todos los POST: leer -> validar tipo (400) -> Delete -> aplicar -> responder */
esp_err_t modo_post(httpd_req_t *req)
{
	cJSON *pedido = leer_json(req);
	if (pedido == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}

	cJSON *modo = cJSON_GetObjectItem(pedido, "modo");
	if (!cJSON_IsString(modo)) {
		cJSON_Delete(pedido);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta modo");
	}

	modoBomba_e nuevo;
	if (strcmp(modo->valuestring, "auto") == 0) {
		nuevo = E_MODO_AUTO;
	} else if (strcmp(modo->valuestring, "manual") == 0) {
		nuevo = E_MODO_MANUAL;
	} else {
		cJSON_Delete(pedido);
		return responder_ok(req, false, "modo invalido");
	}
	cJSON_Delete(pedido);

	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	bomba.modo = nuevo;
	xSemaphoreGive(mutexBomba);

	// NVS del modo aca, afuera del mutex (consigna 6)
	return responder_ok(req, true, NULL);
}

/* La condicion (manual y sin alarma) se mira dentro del mutex: lee y escribe el mismo dato */
esp_err_t bomba_post(httpd_req_t *req)
{
	cJSON *pedido = leer_json(req);
	if (pedido == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}

	cJSON *on = cJSON_GetObjectItem(pedido, "on");
	if (!cJSON_IsBool(on)) {
		cJSON_Delete(pedido);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta on");
	}
	bool prender = cJSON_IsTrue(on);
	cJSON_Delete(pedido);

	bool ok = false;
	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	if (bomba.modo == E_MODO_MANUAL && bomba.alarma == E_ALARMA_APAGADA) {
		bomba.estado = prender ? E_ESTADO_ON : E_ESTADO_OFF;
		ok = true;
	}
	xSemaphoreGive(mutexBomba);

	if (!ok) {
		return responder_ok(req, false, "solo en manual y sin alarma");
	}
	return responder_ok(req, true, NULL);
}

/* Valida todo junto antes de tocar nada */
esp_err_t config_post(httpd_req_t *req)
{
	cJSON *pedido = leer_json(req);
	if (pedido == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}

	cJSON *jmin = cJSON_GetObjectItem(pedido, "min");
	cJSON *jmax = cJSON_GetObjectItem(pedido, "max");
	cJSON *jal  = cJSON_GetObjectItem(pedido, "alarma");
	cJSON *jvel = cJSON_GetObjectItem(pedido, "vel");
	if (!cJSON_IsNumber(jmin) || !cJSON_IsNumber(jmax) ||
		!cJSON_IsNumber(jal)  || !cJSON_IsNumber(jvel)) {
		cJSON_Delete(pedido);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "faltan campos o no son numeros");
	}

	int min = jmin->valueint;
	int max = jmax->valueint;
	int alarma = jal->valueint;
	int vel = jvel->valueint;
	cJSON_Delete(pedido);

	if (!config_valida(min, max, alarma, vel)) {
		return responder_ok(req, false, "config invalida: 0<=min<max<alarma<=100, 10<=vel<=100");
	}

	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	bomba.config.min = min;
	bomba.config.max = max;
	bomba.config.alarma = alarma;
	bomba.config.vel = vel;
	xSemaphoreGive(mutexBomba);

	// NVS de la config aca, afuera del mutex (consigna 6)
	return responder_ok(req, true, NULL);
}

/* El cuerpo es {}: no hace falta leerlo */
esp_err_t reconocer_post(httpd_req_t *req)
{
	bool ok = false;
	xSemaphoreTake(mutexBomba, portMAX_DELAY);
	if (bomba.alarma == E_ALARMA_ACTIVA) {
		bomba.alarma = E_ALARMA_RECONOCIDA;
		ok = true;
	}
	xSemaphoreGive(mutexBomba);

	if (!ok) {
		return responder_ok(req, false, "no hay alarma activa");
	}
	return responder_ok(req, true, NULL);
}

/* ======================= Bonus: comando "estado" en execute() ======================= */

/* Mismo JSON que GET /estado, en una linea por la consola:
 *
 *   if (strcmp(argv[0], "estado") == 0) {
 *       comando_estado();
 *       return 0;
 *   }
 */
void comando_estado(void)
{
	cJSON *obj = estado_json();
	char *texto = cJSON_PrintUnformatted(obj);	// reserva memoria
	cJSON_Delete(obj);
	if (texto != NULL) {
		enviar(texto);
		enviar("\r\n");
		free(texto);							// cada Print con su free
	}
}
