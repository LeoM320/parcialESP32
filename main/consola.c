/**
 * @file consola.c
 * @brief Implementacion de consola.h.
 *
 * microrl llama a su callback de impresion sin decir de que instancia viene
 * (print(const char *)). Por eso, mientras una consola procesa bytes, se guarda
 * en s_actual cual es, y el callback imprimir() la usa. El mutex garantiza que
 * s_actual no cambie a mitad de camino.
 */

#include "consola.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "microrl.h"

#define PRINTF_MAX  256     /* largo maximo de un consola_printf() */

struct consola {
    microrl_t rl;
    void (*enviar)(void *ctx, const char *texto);
    void *ctx;
    char anterior;          /* ultimo byte recibido, para tomar \r\n como un solo Enter */
};

static consola_t s_consolas[CONSOLA_MAX];
static size_t s_cant = 0;
static int (*s_ejecutar)(int, const char *const *) = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static consola_t *s_actual = NULL;      /* consola procesando bytes (con el mutex tomado) */

/* Callback de impresion de microrl: va a la consola actual */
static void imprimir(const char *texto)
{
    if (s_actual != NULL) {
        s_actual->enviar(s_actual->ctx, texto);
    }
}

void consola_init(int (*ejecutar)(int argc, const char *const *argv))
{
    s_ejecutar = ejecutar;
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

consola_t *consola_crear(void (*enviar)(void *ctx, const char *texto), void *ctx)
{
    if (s_cant >= CONSOLA_MAX) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);    /* subir CONSOLA_MAX */
    }
    consola_t *c = &s_consolas[s_cant++];
    c->enviar = enviar;
    c->ctx = ctx;
    c->anterior = 0;
    microrl_init(&c->rl, imprimir);
    microrl_set_execute_callback(&c->rl, s_ejecutar);
    return c;
}

void consola_inicio(consola_t *c)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_actual = c;
    microrl_init(&c->rl, imprimir);             /* linea e historial vacios */
    microrl_set_execute_callback(&c->rl, s_ejecutar);
    c->anterior = 0;
    microrl_insert_char(&c->rl, KEY_LF);        /* linea vacia: solo muestra el prompt */
    s_actual = NULL;
    xSemaphoreGive(s_mutex);
}

void consola_recibir(consola_t *c, const char *datos, size_t largo)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_actual = c;
    for (size_t i = 0; i < largo; i++) {
        char ch = datos[i];
        /* microrl esta configurado con Enter = \n (_ENDL_LF). Las terminales mandan
         * \r (PuTTY), \n (Linux) o \r\n: los tres se toman como un solo Enter. */
        if (ch == '\r') {
            microrl_insert_char(&c->rl, KEY_LF);
        } else if (ch != '\n' || c->anterior != '\r') {
            microrl_insert_char(&c->rl, ch);
        }
        c->anterior = ch;
    }
    s_actual = NULL;
    xSemaphoreGive(s_mutex);
}

void consola_printf(const char *formato, ...)
{
    if (s_actual == NULL) {
        return;                 /* fuera de un comando */
    }
    char texto[PRINTF_MAX];
    va_list args;
    va_start(args, formato);
    vsnprintf(texto, sizeof(texto), formato, args);
    va_end(args);
    s_actual->enviar(s_actual->ctx, texto);
}

void consola_escribir(const char *texto)
{
    if (s_actual != NULL) {
        s_actual->enviar(s_actual->ctx, texto);
    }
}
