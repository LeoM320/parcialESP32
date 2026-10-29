/**
 * @file web.c
 * @brief Implementacion de web.h.
 *
 * Usa el servidor HTTP de ESP-IDF (esp_http_server). Cada direccion (URI) tiene
 * una funcion "handler" que el servidor llama cuando un navegador la pide.
 * Las paginas (www/) estan grabadas en la particion SPIFFS de la flash y se
 * leen como archivos comunes con fopen/fread.
 *
 * Como se apoya en TCP: el servidor HTTP es un servidor TCP en el puerto 80
 * (socket/bind/listen/accept, lo mismo que tcp.c). El navegador manda texto:
 *
 *     GET /led?n=2 HTTP/1.1\r\n
 *     Host: 192.168.4.1\r\n
 *     \r\n
 *
 * El servidor lee eso, busca que handler corresponde a "/led" y lo llama con un
 * httpd_req_t (el pedido ya separado). El handler contesta con httpd_resp_send*
 * y el servidor arma la respuesta "HTTP/1.1 200 OK\r\n...". Nosotros nunca
 * tocamos el socket.
 *
 * Diferencia con el ejemplo del profe (HTTP_ESP32): el profe registra un
 * handler por archivo (/, /style.css, /script.js...) y usa un buffer global
 * chunk[256]. Aca hay UN handler "/\*" que sirve cualquier archivo de www/ por su
 * nombre, y el buffer es local (en la pila de la tarea del servidor).
 *
 * VERSION DE ESTUDIO: comentada linea por linea. La version limpia (solo Doxygen)
 * se restaura cuando terminemos de estudiarla.
 */

/* Nuestro .h: trae esp_http_server.h (httpd_req_t, httpd_uri_t...) y los prototipos. */
#include "web.h"
/* fopen, fread, fclose, snprintf: los archivos de SPIFFS se leen como en la PC. */
#include <stdio.h>
/* strrchr, strcmp, strstr, strcspn, strlen: manejo de texto. */
#include <string.h>
/* esp_vfs_spiffs_register: monta la particion de flash como carpeta. */
#include "esp_spiffs.h"
/* ESP_LOGI / ESP_LOGW. */
#include "esp_log.h"

/* Carpeta "virtual" donde aparece SPIFFS. Despues de montarla, el archivo
 * www/index.html del proyecto se abre como "/spiffs/index.html". */
#define SPIFFS_BASE         "/spiffs"   ///< Carpeta donde aparecen los archivos de www/.
/* Tiene que coincidir con la columna Name de partitions.csv y con
 * spiffs_create_partition_image(storage ...) de CMakeLists.txt. */
#define SPIFFS_PARTICION    "storage"   ///< Nombre de la particion (partitions.csv).
/* "/spiffs" + nombre + '\0'. SPIFFS admite nombres de hasta 32, sobra. */
#define RUTA_MAX            64          ///< Largo maximo de una ruta (SPIFFS admite nombres de hasta 32).
/* Cuanto se lee del archivo y se manda por vez. El profe usa 256. */
#define BLOQUE              1024        ///< Tamanio de cada envio parcial de un archivo.
/* Un campo codificado ocupa hasta 3 veces mas: "#" llega como "%23". */
#define CAMPO_CRUDO_MAX     200         ///< Campo de formulario sin decodificar (64 x 3 + margen).
/* Navegadores conectados a la vez (cada pestania abierta puede tener una o dos). */
#define WEB_MAX_CONEXIONES  4           ///< Conexiones HTTP abiertas a la vez.

/* Etiqueta de los mensajes: "I (1234) web: Servidor HTTP listo...". */
static const char *TAG = "web";         ///< Nombre que aparece en los ESP_LOG de este archivo.

/**
 * @brief Tipo de contenido (Content-Type) segun la extension del archivo.
 * @param ruta Nombre o ruta del archivo.
 * @return Texto del tipo, ej. "text/html"; "text/plain" si no la conoce.
 */
static const char *tipo_contenido(const char *ruta)
{
    /* El navegador no mira la extension: mira la cabecera "Content-Type" de la
     * respuesta. Si un .css llega como text/plain, NO aplica los estilos.
     * strrchr(texto, c): puntero al ULTIMO c del texto (NULL si no hay).
     * De "/spiffs/style.css" da ".css". */
    const char *ext = strrchr(ruta, '.');
    /* Sin punto = sin extension. */
    if (ext == NULL)               return "text/plain";
    /* strcmp da 0 cuando los dos textos son iguales. */
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    /* Cualquier otra: texto comun. */
    return "text/plain";
}

/**
 * @brief Envia /spiffs<nombre> en bloques de BLOQUE bytes.
 *
 * Rechaza (404) nombres demasiado largos o con ".." (intento de salir de la
 * carpeta). Se manda por partes para no necesitar el archivo entero en RAM;
 * un bloque vacio al final avisa que termino.
 * @param req    Pedido del navegador.
 * @param nombre Nombre del archivo, empieza con "/".
 * @param largo  Cuantos caracteres de nombre usar (sin el "?..." de la URL).
 * @return ESP_OK si se mando entero; error si no existe o se corto.
 */
static esp_err_t enviar_archivo(httpd_req_t *req, const char *nombre, size_t largo)
{
    /* Aca se arma "/spiffs/index.html". Local: vive en la pila de la tarea httpd. */
    char ruta[RUTA_MAX];
    /* Dos controles:
     *  - Largo: sizeof(SPIFFS_BASE) = 8 ("/spiffs" son 7 + el '\0'), asi que
     *    largo + 8 es lo que ocupa la ruta entera. Si no entra, 404.
     *  - "..": "/../algo" seria salir de la carpeta. strstr(texto, buscado) da
     *    NULL si no lo encuentra. */
    if (largo + sizeof(SPIFFS_BASE) > sizeof(ruta) || strstr(nombre, "..") != NULL) {
        /* Responde "404 Not Found" y devuelve ESP_OK si lo pudo mandar. */
        return httpd_resp_send_404(req);
    }
    /* SPIFFS_BASE "%.*s": dos textos seguidos el compilador los pega, queda
     * "/spiffs%.*s". %.*s = imprimir como mucho "largo" caracteres de nombre
     * (el (int) es porque * pide un int). Asi se corta el "?n=2" sin copiar
     * la URI a otro lado. */
    snprintf(ruta, sizeof(ruta), SPIFFS_BASE "%.*s", (int)largo, nombre);
    /* Igual que en la PC: abrir para leer. NULL si no existe. */
    FILE *f = fopen(ruta, "r");
    if (f == NULL) {
        /* Aviso en el monitor: sirve para ver que pide el navegador (ej. favicon.ico). */
        ESP_LOGW(TAG, "No existe %s", ruta);
        return httpd_resp_send_404(req);
    }
    /* Cabecera Content-Type. Hay que ponerla ANTES del primer chunk: con el
     * primer envio salen las cabeceras y despues ya no se pueden cambiar. */
    httpd_resp_set_type(req, tipo_contenido(ruta));
    /* 1 KB en la pila. La tarea httpd tiene 4096 de pila por defecto, alcanza.
     * El profe usa un chunk[256] global: tambien anda porque hay una sola tarea
     * de servidor, pero ocupa RAM siempre. */
    char bloque[BLOQUE];
    /* Cuantos bytes dio cada fread. */
    size_t leidos;
    /* Resultado del ultimo envio. Arranca bien por si el archivo esta vacio. */
    esp_err_t err = ESP_OK;
    /* fread(destino, tamanio_elemento, cantidad, archivo): lee hasta 1024 bytes
     * y devuelve cuantos leyo. 0 = fin del archivo. */
    while ((leidos = fread(bloque, 1, sizeof(bloque), f)) > 0) {
        /* Respuesta "chunked" (Transfer-Encoding: chunked): no hace falta saber
         * el tamanio total antes. Cada llamada manda "largo en hex\r\n datos\r\n".
         * La primera vez manda antes "HTTP/1.1 200 OK" y las cabeceras. */
        err = httpd_resp_send_chunk(req, bloque, leidos);
        if (err != ESP_OK) {
            /* El navegador cerro (cambio de pagina, cerro la pestania). */
            break;
        }
    }
    /* Siempre cerrar: max_files = 4, si no se cierra se agotan. */
    fclose(f);
    if (err == ESP_OK) {
        /* Chunk vacio (NULL, 0) = "0\r\n\r\n": avisa al navegador que termino.
         * Sin esto el navegador se queda esperando mas. */
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    /* El servidor mira esto: si no es ESP_OK cierra la conexion. */
    return err;
}

/**
 * @brief Handler de "/\*": cualquier GET que no atendio otro handler se busca como archivo.
 *
 * "/" se toma como "/index.html". Se ignora la parte "?..." de la URL.
 * @param req Pedido del navegador.
 * @return Lo que devuelva enviar_archivo().
 */
static esp_err_t archivo_handler(httpd_req_t *req)
{
    /* req->uri = la URI completa como la mando el navegador, ej. "/style.css?v=3".
     * strcspn(texto, "?") = cuantos caracteres hay antes del primer '?'
     * (o el largo entero si no hay). Para "/style.css?v=3" da 10. */
    size_t largo = strcspn(req->uri, "?");
    /* Largo 1 = la URI es solo "/" (la pagina principal). */
    if (largo == 1) {
        return enviar_archivo(req, "/index.html", strlen("/index.html"));
    }
    /* Cualquier otra: el archivo con ese nombre. "/style.css" -> "/spiffs/style.css". */
    return enviar_archivo(req, req->uri, largo);
}

/**
 * @brief Envia un archivo de www/ desde un handler propio.
 * @param req    Pedido del navegador.
 * @param nombre Nombre del archivo, empieza con "/" (ej. "/wifi.html").
 * @return ESP_OK si se mando entero; error si no existe o se corto.
 */
esp_err_t web_enviar_archivo(httpd_req_t *req, const char *nombre)
{
    /* Igual que el handler pero con el nombre que elegis vos: sirve para un
     * handler que procesa algo y despues muestra una pagina. Aca el nombre no
     * tiene "?...", asi que se usa entero (strlen). */
    return enviar_archivo(req, nombre, strlen(nombre));
}

/**
 * @brief Lee el cuerpo completo de un POST y le agrega '\0'.
 *
 * Como en TCP, el cuerpo puede llegar en varias partes: se lee hasta tener
 * los content_len bytes que anuncio el navegador.
 * @param req    Pedido del navegador.
 * @param buf    Donde guardarlo.
 * @param tamano Tamanio de buf (tiene que entrar el cuerpo + '\0').
 * @return Bytes leidos, o -1 si no entra o se corto la conexion.
 */
int web_leer_cuerpo(httpd_req_t *req, char *buf, size_t tamano)
{
    /* Un POST trae despues de las cabeceras un "cuerpo", ej. un formulario
     * "ssid=MiRed&clave=1234". La cabecera Content-Length dice cuantos bytes
     * son; el servidor la deja en req->content_len. A diferencia de TCP aca
     * SI sabemos el largo del mensaje (HTTP ya resolvio el "framing").
     * >= y no >: hace falta un lugar mas para el '\0'. */
    if (req->content_len >= tamano) {
        return -1;
    }
    /* Cuantos bytes llevamos guardados. */
    size_t total = 0;
    /* Hasta tener todo el cuerpo. */
    while (total < req->content_len) {
        /* httpd_req_recv(req, destino, maximo): como recv() de TCP (por dentro
         * lo usa). buf + total = seguir escribiendo despues de lo que ya llego;
         * content_len - total = lo que falta. Devuelve bytes leidos, o un
         * negativo: HTTPD_SOCK_ERR_TIMEOUT si no llego nada en 5 s
         * (recv_wait_timeout), u otro error. */
        int n = httpd_req_recv(req, buf + total, req->content_len - total);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            /* Todavia no llego el resto: volver a esperar. */
            continue;
        }
        if (n <= 0) {
            /* Conexion cerrada o error. */
            return -1;
        }
        /* Avanzar. */
        total += n;
    }
    /* Terminar el texto para poder usar strcmp, web_campo, printf... */
    buf[total] = '\0';
    return total;
}

/**
 * @brief Valor de un caracter hexadecimal.
 * @param c '0'..'9', 'a'..'f' o 'A'..'F'.
 * @return 0..15, o -1 si no es hexadecimal.
 */
static int valor_hex(char c)
{
    /* En ASCII los digitos son seguidos: '0' = 48 ... '9' = 57. c - '0' da 0..9. */
    if (c >= '0' && c <= '9') return c - '0';
    /* 'a' = 97: 'a' - 'a' + 10 = 10, ..., 'f' = 15. */
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    /* Lo mismo en mayuscula. */
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    /* No es hexadecimal (incluye el '\0' del final). */
    return -1;
}

/**
 * @brief Busca "clave=valor" en "a=1&b=2..." y decodifica el valor.
 *
 * El navegador manda los espacios como '+' y los caracteres raros como %XX
 * (ej. "Mi%20Red" = "Mi Red").
 * @param cuerpo Texto con formato de formulario.
 * @param clave  Nombre del campo.
 * @param dest   Donde dejar el valor decodificado (con '\0').
 * @param tamano Tamanio de dest.
 * @return true si estaba y entro; false si no esta o no entra.
 */
bool web_campo(const char *cuerpo, const char *clave, char *dest, size_t tamano)
{
    /* El valor tal cual llega, todavia codificado. */
    char crudo[CAMPO_CRUDO_MAX];
    /* httpd_query_key_value(texto, clave, destino, tamanio): de la libreria.
     * En "ssid=Mi+Red&clave=1234" con clave "ssid" copia "Mi+Red".
     * Da ESP_ERR_NOT_FOUND si no esta, o ESP_ERR_HTTPD_RESULT_TRUNC si no entra
     * (copia cortado). En los dos casos lo tomamos como que fallo. */
    if (httpd_query_key_value(cuerpo, clave, crudo, sizeof(crudo)) != ESP_OK) {
        return false;
    }
    /* Dos indices: i lee crudo, j escribe dest. Van separados porque "%20"
     * son 3 caracteres que se vuelven 1. */
    size_t j = 0;
    /* Hasta el '\0' de crudo. */
    for (size_t i = 0; crudo[i] != '\0'; i++) {
        /* Dejar siempre un lugar para el '\0' de dest. */
        if (j + 1 >= tamano) {
            return false;
        }
        /* Caracter actual. */
        char c = crudo[i];
        if (c == '+') {
            /* En formularios el espacio llega como '+'. */
            c = ' ';
        } else if (c == '%' && valor_hex(crudo[i + 1]) >= 0 && valor_hex(crudo[i + 2]) >= 0) {
            /* "%XX" = el byte con valor hexadecimal XX. %41 = 4*16 + 1 = 65 = 'A'.
             * Se controla que los dos siguientes sean hex: si crudo termina en
             * "%" o "%4", crudo[i+1] es el '\0' y valor_hex da -1 (no se lee
             * fuera del texto: el && corta antes). */
            c = (char)(valor_hex(crudo[i + 1]) * 16 + valor_hex(crudo[i + 2]));
            /* Saltear los dos digitos (el i++ del for saltea el tercero). */
            i += 2;
        }
        /* Escribir y avanzar j. Un '%' suelto se copia tal cual. */
        dest[j++] = c;
    }
    /* Terminar el texto. */
    dest[j] = '\0';
    return true;
}

/**
 * @brief Lee un parametro de la URL: de "/led?n=2&v=1" saca el valor de "n".
 * @param req    Pedido del navegador.
 * @param clave  Nombre del parametro.
 * @param dest   Donde dejar el valor decodificado (con '\0').
 * @param tamano Tamanio de dest.
 * @return true si estaba y entro; false si la URL no tiene "?..." o no esta la clave.
 */
bool web_parametro(httpd_req_t *req, const char *clave, char *dest, size_t tamano)
{
    /* La parte despues del '?'. */
    char query[CAMPO_CRUDO_MAX];
    /* httpd_req_get_url_query_str(req, destino, tamanio): de "/led?n=2&v=1"
     * copia "n=2&v=1". Da ESP_ERR_NOT_FOUND si no hay '?', o
     * ESP_ERR_HTTPD_RESULT_TRUNC si no entra. */
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    /* La query tiene el mismo formato que un formulario: reusamos web_campo. */
    return web_campo(query, clave, dest, tamano);
}

/**
 * @brief Monta SPIFFS, arranca el servidor HTTP y registra los handlers.
 *
 * Pasos:
 *  1. Monta SPIFFS: los archivos de www/ aparecen en /spiffs/.
 *  2. Configura el servidor (puerto 80): comodines en las URI, como mucho
 *     WEB_MAX_CONEXIONES navegadores (si se llena cierra la conexion mas
 *     vieja) y lugar para todos los handlers.
 *  3. Lo arranca (crea la tarea httpd).
 *  4. Registra los handlers de main.c y, al final, "/\*" para los archivos
 *     (el servidor prueba en orden de registro).
 * @param uris     Arreglo de handlers de main.c.
 * @param cantidad Cuantos tiene el arreglo.
 * @return Handle del servidor.
 */
httpd_handle_t web_init(const httpd_uri_t *uris, size_t cantidad)
{
    /* PASO 1. Configuracion del montaje de SPIFFS (lo mismo que hace el profe
     * en initi_web_page_buffer, el usa "/storage" como carpeta). */
    esp_vfs_spiffs_conf_t spiffs_cfg = {
        /* Carpeta donde aparece: "/spiffs/index.html". */
        .base_path              = SPIFFS_BASE,
        /* Que particion de la flash (partitions.csv). */
        .partition_label        = SPIFFS_PARTICION,
        /* Archivos abiertos a la vez (cada fopen sin fclose ocupa uno). */
        .max_files              = 4,
        /* Si no monta es que no se grabo www/ (faltaria el idf.py flash):
         * false = no formatear, mejor que aborte y se note. */
        .format_if_mount_failed = false,
    };
    /* Monta. ESP_ERROR_CHECK: si falla, imprime el error y reinicia la placa. */
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffs_cfg));
    /* PASO 2. Configuracion del servidor. HTTPD_DEFAULT_CONFIG() es una macro
     * que llena el struct con los valores por defecto: puerto 80, tarea
     * "httpd" con prioridad tskIDLE_PRIORITY + 5 y 4096 de pila, 7 sockets,
     * 8 handlers, 5 s de timeout para recibir y enviar. Despues cambiamos
     * solo lo que necesitamos. */
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    /* Como compara la URI pedida con la de cada handler. Por defecto es strcmp
     * (exacta). Con wildcard, "/\*" coincide con cualquier cosa que empiece con
     * "/". Sin esto el handler de archivos no andaria (el profe no lo usa
     * porque registra cada archivo con su nombre exacto). */
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    /* lwIP admite 10 sockets en total (CONFIG_LWIP_MAX_SOCKETS). El servidor
     * usa 2 propios (el que escucha y uno de control interno) + uno por
     * navegador conectado. Con 4 navegadores quedan 4 para tu servidor TCP. */
    cfg.max_open_sockets = WEB_MAX_CONEXIONES;
    /* LRU = "Least Recently Used". Si se llenaron las conexiones y llega otra,
     * cierra la que hace mas tiempo no se usa en vez de rechazar la nueva.
     * Los navegadores dejan conexiones abiertas (keep-alive) y sin esto la
     * pagina deja de cargar. */
    cfg.lru_purge_enable = true;
    /* Lugar para tus handlers + 1 (el de archivos). Por defecto son 8: si
     * main.c pasa mas, se agranda. Si no hay lugar, el register falla. */
    if (cfg.max_uri_handlers < cantidad + 1) {
        cfg.max_uri_handlers = cantidad + 1;
    }
    /* El "identificador" del servidor, lo llena httpd_start. */
    httpd_handle_t server = NULL;
    /* PASO 3. Arranca: crea el socket que escucha en el puerto 80 y la tarea
     * httpd que hace accept/recv y llama a los handlers. Desde aca todo pasa
     * en ESA tarea, no en la que llamo a web_init. &server: se pasa la
     * direccion para que lo pueda escribir. */
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    /* PASO 4. Registrar los handlers de main.c. Cada httpd_uri_t dice:
     * .uri (ej. "/led"), .method (HTTP_GET o HTTP_POST), .handler (la funcion)
     * y .user_ctx (un puntero que te llega en req->user_ctx). El profe arma
     * un struct suelto por handler y los registra uno por uno; aca vienen
     * todos en un arreglo y se recorren con el for. &uris[i] = direccion del
     * elemento i. La libreria guarda una copia, asi que el arreglo de main.c
     * podria ser local. */
    for (size_t i = 0; i < cantidad; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }
    /* El de archivos va ULTIMO: el servidor prueba los handlers en orden de
     * registro y usa el primero que coincide. "/\*" coincide con todo, asi que
     * si fuera primero tapaba "/led" (buscaria el archivo "/spiffs/led").
     * static: no hace falta (se copia), pero queda claro que vive siempre.
     * Ojo: solo GET. Un POST a una URI sin handler da 405 (metodo no permitido). */
    static const httpd_uri_t archivos = {
        .uri     = "/*",
        .method  = HTTP_GET,
        .handler = archivo_handler,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &archivos));
    /* Aviso en el monitor. server_port quedo en 80 (el de HTTPD_DEFAULT_CONFIG). */
    ESP_LOGI(TAG, "Servidor HTTP listo en el puerto %d", cfg.server_port);
    /* Por si se quiere frenar despues con httpd_stop(server). */
    return server;
}
