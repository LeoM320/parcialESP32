/**
 * @file memoria.c
 * @brief Implementacion de memoria.h.
 *
 * Cada funcion abre el espacio de nombres, opera y lo cierra: no queda
 * ningun handle abierto entre llamadas.
 *
 * NVS (Non-Volatile Storage) guarda pares clave = valor en una particion de
 * la flash. Sobrevive a reinicios y a flashear el programa de nuevo
 * (solo se borra con "idf.py erase-flash").
 */

#include "memoria.h"
#include "nvs_flash.h"      /* nvs_flash_init / erase */
#include "nvs.h"            /* nvs_open, nvs_get_*, nvs_set_*, nvs_commit */
#include "esp_log.h"        /* ESP_LOGE */

#define ESPACIO "config"    /* espacio de nombres: como una carpeta donde van las claves */

static const char *TAG = "memoria";     /* etiqueta que aparece en los logs */

void memoria_init(void)
{
    esp_err_t err = nvs_flash_init();
    /* Si la particion esta llena o tiene formato viejo: borrarla y reintentar */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool memoria_get_str(const char *clave, char *dest, size_t tamano)
{
    nvs_handle_t h;                 /* "archivo abierto" del espacio de nombres */
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;               /* nunca se guardo nada */
    }
    size_t largo = tamano;          /* entrada: lugar disponible; salida: largo leido */
    esp_err_t err = nvs_get_str(h, clave, dest, &largo);    /* si la clave no existe, dest no cambia */
    nvs_close(h);
    return err == ESP_OK;
}

bool memoria_set_str(const char *clave, const char *valor)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);   /* lectura y escritura */
    if (err == ESP_OK) {
        err = nvs_set_str(h, clave, valor);
        if (err == ESP_OK) {
            err = nvs_commit(h);    /* recien aca queda escrito en flash */
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        /* esp_err_to_name convierte el codigo de error en texto */
        ESP_LOGE(TAG, "No se pudo guardar \"%s\": %s", clave, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

/* Igual que las de texto, pero para un entero de 16 bits sin signo (0..65535) */
bool memoria_get_u16(const char *clave, uint16_t *valor)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_u16(h, clave, valor);   /* si no existe, *valor no cambia */
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

/* Entero de 32 bits con signo: igual que u16, con otra funcion de NVS */
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

/* Blob = bytes sin formato. Primero se pregunta el largo (dest NULL) para no
 * copiar algo guardado con otro tamanio (por ejemplo un struct que cambio). */
bool memoria_get_blob(const char *clave, void *dest, size_t tamano)
{
    nvs_handle_t h;
    if (nvs_open(ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t largo = 0;
    esp_err_t err = nvs_get_blob(h, clave, NULL, &largo);  /* solo el largo */
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

bool memoria_borrar(const char *clave)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ESPACIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_key(h, clave);  /* ESP_ERR_NVS_NOT_FOUND si no existia */
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err == ESP_OK;
}
