/**
 * @file memoria.c
 * @brief Implementacion de memoria.h.
 *
 * Cada funcion abre el espacio de nombres, opera y lo cierra: no queda
 * ningun handle abierto entre llamadas.
 */

#include "memoria.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define ESPACIO "config"

static const char *TAG = "memoria";

void memoria_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool memoria_get_str(const char *clave, char *dest, size_t tamano)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;               /* nunca se guardo nada */
    }
    size_t largo = tamano;
    esp_err_t err = nvs_get_str(h, clave, dest, &largo);
    nvs_close(h);
    return err == ESP_OK;
}

bool memoria_set_str(const char *clave, const char *valor)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, clave, valor);
        if (err == ESP_OK) {
            err = nvs_commit(h);    /* recien aca queda escrito en flash */
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar \"%s\": %s", clave, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool memoria_get_u16(const char *clave, uint16_t *valor)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_u16(h, clave, valor);
    nvs_close(h);
    return err == ESP_OK;
}

bool memoria_set_u16(const char *clave, uint16_t valor)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u16(h, clave, valor);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar \"%s\": %s", clave, esp_err_to_name(err));
    }
    return err == ESP_OK;
}
