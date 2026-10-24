/**
 * @file consola.h
 * @brief Linea de comandos (microrl) sobre cualquier transporte: TCP, UART, etc.
 *
 * Una sola consola: el transporte le pasa los bytes que llegan con
 * consola_recibir() y la consola contesta con la funcion enviar.
 * Dentro de un comando, consola_printf() responde por ese mismo transporte.
 *
 * No tiene mutex: llamar a consola_inicio() y consola_recibir() siempre desde
 * la misma tarea (la del transporte).
 */

#ifndef CONSOLA_H_
#define CONSOLA_H_

#include <stddef.h>

/**
 * @brief Prepara la consola. Llamar una vez, antes que el resto.
 * @param ejecutar Recibe la linea separada en palabras: argv[0] es el comando,
 *                 argc la cantidad de palabras (>= 1). Su retorno se ignora.
 * @param enviar   Funcion que manda texto por el transporte (ej. por el socket).
 */
void consola_init(int (*ejecutar)(int argc, const char *const *argv),
                  void (*enviar)(const char *texto));

/**
 * @brief Empieza una sesion nueva: borra la linea y el historial y muestra el prompt.
 *
 * Llamar cuando se conecta alguien (ej. un cliente TCP).
 */
void consola_inicio(void);

/**
 * @brief Pasa a la consola los bytes recibidos por el transporte.
 *
 * Hace el eco, la edicion de la linea y, al llegar Enter (\r, \n o \r\n),
 * ejecuta el comando.
 * @param datos Bytes recibidos.
 * @param largo Cantidad de bytes.
 */
void consola_recibir(const char *datos, size_t largo);

/**
 * @brief printf hacia la consola. Usar dentro de la funcion de comandos.
 *
 * Para terminar una linea usar "\r\n" (las terminales lo esperan asi).
 */
void consola_printf(const char *formato, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Como consola_printf() pero sin formato y sin limite de largo.
 *
 * Para textos armados aparte que pueden ser largos (ej. la ayuda).
 */
void consola_escribir(const char *texto);

#endif /* CONSOLA_H_ */
