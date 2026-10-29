/**
 * @file wifi.c
 * @brief Implementacion de wifi.h en modo simultaneo SoftAP + Station (APSTA).
 */

#include "wifi.h"
#include "rgb.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"

#define AP_CANAL            1           ///< Canal de radio inicial del SoftAP.
#define AP_MAX_CLIENTES     4           ///< Maximo de estaciones conectadas al SoftAP.
#define STA_MAX_INTENTOS    2           ///< Reintentos de conexion STA antes de dejar fija la radio del AP.

static const char *TAG = "wifi";        ///< Etiqueta para los logs de este modulo.
static esp_netif_t *s_ap = NULL;        ///< Interfaz de red del SoftAP.
static esp_netif_t *s_sta = NULL;       ///< Interfaz de red de la Station.
static int s_reintentos = 0;            ///< Contador de reintentos de conexion STA.

/**
 * @brief Arma el struct wifi_config_t para la interfaz Station.
 * @param ssid Nombre de la red WiFi destino.
 * @param pass Clave de la red ("" si es abierta).
 * @return Estructura wifi_config_t lista para esp_wifi_set_config().
 */
static wifi_config_t config_sta(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    if (pass[0] == '\0') {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    return cfg;
}

/**
 * @brief Manejador de eventos de WiFi e IP (inicio, desconexion y obtencion de IP).
 * @param arg  Argumento de usuario (no usado).
 * @param base Familia del evento (WIFI_EVENT o IP_EVENT).
 * @param id   Identificador del evento.
 * @param data Datos asociados al evento.
 */
static void wifi_evento(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            rgb_set(10, 0, 0);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev = data;
            if (s_reintentos < STA_MAX_INTENTOS) {
                s_reintentos++;
                ESP_LOGW(TAG, "STA desconectada (motivo %d), reintento %d/%d", ev->reason, s_reintentos, STA_MAX_INTENTOS);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "STA no disponible (motivo %d), radio fija en SoftAP", ev->reason);
                rgb_set(0, 10, 0);
            }
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *ev = data;
            ESP_LOGI(TAG, "AP: cliente conectado " MACSTR, MAC2STR(ev->mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *ev = data;
            ESP_LOGI(TAG, "AP: cliente desconectado " MACSTR, MAC2STR(ev->mac));
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        s_reintentos = 0;
        rgb_set(0, 10, 0);
        ESP_LOGI(TAG, "STA conectada, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

/**
 * @brief Inicializa lwIP, el loop de eventos y arranca el WiFi en modo AP + STA sin ahorro de energia.
 * @param ap_ssid  Nombre de la red SoftAP.
 * @param ap_pass  Clave del SoftAP (>= 8 caracteres, o "" para abierta).
 * @param sta_ssid SSID del router al que conectarse como Station ("" para solo AP).
 * @param sta_pass Clave del router.
 */
void wifi_init(const char *ap_ssid, const char *ap_pass,
               const char *sta_ssid, const char *sta_pass)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    s_ap = esp_netif_create_default_wifi_ap();
    s_sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_evento, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_evento, NULL, NULL));
    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = AP_CANAL,
            .max_connection = AP_MAX_CLIENTES,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
    strlcpy((char *)ap_cfg.ap.password, ap_pass, sizeof(ap_cfg.ap.password));
    if (ap_pass[0] == '\0') {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    bool usar_sta = sta_ssid[0] != '\0';
    ESP_ERROR_CHECK(esp_wifi_set_mode(usar_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    if (usar_sta) {
        wifi_config_t sta_cfg = config_sta(sta_ssid, sta_pass);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        esp_netif_set_default_netif(s_sta);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (!usar_sta) {
        rgb_set(0, 10, 0);
    }
    ESP_LOGI(TAG, "AP \"%s\" activo en 192.168.4.1%s%s", ap_ssid,
             usar_sta ? ", conectando STA a " : "", usar_sta ? sta_ssid : "");
}

/**
 * @brief Reconfigura la interfaz Station con un nuevo SSID y clave y solicita reconexion.
 * @param ssid SSID de la red WiFi destino.
 * @param pass Clave de la red WiFi destino.
 * @return ESP_OK si se inicio la solicitud de conexion.
 */
esp_err_t wifi_sta_conectar(const char *ssid, const char *pass)
{
    if (ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_mode_t modo;
    esp_wifi_get_mode(&modo);
    if (modo != WIFI_MODE_APSTA) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "No se pudo pasar a AP+STA: %s", esp_err_to_name(err));
            return err;
        }
        esp_netif_set_default_netif(s_sta);
    }
    s_reintentos = 0;
    rgb_set(10, 0, 0);
    esp_wifi_disconnect();
    wifi_config_t sta_cfg = config_sta(ssid, pass);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configuracion STA invalida: %s", esp_err_to_name(err));
        return err;
    }
    esp_wifi_connect();
    ESP_LOGI(TAG, "Conectando STA a \"%s\"", ssid);
    return ESP_OK;
}

/**
 * @brief Devuelve la direccion IPv4 actual de la interfaz SoftAP.
 * @return IPv4 en formato lwIP, o 0 si no esta inicializada.
 */
uint32_t wifi_ip_ap(void)
{
    esp_netif_ip_info_t info;
    if (s_ap == NULL || esp_netif_get_ip_info(s_ap, &info) != ESP_OK) {
        return 0;
    }
    return info.ip.addr;
}

/**
 * @brief Devuelve la direccion IPv4 actual de la interfaz Station.
 * @return IPv4 en formato lwIP, o 0 si no tiene IP asignada.
 */
uint32_t wifi_ip_sta(void)
{
    esp_netif_ip_info_t info;
    if (s_sta == NULL || esp_netif_get_ip_info(s_sta, &info) != ESP_OK) {
        return 0;
    }
    return info.ip.addr;
}
