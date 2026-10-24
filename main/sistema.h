/**
 * @file sistema.h
 * @brief Arranque de todo lo que no es del ejercicio: NVS, WiFi, consola TCP,
 *        servidor web y API de variables.
 *
 * Desde main solo hace falta:
 *   1. registrar las variables (var_dout(), var_ain(), ...);
 *   2. llamar a sistema_init() con tus comandos y endpoints (o NULL);
 *   3. crear tus tareas.
 *
 * Lo que queda funcionando:
 * - WiFi AP "CONFIG_ESP_AP_SSID" (192.168.4.1) + STA con la red guardada en NVS.
 * - Por el AP, "/" es la pagina de configuracion (SSID, clave, puerto de la consola).
 * - Por el router, "/" es el panel; API: GET /api/def, GET /api/valores, POST /api/set.
 * - Consola por TCP (Telnet) en el puerto guardado (1234 si nunca se configuro).
 *   Los comandos son todos tuyos: el sistema solo avisa cuando uno no existe.
 */

#ifndef SISTEMA_H_
#define SISTEMA_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_http_server.h"

#define SISTEMA_ENDPOINTS_MAX   8   /**< Endpoints propios que se pueden agregar */

/**
 * @brief Tus comandos de consola. Se llama con cada linea que llega (Enter).
 * @param argc Cantidad de palabras de la linea (>= 1).
 * @param argv Palabras: argv[0] es el comando.
 * @return true si reconociste el comando (ya respondiste con consola_printf());
 *         false si no: el sistema contesta "ERROR: comando desconocido".
 */
typedef bool (*sistema_comandos_t)(int argc, const char *const *argv);

/**
 * @brief Arranca todo. Llamar una vez, despues de registrar las variables.
 * @param comandos  Tus comandos de consola, o NULL si no hay.
 * @param endpoints Tus endpoints HTTP, o NULL si no hay.
 * @param cantidad  Cantidad de endpoints (como mucho SISTEMA_ENDPOINTS_MAX).
 */
void sistema_init(sistema_comandos_t comandos, const httpd_uri_t *endpoints, size_t cantidad);

#endif /* SISTEMA_H_ */
