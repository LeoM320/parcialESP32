/**
 * @file memoria.h
 * @brief Guardado de valores en flash (NVS): sobreviven a reinicios y a reprogramar.
 *
 * - Cada valor se identifica con una clave de hasta 15 caracteres.
 * - Todo se guarda en un mismo espacio de nombres ("config").
 * - NVS es seguro entre tareas, pero escribir tarda unos ms (borra/escribe flash):
 *   no usar desde una ISR.
 * - Se borra solo con "idf.py erase-flash" (un "idf.py flash" normal no lo toca).
 */

#ifndef MEMORIA_H_
#define MEMORIA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Inicializa NVS. Llamar una vez, antes que todo (wifi_init() tambien lo necesita).
 *
 * Si la particion esta llena o es de otra version, la borra y la reinicia.
 * Aborta si falla.
 */
void memoria_init(void);

/**
 * @brief Lee un texto guardado.
 * @param clave  Nombre del valor.
 * @param dest   Donde copiarlo (con '\0'). Si no existe, no se modifica.
 * @param tamano Tamanio de dest.
 * @return true si existia y entro en dest.
 */
bool memoria_get_str(const char *clave, char *dest, size_t tamano);

/**
 * @brief Guarda un texto (lo reemplaza si ya existia).
 * @param clave Nombre del valor.
 * @param valor Texto a guardar.
 * @return true si se guardo.
 */
bool memoria_set_str(const char *clave, const char *valor);

/**
 * @brief Lee un numero de 16 bits guardado.
 * @param clave Nombre del valor.
 * @param valor Donde copiarlo. Si no existe, no se modifica.
 * @return true si existia.
 */
bool memoria_get_u16(const char *clave, uint16_t *valor);

/**
 * @brief Guarda un numero de 16 bits (lo reemplaza si ya existia).
 * @param clave Nombre del valor.
 * @param valor Numero a guardar.
 * @return true si se guardo.
 */
bool memoria_set_u16(const char *clave, uint16_t valor);

/**
 * @brief Lee un entero de 32 bits con signo guardado (int, negativos incluidos).
 * @param clave Nombre del valor.
 * @param valor Donde copiarlo. Si no existe, no se modifica.
 * @return true si existia.
 */
bool memoria_get_i32(const char *clave, int32_t *valor);

/**
 * @brief Guarda un entero de 32 bits con signo (lo reemplaza si ya existia).
 * @return true si se guardo.
 */
bool memoria_set_i32(const char *clave, int32_t valor);

/**
 * @brief Lee bytes guardados con memoria_set_blob(): un float, un struct, un arreglo...
 *
 * Solo copia si lo guardado mide exactamente tamano bytes (si cambiaste el struct,
 * lo viejo no se usa y dest queda como estaba).
 * @code
 *   config_t cfg = {30, 80};                         // valores por defecto
 *   memoria_get_blob("cfg", &cfg, sizeof(cfg));      // si hay algo guardado, lo pisa
 * @endcode
 * @param clave  Nombre del valor.
 * @param dest   Donde copiarlo. Si no existe o mide distinto, no se modifica.
 * @param tamano sizeof de lo que se lee.
 * @return true si existia y media tamano.
 */
bool memoria_get_blob(const char *clave, void *dest, size_t tamano);

/**
 * @brief Guarda bytes tal cual: memoria_set_blob("cfg", &cfg, sizeof(cfg)).
 * @return true si se guardo.
 */
bool memoria_set_blob(const char *clave, const void *datos, size_t tamano);

/**
 * @brief Borra una clave (la proxima lectura devuelve false y usa el valor por defecto).
 * @return true si existia y se borro.
 */
bool memoria_borrar(const char *clave);

#endif /* MEMORIA_H_ */
