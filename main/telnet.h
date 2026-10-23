/**
 * @file telnet.h
 * @brief Lo minimo de Telnet para usar una consola con MobaXterm / PuTTY (modo Telnet).
 *
 * Un cliente Telnet mezcla con el texto comandos de control que empiezan con el
 * byte IAC (255). Esta libreria:
 * - le pide al cliente que mande cada tecla al instante y que no haga eco propio
 *   (el eco y la edicion de la linea los hace microrl);
 * - saca esos comandos de los datos recibidos, para que no lleguen a la consola.
 */

#ifndef TELNET_H_
#define TELNET_H_

#include <stddef.h>
#include <stdint.h>

/** Estado del filtro: un comando puede llegar partido entre dos recv(). */
typedef struct {
    uint8_t estado;
} telnet_t;

/**
 * @brief Empieza una sesion: reinicia el filtro y negocia con el cliente.
 *
 * Llamar apenas se conecta el cliente, antes de mandarle texto.
 * @param t    Estado del filtro de esta conexion.
 * @param sock Socket del cliente.
 */
void telnet_inicio(telnet_t *t, int sock);

/**
 * @brief Saca los comandos Telnet de lo recibido; deja solo el texto.
 * @param t     Estado del filtro de esta conexion.
 * @param buf   Bytes recibidos; se modifica en el lugar.
 * @param largo Cantidad de bytes recibidos.
 * @return Bytes de texto que quedaron al principio de buf (puede ser 0).
 */
size_t telnet_filtrar(telnet_t *t, char *buf, size_t largo);

#endif /* TELNET_H_ */
