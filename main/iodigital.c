/**
 * @file iodigital.c
 * @brief Implementacion de iodigital.h con el driver GPIO de ESP-IDF.
 *
 * Cada funcion es casi una llamada directa al driver: gpio_config() configura,
 * gpio_set_level() escribe y gpio_get_level() lee.
 */

#include "iodigital.h"

void salida_init(gpio_num_t pin)
{
    /* gpio_config() recibe un struct con toda la configuracion del pin */
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,                /* mascara de bits: un 1 en la posicion del pin
                                                       (ULL = 64 bits, el S3 tiene mas de 32 GPIO) */
        .mode         = GPIO_MODE_INPUT_OUTPUT,     /* salida que tambien se puede leer */
        .pull_up_en   = GPIO_PULLUP_DISABLE,        /* una salida no necesita pull-up */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,          /* sin interrupcion */
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));             /* si falla, reinicia y muestra el error */
    gpio_set_level(pin, 0);                         /* arranca apagado */
}

void salida_set(gpio_num_t pin, bool nivel)
{
    gpio_set_level(pin, nivel);                     /* true = 1 (3,3 V), false = 0 (0 V) */
}

bool salida_get(gpio_num_t pin)
{
    return gpio_get_level(pin);                     /* funciona por el modo INPUT_OUTPUT */
}

void entrada_init(gpio_num_t pin, gpio_pull_mode_t pull)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,            /* solo entrada */
        .pull_up_en   = GPIO_PULLUP_DISABLE,        /* el pull se pone abajo, con gpio_set_pull_mode */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,          /* la interrupcion se activa aparte (entrada_interrupcion) */
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_pull_mode(pin, pull)); /* GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY o GPIO_FLOATING */
}

bool entrada_get(gpio_num_t pin)
{
    return gpio_get_level(pin);                     /* 1 o 0 segun la tension del pin */
}

void entrada_interrupcion(gpio_num_t pin, gpio_int_type_t flanco, gpio_isr_t isr, void *arg)
{
    /* El servicio de ISR de GPIO se instala una sola vez para todos los pines;
     * si ya estaba instalado devuelve ESP_ERR_INVALID_STATE, y esta bien. */
    esp_err_t err = gpio_install_isr_service(0);    /* 0 = flags por defecto */
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);                       /* cualquier otro error si es grave */
    }
    ESP_ERROR_CHECK(gpio_set_intr_type(pin, flanco));       /* NEGEDGE, POSEDGE, ANYEDGE... */
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, isr, arg));   /* al ocurrir el flanco llama a isr(arg) */
}
