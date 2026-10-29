/**
 * @file rgb.c
 * @brief Implementacion de rgb.h con el componente led_strip (periferico RMT).
 */

#include "rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"

static led_strip_handle_t s_led = NULL;     ///< Handle del LED WS2812.
static SemaphoreHandle_t s_mutex = NULL;    ///< Mutex para serializar el acceso a rgb_set().

/**
 * @brief Configura el LED RGB WS2812 en el pin indicado mediante el periferico RMT y lo apaga.
 * @param pin GPIO del WS2812.
 */
void rgb_init(gpio_num_t pin)
{
    led_strip_config_t led_cfg = {
        .strip_gpio_num         = pin,
        .max_leds               = 1,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &s_led));
    s_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(led_strip_clear(s_led));
}

/**
 * @brief Establece el color del LED RGB integrado.
 * @param r Intensidad de rojo (0..255).
 * @param g Intensidad de verde (0..255).
 * @param b Intensidad de azul (0..255).
 */
void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    led_strip_set_pixel(s_led, 0, r, g, b);
    led_strip_refresh(s_led);
    xSemaphoreGive(s_mutex);
}
