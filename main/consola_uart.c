/**
 * @file consola_uart.c
 * @brief Implementacion de consola_uart.h.
 *
 * Una tarea: espera bytes de la UART y se los pasa a la consola.
 */

#include "consola_uart.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "consola.h"

#define RX_BUF      256     /* buffer del driver (tiene que ser > 128, la FIFO) */
#define RX_MAX      64

static uart_port_t s_uart;
static consola_t *s_consola;

/* Salida de la consola: a la UART */
static void enviar(const char *texto)
{
    uart_write_bytes(s_uart, texto, strlen(texto));
}

static void tarea(void *arg)
{
    enviar("\r\nConsola ESP32 (UART)\r\n");
    consola_inicio(s_consola);

    char buf[RX_MAX];
    for (;;) {
        /* Espera el primer byte sin limite; despues junta lo que siga llegando
         * durante 10 ms, para no llamar a la consola byte por byte. */
        int n = uart_read_bytes(s_uart, buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (n > 0) {
            consola_recibir(s_consola, buf, n);
        }
    }
}

void consola_uart_init(uart_port_t uart, int baudios, int tx, int rx,
                       int (*ejecutar)(int argc, const char *const *argv))
{
    s_uart = uart;
    uart_config_t cfg = {
        .baud_rate  = baudios,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(uart, RX_BUF, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(uart, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_consola = consola_crear(ejecutar, enviar);
    xTaskCreate(tarea, "consola_uart", 4096, NULL, 5, NULL);
}
