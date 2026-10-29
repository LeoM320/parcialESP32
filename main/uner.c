/**
 * @file uner.c
 * @brief Implementacion del protocolo UNER (armado y recepcion byte a byte).
 */

#include "uner.h"
#include <string.h>

/** @brief Estados de la maquina de recepcion UNER. */
enum {
    ESPERA_U,           ///< Buscando 'U'.
    ESPERA_N,           ///< Llego 'U', esperando 'N'.
    ESPERA_E,           ///< Llego "UN", esperando 'E'.
    ESPERA_R,           ///< Llego "UNE", esperando 'R'.
    ESPERA_LEN,         ///< Llego "UNER", esperando LEN.
    ESPERA_DOSPUNTOS,   ///< Llego LEN, esperando ':'.
    ESPERA_CARGA,       ///< Recibiendo bytes de carga.
    ESPERA_CHK          ///< Esperando byte de checksum XOR.
};

/**
 * @brief Arma una trama UNER completa a partir de la carga.
 * @param trama Buffer destino (al menos n + 7 bytes).
 * @param carga ID del comando seguido de sus datos.
 * @param n     Cantidad de bytes de carga (hasta UNER_MAX_CARGA).
 * @return Largo total de la trama (n + 7), o 0 si n supera UNER_MAX_CARGA.
 */
size_t uner_armar(uint8_t *trama, const uint8_t *carga, uint8_t n)
{
    if (n > UNER_MAX_CARGA) {
        return 0;
    }
    trama[0] = 'U';
    trama[1] = 'N';
    trama[2] = 'E';
    trama[3] = 'R';
    trama[4] = n + 1;
    trama[5] = ':';
    memcpy(&trama[6], carga, n);
    uint8_t chk = 0;
    for (int i = 0; i < 6 + n; i++) {
        chk ^= trama[i];
    }
    trama[6 + n] = chk;
    return 7 + n;
}

/**
 * @brief Inicializa la maquina de estados receptora UNER.
 * @param rx Puntero a la estructura receptora.
 */
void uner_rx_init(uner_rx_t *rx)
{
    rx->estado = ESPERA_U;
    rx->n = 0;
}

/**
 * @brief Procesa un byte entrante en la maquina de estados UNER.
 * @param rx   Estructura receptora.
 * @param byte Byte recibido.
 * @return true si se completo una trama valida con checksum correcto.
 */
bool uner_rx_byte(uner_rx_t *rx, uint8_t byte)
{
    switch (rx->estado) {
    case ESPERA_U:
        break;
    case ESPERA_N:
        if (byte == 'N') {
            rx->chk ^= byte;
            rx->estado = ESPERA_E;
            return false;
        }
        break;
    case ESPERA_E:
        if (byte == 'E') {
            rx->chk ^= byte;
            rx->estado = ESPERA_R;
            return false;
        }
        break;
    case ESPERA_R:
        if (byte == 'R') {
            rx->chk ^= byte;
            rx->estado = ESPERA_LEN;
            return false;
        }
        break;
    case ESPERA_LEN:
        if (byte >= 1 && byte <= UNER_MAX_CARGA + 1) {
            rx->chk ^= byte;
            rx->esperados = byte - 1;
            rx->estado = ESPERA_DOSPUNTOS;
            return false;
        }
        break;
    case ESPERA_DOSPUNTOS:
        if (byte == ':') {
            rx->chk ^= byte;
            rx->n = 0;
            rx->estado = (rx->esperados > 0) ? ESPERA_CARGA : ESPERA_CHK;
            return false;
        }
        break;
    case ESPERA_CARGA:
        rx->chk ^= byte;
        rx->carga[rx->n++] = byte;
        if (rx->n == rx->esperados) {
            rx->estado = ESPERA_CHK;
        }
        return false;
    case ESPERA_CHK:
        rx->estado = ESPERA_U;
        return byte == rx->chk;
    }
    if (byte == 'U') {
        rx->chk = byte;
        rx->estado = ESPERA_N;
    } else {
        rx->estado = ESPERA_U;
    }
    return false;
}

/**
 * @brief Escribe un entero de 16 bits en formato little-endian.
 * @param p     Puntero a 2 bytes de destino.
 * @param valor Valor a escribir.
 */
void uner_escribir16(uint8_t *p, uint16_t valor)
{
    p[0] = valor & 0xFF;
    p[1] = valor >> 8;
}

/**
 * @brief Escribe un entero de 32 bits en formato little-endian.
 * @param p     Puntero a 4 bytes de destino.
 * @param valor Valor a escribir.
 */
void uner_escribir32(uint8_t *p, uint32_t valor)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (valor >> (8 * i)) & 0xFF;
    }
}

/**
 * @brief Escribe un float IEEE-754 de 32 bits en formato little-endian.
 * @param p     Puntero a 4 bytes de destino.
 * @param valor Valor flotante a escribir.
 */
void uner_escribir_float(uint8_t *p, float valor)
{
    uint32_t bits;
    memcpy(&bits, &valor, 4);
    uner_escribir32(p, bits);
}

/**
 * @brief Lee un entero de 16 bits en formato little-endian.
 * @param p Puntero a 2 bytes de origen.
 * @return Valor leido.
 */
uint16_t uner_leer16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

/**
 * @brief Lee un entero de 32 bits en formato little-endian.
 * @param p Puntero a 4 bytes de origen.
 * @return Valor leido.
 */
uint32_t uner_leer32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Lee un float IEEE-754 de 32 bits en formato little-endian.
 * @param p Puntero a 4 bytes de origen.
 * @return Valor flotante leido.
 */
float uner_leer_float(const uint8_t *p)
{
    uint32_t bits = uner_leer32(p);
    float valor;
    memcpy(&valor, &bits, 4);
    return valor;
}
