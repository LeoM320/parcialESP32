/**
 * @file rgb.c
 * @brief Implementacion de rgb.h con el componente led_strip (periferico RMT).
 *
 * led_strip_set_pixel() solo guarda el color en memoria; recien
 * led_strip_refresh() lo manda al LED. Por eso rgb_set() llama a las dos.
 */

#include "rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"        /* mutex */
#include "led_strip.h"              /* componente espressif/led_strip */

static led_strip_handle_t s_led = NULL;     /* el LED (NULL = todavia no se configuro) */
static SemaphoreHandle_t s_mutex = NULL;    /* un rgb_set() a la vez */

void rgb_init(gpio_num_t pin)
{
    led_strip_config_t led_cfg = {
        .strip_gpio_num = pin,
        .max_leds       = 1,                        /* la placa tiene uno solo */
        .led_model      = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,   /* el WS2812 recibe verde, rojo, azul */
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,          /* RMT a 10 MHz: pulsos de 0,1 us */
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &s_led));

    s_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);     /* NULL = no hubo memoria */

    ESP_ERROR_CHECK(led_strip_clear(s_led));        /* arranca apagado */
}

void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    led_strip_set_pixel(s_led, 0, r, g, b);         /* LED 0 (el unico): guarda el color */
    led_strip_refresh(s_led);                       /* lo manda (~30 us) */
    xSemaphoreGive(s_mutex);
}
