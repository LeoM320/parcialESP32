/**
 * @file variables.c
 * @brief Implementacion de variables.h.
 *
 * Cada variable guarda su ultimo valor y, segun su tipo, como llegar al
 * hardware (pin, rango). var_set() escribe el hardware y el valor juntos con el
 * mutex tomado: nadie puede leer un valor que todavia no se aplico. Las
 * entradas (din, ain) se leen del hardware cada vez que alguien pide su valor.
 *
 * Tipo nuevo: sumarlo a tipo_t y NOMBRE_TIPO, escribir su var_<tipo>() y
 * agregar su caso en leer(), var_set(), agregar_valor_json() y agregar_valor_texto().
 */

#include "variables.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "iodigital.h"
#include "ianalogica.h"
#include "pwm.h"

#define ADC_MAX     4095    /* lectura cruda maxima (12 bits) */

typedef enum {
    TIPO_DOUT,
    TIPO_DIN,
    TIPO_AIN,
    TIPO_PWM,
    TIPO_NUM,
    TIPO_TEXTO,
} tipo_t;

/* Nombre de cada tipo en el JSON (lo usan los clientes para elegir el ladrillo) */
static const char *const NOMBRE_TIPO[] = {
    [TIPO_DOUT]  = "dout",
    [TIPO_DIN]   = "din",
    [TIPO_AIN]   = "ain",
    [TIPO_PWM]   = "pwm",
    [TIPO_NUM]   = "num",
    [TIPO_TEXTO] = "texto",
};

typedef struct {
    const char *id;
    const char *etiqueta;
    tipo_t tipo;
    int fila, col;
    gpio_num_t pin;             /* dout, din, ain, pwm */
    bool activo_bajo;           /* din */
    float min, max;             /* ain, pwm, num */
    const char *unidad;         /* ain, pwm, num */
    float valor;                /* todos menos texto */
    char texto[VAR_TEXTO_MAX];  /* texto */
} var_t;

static var_t s_vars[VAR_MAX];
static size_t s_cant = 0;
static SemaphoreHandle_t s_mutex = NULL;

/* ---------- auxiliares ---------- */

/* Agrega texto al final de un buffer; si no entra, marca 'lleno' */
typedef struct {
    char *buf;
    size_t tamano;
    size_t largo;
    bool lleno;
} texto_t;

static void agregar(texto_t *t, const char *formato, ...)
{
    if (t->lleno) {
        return;
    }
    va_list args;
    va_start(args, formato);
    int n = vsnprintf(t->buf + t->largo, t->tamano - t->largo, formato, args);
    va_end(args);
    if (n < 0 || (size_t)n >= t->tamano - t->largo) {
        t->lleno = true;
    } else {
        t->largo += n;
    }
}

static int terminar(const texto_t *t)
{
    return t->lleno ? -1 : (int)t->largo;
}

static var_t *buscar(const char *id)
{
    for (size_t i = 0; i < s_cant; i++) {
        if (strcmp(s_vars[i].id, id) == 0) {
            return &s_vars[i];
        }
    }
    return NULL;
}

static var_t *nueva(const char *id, const char *etiqueta, tipo_t tipo, int fila, int col)
{
    if (s_cant >= VAR_MAX) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);    /* subir VAR_MAX */
    }
    var_t *v = &s_vars[s_cant++];
    *v = (var_t){ .id = id, .etiqueta = etiqueta, .tipo = tipo, .fila = fila, .col = col,
                  .unidad = "" };
    return v;
}

/* Las entradas toman el valor del hardware; el resto ya lo tiene guardado */
static void leer(var_t *v)
{
    switch (v->tipo) {
    case TIPO_DIN:
        v->valor = entrada_get(v->pin) != v->activo_bajo ? 1 : 0;
        break;
    case TIPO_AIN: {
        int crudo = analogica_get_crudo(v->pin);
        if (crudo >= 0) {               /* si falla, queda el valor anterior */
            float x = v->min + (v->max - v->min) * crudo / ADC_MAX;
            v->valor = roundf(x * 100) / 100;   /* 2 decimales alcanzan */
        }
        break;
    }
    default:
        break;
    }
}

/* "1"/"on" -> 1, "0"/"off" -> 0, "toggle" -> lo contrario de actual */
static bool a_binario(const char *txt, float actual, float *nuevo)
{
    if (strcmp(txt, "1") == 0 || strcmp(txt, "on") == 0) {
        *nuevo = 1;
    } else if (strcmp(txt, "0") == 0 || strcmp(txt, "off") == 0) {
        *nuevo = 0;
    } else if (strcmp(txt, "toggle") == 0) {
        *nuevo = actual != 0 ? 0 : 1;
    } else {
        return false;
    }
    return true;
}

/* Numero completo (ej. "12.5") dentro de min..max */
static bool a_numero(const char *txt, float min, float max, float *nuevo)
{
    char *fin;
    float x = strtof(txt, &fin);
    if (fin == txt || *fin != '\0' || !isfinite(x) || x < min || x > max) {
        return false;
    }
    *nuevo = x;
    return true;
}

/* Cabe en VAR_TEXTO_MAX y se puede poner entre comillas en el JSON sin escapar */
static bool texto_valido(const char *txt)
{
    if (strlen(txt) >= VAR_TEXTO_MAX) {
        return false;
    }
    for (const char *c = txt; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\' || (unsigned char)*c < ' ') {
            return false;
        }
    }
    return true;
}

/* Valor en JSON: numero, o string para texto */
static void agregar_valor_json(texto_t *t, const var_t *v)
{
    switch (v->tipo) {
    case TIPO_DOUT:
    case TIPO_DIN:
        agregar(t, "%d", (int)v->valor);
        break;
    case TIPO_AIN:
    case TIPO_PWM:
    case TIPO_NUM:
        agregar(t, "%g", v->valor);
        break;
    case TIPO_TEXTO:
        agregar(t, "\"%s\"", v->texto);
        break;
    }
}

/* Valor para personas */
static void agregar_valor_texto(texto_t *t, const var_t *v)
{
    switch (v->tipo) {
    case TIPO_DOUT:
    case TIPO_DIN:
        agregar(t, "%s", v->valor != 0 ? "on" : "off");
        break;
    case TIPO_AIN:
    case TIPO_PWM:
    case TIPO_NUM:
        agregar(t, "%g%s%s", v->valor, v->unidad[0] != '\0' ? " " : "", v->unidad);
        break;
    case TIPO_TEXTO:
        agregar(t, "%s", v->texto);
        break;
    }
}

/* ---------- API ---------- */

void var_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

void var_dout(const char *id, const char *etiqueta, gpio_num_t pin, int fila, int col)
{
    var_t *v = nueva(id, etiqueta, TIPO_DOUT, fila, col);
    v->pin = pin;
    salida_init(pin);           /* arranca en 0 */
}

void var_din(const char *id, const char *etiqueta, gpio_num_t pin, bool activo_bajo,
             int fila, int col)
{
    var_t *v = nueva(id, etiqueta, TIPO_DIN, fila, col);
    v->pin = pin;
    v->activo_bajo = activo_bajo;
    entrada_init(pin, activo_bajo ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
}

void var_ain(const char *id, const char *etiqueta, gpio_num_t pin, float min, float max,
             const char *unidad, int fila, int col)
{
    var_t *v = nueva(id, etiqueta, TIPO_AIN, fila, col);
    v->pin = pin;
    v->min = min;
    v->max = max;
    v->unidad = unidad;
    analogica_init(pin);
}

void var_pwm(const char *id, const char *etiqueta, gpio_num_t pin, uint32_t frecuencia_hz,
             int fila, int col)
{
    var_t *v = nueva(id, etiqueta, TIPO_PWM, fila, col);
    v->pin = pin;
    v->min = 0;
    v->max = 100;
    v->unidad = "%";
    pwm_init(pin, frecuencia_hz);
    pwm_set_duty(pin, 0);
}

void var_num(const char *id, const char *etiqueta, float min, float max, float inicial,
             int fila, int col)
{
    var_t *v = nueva(id, etiqueta, TIPO_NUM, fila, col);
    v->min = min;
    v->max = max;
    v->valor = inicial;
}

void var_txt(const char *id, const char *etiqueta, const char *inicial, int fila, int col)
{
    var_t *v = nueva(id, etiqueta, TIPO_TEXTO, fila, col);
    if (!texto_valido(inicial)) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);   /* muy largo o con comillas */
    }
    strcpy(v->texto, inicial);
}

var_resultado_t var_set(const char *id, const char *valor)
{
    var_resultado_t r = VAR_OK;
    float nuevo;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    var_t *v = buscar(id);
    if (v == NULL) {
        r = VAR_NO_EXISTE;
    } else {
        switch (v->tipo) {
        case TIPO_DOUT:
            if (!a_binario(valor, v->valor, &nuevo)) {
                r = VAR_VALOR_INVALIDO;
                break;
            }
            v->valor = nuevo;
            salida_set(v->pin, nuevo != 0);
            break;
        case TIPO_DIN:
        case TIPO_AIN:
            r = VAR_SOLO_LECTURA;
            break;
        case TIPO_PWM:
            if (!a_numero(valor, v->min, v->max, &nuevo)) {
                r = VAR_VALOR_INVALIDO;
                break;
            }
            v->valor = nuevo;
            pwm_set_duty(v->pin, nuevo);
            break;
        case TIPO_NUM:
            if (!a_numero(valor, v->min, v->max, &nuevo)) {
                r = VAR_VALOR_INVALIDO;
                break;
            }
            v->valor = nuevo;
            break;
        case TIPO_TEXTO:
            if (!texto_valido(valor)) {
                r = VAR_VALOR_INVALIDO;
                break;
            }
            strcpy(v->texto, valor);
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return r;
}

bool var_get(const char *id, float *valor)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    var_t *v = buscar(id);
    if (v != NULL) {
        leer(v);
        *valor = v->valor;
    }
    xSemaphoreGive(s_mutex);
    return v != NULL;
}

int var_json_def(char *buf, size_t tamano)
{
    texto_t t = { .buf = buf, .tamano = tamano };
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    agregar(&t, "{\"vars\":[");
    for (size_t i = 0; i < s_cant; i++) {
        const var_t *v = &s_vars[i];
        agregar(&t, "%s{\"id\":\"%s\",\"tipo\":\"%s\",\"etq\":\"%s\",\"fila\":%d,\"col\":%d",
                i > 0 ? "," : "", v->id, NOMBRE_TIPO[v->tipo], v->etiqueta, v->fila, v->col);
        if (v->tipo == TIPO_AIN || v->tipo == TIPO_PWM || v->tipo == TIPO_NUM) {
            agregar(&t, ",\"min\":%g,\"max\":%g,\"unidad\":\"%s\"", v->min, v->max, v->unidad);
        }
        agregar(&t, "}");
    }
    agregar(&t, "]}");
    xSemaphoreGive(s_mutex);
    return terminar(&t);
}

int var_json_valores(char *buf, size_t tamano)
{
    texto_t t = { .buf = buf, .tamano = tamano };
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    agregar(&t, "{");
    for (size_t i = 0; i < s_cant; i++) {
        leer(&s_vars[i]);
        agregar(&t, "%s\"%s\":", i > 0 ? "," : "", s_vars[i].id);
        agregar_valor_json(&t, &s_vars[i]);
    }
    agregar(&t, "}");
    xSemaphoreGive(s_mutex);
    return terminar(&t);
}

int var_listar(char *buf, size_t tamano)
{
    texto_t t = { .buf = buf, .tamano = tamano };
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (size_t i = 0; i < s_cant; i++) {
        var_t *v = &s_vars[i];
        leer(v);
        agregar(&t, "  %-10s %-16s %-6s ", v->id, v->etiqueta, NOMBRE_TIPO[v->tipo]);
        agregar_valor_texto(&t, v);
        agregar(&t, "\r\n");
    }
    xSemaphoreGive(s_mutex);
    return terminar(&t);
}

bool var_texto(const char *id, char *buf, size_t tamano)
{
    texto_t t = { .buf = buf, .tamano = tamano };
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    var_t *v = buscar(id);
    if (v != NULL) {
        leer(v);
        agregar_valor_texto(&t, v);
    }
    xSemaphoreGive(s_mutex);
    return v != NULL && terminar(&t) >= 0;
}
