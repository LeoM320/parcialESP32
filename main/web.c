/**
 * @file web.c
 * @brief Implementacion de web.h.
 */

#include "web.h"
#include <stdio.h>
#include <string.h>
#include "esp_spiffs.h"
#include "esp_log.h"

#define SPIFFS_BASE     "/spiffs"
#define SPIFFS_PARTICION "storage"
#define RUTA_MAX        64      /* SPIFFS admite nombres de hasta 32 caracteres */
#define BLOQUE          1024    /* tamanio de cada envio parcial */
#define CAMPO_CRUDO_MAX 200     /* campo de formulario sin decodificar (64 x 3 + margen) */
#define WEB_MAX_CONEXIONES 4    /* conexiones HTTP abiertas a la vez */

static const char *TAG = "web";

static const char *tipo_contenido(const char *ruta)
{
    const char *ext = strrchr(ruta, '.');
    if (ext == NULL)              return "text/plain";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    return "text/plain";
}

/* Envia /spiffs<nombre> en bloques; nombre empieza con "/" */
static esp_err_t enviar_archivo(httpd_req_t *req, const char *nombre, size_t largo)
{
    char ruta[RUTA_MAX];
    if (largo + sizeof(SPIFFS_BASE) > sizeof(ruta) || strstr(nombre, "..") != NULL) {
        return httpd_resp_send_404(req);
    }
    snprintf(ruta, sizeof(ruta), SPIFFS_BASE "%.*s", (int)largo, nombre);

    FILE *f = fopen(ruta, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "No existe %s", ruta);
        return httpd_resp_send_404(req);
    }

    httpd_resp_set_type(req, tipo_contenido(ruta));

    char bloque[BLOQUE];
    size_t leidos;
    esp_err_t err = ESP_OK;
    while ((leidos = fread(bloque, 1, sizeof(bloque), f)) > 0) {
        err = httpd_resp_send_chunk(req, bloque, leidos);
        if (err != ESP_OK) {
            break;              /* el cliente corto la conexion */
        }
    }
    fclose(f);

    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);   /* fin de la respuesta */
    }
    return err;
}

/* GET /<archivo>: "/" -> index.html */
static esp_err_t archivo_handler(httpd_req_t *req)
{
    size_t largo = strcspn(req->uri, "?");          /* URI sin la parte "?..." */
    if (largo == 1) {
        return enviar_archivo(req, "/index.html", strlen("/index.html"));
    }
    return enviar_archivo(req, req->uri, largo);
}

esp_err_t web_enviar_archivo(httpd_req_t *req, const char *nombre)
{
    return enviar_archivo(req, nombre, strlen(nombre));
}

int web_leer_cuerpo(httpd_req_t *req, char *buf, size_t tamano)
{
    if (req->content_len >= tamano) {
        return -1;
    }

    size_t total = 0;
    while (total < req->content_len) {
        int n = httpd_req_recv(req, buf + total, req->content_len - total);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;           /* todavia no llego el resto: reintentar */
        }
        if (n <= 0) {
            return -1;          /* conexion cerrada o error */
        }
        total += n;
    }
    buf[total] = '\0';
    return total;
}

static int valor_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool web_campo(const char *cuerpo, const char *clave, char *dest, size_t tamano)
{
    /* Codificado ocupa hasta 3 veces mas ("%XX" por caracter) */
    char crudo[CAMPO_CRUDO_MAX];
    if (httpd_query_key_value(cuerpo, clave, crudo, sizeof(crudo)) != ESP_OK) {
        return false;
    }

    size_t j = 0;
    for (size_t i = 0; crudo[i] != '\0'; i++) {
        if (j + 1 >= tamano) {
            return false;               /* no entra en dest */
        }
        char c = crudo[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && valor_hex(crudo[i + 1]) >= 0 && valor_hex(crudo[i + 2]) >= 0) {
            c = (char)(valor_hex(crudo[i + 1]) * 16 + valor_hex(crudo[i + 2]));
            i += 2;
        }
        dest[j++] = c;
    }
    dest[j] = '\0';
    return true;
}

bool web_parametro(httpd_req_t *req, const char *clave, char *dest, size_t tamano)
{
    char query[CAMPO_CRUDO_MAX];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;                   /* la URL no tiene "?..." (o es muy larga) */
    }
    return web_campo(query, clave, dest, tamano);
}

httpd_handle_t web_init(const httpd_uri_t *uris, size_t cantidad)
{
    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path              = SPIFFS_BASE,
        .partition_label        = SPIFFS_PARTICION,
        .max_files              = 4,
        .format_if_mount_failed = false,    /* si falla, es que no se grabo www/: no borrar */
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffs_cfg));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;    // permite URIs con comodin: "/*"
    /* lwIP admite 10 sockets en total (CONFIG_LWIP_MAX_SOCKETS). El servidor usa
     * 2 propios + uno por navegador conectado; con 4 navegadores quedan 4 para
     * tu servidor TCP. Si se llena, cierra la conexion mas vieja. */
    cfg.max_open_sockets = WEB_MAX_CONEXIONES;
    cfg.lru_purge_enable = true;
    if (cfg.max_uri_handlers < cantidad + 1) {
        cfg.max_uri_handlers = cantidad + 1;
    }

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    for (size_t i = 0; i < cantidad; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }

    /* Ultimo: cualquier otro GET se busca como archivo */
    static const httpd_uri_t archivos = {
        .uri     = "/*",
        .method  = HTTP_GET,
        .handler = archivo_handler,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &archivos));

    ESP_LOGI(TAG, "Servidor HTTP listo en el puerto %d", cfg.server_port);
    return server;
}
