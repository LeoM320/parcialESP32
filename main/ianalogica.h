/**
 * @file ianalogica.h
 * @brief Entradas analogicas genericas con el driver adc_oneshot de ESP-IDF
 * (ADC1).
 *
 * No conoce el hardware: recibe los pines (definidos en hardware.h).
 * Basada en adc.h/adc.c de la Actividad 2 (sistemasEmbebidosTP2).
 *
 * Filtrado bajo demanda: analogica_get() toma ANALOGICA_MUESTRAS conversiones
 * separadas ANALOGICA_INTERVALO_MS, las ordena, descarta la menor y la mayor y
 * promedia el resto (promedio recortado). Entre muestras la tarea que llama
 * queda BLOQUEADA (no espera activa): la CPU queda libre para las demas tareas.
 *
 * Por que recortado: medido en la placa, el ruido normal es +-2 cuentas y el
 * zumbido de 50 Hz ~1 cuenta, pero ~2% de las muestras traen picos de hasta
 * -70 (el WiFi al transmitir). Un promedio comun los deja pasar (la lectura
 * saltaba 10-18 cuentas); descartando los extremos queda en 2-6.
 *
 * - ANALOGICA_MUESTRAS debe ser >= 3 (se descartan 2).
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
 * @brief Lectura filtrada: promedio recortado (sin la menor ni la mayor) de
 * ANALOGICA_MUESTRAS conversiones espaciadas.
 *
 * Bloquea a la tarea que llama durante ~ANALOGICA_MUESTRAS *
 * ANALOGICA_INTERVALO_MS (50 ms), sin ocupar la CPU.
 * @param pin GPIO previamente configurado con analogica_init().
 * @return Valor filtrado 0..4095, o -1 si falla alguna conversion.
 */
int analogica_get(gpio_num_t pin);

/**
 * @brief Una sola conversion, sin filtrar. Solo espera si otra tarea esta
 * usando el ADC en ese momento (mismo mutex que analogica_get()).
 * @param pin GPIO previamente configurado con analogica_init().
 * @return Lectura cruda 0..4095, o -1 si falla la conversion.
 */
int analogica_get_crudo(gpio_num_t pin);

#endif /* IANALOGICA_H_ */
