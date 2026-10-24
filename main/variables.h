/**
 * @file variables.h
 * @brief Registro de variables: lo que se puede ver o controlar del ESP32.
 *
 * Cada salida, entrada o dato del programa se registra una vez con un id. Despues,
 * la web, la app y la consola trabajan solo con ids: piden la definicion (que
 * hay y como mostrarlo) y los valores en JSON, y cambian valores con var_set().
 * Agregar una variable = una linea en main; los clientes la muestran solos.
 *
 * Tipos: dout (salida digital), din (entrada digital), ain (entrada analogica),
 * pwm (salida PWM), num (numero del programa) y texto (texto del programa).
 *
 * - Todas las funciones son seguras entre tareas (mutex interno), salvo las
 *   var_<tipo>() de registro: llamarlas al arrancar, antes de crear tareas.
 * - Ids, etiquetas y unidades: sin comillas ni barras (van tal cual al JSON).
 *   Ids sin espacios.
 */

#ifndef VARIABLES_H_
#define VARIABLES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"

#define VAR_MAX         24  /**< Variables que se pueden registrar */
#define VAR_TEXTO_MAX   32  /**< Largo maximo de una variable texto, con el '\0' */

/** Resultado de var_set(). */
typedef enum {
    VAR_OK,
    VAR_NO_EXISTE,          /**< No hay variable con ese id */
    VAR_SOLO_LECTURA,       /**< Es una entrada: no se puede escribir */
    VAR_VALOR_INVALIDO,     /**< El valor no corresponde al tipo o esta fuera de rango */
} var_resultado_t;

/**
 * @brief Inicializa el registro (crea el mutex). Se puede llamar varias veces.
 *
 * No hace falta llamarla: la llaman el primer var_<tipo>() y sistema_init().
 * Llamarla antes de crear tareas.
 */
void var_init(void);

/**
 * @brief Registra una salida digital (ej. un LED). Inicializa el pin en 0.
 *
 * Valores: 0 / 1. var_set() acepta "0", "1", "off", "on" y "toggle".
 * @param id       Nombre corto, ej. "led1".
 * @param etiqueta Texto para mostrar, ej. "LED 1".
 * @param pin      GPIO de salida.
 * @param fila     Fila en la grilla de los clientes.
 * @param col      Columna en la grilla de los clientes.
 */
void var_dout(const char *id, const char *etiqueta, gpio_num_t pin, int fila, int col);

/**
 * @brief Registra una entrada digital (ej. un pulsador). Solo lectura.
 *
 * Valores: 0 / 1. Se lee el pin cada vez que se pide el valor.
 * @param pin         GPIO de entrada (con pull-up si activo_bajo, sin pull si no).
 * @param activo_bajo true si el pin vale 0 cuando esta activo (pulsador a GND
 *                    con pull-up): asi el valor es 1 al apretar.
 */
void var_din(const char *id, const char *etiqueta, gpio_num_t pin, bool activo_bajo,
             int fila, int col);

/**
 * @brief Registra una entrada analogica (ADC1). Solo lectura.
 *
 * El valor es la lectura (0..4095) escalada a min..max, ej. 0..100 "%" o
 * 0..3.1 "V". Una conversion sin filtrar cada vez que se pide el valor
 * (analogica_get() bloquea 50 ms: con varios clientes trabaria a todos).
 * @param pin    GPIO de ADC1.
 * @param min    Valor que corresponde a la lectura 0.
 * @param max    Valor que corresponde a la lectura 4095.
 * @param unidad Texto que muestran los clientes, ej. "V" ("" si no tiene).
 */
void var_ain(const char *id, const char *etiqueta, gpio_num_t pin, float min, float max,
             const char *unidad, int fila, int col);

/**
 * @brief Registra una salida PWM. Arranca en 0 %.
 *
 * Valores: ciclo de trabajo 0..100 (%). var_set() acepta un numero en ese rango.
 * @param pin           GPIO de salida.
 * @param frecuencia_hz Frecuencia del PWM, ej. 1000 para un LED.
 */
void var_pwm(const char *id, const char *etiqueta, gpio_num_t pin, uint32_t frecuencia_hz,
             int fila, int col);

/**
 * @brief Registra un numero del programa (ej. una consigna). No toca hardware.
 *
 * var_set() acepta un numero entre min y max; el programa lo lee con var_get().
 * @param inicial Valor de arranque.
 */
void var_num(const char *id, const char *etiqueta, float min, float max, float inicial,
             int fila, int col);

/**
 * @brief Registra un texto del programa (ej. un estado o mensaje). No toca hardware.
 *
 * var_set() acepta hasta VAR_TEXTO_MAX - 1 caracteres, sin comillas ni barras
 * invertidas. El programa lo lee con var_texto() y lo cambia con var_set().
 * @param inicial Texto de arranque.
 */
void var_txt(const char *id, const char *etiqueta, const char *inicial, int fila, int col);

/**
 * @brief Cambia el valor de una variable.
 * @param id    Id de la variable.
 * @param valor Valor en texto (el formato depende del tipo, ver var_<tipo>()).
 * @return VAR_OK, o el motivo por el que no se pudo.
 */
var_resultado_t var_set(const char *id, const char *valor);

/**
 * @brief Valor actual de una variable numerica (para la logica del programa).
 *
 * Las entradas se leen en el momento. Para un texto da 0: usar var_texto().
 * @param id    Id de la variable.
 * @param valor Donde copiarlo.
 * @return false si no existe.
 */
bool var_get(const char *id, float *valor);

/**
 * @brief JSON con la definicion de todas las variables (que son y como mostrarlas):
 *        {"vars":[{"id":"led1","tipo":"dout","etq":"LED 1","fila":0,"col":0},...]}
 *        ain, pwm y num agregan "min", "max" y "unidad".
 * @return Largo escrito, o -1 si no entra en buf.
 */
int var_json_def(char *buf, size_t tamano);

/**
 * @brief JSON con el valor de todas las variables: {"led2":1,"pot1":42.5,"estado":"ok",...}
 * @return Largo escrito, o -1 si no entra en buf.
 */
int var_json_valores(char *buf, size_t tamano);

/**
 * @brief Texto para personas con una variable por linea ("led1  LED 1  dout  on\r\n").
 * @return Largo escrito, o -1 si no entra en buf.
 */
int var_listar(char *buf, size_t tamano);

/**
 * @brief Valor de una variable en texto para personas (ej. "on", "42.5 %", "ok").
 * @return false si no existe.
 */
bool var_texto(const char *id, char *buf, size_t tamano);

#endif /* VARIABLES_H_ */
