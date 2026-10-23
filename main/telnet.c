/**
 * @file telnet.c
 * @brief Implementacion de telnet.h (RFC 854, 857 y 858).
 */

#include "telnet.h"
#include "tcp.h"

/* Bytes de comando Telnet */
#define IAC     255     /* "lo que sigue es un comando" */
#define DONT    254
#define DO      253
#define WONT    252
#define WILL    251
#define SB      250     /* inicio de subnegociacion (... IAC SE) */
#define SE      240     /* fin de subnegociacion */

/* Opciones */
#define OPT_ECHO    1   /* quien hace el eco */
#define OPT_SGA     3   /* "suppress go ahead": mandar cada tecla al instante */

/* Estados del filtro */
enum {
    DATOS,      /* texto normal */
    COMANDO,    /* llego IAC */
    OPCION,     /* llego IAC WILL/WONT/DO/DONT: falta el byte de la opcion */
    SUB,        /* dentro de IAC SB ... */
    SUB_IAC,    /* dentro de SB llego IAC: si sigue SE, termina */
};

void telnet_inicio(telnet_t *t, int sock)
{
    t->estado = DATOS;
    /* "Yo hago el eco" y "mando todo sin esperar": el cliente pasa a modo
     * caracter por caracter y deja de hacer eco local */
    static const uint8_t negociacion[] ={ IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA };
    tcp_enviar(sock, negociacion, sizeof(negociacion));
}

size_t telnet_filtrar(telnet_t *t, char *buf, size_t largo)
{
    size_t j = 0;
    for (size_t i = 0; i < largo; i++) {
        uint8_t c = (uint8_t)buf[i];
        switch (t->estado) {
        case DATOS:
            if (c == IAC) {
                t->estado = COMANDO;
            } else {
                buf[j++] = c;
            }
            break;
        case COMANDO:
            if (c == IAC) {                 /* IAC IAC = un byte 255 de texto */
                buf[j++] = c;
                t->estado = DATOS;
            } else if (c >= WILL && c <= DONT) {
                t->estado = OPCION;
            } else if (c == SB) {
                t->estado = SUB;
            } else {
                t->estado = DATOS;          /* comando de 2 bytes: se ignora */
            }
            break;
        case OPCION:
            t->estado = DATOS;              /* respuesta del cliente: se ignora */
            break;
        case SUB:
            if (c == IAC) {
                t->estado = SUB_IAC;
            }
            break;
        case SUB_IAC:
            t->estado = (c == SE) ? DATOS : SUB;
            break;
        }
    }
    return j;
}
