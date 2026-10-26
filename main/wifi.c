/**
 * @file wifi.c
 * @brief Implementacion de wifi.h.
 *
 * Los eventos de WiFi/IP llegan al handler desde la tarea del event loop de
 * ESP-IDF: ahi no se bloquea, solo se loguea y se pide reconectar.
 *
 * Dos interfaces a la vez (modo APSTA):
 *   - AP:  el ESP32 es un router propio ("ESP32-Parcial", 192.168.4.1).
 *   - STA: el ESP32 es un cliente del router de tu casa (IP que da el router).
 */

#include "wifi.h"
#include <string.h>         /* strlcpy, strlen */
#include "esp_wifi.h"       /* driver WiFi */
#include "esp_event.h"      /* event loop: avisos de conectado, desconectado... */
#include "esp_netif.h"      /* interfaces de red (IP) */
#include "esp_log.h"
#include "esp_mac.h"        /* MACSTR / MAC2STR para imprimir direcciones MAC */

#define AP_CANAL        1   /* canal de radio del AP (1..13) */
#define AP_MAX_CLIENTES 4   /* celulares/PC conectados al AP a la vez */

static const char *TAG = "wifi";

static esp_netif_t *s_ap = NULL;    /* interfaz de red del AP */
static esp_netif_t *s_sta = NULL;   /* interfaz de red de la STA */

/* Arma la configuracion STA; sin clave = red abierta */
static wifi_config_t config_sta(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,   /* seguridad minima aceptada */
        },
    };
    /* strlcpy copia sin pasarse del tamanio del destino y siempre pone el '\0' */
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    if (pass[0] == '\0') {                          /* clave vacia */
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    return cfg;                                     /* devuelve el struct entero (copia) */
}

/* La llama ESP-IDF cada vez que pasa algo con el WiFi o la IP.
 * base: de que familia es el evento; id: cual; data: datos del evento. */
static void wifi_evento(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:              /* arranco la STA: conectarse */
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {     /* se corto o no pudo conectar: reintentar */
            wifi_event_sta_disconnected_t *ev = data;
            ESP_LOGW(TAG, "STA desconectada (motivo %d), reintentando", ev->reason);
            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {      /* alguien se conecto a nuestro AP */
            wifi_event_ap_staconnected_t *ev = data;
            ESP_LOGI(TAG, "AP: cliente conectado " MACSTR, MAC2STR(ev->mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {   /* alguien se fue de nuestro AP */
            wifi_event_ap_stadisconnected_t *ev = data;
            ESP_LOGI(TAG, "AP: cliente desconectado " MACSTR, MAC2STR(ev->mac));
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {   /* el router nos dio IP */
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "STA conectada, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

void wifi_init(const char *ap_ssid, const char *ap_pass,
               const char *sta_ssid, const char *sta_pass)
{
    /* 1. Pila de red (TCP/IP) y event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) {     /* INVALID_STATE = ya lo creo otro modulo */
        ESP_ERROR_CHECK(err);
    }
    /* 2. Crear las dos interfaces de red (con su DHCP) */
    s_ap = esp_netif_create_default_wifi_ap();
    s_sta = esp_netif_create_default_wifi_sta();

    /* 3. Arrancar el driver WiFi con la configuracion por defecto */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* El driver no guarda su config en flash: de guardar se encarga quien llama */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    /* 4. Que los eventos de WiFi (todos) y de "obtuve IP" vayan a wifi_evento() */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_evento, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_evento, NULL, NULL));

    /* 5. Configuracion del AP */
    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = AP_CANAL,
            .max_connection = AP_MAX_CLIENTES,
            .authmode       = WIFI_AUTH_WPA2_PSK,   /* con clave */
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen((char *)ap_cfg.ap.ssid);
    strlcpy((char *)ap_cfg.ap.password, ap_pass, sizeof(ap_cfg.ap.password));
    if (ap_pass[0] == '\0') {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;        /* sin clave: abierto */
    }

    /* 6. Modo: AP + STA si hay red del router, si no solo AP */
    bool usar_sta = sta_ssid[0] != '\0';
    ESP_ERROR_CHECK(esp_wifi_set_mode(usar_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    if (usar_sta) {
        wifi_config_t sta_cfg = config_sta(sta_ssid, sta_pass);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        esp_netif_set_default_netif(s_sta);     /* salida a otras redes por el router */
    }
    /* 7. Arrancar: dispara WIFI_EVENT_STA_START y wifi_evento() se conecta */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP \"%s\" activo en 192.168.4.1%s%s", ap_ssid,
             usar_sta ? ", conectando STA a " : "", usar_sta ? sta_ssid : "");
}

esp_err_t wifi_sta_conectar(const char *ssid, const char *pass)
{
    if (ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;             /* sin nombre de red no se puede */
    }

    /* Si arranco solo como AP, se habilita la STA */
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

    /* Mismo orden que el comando WIFI del ejemplo del curso: desconectar,
     * cambiar la configuracion, conectar. Si no estaba conectada,
     * esp_wifi_disconnect() devuelve error y se ignora. */
    esp_wifi_disconnect();

    wifi_config_t sta_cfg = config_sta(ssid, pass);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configuracion STA invalida: %s", esp_err_to_name(err));
        return err;
    }

    esp_wifi_connect();                         /* el resultado llega despues, como evento */
    ESP_LOGI(TAG, "Conectando STA a \"%s\"", ssid);
    return ESP_OK;
}

/* IP del AP como numero de 32 bits (0 si todavia no hay) */
uint32_t wifi_ip_ap(void)
{
    esp_netif_ip_info_t info;
    if (s_ap == NULL || esp_netif_get_ip_info(s_ap, &info) != ESP_OK) {
        return 0;
    }
    return info.ip.addr;
}
