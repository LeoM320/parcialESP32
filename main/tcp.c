/**
 * @file tcp.c
 * @brief Implementacion de tcp.h.
 *
 * Sockets BSD de lwIP (los mismos que en una PC). Un servidor TCP:
 *   socket() -> bind() al puerto -> listen() -> accept() por cada cliente.
 * accept() devuelve un socket NUEVO para hablar con ese cliente; el del
 * servidor sigue escuchando.
 * Un cliente TCP: socket() -> connect() a la IP y puerto del servidor.
 */

#include "tcp.h"
#include <errno.h>              /* errno: codigo del ultimo error */
#include <fcntl.h>              /* fcntl(), O_NONBLOCK */
#include <unistd.h>             /* close() */
#include "lwip/sockets.h"       /* socket, bind, listen, accept, connect, recv, send */
#include "esp_log.h"

/* Keepalive: cliente caido detectado en ~ 5 + 3 x 2 = 11 s */
#define KEEPALIVE_INACTIVO_S    5
#define KEEPALIVE_INTERVALO_S   2
#define KEEPALIVE_INTENTOS      3

#define CONECTAR_ESPERA_MS      3000    /* espera maxima de connect() en tcp_conectar() */

static const char *TAG = "tcp";

/* Timeout de recepcion y keepalive: lo mismo para clientes aceptados y conectados */
static void configurar_socket(int sock, const struct timeval *tv)
{
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, tv, sizeof(*tv));

    /* Keepalive: si el otro extremo desaparece sin cerrar (se cae el WiFi, se
     * cuelga la PC), tras KEEPALIVE_INACTIVO_S sin trafico se lo sondea cada
     * KEEPALIVE_INTERVALO_S; si no contesta KEEPALIVE_INTENTOS veces,
     * tcp_recibir() devuelve -1 y se libera la conexion. */
    int si = 1;
    int inactivo = KEEPALIVE_INACTIVO_S;
    int intervalo = KEEPALIVE_INTERVALO_S;
    int intentos = KEEPALIVE_INTENTOS;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &si, sizeof(si));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &inactivo, sizeof(inactivo));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intervalo, sizeof(intervalo));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &intentos, sizeof(intentos));
}

int tcp_servidor(uint16_t puerto, uint32_t timeout_ms)
{
    /* AF_INET = IPv4, SOCK_STREAM = TCP */
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear el socket (errno %d)", errno);
        return -1;
    }

    /* Permite reabrir el mismo puerto enseguida despues de cerrarlo */
    int si = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &si, sizeof(si));

    /* Timeout de accept(); tcp_aceptar() se lo copia a cada cliente */
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,               /* segundos enteros */
        .tv_usec = (timeout_ms % 1000) * 1000,      /* el resto, en microsegundos */
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Direccion donde escuchar */
    struct sockaddr_in dir = {
        .sin_family      = AF_INET,
        .sin_port        = htons(puerto),           /* htons: al orden de bytes de la red */
        .sin_addr.s_addr = htonl(INADDR_ANY),   /* todas las interfaces */
    };
    /* bind: atar el socket al puerto; listen: ponerlo a esperar (1 = cola de 1 cliente pendiente) */
    if (bind(sock, (struct sockaddr *)&dir, sizeof(dir)) != 0 ||
        listen(sock, 1) != 0) {
        ESP_LOGE(TAG, "No se pudo abrir el puerto %u (errno %d)", puerto, errno);
        close(sock);
        return -1;
    }

    ESP_LOGI(TAG, "Servidor escuchando en el puerto %u", puerto);
    return sock;
}

int tcp_aceptar(int servidor)
{
    struct sockaddr_in cliente;             /* aca accept() deja la IP y puerto del cliente */
    socklen_t largo = sizeof(cliente);
    int sock = accept(servidor, (struct sockaddr *)&cliente, &largo);   /* bloquea hasta el timeout */
    if (sock < 0) {
        return -1;                      /* timeout: nadie se conecto */
    }

    /* El socket del cliente no hereda el timeout: copiarle el del servidor */
    struct timeval tv;
    socklen_t largo_tv = sizeof(tv);
    getsockopt(servidor, SOL_SOCKET, SO_RCVTIMEO, &tv, &largo_tv);
    configurar_socket(sock, &tv);

    /* Mostrar en el log quien se conecto */
    char ip[INET_ADDRSTRLEN];                       /* "255.255.255.255" + '\0' */
    inet_ntop(AF_INET, &cliente.sin_addr, ip, sizeof(ip));  /* IP numerica -> texto */
    ESP_LOGI(TAG, "Cliente conectado: %s:%u", ip, ntohs(cliente.sin_port));
    return sock;
}

int tcp_conectar(const char *ip, uint16_t puerto, uint32_t timeout_ms)
{
    struct sockaddr_in dir = {
        .sin_family = AF_INET,
        .sin_port   = htons(puerto),
    };
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) {     /* texto -> IP numerica */
        ESP_LOGE(TAG, "IP no valida: '%s'", ip);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear el socket (errno %d)", errno);
        return -1;
    }

    /* connect() normal puede bloquear ~20 s si el equipo no existe. Se lo hace
     * no bloqueante y se espera con select() como mucho CONECTAR_ESPERA_MS. */
    struct timeval espera = {
        .tv_sec  = CONECTAR_ESPERA_MS / 1000,
        .tv_usec = (CONECTAR_ESPERA_MS % 1000) * 1000,
    };
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int r = connect(sock, (struct sockaddr *)&dir, sizeof(dir));
    if (r != 0 && errno == EINPROGRESS) {          /* conectando: esperar */
        fd_set escritura;
        FD_ZERO(&escritura);
        FD_SET(sock, &escritura);
        r = -1;
        if (select(sock + 1, NULL, &escritura, NULL, &espera) > 0) {     /* listo para escribir = termino */
            int error = 0;
            socklen_t largo = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &largo);  /* 0 = conecto bien */
            r = (error == 0) ? 0 : -1;
        }
    }
    if (r != 0) {
        ESP_LOGW(TAG, "No se pudo conectar a %s:%u", ip, puerto);
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, flags);                   /* volver a bloqueante */
    struct timeval tv = {                          /* timeout de tcp_recibir() */
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    configurar_socket(sock, &tv);
    ESP_LOGI(TAG, "Conectado a %s:%u", ip, puerto);
    return sock;
}

int tcp_recibir(int sock, void *buf, size_t tamano)
{
    /* recv devuelve: > 0 bytes leidos; 0 el cliente cerro; < 0 error o timeout */
    int n = recv(sock, buf, tamano, 0);
    if (n > 0) {
        return n;
    }
    if (n < 0 && errno == EAGAIN) {     /* timeout sin datos (EAGAIN = EWOULDBLOCK) */
        return 0;
    }
    if (n == 0) {
        ESP_LOGI(TAG, "Cliente desconectado");
    } else {
        ESP_LOGW(TAG, "Error al recibir (errno %d)", errno);
    }
    return -1;
}

bool tcp_enviar(int sock, const void *datos, size_t largo)
{
    const char *p = datos;              /* char *: para poder avanzar de a bytes */
    while (largo > 0) {
        int n = send(sock, p, largo, 0);    /* puede mandar menos de lo pedido */
        if (n < 0) {
            ESP_LOGW(TAG, "Error al enviar (errno %d)", errno);
            return false;
        }
        p += n;                         /* avanzar lo que ya se mando */
        largo -= n;                     /* y descontarlo de lo que falta */
    }
    return true;
}
