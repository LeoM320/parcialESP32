/**
 * @file serie.c
 * @brief Implementacion de serie.h con el driver UART de ESP-IDF.
 */

#include "serie.h"
#include "freertos/FreeRTOS.h"

static TickType_t s_timeout[UART_NUM_MAX];  ///< Timeout de serie_recibir(), en ticks, por puerto.

/**
 * @brief Levanta un puerto serie en formato 8N1 sin control de flujo.
 * @param uart       UART_NUM_0, UART_NUM_1 o UART_NUM_2.
 * @param tx         Pin de TX, o UART_PIN_NO_CHANGE para dejar el de fabrica.
 * @param rx         Pin de RX, o UART_PIN_NO_CHANGE para dejar el de fabrica.
 * @param baudios    Velocidad en bps (ej. 115200).
 * @param timeout_ms Espera maxima de serie_recibir() sin datos.
 */
void serie_init(uart_port_t uart, int tx, int rx, uint32_t baudios, uint32_t timeout_ms)
{
    uart_config_t cfg = {
        .baud_rate  = baudios,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(uart, SERIE_BUF_RX, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(uart, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    s_timeout[uart] = pdMS_TO_TICKS(timeout_ms);
}

/**
 * @brief Recibe los bytes disponibles esperando hasta el timeout por el primer byte.
 * @param uart   Puerto levantado con serie_init().
 * @param buf    Buffer destino.
 * @param tamano Tamanio de buf.
 * @return Bytes recibidos (> 0); 0 si vencio el timeout; -1 si hubo error.
 */
int serie_recibir(uart_port_t uart, void *buf, size_t tamano)
{
    uint8_t *p = buf;
    if (tamano == 0) {
        return 0;
    }
    int n = uart_read_bytes(uart, p, 1, s_timeout[uart]);
    if (n <= 0) {
        return n;
    }
    int resto = uart_read_bytes(uart, p + 1, tamano - 1, 0);
    if (resto > 0) {
        n += resto;
    }
    return n;
}

/**
 * @brief Envia bytes por el puerto serie bloqueando hasta cargar el FIFO de salida.
 * @param uart  Puerto levantado con serie_init().
 * @param datos Bytes a enviar.
 * @param largo Cantidad de bytes.
 * @return true si se enviaron todos; false si hubo error.
 */
bool serie_enviar(uart_port_t uart, const void *datos, size_t largo)
{
    int n = uart_write_bytes(uart, datos, largo);
    return n == (int)largo;
}
