/**
 * @file consola_tcp.c
 * @brief Implementacion de consola_tcp.h.
 *
 * Una sola tarea atiende el puerto y a todos los clientes con select(): se
 * bloquea hasta que alguno tenga algo (un cliente nuevo o datos), como mucho
 * ESPERA_MS, y despues revisa si cambio el puerto.
 */

#include "consola_tcp.h"
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "consola.h"
#include "telnet.h"
#include "tcp.h"

#define ESPERA_MS   1000    /* cada cuanto se revisa si cambio el puerto */
#define RX_MAX      128

static const char *TAG = "consola_tcp";

typedef struct {
    int sock;               /* -1 = lugar libre */
    telnet_t telnet;
    consola_t *consola;
} cliente_t;

static cliente_t s_clientes[CONSOLA_TCP_CLIENTES];

/* Cola de 1 elemento: consola_tcp_puerto() la pisa con xQueueOverwrite y la
 * tarea la lee. Asi se pasa el puerto entre tareas sin variables compartidas. */
static QueueHandle_t s_puerto;

/* Salida de la consola de un cliente: a su socket */
static void enviar(void *ctx, const char *texto)
{
    cliente_t *c = ctx;
    if (c->sock >= 0) {
        tcp_enviar(c->sock, texto, strlen(texto));  /* si falla, lo detecta tcp_recibir */
    }
}

static void cerrar(cliente_t *c)
{
    close(c->sock);
    c->sock = -1;
}

/* Hay un cliente esperando: darle un lugar libre o rechazarlo */
static void aceptar(int servidor)
{
    int sock = tcp_aceptar(servidor);
    if (sock < 0) {
        vTaskDelay(pdMS_TO_TICKS(100));     /* ej. sin sockets libres: no girar en vacio */
        return;
    }

    for (size_t i = 0; i < CONSOLA_TCP_CLIENTES; i++) {
        cliente_t *c = &s_clientes[i];
        if (c->sock < 0) {
            c->sock = sock;
            telnet_inicio(&c->telnet, sock);
            enviar(c, "\r\nConsola ESP32 - 'help' para ver los comandos\r\n");
            consola_inicio(c->consola);
            return;
        }
    }

    static const char llena[] = "Consola llena: no hay lugar para otro cliente\r\n";
    tcp_enviar(sock, llena, sizeof(llena) - 1);
    close(sock);
    ESP_LOGW(TAG, "Cliente rechazado: no hay lugar");
}

/* El cliente tiene datos (o se desconecto) */
static void atender(cliente_t *c)
{
    char buf[RX_MAX];
    int n = tcp_recibir(c->sock, buf, sizeof(buf));
    if (n > 0) {
        size_t texto = telnet_filtrar(&c->telnet, buf, n);
        consola_recibir(c->consola, buf, texto);
    } else if (n < 0) {
        cerrar(c);
    }
}

static void tarea(void *arg)
{
    uint16_t puerto;
    xQueueReceive(s_puerto, &puerto, portMAX_DELAY);    /* el de consola_tcp_init */
    int servidor = -1;

    for (;;) {
        /* Puerto nuevo: cerrar todo y reabrir en el siguiente paso */
        uint16_t nuevo;
        if (xQueueReceive(s_puerto, &nuevo, 0) == pdTRUE && nuevo != puerto) {
            puerto = nuevo;
            for (size_t i = 0; i < CONSOLA_TCP_CLIENTES; i++) {
                if (s_clientes[i].sock >= 0) {
                    cerrar(&s_clientes[i]);
                }
            }
            if (servidor >= 0) {
                close(servidor);
                servidor = -1;
            }
        }

        if (servidor < 0) {
            servidor = tcp_servidor(puerto, ESPERA_MS);
            if (servidor < 0) {
                vTaskDelay(pdMS_TO_TICKS(ESPERA_MS));   /* reintentar luego */
            }
            continue;
        }

        /* Esperar a que el servidor o algun cliente tenga algo */
        fd_set listos;
        FD_ZERO(&listos);
        FD_SET(servidor, &listos);
        int mayor = servidor;
        for (size_t i = 0; i < CONSOLA_TCP_CLIENTES; i++) {
            int s = s_clientes[i].sock;
            if (s >= 0) {
                FD_SET(s, &listos);
                if (s > mayor) {
                    mayor = s;
                }
            }
        }
        struct timeval espera = { .tv_sec = ESPERA_MS / 1000, .tv_usec = (ESPERA_MS % 1000) * 1000 };
        if (select(mayor + 1, &listos, NULL, NULL, &espera) <= 0) {
            continue;                   /* vencio la espera (o error): revisar el puerto */
        }

        if (FD_ISSET(servidor, &listos)) {
            aceptar(servidor);
        }
        for (size_t i = 0; i < CONSOLA_TCP_CLIENTES; i++) {
            cliente_t *c = &s_clientes[i];
            if (c->sock >= 0 && FD_ISSET(c->sock, &listos)) {
                atender(c);
            }
        }
    }
}

void consola_tcp_init(uint16_t puerto)
{
    for (size_t i = 0; i < CONSOLA_TCP_CLIENTES; i++) {
        s_clientes[i].sock = -1;
        s_clientes[i].consola = consola_crear(enviar, &s_clientes[i]);
    }
    s_puerto = xQueueCreate(1, sizeof(uint16_t));
    xQueueOverwrite(s_puerto, &puerto);
    xTaskCreate(tarea, "consola_tcp", 4096, NULL, 5, NULL);
}

void consola_tcp_puerto(uint16_t puerto)
{
    xQueueOverwrite(s_puerto, &puerto);
}
