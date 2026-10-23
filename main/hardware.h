/*
 * hardware.h
 * Mapa de pines de la placa. Unico lugar donde se tocan los GPIO.
 */

#ifndef HARDWARE_H_
#define HARDWARE_H_

#include "driver/gpio.h"

/* ---------- Salidas digitales: LEDs (activos en alto) ---------- */
#define LED1_GPIO   GPIO_NUM_4
#define LED2_GPIO   GPIO_NUM_5
#define LED3_GPIO   GPIO_NUM_6
#define LED4_GPIO   GPIO_NUM_7

/* ---------- Entradas digitales: switches a GND (pull-up interno, apretado = 0) ---------- */
#define SW1_GPIO    GPIO_NUM_11
#define SW2_GPIO    GPIO_NUM_12
#define SW3_GPIO    GPIO_NUM_13
#define SW4_GPIO    GPIO_NUM_14

/* ---------- Entradas analogicas: potenciometros (solo ADC1: GPIO1..GPIO10) ---------- */
#define POT1_GPIO   GPIO_NUM_1
#define POT2_GPIO   GPIO_NUM_2

/* ---------- Salidas PWM (un pin PWM no se usa ademas como salida digital) ---------- */
#define LED_PWM_GPIO        LED1_GPIO
#define LED_PWM_FREC_HZ     1000

/* ---------- Tablas para recorrer por indice ---------- */
static const gpio_num_t leds[]     = { LED1_GPIO, LED2_GPIO, LED3_GPIO, LED4_GPIO };
static const gpio_num_t switches[] = { SW1_GPIO, SW2_GPIO, SW3_GPIO, SW4_GPIO };
static const gpio_num_t pots[]     = { POT1_GPIO, POT2_GPIO };
#define LED_COUNT   (sizeof(leds) / sizeof(leds[0]))
#define SW_COUNT    (sizeof(switches) / sizeof(switches[0]))
#define POT_COUNT   (sizeof(pots) / sizeof(pots[0]))

#endif /* HARDWARE_H_ */
