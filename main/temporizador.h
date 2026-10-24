/**
 * @file temporizador.h
 * @brief Interrupcion periodica con un timer de hardware (gptimer).
 *
 * Para muestrear a ritmo fijo (ej. leer el ADC cada 10 ms) o generar una base
 * de tiempo exacta, que vTaskDelay no garantiza.
 *
 * La ISR corre en contexto de interrupcion: tiene que ser corta, marcada
 * IRAM_ATTR y usar solo funciones ...FromISR. Nada de printf, var_set ni ADC
 * (adc_oneshot usa un mutex): avisarle a una tarea y que la tarea haga el trabajo.
 *
 * Ejemplo:
 *     static bool IRAM_ATTR al_vencer(void *arg)
 *     {
 *         BaseType_t despertada = pdFALSE;
 *         xEventGroupSetBitsFromISR(s_eventos, BIT_MUESTREAR, &despertada);
 *         return despertada == pdTRUE;
 *     }
 *     ...
 *     temporizador_init(10000, al_vencer, NULL);   // cada 10 ms
 */

#ifndef TEMPORIZADOR_H_
#define TEMPORIZADOR_H_

#include <stdbool.h>
#include <stdint.h>

#define TEMPORIZADOR_MAX    4   /**< Timers de hardware del ESP32-S3 */

/**
 * @brief ISR del temporizador.
 * @param arg El arg de temporizador_init().
 * @return true si la ISR desperto a una tarea de mas prioridad (el
 *         "despertada" de las funciones ...FromISR): asi el cambio de
 *         tarea es inmediato al salir de la interrupcion.
 */
typedef bool (*temporizador_isr_t)(void *arg);

/**
 * @brief Crea un timer de hardware que llama a la ISR cada periodo_us, y lo arranca.
 *
 * Aborta si ya se crearon TEMPORIZADOR_MAX.
 * @param periodo_us Periodo en microsegundos (ej. 1000 = 1 ms).
 * @param isr        Funcion a llamar en cada vencimiento.
 * @param arg        Dato que recibe la ISR tal cual, o NULL.
 */
void temporizador_init(uint32_t periodo_us, temporizador_isr_t isr, void *arg);

#endif /* TEMPORIZADOR_H_ */
