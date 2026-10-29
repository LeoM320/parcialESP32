/**
 * @file ianalogica.h
 * @brief Entradas analogicas genericas con el driver adc_oneshot de ESP-IDF
 * (ADC1).
 *
 * No conoce el hardware: recibe los pines (definidos en hardware.h).
 * No filtra: devuelve una sola conversion. El filtro (media movil, etc.)
 * se hace en main.c.
 *
 * - Solo ADC1 (GPIO1..GPIO10 en ESP32-S3): ADC2 no se puede usar con WiFi
 *   activo.
 * - Leer desde UNA sola tarea (si dos leen a la vez, una puede fallar y
 *   devolver -1).
 * - NO llamar desde una ISR.
 */

#ifndef IANALOGICA_H_
#define IANALOGICA_H_

#include "driver/gpio.h"

/**
 * @brief Configura el pin como entrada analogica: 12 bits, atenuacion 12 dB
 * (~0-3.1 V).
 *
 * La primera llamada crea la unidad ADC1. Aborta si el pin no pertenece a ADC1.
 * @param pin GPIO de ADC1 a configurar.
 */
void analogica_init(gpio_num_t pin);

/**
 * @brief Una conversion, sin filtrar.
 * @param pin GPIO previamente configurado con analogica_init().
 * @return Lectura 0..4095, o -1 si falla la conversion.
 */
int analogica_get(gpio_num_t pin);

#endif /* IANALOGICA_H_ */
