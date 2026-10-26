/**
 * @file uner.c
 * @brief Implementacion de uner.h.
 *
 * El receptor es una maquina de estados: espera 'U','N','E','R', despues LEN,
 * despues ':', despues LEN - 1 bytes de carga y por ultimo el checksum.
 */

#include "uner.h"
#include <string.h>

/* Pasos del receptor */
enum { ESPERA_U, ESPERA_N, ESPERA_E, ESPERA_R, ESPERA_LEN, ESPERA_DOSPUNTOS, ESPERA_CARGA, ESPERA_CHK };

size_t uner_armar(uint8_t *trama, const uint8_t *carga, uint8_t n)
{
    if (n > UNER_MAX_CARGA) {
        return 0;
    }
    trama[0] = 'U';
    trama[1] = 'N';
    trama[2] = 'E';
    trama[3] = 'R';
    trama[4] = n + 1;               /* carga + checksum */
    trama[5] = ':';
    memcpy(&trama[6], carga, n);

    uint8_t chk = 0;
    for (int i = 0; i < 6 + n; i++) {
        chk ^= trama[i];
    }
    trama[6 + n] = chk;
    return 7 + n;
}

void uner_rx_init(uner_rx_t *rx)
{
    rx->estado = ESPERA_U;
    rx->n = 0;
}

bool uner_rx_byte(uner_rx_t *rx, uint8_t byte)
{
    switch (rx->estado) {
    case ESPERA_U:
        break;                      /* se resuelve abajo */
    case ESPERA_N:
        if (byte == 'N') { rx->chk ^= byte; rx->estado = ESPERA_E; return false; }
        break;
    case ESPERA_E:
        if (byte == 'E') { rx->chk ^= byte; rx->estado = ESPERA_R; return false; }
        break;
    case ESPERA_R:
        if (byte == 'R') { rx->chk ^= byte; rx->estado = ESPERA_LEN; return false; }
        break;
    case ESPERA_LEN:
        /* LEN = carga + 1: 0 no es valido y la carga tiene que entrar */
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
        return byte == rx->chk;     /* trama completa: vale solo si coincide */
    }

    /* Byte que no encaja: si es una 'U' puede ser el inicio de otra trama */
    if (byte == 'U') {
        rx->chk = byte;
        rx->estado = ESPERA_N;
    } else {
        rx->estado = ESPERA_U;
    }
    return false;
}

void uner_escribir16(uint8_t *p, uint16_t valor)
{
    p[0] = valor & 0xFF;            /* byte bajo */
    p[1] = valor >> 8;              /* byte alto */
}

void uner_escribir32(uint8_t *p, uint32_t valor)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (valor >> (8 * i)) & 0xFF;
    }
}

void uner_escribir_float(uint8_t *p, float valor)
{
    uint32_t bits;
    memcpy(&bits, &valor, 4);       /* los 4 bytes del float tal cual */
    uner_escribir32(p, bits);
}

uint16_t uner_leer16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

uint32_t uner_leer32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

float uner_leer_float(const uint8_t *p)
{
    uint32_t bits = uner_leer32(p);
    float valor;
    memcpy(&valor, &bits, 4);
    return valor;
}
