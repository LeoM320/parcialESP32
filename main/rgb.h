/**
 * @file rgb.h
 * @brief Control del LED RGB integrado WS2812 mediante el periferico RMT.
 */

#ifndef RGB_H_
#define RGB_H_

#include <stdint.h>
#include "driver/gpio.h"

/**
 * @brief Configura el LED RGB WS2812 en el pin indicado y lo deja apagado.
 * @param pin GPIO del WS2812 (GPIO_NUM_48 en ESP32-S3 DevKitC-1).
 */
void rgb_init(gpio_num_t pin);

/**
 * @brief Establece el color del LED RGB integrado (0, 0, 0 lo apaga).
 * @param r Intensidad de rojo (0..255).
 * @param g Intensidad de verde (0..255).
 * @param b Intensidad de azul (0..255).
 */
void rgb_set(uint8_t r, uint8_t g, uint8_t b);

#endif /* RGB_H_ */
