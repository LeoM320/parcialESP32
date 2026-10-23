/**
 * @file consola_tcp.h
 * @brief Consola (consola.h) por TCP con Telnet, para varios clientes a la vez.
 *
 * - Cada cliente (MobaXterm, la app Qt, ...) tiene su propia consola: su linea,
 *   su historial y sus respuestas. Lo que escribe uno no lo ve el otro.
 * - Los comandos de todos pasan por la misma funcion de comandos, de a uno.
 * - Si ya hay CONSOLA_TCP_CLIENTES conectados, al siguiente se le avisa que la
 *   consola esta llena y se lo desconecta.
 * - Un cliente que desaparece sin cerrar se libera en ~11 s (keepalive, tcp.h).
 */

#ifndef CONSOLA_TCP_H_
#define CONSOLA_TCP_H_

#include <stdint.h>

#define CONSOLA_TCP_CLIENTES    3   /**< Clientes a la vez (cada uno usa una consola y un socket) */

/**
 * @brief Crea las consolas de los clientes y arranca la tarea del servidor.
 *
 * Llamar despues de consola_init() y wifi_init(), una sola vez.
 * @param puerto Puerto a escuchar.
 */
void consola_tcp_init(uint16_t puerto);

/**
 * @brief Cambia el puerto: corta a todos los clientes y escucha en el nuevo.
 *
 * Se puede llamar desde cualquier tarea (ej. un handler HTTP); no bloquea.
 * Se aplica en menos de 1 s.
 * @param puerto Puerto nuevo.
 */
void consola_tcp_puerto(uint16_t puerto);

#endif /* CONSOLA_TCP_H_ */
