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

#endif /* IODIGITAL_H_ */
