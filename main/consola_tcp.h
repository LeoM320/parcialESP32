/**
 * @file consola_tcp.h
 * @brief Consola (consola.h) por TCP con Telnet, para una terminal (MobaXterm, PuTTY).
 *
 * - Un cliente a la vez. Si se conecta otro, queda esperando hasta que el
 *   primero se vaya.
 * - Un cliente que desaparece sin cerrar se libera en ~11 s (keepalive, tcp.h).
 */

#ifndef CONSOLA_TCP_H_
#define CONSOLA_TCP_H_

#include <stdint.h>

/**
 * @brief Prepara la consola y arranca la tarea del servidor.
 *
 * Llamar despues de wifi_init(), una sola vez.
 * @param puerto   Puerto a escuchar.
 * @param ejecutar Funcion de comandos (ver consola_crear()).
 */
void consola_tcp_init(uint16_t puerto, int (*ejecutar)(int argc, const char *const *argv));

/**
 * @brief Cambia el puerto: corta al cliente y escucha en el nuevo.
 *
 * Se puede llamar desde cualquier tarea (ej. un handler HTTP); no bloquea.
 * Se aplica en menos de 1 s.
 * @param puerto Puerto nuevo.
 */
void consola_tcp_puerto(uint16_t puerto);

#endif /* CONSOLA_TCP_H_ */
