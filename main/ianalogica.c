/**
 * @file ianalogica.c
 * @brief Implementacion de ianalogica.h con el driver adc_oneshot de ESP-IDF.
 *
 * El handle de ADC1 vive aca para que exista una unica vez en todo el programa
 * (adc_oneshot_new_unit falla si se llama dos veces para la misma unidad).
 */

#include "ianalogica.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc1 = NULL;     ///< Handle de la unidad ADC1.

/**
 * @brief Configura el pin como entrada analogica de 12 bits con atenuacion de 12 dB.
 * @param pin GPIO de ADC1 a configurar.
 */
void analogica_init(gpio_num_t pin)
{
    adc_unit_t unidad;
    adc_channel_t canal;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(pin, &unidad, &canal));
    ESP_ERROR_CHECK(unidad == ADC_UNIT_1 ? ESP_OK : ESP_ERR_INVALID_ARG);
    if (s_adc1 == NULL) {
        adc_oneshot_unit_init_cfg_t unidad_cfg = {
            .unit_id  = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unidad_cfg, &s_adc1));
    }
    adc_oneshot_chan_cfg_t canal_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, canal, &canal_cfg));
}

/**
 * @brief Realiza una conversion en el pin indicado.
 * @param pin GPIO previamente configurado con analogica_init().
 * @return Valor 0..4095, o -1 si falla la lectura.
 */
int analogica_get(gpio_num_t pin)
{
    adc_unit_t unidad;
    adc_channel_t canal;
    int valor;
    if (adc_oneshot_io_to_channel(pin, &unidad, &canal) != ESP_OK) {
        return -1;
    }
    if (adc_oneshot_read(s_adc1, canal, &valor) != ESP_OK) {
        return -1;
    }
    return valor;
}
