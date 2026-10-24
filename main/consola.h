/**
 * @file consola.h
 * @brief Linea de comandos (microrl) sobre cualquier transporte: TCP, UART, etc.
 *
 * Cada transporte crea su consola con consola_crear(), le pasa los bytes que
 * llegan con consola_recibir() y la consola contesta con la funcion enviar.
 * Cada consola tiene su propia linea e historial, asi TCP y UART andan a la vez.
 *
 * Dentro de un comando, consola_printf() responde por la consola que lo
 * recibio. Los comandos se ejecutan de a uno (mutex): un comando que llega
 * por UART espera a que termine el que llego por TCP.
 */

#ifndef CONSOLA_H_
#define CONSOLA_H_

#include <stddef.h>

#define CONSOLA_MAX     2   /**< Consolas que se pueden crear (TCP + UART) */

/** @brief Una consola; se usa solo a traves de esta API. */
typedef struct consola consola_t;

/**
 * @brief Crea una consola. Llamar una vez por transporte.
 *
 * Aborta si ya se crearon CONSOLA_MAX.
 * @param ejecutar Recibe la linea separada en palabras: argv[0] es el comando,
 *                 argc la cantidad de palabras (>= 1). Su retorno se ignora.
 * @param enviar   Funcion que manda texto por el transporte (ej. por el socket).
 * @return La consola, para pasarla a las demas funciones.
 */
consola_t *consola_crear(int (*ejecutar)(int argc, const char *const *argv),
                         void (*enviar)(const char *texto));

/**
 * @brief Empieza una sesion nueva: borra la linea y el historial y muestra el prompt.
 *
 * Llamar cuando se conecta alguien (ej. un cliente TCP) o al arrancar (UART).
 * @param c La consola.
 */
void consola_inicio(consola_t *c);

/**
 * @brief Pasa a la consola los bytes recibidos por el transporte.
 *
 * Hace el eco, la edicion de la linea y, al llegar Enter (\r, \n o \r\n),
 * ejecuta el comando.
 * @param c     La consola.
 * @param datos Bytes recibidos.
 * @param largo Cantidad de bytes.
 */
void consola_recibir(consola_t *c, const char *datos, size_t largo);

/**
 * @brief printf hacia la consola que esta ejecutando el comando.
 *
 * Usar solo dentro de la funcion de comandos. Para terminar una linea usar
 * "\r\n" (las terminales lo esperan asi).
 */
void consola_printf(const char *formato, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Como consola_printf() pero sin formato y sin limite de largo.
 *
 * Para textos armados aparte que pueden ser largos (ej. la ayuda).
 */
void consola_escribir(const char *texto);

#endif /* CONSOLA_H_ */
