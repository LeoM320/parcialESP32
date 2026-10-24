/**
 * @file sistema.c
 * @brief Implementacion de sistema.h.
 *
 * Orden de arranque (importa):
 *   consola UART -> memoria (NVS) -> leer config -> WiFi -> consola TCP -> servidor web
 * La consola UART va primero porque no depende de nada.
 * La consola va despues de WiFi porque usa sockets, y el servidor web al final
 * porque /api/config usa consola_tcp_puerto().
 */

#include "sistema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "wifi.h"
#include "web.h"
#include "memoria.h"
#include "consola.h"
#include "consola_tcp.h"
#include "consola_uart.h"
#include "variables.h"
#include "json_mini.h"

#define CUERPO_MAX      512
#define PUERTO_INICIAL  1234    /* si nunca se configuro otro desde la pagina */
#define JSON_MAX        2048    /* definicion de ~20 variables */
#define UART_CONSOLA    UART_NUM_0  /* la del monitor (el COM del USB) */
#define UART_BAUDIOS    115200

/* Claves en NVS (memoria.h) */
#define CLAVE_SSID      "ssid"
#define CLAVE_PASS      "pass"
#define CLAVE_PUERTO    "puerto"

static sistema_comandos_t s_comandos = NULL;    /* los de main */

/* ======================= Consola ======================= */

/* Lo llama microrl al apretar Enter: argv[0] es el comando. Todos los
 * comandos son de main; aca solo se avisa si no lo reconocio. */
static int ejecutar(int argc, const char *const *argv)
{
    if (s_comandos == NULL || !s_comandos(argc, argv)) {
        consola_printf("ERROR: comando desconocido: %s\r\n", argv[0]);
    }
    return 0;
}

/* Convierte "1".."65535" a numero; 0 si no es un puerto valido */
static uint16_t a_puerto(const char *txt)
{
    char *fin;
    long n = strtol(txt, &fin, 10);
    if (fin == txt || *fin != '\0' || n < 1 || n > 65535) {
        return 0;
    }
    return (uint16_t)n;
}

/* ======================= Web ======================= */

/* true si el pedido entro por la red propia del ESP32 (AP) */
static bool por_ap(httpd_req_t *req)
{
    return web_ip_local(req) == wifi_ip_ap();
}

/* POST /api/config (solo desde el AP)
 * Cuerpo (formulario urlencoded):
 *   ssid=MiRed&pass=clave&puerto_int=3333
 * Imprime lo recibido, lo guarda en NVS, se conecta a la red y pasa el
 * puerto a la consola TCP. */
static esp_err_t config_post(httpd_req_t *req)
{
    if (!por_ap(req)) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Configurar solo desde la red del ESP32");
    }

    char cuerpo[CUERPO_MAX];
    if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Cuerpo invalido");
    }

    char ssid[33], pass[65];                    /* limites de WiFi: 32 y 64 caracteres */
    char puerto_txt[6];
    if (!web_campo(cuerpo, "ssid", ssid, sizeof(ssid)) ||
        !web_campo(cuerpo, "pass", pass, sizeof(pass)) ||
        !web_campo(cuerpo, "puerto_int", puerto_txt, sizeof(puerto_txt))) {
        /* falta un campo o es mas largo que su buffer */
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campos faltantes o invalidos");
    }
    uint16_t puerto = a_puerto(puerto_txt);
    if (ssid[0] == '\0' || puerto == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID vacio o puerto invalido");
    }

    printf("\n=== Configuracion recibida ===\n");
    printf("SSID:           %s\n", ssid);
    printf("Contrasena:     %s\n", pass);
    printf("Puerto interno: %u\n", puerto);

    if (!memoria_set_str(CLAVE_SSID, ssid) ||
        !memoria_set_str(CLAVE_PASS, pass) ||
        !memoria_set_u16(CLAVE_PUERTO, puerto)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo guardar");
    }

    /* Responder ANTES de cambiar de red: si el navegador entro por la red
     * vieja, despues ya no recibiria la respuesta */
    esp_err_t err = httpd_resp_sendstr(req, "OK");

    wifi_sta_conectar(ssid, pass);
    consola_tcp_puerto(puerto);
    return err;
}

/* ---- API de variables ---- */

/* Buffer para las respuestas. Solo lo usan los handlers HTTP, que corren todos
 * en la tarea del servidor (de a uno): no hace falta mutex. Es static para no
 * ocupar 2 KB de la pila de esa tarea. */
static char s_json_web[JSON_MAX];

static esp_err_t responder_json(httpd_req_t *req, int largo)
{
    if (largo < 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Respuesta demasiado grande");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_json_web, largo);
}

/* GET /api/def -> {"vars":[{"id":"led1","tipo":"dout",...},...]} */
static esp_err_t def_get(httpd_req_t *req)
{
    return responder_json(req, var_json_def(s_json_web, sizeof(s_json_web)));
}

/* GET /api/valores -> {"led1":1,...} */
static esp_err_t valores_get(httpd_req_t *req)
{
    return responder_json(req, var_json_valores(s_json_web, sizeof(s_json_web)));
}

/* POST /api/set  {"id":"led1","valor":"toggle"}  -> valores actualizados */
static esp_err_t set_post(httpd_req_t *req)
{
    char cuerpo[128];
    char id[32];
    char valor[64];
    if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0 ||
        !json_valor(cuerpo, "id", id, sizeof(id)) ||
        !json_valor(cuerpo, "valor", valor, sizeof(valor))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Se espera {\"id\":...,\"valor\":...}");
    }

    switch (var_set(id, valor)) {
    case VAR_OK:
        return valores_get(req);
    case VAR_NO_EXISTE:
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Variable desconocida");
    case VAR_SOLO_LECTURA:
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Variable de solo lectura");
    case VAR_VALOR_INVALIDO:
        break;
    }
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Valor invalido");
}

/* GET /: la config solo por la red propia (AP); por la red del router, el panel */
static esp_err_t raiz_get(httpd_req_t *req)
{
    return web_enviar_archivo(req, por_ap(req) ? "/config.html" : "/panel.html");
}

static const httpd_uri_t ENDPOINTS_SISTEMA[] = {
    { .uri = "/",            .method = HTTP_GET,  .handler = raiz_get },
    { .uri = "/api/config",  .method = HTTP_POST, .handler = config_post },
    { .uri = "/api/def",     .method = HTTP_GET,  .handler = def_get },
    { .uri = "/api/valores", .method = HTTP_GET,  .handler = valores_get },
    { .uri = "/api/set",     .method = HTTP_POST, .handler = set_post },
};
#define CANT_SISTEMA    (sizeof(ENDPOINTS_SISTEMA) / sizeof(ENDPOINTS_SISTEMA[0]))

/* Los del sistema + los de main, en una sola tabla para web_init() */
static httpd_uri_t s_endpoints[CANT_SISTEMA + SISTEMA_ENDPOINTS_MAX];

/* ======================= Arranque ======================= */

void sistema_init(sistema_comandos_t comandos, const httpd_uri_t *endpoints, size_t cantidad)
{
    if (cantidad > SISTEMA_ENDPOINTS_MAX) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);   /* subir SISTEMA_ENDPOINTS_MAX */
    }
    s_comandos = comandos;
    var_init();                 /* por si main no registro ninguna variable */

    consola_uart_init(UART_CONSOLA, UART_BAUDIOS, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, ejecutar);

    memoria_init();

    /* Config guardada; lo que nunca se guardo queda con el valor por defecto */
    char ssid[33] = CONFIG_ESP_WIFI_SSID;
    char pass[65] = CONFIG_ESP_WIFI_PASSWORD;
    uint16_t puerto = PUERTO_INICIAL;
    memoria_get_str(CLAVE_SSID, ssid, sizeof(ssid));
    memoria_get_str(CLAVE_PASS, pass, sizeof(pass));
    memoria_get_u16(CLAVE_PUERTO, &puerto);
    printf("Config de arranque: SSID \"%s\", puerto %u\n", ssid, puerto);

    wifi_init(CONFIG_ESP_AP_SSID, CONFIG_ESP_AP_PASSWORD, ssid, pass);

    consola_tcp_init(puerto, ejecutar);         /* despues de wifi_init */

    memcpy(s_endpoints, ENDPOINTS_SISTEMA, sizeof(ENDPOINTS_SISTEMA));
    if (cantidad > 0) {
        memcpy(&s_endpoints[CANT_SISTEMA], endpoints, cantidad * sizeof(httpd_uri_t));
    }
    web_init(s_endpoints, CANT_SISTEMA + cantidad);   /* al final: usa consola_tcp_puerto */
}
