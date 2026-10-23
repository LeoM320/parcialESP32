/**
 * @file consola.h
 * @brief Linea de comandos (microrl) sobre cualquier transporte: TCP, UART, etc.
 *
 * - Cada conexion crea su consola con consola_crear(), pasando como enviar texto
 *   (ej. una por cada cliente TCP y otra para la UART): cada una tiene su propia
 *   linea e historial.
 * - Todas comparten la misma funcion de comandos (consola_init()).
 * - Los comandos se ejecutan de a uno (mutex): aunque lleguen por varias consolas a
 *   la vez, nunca corren dos al mismo tiempo.
 * - Dentro de un comando, consola_printf() responde por la consola que lo recibio.
 */

#ifndef CONSOLA_H_
#define CONSOLA_H_

#include <stddef.h>

#define CONSOLA_MAX     4       /**< Consolas que se pueden crear (ej. 3 clientes TCP + UART) */

/** Consola creada con consola_crear() (su contenido es interno). */
typedef struct consola consola_t;

/**
 * @brief Registra la funcion que ejecuta los comandos. Llamar una vez, antes que el resto.
 * @param ejecutar Recibe la linea separada en palabras: argv[0] es el comando,
 *                 argc la cantidad de palabras (>= 1). Su retorno se ignora.
 */
void consola_init(int (*ejecutar)(int argc, const char *const *argv));

/**
 * @brief Crea una consola para una conexion.
 * @param enviar Funcion que manda texto por esa conexion (ej. por un socket TCP).
 *               Recibe ctx tal cual. Solo se llama desde consola_recibir() / consola_inicio().
 * @param ctx    Dato propio de la conexion para enviar (ej. el cliente TCP), o NULL.
 * @return La consola. Aborta si ya se crearon CONSOLA_MAX.
 */
consola_t *consola_crear(void (*enviar)(void *ctx, const char *texto), void *ctx);

/**
 * @brief Empieza una sesion nueva: borra la linea y el historial y muestra el prompt.
 *
 * Llamar cuando se conecta alguien (ej. un cliente TCP).
 * @param c Consola.
 */
void consola_inicio(consola_t *c);

/**
 * @brief Pasa a la consola los bytes recibidos por su transporte.
 *
 * Hace el eco, la edicion de la linea y, al llegar Enter (\r, \n o \r\n),
 * ejecuta el comando. Bloquea mientras otra consola ejecuta un comando.
 * @param c     Consola.
 * @param datos Bytes recibidos.
 * @param largo Cantidad de bytes.
 */
void consola_recibir(consola_t *c, const char *datos, size_t largo);

/**
 * @brief printf hacia la consola que esta ejecutando el comando.
 *
 * Solo dentro de la funcion de comandos; fuera de ella no hace nada.
 * Para terminar una linea usar "\r\n" (las terminales lo esperan asi).
 */
void consola_printf(const char *formato, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Como consola_printf() pero sin formato y sin limite de largo.
 *
 * Para textos armados aparte que pueden ser largos (ej. un JSON).
 */
void consola_escribir(const char *texto);

#endif /* CONSOLA_H_ */
