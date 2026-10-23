/**
 * @file ianalogica.c
 * @brief Implementacion de ianalogica.h con el driver adc_oneshot de ESP-IDF.
 *
 * El handle de ADC1 vive aca para que exista una unica vez en todo el programa
 * (adc_oneshot_new_unit falla si se llama dos veces para la misma unidad).
 */

#include "ianalogica.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc1 = NULL;
static SemaphoreHandle_t s_mutex = NULL;    /* un analogica_get() a la vez */

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

        s_mutex = xSemaphoreCreateMutex();
        ESP_ERROR_CHECK(s_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    }

    adc_oneshot_chan_cfg_t canal_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,   /* 12 bits en ESP32-S3 */
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, canal, &canal_cfg));
}

int analogica_get_crudo(gpio_num_t pin)
{
    adc_unit_t unidad;
    adc_channel_t canal;
    int crudo;

    if (adc_oneshot_io_to_channel(pin, &unidad, &canal) != ESP_OK ||
        adc_oneshot_read(s_adc1, canal, &crudo) != ESP_OK) {
        return -1;
    }
    return crudo;
}

int analogica_get(gpio_num_t pin)
{
    adc_unit_t unidad;
    adc_channel_t canal;
    if (adc_oneshot_io_to_channel(pin, &unidad, &canal) != ESP_OK) {
        return -1;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* xTaskDelayUntil despierta siempre justo despues de un tick, asi las
     * muestras quedan separadas exactamente ANALOGICA_INTERVALO_MS. */
    TickType_t t = xTaskGetTickCount();
    int suma = 0;
    int resultado = 0;

    for (int i = 0; i < ANALOGICA_MUESTRAS; i++) {
        xTaskDelayUntil(&t, pdMS_TO_TICKS(ANALOGICA_INTERVALO_MS));   /* bloquea, CPU libre */

        int crudo;
        if (adc_oneshot_read(s_adc1, canal, &crudo) != ESP_OK) {
            resultado = -1;
            break;
        }
        suma += crudo;
    }

    xSemaphoreGive(s_mutex);

    if (resultado == 0) {
        resultado = (suma + ANALOGICA_MUESTRAS / 2) / ANALOGICA_MUESTRAS;   /* redondeo */
    }
    return resultado;
}
