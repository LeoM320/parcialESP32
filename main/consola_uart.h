/**
 * @file consola_uart.h
 * @brief Consola (consola.h) por UART, para una terminal serie (idf.py monitor,
 *        MobaXterm Serial, PuTTY).
 *
 * Con UART0 se usa el mismo puerto COM que el monitor; ahi tambien salen los
 * logs del sistema (ESP_LOG, printf), que se mezclan con la consola: si tapan
 * el prompt, apretar Enter.
 */

#ifndef CONSOLA_UART_H_
#define CONSOLA_UART_H_

#include "driver/uart.h"

/**
 * @brief Instala el driver de la UART, crea la consola y arranca su tarea.
 *
 * Llamar una sola vez.
 * @param uart     UART a usar (ej. UART_NUM_0, la del monitor).
 * @param baudios  Velocidad (ej. 115200).
 * @param tx       Pin TX, o UART_PIN_NO_CHANGE para el de siempre de esa UART.
 * @param rx       Pin RX, o UART_PIN_NO_CHANGE.
 * @param ejecutar Funcion de comandos (ver consola_crear()).
 */
void consola_uart_init(uart_port_t uart, int baudios, int tx, int rx,
                       int (*ejecutar)(int argc, const char *const *argv));

#endif /* CONSOLA_UART_H_ */
