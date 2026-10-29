/**
 * @file memoria.c
 * @brief Implementacion de memoria.h sobre NVS (Non-Volatile Storage).
 *
 * Cada funcion abre el espacio de nombres "config", opera y lo cierra.
 */

#include "memoria.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define ESPACIO "config"                ///< Espacio de nombres de NVS.

static const char *TAG = "memoria";     ///< Etiqueta para los logs.

/**
 * @brief Inicializa la particion NVS. Si esta llena o cambia de version, la borra y reintenta.
 */
void memoria_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/**
 * @brief Lee una cadena guardada en NVS.
 * @param clave  Nombre de la clave (maximo 15 caracteres).
 * @param dest   Buffer destino. Si la clave no existe no se modifica.
 * @param tamano Tamanio de dest.
 * @return true si la clave existia y se copio en dest.
 */
bool memoria_get_str(const char *clave, char *dest, size_t tamano)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t largo = tamano;
    esp_err_t err = nvs_get_str(h, clave, dest, &largo);
    nvs_close(h);
    return err == ESP_OK;
}

/**
 * @brief Guarda una cadena en NVS.
 * @param clave Nombre de la clave.
 * @param valor Cadena terminada en '\0'.
 * @return true si se guardo correctamente.
 */
bool memoria_set_str(const char *clave, const char *valor)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, clave, valor);
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

/**
 * @brief Lee un entero sin signo de 16 bits desde NVS.
 * @param clave Nombre de la clave.
 * @param valor Puntero donde se guarda el valor leido.
 * @return true si la clave existia.
 */
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

/**
 * @brief Guarda un entero sin signo de 16 bits en NVS.
 * @param clave Nombre de la clave.
 * @param valor Valor a guardar.
 * @return true si se guardo correctamente.
 */
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

/**
 * @brief Lee un entero con signo de 32 bits desde NVS.
 * @param clave Nombre de la clave.
 * @param valor Puntero donde se guarda el valor leido.
 * @return true si la clave existia.
 */
bool memoria_get_i32(const char *clave, int32_t *valor)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_i32(h, clave, valor);
    nvs_close(h);
    return err == ESP_OK;
}

/**
 * @brief Guarda un entero con signo de 32 bits en NVS.
 * @param clave Nombre de la clave.
 * @param valor Valor a guardar.
 * @return true si se guardo correctamente.
 */
bool memoria_set_i32(const char *clave, int32_t valor)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, clave, valor);
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

/**
 * @brief Lee un bloque de bytes desde NVS si coincide con el tamanio esperado.
 * @param clave  Nombre de la clave.
 * @param dest   Buffer destino.
 * @param tamano Cantidad exacta de bytes esperados.
 * @return true si existia y su tamanio coincide.
 */
bool memoria_get_blob(const char *clave, void *dest, size_t tamano)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t largo = 0;
    esp_err_t err = nvs_get_blob(h, clave, NULL, &largo);
    if (err == ESP_OK && largo == tamano) {
        err = nvs_get_blob(h, clave, dest, &largo);
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "\"%s\" mide %u bytes y se esperaban %u: no se usa",
                 clave, (unsigned)largo, (unsigned)tamano);
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    nvs_close(h);
    return err == ESP_OK;
}

/**
 * @brief Guarda un bloque arbitrario de bytes en NVS.
 * @param clave  Nombre de la clave.
 * @param datos  Puntero a los bytes a guardar.
 * @param tamano Cantidad de bytes.
 * @return true si se guardo correctamente.
 */
bool memoria_set_blob(const char *clave, const void *datos, size_t tamano)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, clave, datos, tamano);
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

/**
 * @brief Borra una clave del espacio de nombres de NVS.
 * @param clave Nombre de la clave a eliminar.
 * @return true si existia y fue borrada.
 */
bool memoria_borrar(const char *clave)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_key(h, clave);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err == ESP_OK;
}
