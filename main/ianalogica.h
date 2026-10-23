/**
 * @file ianalogica.h
 * @brief Entradas analogicas genericas con el driver adc_oneshot de ESP-IDF
 * (ADC1).
 *
 * No conoce el hardware: recibe los pines (definidos en hardware.h).
 * Basada en adc.h/adc.c de la Actividad 2 (sistemasEmbebidosTP2).
 *
 * Filtrado bajo demanda: analogica_get() toma ANALOGICA_MUESTRAS conversiones
 * separadas ANALOGICA_INTERVALO_MS y devuelve el promedio. Entre muestras la
 * tarea que llama queda BLOQUEADA (no espera activa): la CPU queda libre para
 * las demas tareas. Con muestras cada 10 ms (medio ciclo de 50 Hz) el zumbido
 * de red se cancela al promediar.
 *
 * - ANALOGICA_INTERVALO_MS debe ser >= 1 tick de FreeRTOS (10 ms con 100 Hz).
 * - Solo ADC1 (GPIO1..GPIO10 en ESP32-S3): ADC2 no se puede usar con WiFi
 * activo.
 * - Se puede llamar desde varias tareas: un mutex atiende un pedido a la vez.
 * - NO llamar desde una ISR ni desde un callback de timer (se bloquea).
 */

#ifndef IANALOGICA_H_
#define IANALOGICA_H_

#include "driver/gpio.h"

#define ANALOGICA_MUESTRAS                                                     \
	5 /**< Conversiones promediadas por analogica_get() */
#define ANALOGICA_INTERVALO_MS                                                 \
	10 /**< Separacion entre conversiones (>= 1 tick) */

/**
 * @brief Configura el pin como entrada analogica: 12 bits, atenuacion 12 dB
 * (~0-3.1 V).
 *
 * La primera llamada crea la unidad ADC1. Aborta si el pin no pertenece a ADC1.
 * @param pin GPIO de ADC1 a configurar.
 */
void analogica_init(gpio_num_t pin);

/**
 * @brief Lectura filtrada: promedio de ANALOGICA_MUESTRAS conversiones
 * espaciadas.
 *
 * Bloquea a la tarea que llama durante ~ANALOGICA_MUESTRAS *
 * ANALOGICA_INTERVALO_MS (50 ms), sin ocupar la CPU.
 * @param pin GPIO previamente configurado con analogica_init().
 * @return Valor filtrado 0..4095, o -1 si falla alguna conversion.
 */
int analogica_get(gpio_num_t pin);

/**
 * @brief Una sola conversion, sin filtrar y sin bloquear.
 * @param pin GPIO previamente configurado con analogica_init().
 * @return Lectura cruda 0..4095, o -1 si falla la conversion.
 */
int analogica_get_crudo(gpio_num_t pin);

#endif /* IANALOGICA_H_ */
