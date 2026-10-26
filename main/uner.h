/**
 * @file uner.h
 * @brief Protocolo UNER: arma y decodifica tramas, sin importar el medio
 *        (TCP, UART, ...).
 *
 * Trama:
 * @code
 *   'U' 'N' 'E' 'R' | LEN | ':' | CARGA (n bytes) | CHK
 *   LEN = n + 1        (la carga mas el checksum)
 *   CHK = XOR de todos los bytes anteriores ('U' ^ 'N' ^ ... ^ ultimo de la carga)
 * @endcode
 * - CARGA[0] es el ID del comando; despues van sus datos.
 * - Los valores de mas de un byte van en little-endian (byte bajo primero).
 * - TCP es un flujo de bytes: una trama puede llegar partida o junto con otra.
 *   Por eso se decodifica de a un byte con uner_rx_byte().
 */

#ifndef UNER_H_
#define UNER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UNER_MAX_CARGA  64                      /**< Carga maxima (ID + datos) */
#define UNER_MAX_TRAMA  (UNER_MAX_CARGA + 7)    /**< Carga + 'UNER' + LEN + ':' + CHK */

/** @brief Estado del receptor. Uno por cada conexion. */
typedef struct {
    uint8_t estado;                 /**< Paso de la maquina de estados */
    uint8_t esperados;              /**< Bytes de carga que faltan leer (LEN - 1) */
    uint8_t chk;                    /**< XOR acumulado */
    uint8_t n;                      /**< Bytes de carga recibidos */
    uint8_t carga[UNER_MAX_CARGA];  /**< Carga de la ultima trama valida */
} uner_rx_t;

/**
 * @brief Arma una trama completa a partir de la carga.
 * @param trama Donde dejar la trama (al menos n + 7 bytes, UNER_MAX_TRAMA alcanza).
 * @param carga ID del comando y sus datos.
 * @param n     Bytes de carga (hasta UNER_MAX_CARGA).
 * @return Largo de la trama, listo para tcp_enviar(); 0 si la carga es muy larga.
 */
size_t uner_armar(uint8_t *trama, const uint8_t *carga, uint8_t n);

/**
 * @brief Deja el receptor esperando el inicio de una trama.
 *
 * Llamar al crear el receptor y cada vez que se abre una conexion nueva.
 */
void uner_rx_init(uner_rx_t *rx);

/**
 * @brief Procesa un byte recibido.
 *
 * Si el byte no encaja se descarta lo que venia y se busca otra 'U'.
 * @return true si con este byte se completo una trama con checksum correcto:
 *         la carga queda en rx->carga (rx->n bytes) hasta el proximo byte.
 */
bool uner_rx_byte(uner_rx_t *rx, uint8_t byte);

/**
 * @name Valores de mas de un byte dentro de la carga (little-endian)
 * @code
 *   uint8_t carga[7];
 *   carga[0] = 0xA2;                     // comando
 *   uner_escribir16(&carga[1], 1500);    // bytes 1 y 2
 *   uner_escribir_float(&carga[3], 2.5f);// bytes 3..6
 *   ...
 *   uint16_t v = uner_leer16(&rx.carga[1]);
 * @endcode
 * Para valores con signo, convertir: (int16_t)uner_leer16(p), uner_escribir16(p, (uint16_t)x).
 * @{
 */
void uner_escribir16(uint8_t *p, uint16_t valor);       /**< 2 bytes, el bajo primero */
void uner_escribir32(uint8_t *p, uint32_t valor);       /**< 4 bytes, el bajo primero */
void uner_escribir_float(uint8_t *p, float valor);      /**< 4 bytes IEEE-754 (como el float de la PC) */
uint16_t uner_leer16(const uint8_t *p);
uint32_t uner_leer32(const uint8_t *p);
float uner_leer_float(const uint8_t *p);
/** @} */

#endif /* UNER_H_ */
