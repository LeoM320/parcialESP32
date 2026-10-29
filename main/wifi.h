/**
 * @file wifi.h
 * @brief WiFi en modo AP + STA.
 *
 * - AP:  el ESP32 crea su propia red (IP fija 192.168.4.1). Sirve sin router.
 * - STA: ademas se conecta a un router; si se cae, reintenta solo.
 * Basada en el "Ejemplo WiFi" (APSTA) y en connect_wifi.c del ejemplo HTTP del curso.
 *
 * - La radio es una sola: cuando la STA se conecta, el AP pasa al canal del router.
 * - No bloquea: devuelve apenas arranca el WiFi; la conexion STA ocurre en segundo plano.
 */

#ifndef WIFI_H_
#define WIFI_H_

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Inicializa la pila de red y arranca el WiFi en modo AP + STA.
 *
 * Llamar una sola vez, con NVS ya inicializado (memoria_init()): el driver
 * guarda ahi su calibracion. La config de red NO se guarda: solo vive en RAM.
 * Aborta si falla la inicializacion.
 * @param ap_ssid  Nombre de la red propia.
 * @param ap_pass  Clave de la red propia (>= 8 caracteres) o "" para red abierta.
 * @param sta_ssid Router al que conectarse, o "" para no usar STA.
 * @param sta_pass Clave del router ("" si es abierto).
 */
void wifi_init(const char *ap_ssid, const char *ap_pass,
               const char *sta_ssid, const char *sta_pass);

/**
 * @brief Cambia la red del modo STA y se conecta a ella (en segundo plano).
 *
 * Corta la conexion STA actual (el AP sigue activo). Si se llama desde un handler
 * HTTP, responder ANTES: quien estaba conectado por la red vieja pierde la conexion.
 * Llamar despues de wifi_init().
 * @param ssid Router al que conectarse (no vacio).
 * @param pass Clave del router ("" si es abierto).
 * @return ESP_OK si se pidio la conexion; error si la configuracion es invalida.
 *         ESP_OK no significa conectado: eso lo informa el log al obtener IP.
 */
esp_err_t wifi_sta_conectar(const char *ssid, const char *pass);

/**
 * @brief IP del ESP32 en su propia red (AP), normalmente 192.168.4.1.
 * @return IPv4 en el formato de lwIP/esp_netif, o 0 si el WiFi no arranco.
 */
uint32_t wifi_ip_ap(void);

/**
 * @brief IP del ESP32 en la red del router (STA), o 0 si no esta conectado.
 * @return IPv4 en formato lwIP, o 0 si la STA no tiene IP.
 */
uint32_t wifi_ip_sta(void);

#endif /* WIFI_H_ */
