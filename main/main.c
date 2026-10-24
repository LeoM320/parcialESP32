#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hardware.h"
#include "wifi.h"
#include "web.h"
#include "memoria.h"
#include "consola.h"
#include "consola_tcp.h"
#include "variables.h"
#include "json_mini.h"

#define CUERPO_MAX      512
#define PUERTO_INICIAL  1234    /* si nunca se configuro otro desde la pagina */
#define JSON_MAX        2048    /* definicion de ~20 variables */

/* Claves en NVS (memoria.h) */
#define CLAVE_SSID      "ssid"
#define CLAVE_PASS      "pass"
#define CLAVE_PUERTO    "puerto"

/* ======================= Variables =======================
 * Todo lo que se ve o controla desde la web, la app y la consola.
 * Agregar una = una linea aca. (id, etiqueta, hardware/rango, fila, columna) */
static void registrar_variables(void)
{
    var_init();
    /* fila 0: salidas (el LED 1 es el del PWM, ver hardware.h) */
    var_pwm ("led1", "LED 1 (brillo)", LED_PWM_GPIO, LED_PWM_FREC_HZ, 0, 0);
    var_dout("led2", "LED 2", LED2_GPIO, 0, 1);
    var_dout("led3", "LED 3", LED3_GPIO, 0, 2);
    var_dout("led4", "LED 4", LED4_GPIO, 0, 3);
    /* fila 1: pulsadores (pull-up: activos en 0) */
    var_din("sw1", "SW 1", SW1_GPIO, true, 1, 0);
    var_din("sw2", "SW 2", SW2_GPIO, true, 1, 1);
    var_din("sw3", "SW 3", SW3_GPIO, true, 1, 2);
    var_din("sw4", "SW 4", SW4_GPIO, true, 1, 3);
    /* fila 2: potes y datos del programa */
    var_ain("pot1", "Pote 1", POT1_GPIO, 0, 100, "%", 2, 0);
    var_ain("pot2", "Pote 2", POT2_GPIO, 0, 3.1, "V", 2, 1);
    var_num("consigna", "Consigna", 0, 50, 25, 2, 2);
    var_txt("estado", "Estado", "listo", 2, 3);
}

/* ======================= Comandos (microrl) ======================= */

/* Buffer para las respuestas largas. Solo se usa dentro de ejecutar(), y la
 * consola ejecuta los comandos de a uno: no hace falta mutex. */
static char s_resp[JSON_MAX];

static void responder(int largo)
{
    if (largo < 0) {
        consola_printf("ERROR: la respuesta no entra en %u bytes\r\n", (unsigned)sizeof(s_resp));
        return;
    }
    consola_escribir(s_resp);
    consola_escribir("\r\n");
}

/* <id>          -> muestra el valor
 * <id> <valor>  -> lo cambia (ej. "led2 on", "led1 50", "estado hola mundo")
 * microrl separa por espacios: se vuelven a unir para los textos */
static void cmd_variable(int argc, const char *const *argv)
{
    if (argc >= 2) {
        char nuevo[VAR_TEXTO_MAX + 1] = "";     /* +1: uno de mas para detectar "muy largo" */
        for (int i = 1; i < argc; i++) {
            if (i > 1) {
                strlcat(nuevo, " ", sizeof(nuevo));
            }
            strlcat(nuevo, argv[i], sizeof(nuevo));
        }
        switch (var_set(argv[0], nuevo)) {
        case VAR_OK:
            break;
        case VAR_NO_EXISTE:
            consola_printf("ERROR: comando o variable desconocida: %s ('help')\r\n", argv[0]);
            return;
        case VAR_SOLO_LECTURA:
            consola_printf("ERROR: %s es de solo lectura\r\n", argv[0]);
            return;
        case VAR_VALOR_INVALIDO:
            consola_printf("ERROR: valor invalido para %s: %s\r\n", argv[0], nuevo);
            return;
        }
    }

    char valor[VAR_TEXTO_MAX + 16];             /* el texto mas largo, o numero + unidad */
    if (!var_texto(argv[0], valor, sizeof(valor))) {
        consola_printf("ERROR: comando o variable desconocida: %s ('help')\r\n", argv[0]);
        return;
    }
    consola_printf("%s = %s\r\n", argv[0], valor);
}

/* Lo llama microrl al apretar Enter: argv[0] es el comando */
static int ejecutar(int argc, const char *const *argv)
{
    if (strcmp(argv[0], "help") == 0) {
        consola_escribir("Comandos:\r\n"     /* escribir: mas largo que el limite de printf */
                       "  help             esta ayuda\r\n"
                       "  vars             lista de variables y sus valores\r\n"
                       "  <id>             valor de una variable (ej. sw1, pot1)\r\n"
                       "  <id> <valor>     cambia una variable:\r\n"
                       "                     dout   led2 on | off | toggle\r\n"
                       "                     pwm    led1 0..100\r\n"
                       "                     num    consigna 12.5\r\n"
                       "                     texto  estado hola mundo\r\n");
    } else if (strcmp(argv[0], "vars") == 0) {
        responder(var_listar(s_resp, sizeof(s_resp)));
    } else {
        cmd_variable(argc, argv);
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

/* ---- API de variables: lo mismo que los comandos def / valores / <id> <valor> ---- */

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

static const httpd_uri_t endpoints[] = {
    { .uri = "/",            .method = HTTP_GET,  .handler = raiz_get },
    { .uri = "/api/config",  .method = HTTP_POST, .handler = config_post },
    { .uri = "/api/def",     .method = HTTP_GET,  .handler = def_get },
    { .uri = "/api/valores", .method = HTTP_GET,  .handler = valores_get },
    { .uri = "/api/set",     .method = HTTP_POST, .handler = set_post },
};

void app_main(void)
{
    registrar_variables();      /* antes de crear tareas */

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

    consola_tcp_init(puerto, ejecutar);             /* despues de wifi_init */

    web_init(endpoints, sizeof(endpoints) / sizeof(endpoints[0]));   /* despues: su handler usa consola_tcp_puerto */
}
