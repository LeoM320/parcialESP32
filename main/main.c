/* main.c: lo del ejercicio. El resto (WiFi, web, consola, API) lo arranca sistema_init().
 *
 *   1. Variables:  lo que se ve y se controla desde la web, Qt y la consola.
 *   2. Comandos:   los comandos de la consola (MobaXterm).
 *   3. Endpoints:  pedidos HTTP propios (opcional).
 *   4. app_main:   arranque y tus tareas.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware.h"
#include "sistema.h"
#include "variables.h"
#include "consola.h"

/* ======================= 1. Variables =======================
 * Agregar una = una linea aca. (id, etiqueta, hardware/rango, fila, columna) */
static void registrar_variables(void)
{
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

/* ======================= 2. Comandos de consola =======================
 * Se llama con cada linea que llega. argv[0] es el comando, argv[1].. los
 * argumentos, argc cuantas palabras hay. Responder con consola_printf()
 * terminando las lineas en "\r\n". Devolver true si reconociste el comando;
 * con false el sistema contesta "ERROR: comando desconocido".
 *
 * Ejemplo:
 *     if (strcmp(argv[0], "hola") == 0) {
 *         consola_printf("Hola!\r\n");
 *         return true;
 *     }
 */
static bool comandos(int argc, const char *const *argv)
{
    return false;
}

/* ======================= 3. Endpoints HTTP (opcional) =======================
 * Ejemplo: GET /api/hola -> {"mensaje":"hola"}
 *
 * static esp_err_t hola_get(httpd_req_t *req)
 * {
 *     httpd_resp_set_type(req, "application/json");
 *     return httpd_resp_sendstr(req, "{\"mensaje\":\"hola\"}");
 * }
 *
 * static const httpd_uri_t endpoints[] = {
 *     { .uri = "/api/hola", .method = HTTP_GET, .handler = hola_get },
 * };
 *
 * y en app_main:  sistema_init(comandos, endpoints, sizeof(endpoints) / sizeof(endpoints[0]));
 */

/* ======================= 4. Arranque y tareas =======================
 * Ejemplo de tarea con la logica del ejercicio:
 *
 * static void tarea_control(void *arg)
 * {
 *     for (;;) {
 *         float pot, consigna;
 *         var_get("pot1", &pot);
 *         var_get("consigna", &consigna);
 *         var_set("led2", pot > consigna ? "on" : "off");
 *         vTaskDelay(pdMS_TO_TICKS(100));
 *     }
 * }
 */

void app_main(void)
{
    registrar_variables();                      /* antes de sistema_init */
    sistema_init(comandos, NULL, 0);            /* sin endpoints propios */

    /* Tus tareas, despues de sistema_init. Ej:
     * xTaskCreate(tarea_control, "control", 4096, NULL, 4, NULL); */
}
