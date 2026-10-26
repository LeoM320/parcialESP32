/**
 * @file tcp.h
 * @brief Servidor y cliente TCP minimos sobre sockets de lwIP.
 *
 * - Servidor: tcp_servidor() + tcp_aceptar() (el ESP espera que se conecten).
 * - Cliente:  tcp_conectar() (el ESP se conecta a otro equipo).
 * - En los dos casos se habla con tcp_recibir() y tcp_enviar().
 * - Las funciones BLOQUEAN la tarea que las llama (no la CPU), como mucho el
 *   timeout dado: usarlas desde una tarea propia, nunca desde
 *   un handler HTTP, de eventos o una ISR.
 * - Los sockets se cierran con close() (<unistd.h>).
 */

#ifndef TCP_H_
#define TCP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Abre un puerto y lo deja escuchando conexiones.
 *
 * Escucha en todas las interfaces (AP y STA). Llamar despues de wifi_init().
 * @param puerto     Puerto a escuchar.
 * @param timeout_ms Espera maxima de tcp_aceptar() y tcp_recibir(); al vencer
 *                   devuelven sin datos y la tarea puede revisar otras cosas.
 * @return Socket servidor (>= 0), o -1 si no se pudo abrir el puerto.
 */
int tcp_servidor(uint16_t puerto, uint32_t timeout_ms);

/**
 * @brief Espera que se conecte un cliente.
 *
 * Bloquea hasta timeout_ms. Imprime la IP y el puerto del cliente.
 * Activa keepalive: si el cliente desaparece sin cerrar la conexion, en ~11 s
 * tcp_recibir() devuelve -1.
 * @param servidor Socket devuelto por tcp_servidor().
 * @return Socket del cliente (>= 0), o -1 si vencio el timeout o hubo error.
 */
int tcp_aceptar(int servidor);

/**
 * @brief Se conecta como cliente a un servidor TCP.
 *
 * Bloquea hasta 3 s mientras intenta conectar. Activa keepalive igual
 * que tcp_aceptar().
 * @param ip         IP del servidor en texto ("192.168.1.50").
 * @param puerto     Puerto del servidor.
 * @param timeout_ms Espera maxima de tcp_recibir() con este socket.
 * @return Socket conectado (>= 0), o -1 si la IP no es valida, no contesto o rechazo.
 */
int tcp_conectar(const char *ip, uint16_t puerto, uint32_t timeout_ms);

/**
 * @brief Recibe lo que haya llegado del cliente.
 *
 * Bloquea hasta timeout_ms. TCP es un flujo de bytes: un mensaje puede llegar
 * partido o junto con otro.
 * @param sock    Socket devuelto por tcp_aceptar() o tcp_conectar().
 * @param buf     Donde guardar los bytes (no agrega '\0').
 * @param tamano  Tamanio de buf.
 * @return Bytes recibidos (> 0); 0 si vencio el timeout sin datos;
 *         -1 si el cliente se desconecto o hubo error (cerrar el socket).
 */
int tcp_recibir(int sock, void *buf, size_t tamano);

/**
 * @brief Envia todos los bytes por un socket conectado.
 *
 * send() puede mandar menos bytes de los pedidos: reintenta hasta mandar todo.
 * @param sock  Socket devuelto por tcp_aceptar() o tcp_conectar().
 * @param datos Bytes a enviar.
 * @param largo Cantidad de bytes.
 * @return true si se envio todo; false si se corto la conexion (cerrar el socket).
 */
bool tcp_enviar(int sock, const void *datos, size_t largo);

#endif /* TCP_H_ */
