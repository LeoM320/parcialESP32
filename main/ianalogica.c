/**
 * @file ianalogica.c
 * @brief Implementacion de ianalogica.h con el driver adc_oneshot de ESP-IDF.
 *
 * El handle de ADC1 vive aca para que exista una unica vez en todo el programa
 * (adc_oneshot_new_unit falla si se llama dos veces para la misma unidad).
 *
 * "oneshot" = una conversion cada vez que se pide (no continuo por DMA).
 */

#include "ianalogica.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"          /* xTaskDelayUntil, xTaskGetTickCount */
#include "freertos/semphr.h"        /* mutex */
#include "esp_adc/adc_oneshot.h"    /* driver del ADC */

/* static en una variable global: solo se ve dentro de este archivo.
 * La s_ es convencion para "static". */
static adc_oneshot_unit_handle_t s_adc1 = NULL;     /* la unidad ADC1 (NULL = todavia no se creo) */
static SemaphoreHandle_t s_mutex = NULL;    /* un analogica_get() a la vez */

void analogica_init(gpio_num_t pin)
{
    /* El driver trabaja con unidad + canal, no con GPIO: se traduce */
    adc_unit_t unidad;
    adc_channel_t canal;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(pin, &unidad, &canal));
    ESP_ERROR_CHECK(unidad == ADC_UNIT_1 ? ESP_OK : ESP_ERR_INVALID_ARG);  /* solo ADC1 (el ADC2 lo usa el WiFi) */

    /* La primera vez que se llama: crear la unidad y el mutex */
    if (s_adc1 == NULL) {
        adc_oneshot_unit_init_cfg_t unidad_cfg = {
            .unit_id  = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,       /* sin el coprocesador de bajo consumo */
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unidad_cfg, &s_adc1));

        s_mutex = xSemaphoreCreateMutex();
        ESP_ERROR_CHECK(s_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);     /* NULL = no hubo memoria */
    }

    /* Configurar el canal de este pin */
    adc_oneshot_chan_cfg_t canal_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,   /* 12 bits en ESP32-S3: 0..4095 */
        .atten    = ADC_ATTEN_DB_12,        /* atenuacion maxima: mide de 0 a ~3,1 V */
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, canal, &canal_cfg));
}

int analogica_get_crudo(gpio_num_t pin)
{
    adc_unit_t unidad;
    adc_channel_t canal;
    int crudo;

    if (adc_oneshot_io_to_channel(pin, &unidad, &canal) != ESP_OK) {
        return -1;
    }

    /* Tambien con mutex: si otra tarea esta leyendo, adc_oneshot_read no espera,
     * falla con ESP_ERR_TIMEOUT (medido: ~5 de cada 1000 lecturas sin mutex) */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = adc_oneshot_read(s_adc1, canal, &crudo);
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK) {
        return -1;
    }
    return crudo;                           /* 0..4095, sin filtrar */
}

int analogica_get(gpio_num_t pin)
{
    adc_unit_t unidad;
    adc_channel_t canal;
    if (adc_oneshot_io_to_channel(pin, &unidad, &canal) != ESP_OK) {
        return -1;
    }

    /* Si dos tareas piden a la vez, la segunda espera aca a que termine la primera */
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* xTaskDelayUntil despierta siempre justo despues de un tick, asi las
     * muestras quedan separadas exactamente ANALOGICA_INTERVALO_MS. Separarlas
     * hace que un pico (el WiFi transmitiendo) caiga en una sola muestra. */
    TickType_t t = xTaskGetTickCount();     /* tick actual: punto de partida */
    int muestras[ANALOGICA_MUESTRAS];
    int resultado = 0;                      /* 0 = todo bien hasta ahora, -1 = error */

    for (int i = 0; i < ANALOGICA_MUESTRAS; i++) {
        xTaskDelayUntil(&t, pdMS_TO_TICKS(ANALOGICA_INTERVALO_MS));   /* bloquea, CPU libre */

        if (adc_oneshot_read(s_adc1, canal, &muestras[i]) != ESP_OK) {
            resultado = -1;
            break;                          /* sale del for (igual devuelve el mutex abajo) */
        }
    }

    xSemaphoreGive(s_mutex);                /* siempre devolver el mutex, haya fallado o no */

    if (resultado == 0) {
        /* Ordenar de menor a mayor (insercion: cada muestra se corre a la
         * izquierda hasta quedar en su lugar) */
        for (int i = 1; i < ANALOGICA_MUESTRAS; i++) {
            int x = muestras[i];
            int j = i - 1;
            while (j >= 0 && muestras[j] > x) {
                muestras[j + 1] = muestras[j];
                j--;
            }
            muestras[j + 1] = x;
        }

        /* Promedio recortado: se descartan la menor y la mayor (ahi caen los
         * picos) y se promedian las del medio */
        int suma = 0;
        for (int i = 1; i < ANALOGICA_MUESTRAS - 1; i++) {
            suma += muestras[i];
        }
        int n = ANALOGICA_MUESTRAS - 2;
        resultado = (suma + n / 2) / n;     /* sumar la mitad del divisor redondea en vez de truncar */
    }
    return resultado;
}
