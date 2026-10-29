/**
 * @file pwm.h
 * @brief Salidas PWM genericas con el driver MCPWM de ESP-IDF.
 *
 * No conoce el hardware: recibe los pines (definidos en hardware.h).
 * Basada en el "Ejemplo MCPWM" del curso (servo): timer de 1 MHz (1 tick = 1 us),
 * salida en ALTO al llegar a cero y en BAJO al coincidir con el comparador.
 *
 * - Cada pin usa su propio timer, asi cada salida puede tener su frecuencia.
 *   El ESP32-S3 tiene 2 grupos MCPWM x 3 timers = hasta 6 salidas PWM.
 * - Frecuencia valida: 16 Hz .. 500 kHz (periodo de 2 a 65535 ticks de 1 us).
 * - Un pin usado como PWM no debe configurarse ademas con salida_init().
 * - pwm_set_duty() y pwm_set_pulso_us() se pueden llamar desde varias tareas.
 */

#ifndef PWM_H_
#define PWM_H_

#include <stdint.h>
#include "driver/gpio.h"

/**
 * @brief Configura el pin como salida PWM y la arranca con ciclo de trabajo 0 %.
 *
 * Aborta si no quedan timers libres o si la frecuencia esta fuera de rango.
 * @param pin           GPIO de salida.
 * @param frecuencia_hz Frecuencia del PWM (por ejemplo 50 para servos, 1000 para LEDs).
 */
void pwm_init(gpio_num_t pin, uint32_t frecuencia_hz);

/**
 * @brief Fija el ciclo de trabajo en porcentaje.
 * @param pin        GPIO previamente configurado con pwm_init().
 * @param porcentaje 0.0 .. 100.0 (fuera de rango se recorta).
 */
void pwm_set_duty(gpio_num_t pin, float porcentaje);

/**
 * @brief Fija el ancho del pulso en microsegundos (util para servos: 500..2500 us a 50 Hz).
 * @param pin GPIO previamente configurado con pwm_init().
 * @param us  Ancho del pulso; si supera el periodo se recorta al periodo (100 %).
 */
void pwm_set_pulso_us(gpio_num_t pin, uint32_t us);

#endif /* PWM_H_ */
