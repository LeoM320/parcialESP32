/**
 * @file iodigital.c
 * @brief Implementacion de iodigital.h con el driver GPIO de ESP-IDF.
 */

#include "iodigital.h"

void salida_init(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, 0);
}

void salida_set(gpio_num_t pin, bool nivel)
{
    gpio_set_level(pin, nivel);
}

void entrada_init(gpio_num_t pin, gpio_pull_mode_t pull)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_pull_mode(pin, pull));
}

bool entrada_get(gpio_num_t pin)
{
    return gpio_get_level(pin);
}
