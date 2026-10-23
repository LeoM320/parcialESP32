/**
 * @file json_mini.h
 * @brief Lectura minima de JSON: sacar un campo de un objeto plano.
 *
 * Alcanza para lo que mandan la web y la app, ej. {"id":"led1","valor":"toggle"}.
 * No es un parser completo, a proposito:
 * - Solo objetos planos (sin objetos ni listas adentro).
 * - Escapes soportados en textos: \" \\ \/ (no \n, \uXXXX, etc.).
 * Para armar JSON alcanza con snprintf (ver variables.c).
 */

#ifndef JSON_MINI_H_
#define JSON_MINI_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Copia el valor de un campo como texto.
 *
 * Textos sin comillas ("led1" -> led1); numeros y true/false/null tal cual
 * (1 -> "1", 50.5 -> "50.5").
 * @param json   Texto JSON, ej. el cuerpo de un POST.
 * @param clave  Nombre del campo.
 * @param dest   Donde copiar el valor (con '\0').
 * @param tamano Tamanio de dest.
 * @return true si el campo existe y entra en dest.
 */
bool json_valor(const char *json, const char *clave, char *dest, size_t tamano);

#endif /* JSON_MINI_H_ */
