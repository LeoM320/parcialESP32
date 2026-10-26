/**
 * @file rgb.h
 * @brief LED RGB integrado de la placa (WS2812, "NeoPixel").
 *
 * No conoce el hardware: recibe el pin (definido en hardware.h).
 * Basada en heartbeat.c de la Actividad 2 (sistemasEmbebidosTP2).
 *
 * El WS2812 no se maneja con PWM ni con salida_set: tiene un chip adentro que
 * recibe el color por un solo cable, como una trama serie de pulsos de
 * nanosegundos. Esos pulsos los genera el periferico RMT del ESP32 (componente
 * espressif/led_strip, declarado en idf_component.yml).
 *
 * - Brillo: 0..255 por color, pero con 255 encandila. Con 5..30 se ve bien.
 * - Se puede llamar desde varias tareas y desde un software timer (un mutex
 *   atiende un pedido a la vez). NO desde una ISR (espera a que salga la trama).
 */

#ifndef RGB_H_
#define RGB_H_

#include <stdint.h>
#include "driver/gpio.h"

/**
 * @brief Configura el LED RGB en ese pin y lo deja apagado.
 * @param pin GPIO del WS2812 (en la DevKitC-1 v1.0: GPIO48; en la v1.1: GPIO38).
 */
void rgb_init(gpio_num_t pin);

/**
 * @brief Pone el color del LED. rgb_set(0, 0, 0) lo apaga.
 * @param r Rojo  0..255.
 * @param g Verde 0..255.
 * @param b Azul  0..255.
 */
void rgb_set(uint8_t r, uint8_t g, uint8_t b);

#endif /* RGB_H_ */
