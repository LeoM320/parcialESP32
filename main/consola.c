/**
 * @file consola.c
 * @brief Implementacion de consola.h.
 */

#include "consola.h"
#include <stdarg.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "microrl.h"

#define PRINTF_MAX  256     /* largo maximo de un consola_printf() */

struct consola {
    microrl_t rl;
    int (*ejecutar)(int, const char *const *);
    void (*enviar)(const char *texto);
    char anterior;          /* ultimo byte recibido, para tomar \r\n como un solo Enter */
};

static struct consola s_consolas[CONSOLA_MAX];
static int s_cant = 0;

/* Los comandos se ejecutan de a uno; s_actual es la consola que esta
 * ejecutando, a donde van consola_printf() y consola_escribir(). */
static SemaphoreHandle_t s_mutex = NULL;
static consola_t *s_actual = NULL;

consola_t *consola_crear(int (*ejecutar)(int argc, const char *const *argv),
                         void (*enviar)(const char *texto))
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_cant >= CONSOLA_MAX) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    consola_t *c = &s_consolas[s_cant++];
    c->ejecutar = ejecutar;
    c->enviar = enviar;
    return c;
}

void consola_inicio(consola_t *c)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    microrl_init(&c->rl, c->enviar);                /* linea e historial vacios */
    microrl_set_execute_callback(&c->rl, c->ejecutar);
    c->anterior = 0;
    microrl_insert_char(&c->rl, KEY_LF);            /* linea vacia: solo muestra el prompt */
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
    char texto[PRINTF_MAX];
    va_list args;
    va_start(args, formato);
    vsnprintf(texto, sizeof(texto), formato, args);
    va_end(args);
    consola_escribir(texto);
}

void consola_escribir(const char *texto)
{
    if (s_actual != NULL) {             /* fuera de un comando no hay a quien */
        s_actual->enviar(texto);
    }
}
