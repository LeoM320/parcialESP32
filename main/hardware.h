/**
 * @file hardware.h
 * @brief Mapa de pines de la placa ESP32-S3 (LEDs, pulsadores y potenciometros).
 */

#ifndef HARDWARE_H_
#define HARDWARE_H_

#include "driver/gpio.h"

#define LED1_GPIO   GPIO_NUM_4      ///< LED 0 (ARM1_L), activo en alto.
#define LED2_GPIO   GPIO_NUM_5      ///< LED 1 (ARM1_R), activo en alto.
#define LED3_GPIO   GPIO_NUM_6      ///< LED 2 (ARM2_L), activo en alto.
#define LED4_GPIO   GPIO_NUM_7      ///< LED 3 (ARM2_R), activo en alto.

#define SW1_GPIO    GPIO_NUM_11     ///< Pulsador SW0 (+ang1), pull-up a GND.
#define SW2_GPIO    GPIO_NUM_12     ///< Pulsador SW1 (-ang1), pull-up a GND.
#define SW3_GPIO    GPIO_NUM_13     ///< Pulsador SW2 (+ang2), pull-up a GND.
#define SW4_GPIO    GPIO_NUM_14     ///< Pulsador SW3 (-ang2), pull-up a GND.

#define POT1_GPIO   GPIO_NUM_1      ///< Potenciometro 1 (ADC1 canal 0).
#define POT2_GPIO   GPIO_NUM_2      ///< Potenciometro 2 (ADC1 canal 1).

#define RGB_GPIO    GPIO_NUM_48     ///< LED RGB WS2812 integrado en la placa DevKitC-1.

static const gpio_num_t leds[]     = { LED1_GPIO, LED2_GPIO, LED3_GPIO, LED4_GPIO };    ///< Arreglo de pines de LEDs.
static const gpio_num_t switches[] = { SW1_GPIO, SW2_GPIO, SW3_GPIO, SW4_GPIO };        ///< Arreglo de pines de switches.
static const gpio_num_t pots[]     = { POT1_GPIO, POT2_GPIO };                          ///< Arreglo de pines de potes.

#define LED_COUNT   (sizeof(leds) / sizeof(leds[0]))            ///< Cantidad de LEDs (4).
#define SW_COUNT    (sizeof(switches) / sizeof(switches[0]))    ///< Cantidad de switches (4).
#define POT_COUNT   (sizeof(pots) / sizeof(pots[0]))            ///< Cantidad de potes (2).

#endif /* HARDWARE_H_ */
