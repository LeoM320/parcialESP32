/**
 * @file tcp.c
 * @brief Implementacion de tcp.h.
 */

#include "tcp.h"
#include <errno.h>
#include <unistd.h>
#include "lwip/sockets.h"
#include "esp_log.h"

/* Keepalive: cliente caido detectado en ~ 5 + 3 x 2 = 11 s */
#define KEEPALIVE_INACTIVO_S    5
#define KEEPALIVE_INTERVALO_S   2
#define KEEPALIVE_INTENTOS      3

static const char *TAG = "tcp";

int tcp_servidor(uint16_t puerto, uint32_t timeout_ms)
{
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
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dir = {
        .sin_family      = AF_INET,
        .sin_port        = htons(puerto),
        .sin_addr.s_addr = htonl(INADDR_ANY),   /* todas las interfaces */
    };
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
    struct sockaddr_in cliente;
    socklen_t largo = sizeof(cliente);
    int sock = accept(servidor, (struct sockaddr *)&cliente, &largo);
    if (sock < 0) {
        return -1;                      /* timeout: nadie se conecto */
    }

    struct timeval tv;
    socklen_t largo_tv = sizeof(tv);
    getsockopt(servidor, SOL_SOCKET, SO_RCVTIMEO, &tv, &largo_tv);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Keepalive: si el cliente desaparece sin cerrar (se cae el WiFi, se
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

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cliente.sin_addr, ip, sizeof(ip));
    ESP_LOGI(TAG, "Cliente conectado: %s:%u", ip, ntohs(cliente.sin_port));
    return sock;
}

int tcp_recibir(int sock, void *buf, size_t tamano)
{
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
    const char *p = datos;
    while (largo > 0) {
        int n = send(sock, p, largo, 0);
        if (n < 0) {
            ESP_LOGW(TAG, "Error al enviar (errno %d)", errno);
            return false;
        }
        p += n;
        largo -= n;
    }
    return true;
}
