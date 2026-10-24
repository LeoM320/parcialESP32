/**
 * @file consola.c
 * @brief Implementacion de consola.h.
 */

#include "consola.h"
#include <stdarg.h>
#include <stdio.h>
#include "microrl.h"

#define PRINTF_MAX  256     /* largo maximo de un consola_printf() */

static microrl_t s_rl;
static int (*s_ejecutar)(int, const char *const *) = NULL;
static void (*s_enviar)(const char *texto) = NULL;
static char s_anterior = 0;     /* ultimo byte recibido, para tomar \r\n como un solo Enter */

void consola_init(int (*ejecutar)(int argc, const char *const *argv),
                  void (*enviar)(const char *texto))
{
    s_ejecutar = ejecutar;
    s_enviar = enviar;
}

void consola_inicio(void)
{
    microrl_init(&s_rl, s_enviar);                  /* linea e historial vacios */
    microrl_set_execute_callback(&s_rl, s_ejecutar);
    s_anterior = 0;
    microrl_insert_char(&s_rl, KEY_LF);             /* linea vacia: solo muestra el prompt */
}

void consola_recibir(const char *datos, size_t largo)
{
    for (size_t i = 0; i < largo; i++) {
        char ch = datos[i];
        /* microrl esta configurado con Enter = \n (_ENDL_LF). Las terminales mandan
         * \r (PuTTY), \n (Linux) o \r\n: los tres se toman como un solo Enter. */
        if (ch == '\r') {
            microrl_insert_char(&s_rl, KEY_LF);
        } else if (ch != '\n' || s_anterior != '\r') {
            microrl_insert_char(&s_rl, ch);
        }
        s_anterior = ch;
    }
}

void consola_printf(const char *formato, ...)
{
    char texto[PRINTF_MAX];
    va_list args;
    va_start(args, formato);
    vsnprintf(texto, sizeof(texto), formato, args);
    va_end(args);
    s_enviar(texto);
}

void consola_escribir(const char *texto)
{
    s_enviar(texto);
}
