/**
 * @file includes.h
 * @brief Todos los includes del main en un solo lugar.
 *
 * main.c incluye solo este archivo. Si el editor mete solo algun include en
 * main.c (portmacro.h, projdefs.h, FreeRTOSConfig_arch.h...), se ve enseguida:
 * en main.c tiene que haber UNA sola linea de #include. Ese include sobra,
 * se borra.
 *
 * OJO: freertos/FreeRTOS.h siempre primero que los otros de freertos.
 */

#ifndef INCLUDES_H_
#define INCLUDES_H_

/* ======================= C estandar ======================= */

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>				// sinf, cosf, acosf, atan2f de la cinematica
#include <unistd.h>				// close() de los sockets

/* ======================= ESP-IDF ======================= */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_attr.h"			// IRAM_ATTR
#include "esp_netif_types.h"
#include "esp_spiffs.h"			// esp_vfs_spiffs_register()

/* ======================= FreeRTOS ======================= */

#include "freertos/FreeRTOS.h"	// primero!!
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

/* ======================= Librerias del proyecto ======================= */

#include "hardware.h"
#include "iodigital.h"
#include "ianalogica.h"
#include "rgb.h"
#include "memoria.h"
#include "wifi.h"
#include "tcp.h"
#include "serie.h"
#include "microrl.h"
#include "uner.h"
#include "cJSON.h"

#endif /* INCLUDES_H_ */
