/**
 * @file tcp.c
 * @brief Implementacion de servidor y cliente TCP sobre sockets BSD de lwIP.
 */

#include "tcp.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "lwip/sockets.h"
#include "esp_log.h"

#define KEEPALIVE_INACTIVO_S    5       ///< Segundos sin trafico antes de sondear al otro extremo.
#define KEEPALIVE_INTERVALO_S   2       ///< Segundos entre sondeos keepalive.
#define KEEPALIVE_INTENTOS      3       ///< Sondeos sin respuesta para dar la conexion por caida.
#define CONECTAR_ESPERA_MS      3000    ///< Espera maxima de connect() en tcp_conectar().

static const char *TAG = "tcp";         ///< Etiqueta para los logs de este modulo.

/**
 * @brief Configura timeout de recepcion, keepalive y TCP_NODELAY en un socket conectado.
 * @param sock Socket conectado.
 * @param tv   Timeout de recepcion.
 */
static void configurar_socket(int sock, const struct timeval *tv)
{
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, tv, sizeof(*tv));
    int si = 1;
    int inactivo = KEEPALIVE_INACTIVO_S;
    int intervalo = KEEPALIVE_INTERVALO_S;
    int intentos = KEEPALIVE_INTENTOS;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &si, sizeof(si));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &inactivo, sizeof(inactivo));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intervalo, sizeof(intervalo));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &intentos, sizeof(intentos));
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &si, sizeof(si));
}

/**
 * @brief Abre un puerto TCP y lo deja en escucha.
 * @param puerto     Puerto donde escuchar.
 * @param timeout_ms Timeout maximo para tcp_aceptar() y tcp_recibir().
 * @return Descriptor del socket servidor (>= 0), o -1 en caso de error.
 */
int tcp_servidor(uint16_t puerto, uint32_t timeout_ms)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear el socket (errno %d)", errno);
        return -1;
    }
    int si = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &si, sizeof(si));
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in dir = {
        .sin_family      = AF_INET,
        .sin_port        = htons(puerto),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&dir, sizeof(dir)) != 0 ||
        listen(sock, 4) != 0) {
        ESP_LOGE(TAG, "No se pudo abrir el puerto %u (errno %d)", puerto, errno);
        close(sock);
        return -1;
    }
    ESP_LOGI(TAG, "Servidor escuchando en el puerto %u", puerto);
    return sock;
}

/**
 * @brief Espera la conexion de un cliente TCP hasta el timeout configurado en el servidor.
 * @param servidor Socket devuelto por tcp_servidor().
 * @return Socket del cliente conectado (>= 0), o -1 si vencio el timeout o fallo.
 */
int tcp_aceptar(int servidor)
{
    struct sockaddr_in cliente;
    socklen_t largo = sizeof(cliente);
    int sock = accept(servidor, (struct sockaddr *)&cliente, &largo);
    if (sock < 0) {
        return -1;
    }
    struct timeval tv;
    socklen_t largo_tv = sizeof(tv);
    getsockopt(servidor, SOL_SOCKET, SO_RCVTIMEO, &tv, &largo_tv);
    configurar_socket(sock, &tv);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cliente.sin_addr, ip, sizeof(ip));
    ESP_LOGI(TAG, "Cliente conectado: %s:%u", ip, ntohs(cliente.sin_port));
    return sock;
}

/**
 * @brief Conecta como cliente a un servidor TCP remoto con timeout de conexion.
 * @param ip         Direccion IPv4 en texto.
 * @param puerto     Puerto del servidor remoto.
 * @param timeout_ms Timeout de recepcion para tcp_recibir().
 * @return Socket conectado (>= 0), o -1 si fallo la conexion.
 */
int tcp_conectar(const char *ip, uint16_t puerto, uint32_t timeout_ms)
{
    struct sockaddr_in dir = {
        .sin_family = AF_INET,
        .sin_port   = htons(puerto),
    };
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) {
        ESP_LOGE(TAG, "IP no valida: '%s'", ip);
        return -1;
    }
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "No se pudo crear el socket (errno %d)", errno);
        return -1;
    }
    struct timeval espera = {
        .tv_sec  = CONECTAR_ESPERA_MS / 1000,
        .tv_usec = (CONECTAR_ESPERA_MS % 1000) * 1000,
    };
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    int r = connect(sock, (struct sockaddr *)&dir, sizeof(dir));
    if (r != 0 && errno == EINPROGRESS) {
        fd_set escritura;
        FD_ZERO(&escritura);
        FD_SET(sock, &escritura);
        r = -1;
        if (select(sock + 1, NULL, &escritura, NULL, &espera) > 0) {
            int error = 0;
            socklen_t largo = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &largo);
            r = (error == 0) ? 0 : -1;
        }
    }
    if (r != 0) {
        ESP_LOGW(TAG, "No se pudo conectar a %s:%u", ip, puerto);
        close(sock);
        return -1;
    }
    fcntl(sock, F_SETFL, flags);
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    configurar_socket(sock, &tv);
    ESP_LOGI(TAG, "Conectado a %s:%u", ip, puerto);
    return sock;
}

/**
 * @brief Recibe datos de un socket TCP conectado.
 * @param sock   Socket activo.
 * @param buf    Buffer donde almacenar los bytes leidos.
 * @param tamano Capacidad maxima del buffer.
 * @return Cantidad de bytes leidos (> 0), 0 si expiro el timeout, o -1 si se cerro la conexion.
 */
int tcp_recibir(int sock, void *buf, size_t tamano)
{
    int n = recv(sock, buf, tamano, 0);
    if (n > 0) {
        return n;
    }
    if (n < 0 && errno == EAGAIN) {
        return 0;
    }
    if (n == 0) {
        ESP_LOGI(TAG, "Cliente desconectado");
    } else {
        ESP_LOGW(TAG, "Error al recibir (errno %d)", errno);
    }
    return -1;
}

/**
 * @brief Envia todos los bytes indicados a traves de un socket TCP conectado.
 * @param sock  Socket activo.
 * @param datos Puntero al bloque de bytes a enviar.
 * @param largo Cantidad de bytes a enviar.
 * @return true si se enviaron todos los bytes, false si hubo error.
 */
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
