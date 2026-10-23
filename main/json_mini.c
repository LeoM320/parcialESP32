/**
 * @file json_mini.c
 * @brief Implementacion de json_mini.h.
 *
 * Recorre el texto de string en string ("..."). Si un string es la clave
 * buscada y despues viene ':', lo que sigue es el valor. Los demas strings se
 * saltean enteros, asi un valor que casualmente se llame como la clave no confunde.
 */

#include "json_mini.h"
#include <ctype.h>
#include <string.h>

static const char *saltar_espacios(const char *p)
{
    while (isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/* p apunta despues de la comilla de apertura. Copia el string a dest (si no
 * es NULL) y devuelve el puntero despues de la comilla de cierre, o NULL si
 * el string no termina o no entra en dest. */
static const char *leer_string(const char *p, char *dest, size_t tamano)
{
    size_t j = 0;
    while (*p != '"') {
        if (*p == '\0') {
            return NULL;
        }
        char c = *p++;
        if (c == '\\') {
            c = *p++;               /* \" \\ \/ -> el caracter */
            if (c == '\0') {
                return NULL;
            }
        }
        if (dest != NULL) {
            if (j + 1 >= tamano) {
                return NULL;
            }
            dest[j++] = c;
        }
    }
    if (dest != NULL) {
        dest[j] = '\0';
    }
    return p + 1;
}

/* Valor sin comillas (numero, true, false, null): hasta ',' '}' o espacio */
static bool leer_literal(const char *p, char *dest, size_t tamano)
{
    size_t j = 0;
    while (*p != '\0' && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
        if (j + 1 >= tamano) {
            return false;
        }
        dest[j++] = *p++;
    }
    dest[j] = '\0';
    return j > 0;
}

bool json_valor(const char *json, const char *clave, char *dest, size_t tamano)
{
    size_t largo_clave = strlen(clave);
    const char *p = json;

    while ((p = strchr(p, '"')) != NULL) {
        const char *inicio = p + 1;
        p = leer_string(inicio, NULL, 0);           /* saltear el string entero */
        if (p == NULL) {
            return false;
        }

        bool es_clave = (size_t)(p - 1 - inicio) == largo_clave &&
                        strncmp(inicio, clave, largo_clave) == 0;
        const char *q = saltar_espacios(p);
        if (!es_clave || *q != ':') {
            continue;
        }

        q = saltar_espacios(q + 1);
        if (*q == '"') {
            return leer_string(q + 1, dest, tamano) != NULL;
        }
        return leer_literal(q, dest, tamano);
    }
    return false;
}
