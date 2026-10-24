/**
 * @file iodigital.h
 * @brief Entradas y salidas digitales genericas.
 *
 * No conoce el hardware: recibe los pines (definidos en hardware.h).
 */

#ifndef IODIGITAL_H_
#define IODIGITAL_H_

#include <stdbool.h>
#include "driver/gpio.h"

/**
 * @brief Configura el pin como salida y lo deja en 0.
 *
 * Queda como entrada/salida, asi salida_get() puede leer en que estado esta.
 * @param pin GPIO a configurar.
 */
void salida_init(gpio_num_t pin);

/**
 * @brief Pone la salida en 1 o en 0.
 * @param pin   GPIO previamente configurado con salida_init().
 * @param nivel true = 1, false = 0.
 */
void salida_set(gpio_num_t pin, bool nivel);

/**
 * @brief Lee en que estado esta una salida (para mostrarla o invertirla).
 * @param pin GPIO previamente configurado con salida_init().
 * @return true si esta en 1.
 */
bool salida_get(gpio_num_t pin);

/**
 * @brief Configura el pin como entrada.
 * @param pin  GPIO a configurar.
 * @param pull GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY o GPIO_FLOATING.
 */
void entrada_init(gpio_num_t pin, gpio_pull_mode_t pull);

/**
 * @brief Lee el nivel fisico de la entrada.
 * @param pin GPIO previamente configurado con entrada_init().
 * @return true si el pin esta en 1, false si esta en 0.
 */
bool entrada_get(gpio_num_t pin);

/**
 * @brief Llama a una funcion (ISR) cuando la entrada cambia.
 *
 * La entrada tiene que estar configurada antes con entrada_init().
 * La ISR corre en contexto de interrupcion: tiene que ser corta, marcada
 * IRAM_ATTR y usar solo funciones ...FromISR (ej. xQueueSendFromISR,
 * xSemaphoreGiveFromISR, xEventGroupSetBitsFromISR). Nada de printf, mutex
 * ni ADC: avisarle a una tarea y que la tarea haga el trabajo.
 * @param pin    GPIO de entrada.
 * @param flanco GPIO_INTR_NEGEDGE (1 -> 0, ej. al apretar un pulsador con pull-up),
 *               GPIO_INTR_POSEDGE (0 -> 1) o GPIO_INTR_ANYEDGE (los dos).
 * @param isr    Funcion a llamar: void isr(void *arg).
 * @param arg    Dato que recibe la ISR tal cual (ej. el numero de pulsador), o NULL.
 */
void entrada_interrupcion(gpio_num_t pin, gpio_int_type_t flanco, gpio_isr_t isr, void *arg);

#endif /* IODIGITAL_H_ */
