/**
 * @file consola_tcp.c
 * @brief Implementacion de consola_tcp.h.
 *
 * Una tarea: espera un cliente, lo atiende hasta que se va y vuelve a esperar.
 * Todas las esperas (accept y recv) vencen cada ESPERA_MS para revisar si
 * cambio el puerto.
 */

#include "consola_tcp.h"
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "consola.h"
#include "telnet.h"
#include "tcp.h"

#define ESPERA_MS   1000    /* cada cuanto se revisa si cambio el puerto */
#define RX_MAX      128

static int s_cliente = -1;  /* socket del cliente, -1 = nadie */

/* Cola de 1 elemento: consola_tcp_puerto() la pisa con xQueueOverwrite y la
 * tarea la lee. Asi se pasa el puerto entre tareas sin variables compartidas. */
static QueueHandle_t s_puerto;

/* Salida de la consola: al socket del cliente */
static void enviar(const char *texto)
{
    if (s_cliente >= 0) {
        tcp_enviar(s_cliente, texto, strlen(texto));    /* si falla, lo detecta tcp_recibir */
    }
}

/* true si pidieron un puerto distinto de 'actual' (no lo saca de la cola) */
static bool puerto_cambio(uint16_t actual)
{
    uint16_t nuevo;
    return xQueuePeek(s_puerto, &nuevo, 0) == pdTRUE && nuevo != actual;
}

/* Atiende al cliente hasta que se desconecta o cambia el puerto */
static void atender(int sock, uint16_t puerto)
{
    telnet_t telnet;
    s_cliente = sock;
    telnet_inicio(&telnet, sock);
    enviar("\r\nConsola ESP32 - 'help' para ver los comandos\r\n");
    consola_inicio();

    char buf[RX_MAX];
    while (!puerto_cambio(puerto)) {
        int n = tcp_recibir(sock, buf, sizeof(buf));    /* 0 = vencio la espera */
        if (n < 0) {
            break;                                      /* se desconecto */
        }
        consola_recibir(buf, telnet_filtrar(&telnet, buf, n));
    }

    s_cliente = -1;
    close(sock);
}

static void tarea(void *arg)
{
    uint16_t puerto;
    xQueueReceive(s_puerto, &puerto, portMAX_DELAY);    /* el de consola_tcp_init */

    for (;;) {
        int servidor = tcp_servidor(puerto, ESPERA_MS);
        if (servidor < 0) {
            vTaskDelay(pdMS_TO_TICKS(ESPERA_MS));       /* reintentar luego */
        } else {
            while (!puerto_cambio(puerto)) {
                int sock = tcp_aceptar(servidor);       /* -1 = nadie en ESPERA_MS */
                if (sock >= 0) {
                    atender(sock, puerto);
                }
            }
            close(servidor);
        }
        xQueueReceive(s_puerto, &puerto, 0);            /* tomar el nuevo, si hay */
    }
}

void consola_tcp_init(uint16_t puerto, int (*ejecutar)(int argc, const char *const *argv))
{
    consola_init(ejecutar, enviar);
    s_puerto = xQueueCreate(1, sizeof(uint16_t));
    xQueueOverwrite(s_puerto, &puerto);
    xTaskCreate(tarea, "consola_tcp", 4096, NULL, 5, NULL);
}

void consola_tcp_puerto(uint16_t puerto)
{
    xQueueOverwrite(s_puerto, &puerto);
}
