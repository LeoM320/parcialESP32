/**
 * @file iodigital.c
 * @brief Implementacion de iodigital.h con el driver GPIO de ESP-IDF.
 *
 * Cada funcion es casi una llamada directa al driver: gpio_config() configura,
 * gpio_set_level() escribe y gpio_get_level() lee.
 */

#include "iodigital.h"

/**
 * @brief Configura el pin como salida y lo deja en 0.
 * @param pin GPIO a configurar.
 */
void salida_init(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, 0);
}

/**
 * @brief Pone la salida en 1 o en 0.
 * @param pin   GPIO previamente configurado con salida_init().
 * @param nivel true = 1, false = 0.
 */
void salida_set(gpio_num_t pin, bool nivel)
{
    gpio_set_level(pin, nivel);
}

/**
 * @brief Lee en que estado esta una salida.
 * @param pin GPIO previamente configurado con salida_init().
 * @return true si esta en 1.
 */
bool salida_get(gpio_num_t pin)
{
    return gpio_get_level(pin);
}

/**
 * @brief Configura el pin como entrada.
 * @param pin  GPIO a configurar.
 * @param pull GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY o GPIO_FLOATING.
 */
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

/**
 * @brief Lee el nivel fisico de la entrada.
 * @param pin GPIO previamente configurado con entrada_init().
 * @return true si el pin esta en 1, false si esta en 0.
 */
bool entrada_get(gpio_num_t pin)
{
    return gpio_get_level(pin);
}

/**
 * @brief Registra una ISR para el flanco indicado en el pin de entrada.
 * @param pin    GPIO de entrada.
 * @param flanco Tipo de flanco (GPIO_INTR_NEGEDGE, GPIO_INTR_POSEDGE, etc.).
 * @param isr    Funcion de interrupcion a invocar.
 * @param arg    Argumento pasado a la ISR.
 */
void entrada_interrupcion(gpio_num_t pin, gpio_int_type_t flanco, gpio_isr_t isr, void *arg)
{
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(gpio_set_intr_type(pin, flanco));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, isr, arg));
}
