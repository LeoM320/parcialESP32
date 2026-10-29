/**
 * @file serie.h
 * @brief Puerto serie (UART) minimo: levantar, recibir y enviar bytes.
 *
 * Misma forma que tcp.h:
 *   - serie_init()    = levantar el puerto (como tcp_servidor + tcp_aceptar).
 *   - serie_recibir() = lo que haya llegado, con timeout (como tcp_recibir).
 *   - serie_enviar()  = mandar bytes (como tcp_enviar).
 * La diferencia: no hay conexion. El cable esta siempre "conectado", asi que
 * serie_recibir() nunca devuelve "se desconecto".
 *
 * - UART_NUM_0 es la del USB (la del monitor, COM6). Si la usas, lo que mandas
 *   se mezcla con los printf y los ESP_LOG.
 * - UART_NUM_1 con pines libres (ej. TX 17, RX 18) y un adaptador USB-serie
 *   aparte queda limpia para tu protocolo.
 * - Las funciones BLOQUEAN la tarea que las llama (no la CPU): usarlas desde
 *   una tarea, nunca desde una ISR.
 */

#ifndef SERIE_H_
#define SERIE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/uart.h"

#define SERIE_BUF_RX    512     ///< Buffer circular del driver para lo recibido (> 128, el FIFO).

/**
 * @brief Levanta un puerto serie: 8 bits de datos, sin paridad, 1 bit de stop (8N1).
 *
 * Aborta si falla (puerto o pines invalidos, o ya estaba instalado).
 * @param uart       UART_NUM_0, UART_NUM_1 o UART_NUM_2.
 * @param tx         Pin de TX, o UART_PIN_NO_CHANGE para dejar el de fabrica.
 * @param rx         Pin de RX, o UART_PIN_NO_CHANGE para dejar el de fabrica.
 * @param baudios    Velocidad, ej. 115200 (la misma del otro lado).
 * @param timeout_ms Espera maxima de serie_recibir() sin datos.
 */
void serie_init(uart_port_t uart, int tx, int rx, uint32_t baudios, uint32_t timeout_ms);

/**
 * @brief Recibe lo que haya llegado.
 *
 * Espera hasta timeout_ms el primer byte y despues se lleva todo lo que ya
 * este en el buffer (sin esperar mas). Como en TCP, un mensaje puede llegar
 * partido o junto con otro.
 * @param uart   Puerto levantado con serie_init().
 * @param buf    Donde guardar los bytes (no agrega '\0').
 * @param tamano Tamanio de buf.
 * @return Bytes recibidos (> 0); 0 si vencio el timeout sin datos; -1 si hubo error.
 */
int serie_recibir(uart_port_t uart, void *buf, size_t tamano);

/**
 * @brief Envia bytes por el puerto serie.
 *
 * Bloquea hasta que todos los bytes entraron al FIFO de salida.
 * @param uart  Puerto levantado con serie_init().
 * @param datos Bytes a enviar.
 * @param largo Cantidad de bytes.
 * @return true si se enviaron todos; false si hubo error.
 */
bool serie_enviar(uart_port_t uart, const void *datos, size_t largo);

#endif /* SERIE_H_ */
