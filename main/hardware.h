/*
 * hardware.h
 * Mapa de pines de la placa. Unico lugar donde se tocan los GPIO.
 * Si cambia el cableado, se cambia solo aca.
 */

#ifndef HARDWARE_H_		// guarda de inclusion: si dos archivos lo incluyen,
#define HARDWARE_H_		// el compilador lo lee una sola vez

#include "driver/gpio.h"	// tipo gpio_num_t y las constantes GPIO_NUM_x

/* ---------- Salidas digitales: LEDs (activos en alto) ---------- */
/* Activo en alto: 1 = prendido, 0 = apagado (cada LED con su resistencia de 330 ohm) */
#define LED1_GPIO   GPIO_NUM_4
#define LED2_GPIO   GPIO_NUM_5
#define LED3_GPIO   GPIO_NUM_6
#define LED4_GPIO   GPIO_NUM_7

/* ---------- Entradas digitales: switches a GND (pull-up interno, apretado = 0) ---------- */
/* El pull-up deja el pin en 1; al apretar, el switch lo une a GND y se lee 0 */
#define SW1_GPIO    GPIO_NUM_11
#define SW2_GPIO    GPIO_NUM_12
#define SW3_GPIO    GPIO_NUM_13
#define SW4_GPIO    GPIO_NUM_14

/* ---------- Entradas analogicas: potenciometros (solo ADC1: GPIO1..GPIO10) ---------- */
/* ADC2 lo usa el WiFi, por eso los potes van en pines del ADC1 */
#define POT1_GPIO   GPIO_NUM_1		// ADC1 canal 0
#define POT2_GPIO   GPIO_NUM_2		// ADC1 canal 1

/* ---------- Salidas PWM (un pin PWM no se usa ademas como salida digital) ---------- */
#define LED_PWM_GPIO        LED1_GPIO	// si usas PWM en el LED 1, no le hagas salida_init
#define LED_PWM_FREC_HZ     1000		// 1 kHz: no se ve parpadeo

/* ---------- LED RGB integrado (WS2812, se maneja con rgb.h) ---------- */
#define RGB_GPIO    GPIO_NUM_48		// DevKitC-1 v1.0; en la v1.1 es GPIO_NUM_38

/* ---------- Tablas para recorrer por indice ---------- */
/* leds[0] es el LED 1, leds[1] el LED 2... asi se recorren con un for.
 * static: cada .c que incluye este archivo tiene su propia copia (son constantes, no importa). */
static const gpio_num_t leds[]     = { LED1_GPIO, LED2_GPIO, LED3_GPIO, LED4_GPIO };
static const gpio_num_t switches[] = { SW1_GPIO, SW2_GPIO, SW3_GPIO, SW4_GPIO };
static const gpio_num_t pots[]     = { POT1_GPIO, POT2_GPIO };

/* Cantidad de elementos: tamanio total del arreglo / tamanio de un elemento */
#define LED_COUNT   (sizeof(leds) / sizeof(leds[0]))			// 4
#define SW_COUNT    (sizeof(switches) / sizeof(switches[0]))	// 4
#define POT_COUNT   (sizeof(pots) / sizeof(pots[0]))			// 2

#endif /* HARDWARE_H_ */
