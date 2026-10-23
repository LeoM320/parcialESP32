/**
 * @file web.h
 * @brief Servidor HTTP: archivos de la pagina desde SPIFFS + endpoints de la aplicacion.
 *
 * Basada en el "Servidor HTTP Embebido" del curso (SPIFFS + esp_http_server).
 * Los archivos de la carpeta www/ del proyecto se graban en la particion "storage"
 * y se sirven por su nombre ("/" -> index.html). La aplicacion pasa sus propios
 * endpoints (por ejemplo /api/led) sin que esta libreria los conozca.
 *
 * - El servidor atiende de a un pedido por vez (una sola tarea): los handlers
 *   tienen que responder rapido y no bloquearse mucho.
 */

#ifndef WEB_H_
#define WEB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_http_server.h"

/**
 * @brief Monta SPIFFS y arranca el servidor HTTP en el puerto 80.
 *
 * Registra primero los endpoints de la aplicacion y despues el que sirve archivos,
 * asi los endpoints tienen prioridad. Aborta si falla el montaje o el servidor.
 * @param uris     Endpoints de la aplicacion (puede ser NULL si cantidad = 0).
 * @param cantidad Cantidad de elementos de uris.
 * @return Handle del servidor (para registrar mas endpoints con httpd_register_uri_handler).
 */
httpd_handle_t web_init(const httpd_uri_t *uris, size_t cantidad);

/**
 * @brief Lee el cuerpo completo de un pedido (por ejemplo un POST) como texto.
 *
 * Para usar dentro de un handler. Agrega el '\0' final.
 * @param req    Pedido recibido por el handler.
 * @param buf    Donde se copia el cuerpo.
 * @param tamano Tamanio de buf (incluye el '\0').
 * @return Cantidad de bytes leidos, o -1 si el cuerpo no entra en buf o se corto la conexion.
 */
int web_leer_cuerpo(httpd_req_t *req, char *buf, size_t tamano);

/**
 * @brief Extrae un campo de un formulario urlencoded ("clave=valor&...") y lo decodifica.
 *
 * El navegador codifica los caracteres especiales ("mi red" llega como "mi+red",
 * "#" como "%23"); esta funcion los devuelve a su forma original.
 * @param cuerpo Texto del formulario (por ejemplo leido con web_leer_cuerpo()).
 * @param clave  Nombre del campo.
 * @param dest   Donde se copia el valor decodificado.
 * @param tamano Tamanio de dest (incluye el '\0').
 * @return true si el campo existe y entra en dest; false si falta o es demasiado largo.
 */
bool web_campo(const char *cuerpo, const char *clave, char *dest, size_t tamano);

/**
 * @brief Responde con un archivo de www/ (para un handler que elige que pagina mostrar).
 * @param req    Pedido recibido por el handler.
 * @param nombre Archivo dentro de www/, con "/" al principio (ej. "/leds.html").
 * @return ESP_OK si se envio; si no existe responde 404 y devuelve ese resultado.
 */
esp_err_t web_enviar_archivo(httpd_req_t *req, const char *nombre);

/**
 * @brief IP del ESP32 por la que entro el pedido.
 *
 * Con AP + STA el servidor atiende en las dos IP: esto dice cual uso el cliente,
 * para comparar con la IP de una interfaz (ej. wifi_ip_ap()).
 * @param req Pedido recibido por el handler.
 * @return IPv4 en el formato de lwIP/esp_netif, o 0 si no se pudo obtener.
 */
uint32_t web_ip_local(httpd_req_t *req);

#endif /* WEB_H_ */
