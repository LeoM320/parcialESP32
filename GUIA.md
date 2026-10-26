# Guía del proyecto ParcialESP32

Esqueleto para el parcial: librerías finitas que **ponen en marcha** cada cosa (GPIO, ADC,
PWM, timer, NVS, WiFi, servidor web, sockets TCP, microrl, protocolo UNER) y un `main.c`
donde está **todo lo demás a la vista**: tus tareas, tus comandos y lo que hace la página web.

Para FreeRTOS (tareas, colas, mutex, event groups...) ver `GUIA_FREERTOS.md`; para C,
`GUIA_C.md`; para los diagramas, `GUIA_UML.md`.

---

## 1. Mapa

```
                         ESP32-S3
   Router ──WiFi── STA (IP del router, ej. 192.168.123.38)
   Celular ─WiFi── AP  "ESP32-Parcial" (192.168.4.1)
                    │
                    ├─ HTTP :80  ── web.c ── archivos de www/ (index.html, wifi.html, estilos.css)
                    │                     └─ tus handlers en main.c (/wifi, /destino)
                    │
                    ├─ TCP :1234 (servidor) ── task_tcp ── telnet ── microrl ──┐
                    │                                                          ├─► execute() (main.c)
                    ├─ UART0 (USB, COM6) ──── task_uart ──────────── microrl ──┘   (si CONSOLA_UART)
                    │
                    └─ TCP cliente ──► destino IP:puerto ── task_uner ── uner_rx_byte ──► procesar_uner()
                                                                         (si CLIENTE_UNER)

   USB (COM6): flashear, ver los logs (idf.py -p COM6 monitor) y, con CONSOLA_UART, la consola
```

Los módulos se prenden y apagan arriba de `main.c`:

```c
#define CONSOLA_UART	1	// la consola microrl tambien por UART
#define CLIENTE_UNER	1	// el ESP se conecta a destino IP:puerto y habla UNER
```

Con `0` el código de ese módulo no se compila (queda la consola TCP y la web solas).

| Archivo | Qué hace | Funciones |
|---|---|---|
| `hardware.h` | pines de la placa y tablas `leds[]`, `switches[]`, `pots[]` | — |
| `iodigital` | GPIO | `salida_init/set/get`, `entrada_init/get`, `entrada_interrupcion` |
| `ianalogica` | ADC1 | `analogica_init`, `analogica_get` (5 muestras, descarta la menor y la mayor y promedia el resto), `analogica_get_crudo` (1 lectura) |
| `pwm` | PWM (MCPWM) | `pwm_init`, `pwm_set_duty` (0–100 %), `pwm_set_pulso_us` (servos) |
| `rgb` | LED RGB integrado (WS2812, GPIO48) con el componente `led_strip` | `rgb_init`, `rgb_set(r, g, b)` (0–255; con 5–30 alcanza) |
| `temporizador` | interrupción periódica (gptimer) | `temporizador_init` |
| `memoria` | NVS: guardar y leer datos que sobreviven al reinicio | `memoria_init`, `memoria_get/set_str`, `memoria_get/set_u16`, `memoria_get/set_i32`, `memoria_get/set_blob`, `memoria_borrar` |
| `wifi` | AP + STA | `wifi_init`, `wifi_sta_conectar` |
| `web` | servidor HTTP + archivos de `www/` | `web_init`, `web_parametro`, `web_leer_cuerpo`, `web_campo` |
| `tcp` | sockets TCP | servidor: `tcp_servidor`, `tcp_aceptar`; cliente: `tcp_conectar`; los dos: `tcp_recibir`, `tcp_enviar` |
| `telnet` | que MobaXterm (Telnet) no duplique el eco | `telnet_inicio`, `telnet_filtrar` |
| `microrl` + `config.h` | línea de comandos (la del profe) | `microrl_init`, `microrl_set_execute_callback`, `microrl_insert_char` |
| `uner` | tramas del protocolo UNER | `uner_armar`, `uner_rx_init`, `uner_rx_byte`, `uner_escribir16`, `uner_escribir32`, `uner_escribir_float`, `uner_leer16`, `uner_leer32`, `uner_leer_float` |
| **`main.c`** | **todo tu programa** | `app_main`, `task_tcp`, `task_uart`, `task_uner`, `execute`, `enviar`, `procesar_uner`, `uner_mandar`, handlers web |
| `www/` | la página | `index.html` (vacía, para vos), `wifi.html` (red + destino UNER), `estilos.css` (vacía, ver sección 5) |

Cada `.h` tiene la documentación de sus funciones (ejemplos de comentarios Doxygen en
`practicas/ejemploDoxygen.h`).

### Ejemplos en `practicas/` (todos compilan con las librerías actuales)

| Archivo | Qué muestra |
|---|---|
| `implementacionUartMicrorl.c` | `main.c` completo: consola TCP + UART, comandos que reciben datos de cada tipo (entero, real, bool, texto, color, lista) y que mandan (`ver`, `json`), aviso a las dos consolas |
| `implementacionTcpUner.c` (+ `.html`) | `main.c` completo: cliente UNER de la válvula (Actividad 3) + ejemplos A1..A7 de mandar y recibir u8, u16, i16, u32, float, texto y mixto |
| `emulador_valvula.py` | la válvula en la PC, para probar UNER sin el equipo real (`python emulador_valvula.py`) |
| `ejemploNvs.c` | guardar int, float, bool, texto y struct; contador de arranques; `GET/POST /config` |
| `ejemploMicrorl.c`, `ejemploWeb.c`, `ejemploSetSend.c`, `ejemploWifiGet.c` | consola con cola y mutex, página con JSON, set/send, leer y escribir datos por la web |
| `practicaParcial1.c`, `practicaParcial1Uner.c` | el simulacro resuelto, sin y con UNER |
| `ejemploEstilos.css/.html`, `ejemploBootstrap.html`, `ejemploPropiedadesJs.html`, `elementos.html` | estilos propios, Bootstrap, cambiar colores y propiedades desde JS, todos los controles |
| `ejemploC.c`, `ejemploDoxygen.h` | repaso de C y comentarios Doxygen |
| `paginaPracticaParcial.c/.html`, `pruebas1.c` | piezas web del simulacro y tus pruebas |

Para usar uno: copiarlo entero sobre `main/main.c` (o solo la parte marcada `>>>`), compilar
y flashear. **Guardá antes tu `main.c`.**

---

## 2. El arranque (`app_main`)

```c
init_hardware();                    // salida_init de los LEDs, entrada_init de los SW, analogica_init de los potes

memoria_init();                     // NVS
memoria_get_str("ssid", ...);       // la red guardada (si nunca se guardó: la de menuconfig)
memoria_get_str("pass", ...);
wifi_init(AP_SSID, AP_PASS, ssid, pass);

mutexConsola = xSemaphoreCreateMutex();             // objetos de FreeRTOS ANTES de quien los use
#if CLIENTE_UNER
mutexDestino = xSemaphoreCreateMutex();
colaUner = xQueueCreate(10, sizeof(mensajeUner_t));
memoria_get_str("dest_ip", ...);    // el destino UNER guardado
memoria_get_u16("dest_port", ...);
#endif

web_init(uris, cantidad);           // servidor HTTP: tus handlers + archivos de www/

xTaskCreate(task_tcp, ...);         // consola por TCP            prioridad +2
xTaskCreate(task_uart, ...);        // consola por UART           +2  (si CONSOLA_UART)
xTaskCreate(task_uner, ...);        // cliente UNER               +3  (si CLIENTE_UNER)
// ... tus tareas
```

El orden importa en tres lugares: `memoria_init` antes de `wifi_init` (el WiFi usa la NVS),
`wifi_init` antes de `web_init` y las tareas (usan la red), y **mutex y colas antes** de las
tareas y de `web_init` (los handlers ya pueden llegar).

**Qué prioridad darle a cada tarea:** está la tabla en la cabecera de `main.c` (de +1 a +6
según qué tan urgente es) y las del sistema en `GUIA_FREERTOS.md`, sección 0.

---

## 3. WiFi y NVS

- **Al arrancar** se conecta a la red guardada en NVS (claves `"ssid"` y `"pass"`). Si nunca
  se guardó ninguna, usa la de `idf.py menuconfig` → *Configuracion WiFi*.
- **Siempre** levanta además su propio AP (`ESP32-Parcial`, clave `12345678`, IP
  `192.168.4.1`): si el router no está, conectate a esa red y entrá a `http://192.168.4.1`.
- **Cambiar de red:** en la página, *Configurar WiFi* → `wifi.html` → manda un POST a
  `/wifi`. El handler `wifi_post()` lo guarda en NVS y se conecta. La IP nueva sale en el
  monitor (`idf.py -p COM6 monitor`).

Guardar tus propios datos en NVS (ej. un umbral que no se pierda al reiniciar):

```c
uint16_t umbral = 2000;                     // 1. variable con su valor por defecto
memoria_get_u16("umbral", &umbral);         // 2. al arrancar: si no existe, queda el por defecto
...
umbral = nuevo;                             // 3. cuando el usuario lo cambia: validar y guardar
memoria_set_u16("umbral", umbral);
```

| Tipo | Guardar | Leer |
|---|---|---|
| texto | `memoria_set_str("nombre", nombre)` | `memoria_get_str("nombre", nombre, sizeof(nombre))` |
| 0..65535 (y `bool` como 0/1) | `memoria_set_u16("puerto", p)` | `memoria_get_u16("puerto", &p)` |
| `int` con signo | `memoria_set_i32("entero", n)` | `memoria_get_i32("entero", &n)` (`int32_t`) |
| `float`, `struct`, arreglo | `memoria_set_blob("cfg", &cfg, sizeof(cfg))` | `memoria_get_blob("cfg", &cfg, sizeof(cfg))` |
| borrar | `memoria_borrar("clave")` | |

- Las claves tienen hasta 15 caracteres. Ya usadas: `ssid`, `pass`, `dest_ip`, `dest_port`.
- Guardar **solo cuando el usuario cambia algo** (y si cambió): la flash se gasta y escribir
  tarda unos ms. Nunca en un lazo periódico ni desde una ISR.
- `idf.py flash` no borra lo guardado; `idf.py erase-flash` borra todo (también la red).
- Ejemplo completo y probado: `practicas/ejemploNvs.c`.

---

## 4. La consola (TCP y UART) y `execute()`

Lo que escribís en cualquiera de las dos consolas termina en **`execute(argc, argv)`** de
`main.c`, y vos contestás con **`enviar(texto)`**: la respuesta vuelve a la consola de donde
vino el comando.

### Conectarse

- **TCP:** MobaXterm → *Session → Telnet* → la IP del ESP32 → puerto `1234`. Un cliente a la vez.
- **UART** (`CONSOLA_UART 1`): opción A (la que viene), UART0 por el mismo USB: MobaXterm →
  *Serial* → COM6 a 115200, o `idf.py -p COM6 monitor`. Los logs (`ESP_LOG`, `printf`) salen
  mezclados con la consola. Opción B: UART1 en GPIO 17/18 con un CP2102 aparte (otro COM, sin
  logs); se elige en las constantes `UART_CONSOLA` de `main.c`.

Mientras algo tenga el COM6 abierto (monitor, MobaXterm), **no se puede flashear**.

### El camino de una tecla

```
task_tcp:  tcp_recibir() → telnet_filtrar() ─┐
task_uart: uart_read_bytes() ────────────────┤  toma mutexConsola y pone salida = enviar_tcp / enviar_uart
                                             ▼
                              microrl_insert_char()  (eco, edición, historial)
                                             │ al Enter
                                             ▼
                                     execute(argc, argv)   ← TUS COMANDOS
                                             │
                                             ▼
                                     enviar(texto) → salida(texto) → a la consola que escribió
```

Microrl separa la línea en palabras: `argv[0]` es el comando, `argc` cuántas palabras hay.
Cada consola tiene su propio `microrl_t` (`rl`, `rlUart`) y su función de eco
(`enviar_tcp`, `enviar_uart`).

### Agregar un comando

Hoy `execute()` trae solo los comandos de UNER (`destino`, `uner`, si `CLIENTE_UNER` es 1);
a todo lo demás contesta `ERROR: comando desconocido`. Tus comandos van antes de ese
mensaje. Este es un ejemplo de cómo quedaría con tres:

```c
int execute(int argc, const char *const *argv)
{
	char respuesta[64];

	if (strcmp(argv[0], "hola") == 0) {
		enviar("Hola\r\n");

	} else if (strcmp(argv[0], "led") == 0) {          // led <1-4> <on|off>
		if (argc != 3) {
			enviar("uso: led <1-4> <on|off>\r\n");
			return 0;
		}
		int n = atoi(argv[1]);
		if (n < 1 || n > LED_COUNT) {
			enviar("ERROR: no existe ese LED\r\n");
			return 0;
		}
		salida_set(leds[n - 1], strcmp(argv[2], "on") == 0);
		enviar("OK\r\n");

	} else if (strcmp(argv[0], "pote") == 0) {         // pote -> valor del pote 1
		snprintf(respuesta, sizeof(respuesta), "pote1 = %d\r\n", analogica_get(POT1_GPIO));
		enviar(respuesta);

	} else {
		snprintf(respuesta, sizeof(respuesta), "ERROR: comando desconocido: %s\r\n", argv[0]);
		enviar(respuesta);
	}
	return 0;
}
```

Reglas:
- `execute` **devuelve `int`** (así lo pide microrl): terminá siempre con `return 0`.
- Terminá las líneas con `"\r\n"` (las terminales lo esperan así).
- Para responder con números: `snprintf` a un buffer y `enviar(buf)`.
- Convertir argumentos: `atoi(argv[1])` (entero), `strtof(argv[1], NULL)` (float).
- **Validá `argc` antes de usar `argv[1]`**: si no existe, es un puntero inválido.
- Un comando que tarda (mover algo durante 3 s) **no** lo hace él: le manda una orden a
  una tarea por una cola y contesta al toque (`GUIA_FREERTOS.md`, patrón 14.B).

### Concurrencia

`execute()` corre en `task tcp` o en `task uart`, nunca las dos a la vez (las separa
`mutexConsola`). Si un comando modifica datos que también usa otra tarea o un handler web
(un `struct`, un arreglo, un contador), protegelos con **su** mutex
(`GUIA_FREERTOS.md`, sección 5).

---

## 5. La página web

### Cómo funciona

1. El navegador pide `/` → el servidor manda `www/index.html` (es un archivo).
2. Desde el HTML, `fetch('/algo?x=1')` pide una dirección sin recargar la página → la
   atiende **tu** handler en `main.c`, si está en la tabla `uris[]`.
3. Si no hay handler para esa dirección, se busca un archivo con ese nombre en `www/`
   (si no existe: 404).

Hoy `main.c` trae `wifi_post()` (`POST /wifi`, el formulario de `wifi.html`: guarda la red
en NVS y se conecta) y, si `CLIENTE_UNER` es 1, `destino_get()` / `destino_post()`
(`/destino`, la sección de destino UNER de `wifi.html`, que se oculta sola si no está).
`index.html` está vacía: el resto lo agregás vos.

**Estilos:** `www/estilos.css` está vacía y ninguna página la enlaza. Si los piden: pegar ahí
`practicas/ejemploEstilos.css` (sin internet) o usar Bootstrap remoto (instrucciones adentro
de `estilos.css`; necesita que el navegador tenga internet, en el AP del ESP no anda), y
agregar `<link rel="stylesheet" href="/estilos.css">` en el `<head>` de cada página. Cambiar
colores y clases desde JS: `practicas/ejemploPropiedadesJs.html`.

```
index.html / wifi.html               main.c
──────────────────────               ──────
fetch('/algo?x=1')       ──►  GET /algo?x=1  ──►  algo_get()   (lo escribís vos)
<form action="/wifi">    ──►  POST /wifi     ──►  wifi_post(): NVS + conectar
```

Los archivos de `www/` se graban en la flash con `idf.py flash` (partición SPIFFS). Si
cambiás un `.html`, hay que volver a flashear. Más rápido: `idf.py -p COM6 storage-flash`
graba solo `www/` y `idf.py -p COM6 app-flash` solo el programa.

### Agregar algo a la página

Siempre son tres pasos: **handler** en `main.c` + **línea en `uris[]`** (y su prototipo) +
**HTML** que lo llame. Ejemplos de las dos formas típicas:

**Controlar algo** (la página manda un valor):

```c
// main.c: GET /led?n=2&v=1
esp_err_t led_get(httpd_req_t *req)
{
	char n[4], v[4];
	if (!web_parametro(req, "n", n, sizeof(n)) || !web_parametro(req, "v", v, sizeof(v))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Faltan n y v");
	}
	int i = atoi(n) - 1;                       // LED 1 = leds[0]
	if (i < 0 || i >= LED_COUNT) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No existe ese LED");
	}
	salida_set(leds[i], strcmp(v, "1") == 0);
	return httpd_resp_sendstr(req, "OK");
}

// en la tabla uris[]:
	{ .uri = "/led", .method = HTTP_GET, .handler = led_get },
```

```html
<!-- www/index.html -->
<p>LED 2 <button onclick="fetch('/led?n=2&v=1')">Prender</button>
         <button onclick="fetch('/led?n=2&v=0')">Apagar</button></p>
```

**Ver datos** (la página pregunta cada tanto):

```c
// main.c: GET /estado -> texto
esp_err_t estado_get(httpd_req_t *req)
{
	char txt[128];
	int largo = 0;
	// cada snprintf escribe a continuacion del anterior (txt + largo)
	largo += snprintf(txt + largo, sizeof(txt) - largo, "Pote 1: %d\n", analogica_get(POT1_GPIO));
	largo += snprintf(txt + largo, sizeof(txt) - largo, "SW 1: %s\n",
					  entrada_get(SW1_GPIO) ? "suelto" : "apretado");
	httpd_resp_set_type(req, "text/plain");
	return httpd_resp_sendstr(req, txt);
}

// en la tabla uris[]:
	{ .uri = "/estado", .method = HTTP_GET, .handler = estado_get },
```

```html
<!-- www/index.html -->
<pre id="estado">...</pre>
<script>
function actualizar() {
  fetch('/estado')
    .then(r => r.text())
    .then(t => document.getElementById('estado').textContent = t)
    .catch(() => document.getElementById('estado').textContent = 'Sin conexion');
}
setInterval(actualizar, 1000);   // cada 1 s
</script>
```

Para mandar un número escrito: `<input id="umbral"> <button
onclick="fetch('/umbral?v=' + umbral.value)">Enviar</button>`, y en el handler
`web_parametro(req, "v", ...)` + `atoi`.

Funciones útiles dentro de un handler:

| Función | Para qué |
|---|---|
| `web_parametro(req, "n", buf, tam)` | leer `?n=...` de la URL |
| `web_leer_cuerpo(req, buf, tam)` + `web_campo(buf, "x", ...)` | leer un formulario POST |
| `httpd_resp_sendstr(req, "texto")` | responder (200 OK) |
| `httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "motivo")` | responder con error |
| `httpd_resp_set_type(req, "text/plain")` | tipo de la respuesta (antes de mandarla) |

Reglas:
- El handler corre en la **tarea del servidor HTTP**: tiene que responder rápido (nada de
  esperas largas) y, si toca datos que usan tus tareas, con mutex.
- Si el handler no responde nada, el navegador queda esperando: **siempre** un
  `httpd_resp_...`.
- La tabla `uris[]` puede tener los handlers que quieras: `web_init()` agranda solo el
  límite del servidor (`max_uri_handlers`).

### Datos en JSON (cJSON)

El profe pide JSON en los dos sentidos. En C se usa **cJSON** (componente
`espressif/cjson`, ya está en `main/idf_component.yml`; en ESP-IDF 6 no viene incluido).

```
pagina                                          ESP32
──────                                          ─────
fetch('/estado') .then(r => r.json())  ◄──  cJSON_CreateObject + Add... + PrintUnformatted
fetch('/config', {method:'POST',        ──►  web_leer_cuerpo + cJSON_Parse + GetObjectItem
  body: JSON.stringify({...})})
```

**La regla de oro de la memoria:** todo lo que crea cJSON (`cJSON_Parse`,
`cJSON_CreateObject`) se libera con `cJSON_Delete(raiz)` (una vez, la raíz: se lleva todo
lo de adentro). El texto de `cJSON_PrintUnformatted` se libera con `free()`. Si te olvidás,
cada pedido pierde memoria y la placa termina reiniciándose.

#### Dos ayudas (ya están al final de `main.c`)

```c
#include "cJSON.h"

/* Manda el objeto como respuesta JSON y libera todo */
esp_err_t responder_json(httpd_req_t *req, cJSON *obj)
{
	char *texto = cJSON_PrintUnformatted(obj);      // objeto -> texto {"a":1,...} (reserva memoria)
	cJSON_Delete(obj);                              // el objeto ya no hace falta
	if (texto == NULL) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sin memoria");
	}
	httpd_resp_set_type(req, "application/json");
	esp_err_t err = httpd_resp_sendstr(req, texto);
	free(texto);                                    // el texto tambien se libera
	return err;
}

/* Lee el cuerpo del pedido y lo convierte en objeto (NULL si no es JSON valido).
 * Quien la llama tiene que hacer cJSON_Delete del resultado. */
cJSON *leer_json(httpd_req_t *req)
{
	char cuerpo[256];
	if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0) {
		return NULL;                                // no entra o se corto
	}
	return cJSON_Parse(cuerpo);                     // NULL si esta mal escrito
}
```

#### ESP → página: armar JSON

```c
/* GET /estado ->
 * {"pot1":1971,"volt":1.49,"led1":true,"nombre":"ESP32","leds":[1,0,1,1],
 *  "config":{"umbral":2000,"periodo":500,"modo":"auto"}} */
esp_err_t estado_get(httpd_req_t *req)
{
	int p1 = analogica_get(POT1_GPIO);

	cJSON *raiz = cJSON_CreateObject();                             // {}
	cJSON_AddNumberToObject(raiz, "pot1", p1);                      // entero
	cJSON_AddNumberToObject(raiz, "volt", p1 * 3.1 / 4095);         // float (es el mismo "Number")
	cJSON_AddBoolToObject(raiz, "led1", salida_get(LED1_GPIO));     // true / false
	cJSON_AddStringToObject(raiz, "nombre", "ESP32");               // texto

	cJSON *arr = cJSON_AddArrayToObject(raiz, "leds");              // arreglo []
	for (int i = 0; i < LED_COUNT; i++) {
		cJSON_AddItemToArray(arr, cJSON_CreateNumber(salida_get(leds[i])));
	}

	config_t copia;                                                 // struct compartido: copiar con mutex
	xSemaphoreTake(mutexConfig, portMAX_DELAY);
	copia = config;
	xSemaphoreGive(mutexConfig);
	cJSON *cfg = cJSON_AddObjectToObject(raiz, "config");           // objeto adentro de objeto
	cJSON_AddNumberToObject(cfg, "umbral", copia.umbral);
	cJSON_AddNumberToObject(cfg, "periodo", copia.periodo_ms);
	cJSON_AddStringToObject(cfg, "modo", copia.modo);

	return responder_json(req, raiz);                               // manda y libera
}

/* GET /historial -> [{"n":0,"pot1":1970},{"n":1,"pot1":1972},...]  (arreglo de objetos) */
esp_err_t historial_get(httpd_req_t *req)
{
	cJSON *lista = cJSON_CreateArray();                             // la raiz puede ser un arreglo
	for (int i = 0; i < 3; i++) {
		cJSON *item = cJSON_CreateObject();
		cJSON_AddNumberToObject(item, "n", i);
		cJSON_AddNumberToObject(item, "pot1", analogica_get_crudo(POT1_GPIO));
		cJSON_AddItemToArray(lista, item);                          // el arreglo se adueña del item
	}
	return responder_json(req, lista);
}
```

En la página:

```js
fetch('/estado')
	.then(r => r.json())
	.then(d => {
		pot1.textContent   = d.pot1;                  // numero
		volt.textContent   = d.volt.toFixed(2);       // float con 2 decimales
		led1.textContent   = d.led1 ? 'ON' : 'OFF';   // bool
		nombre.textContent = d.nombre;                // texto
		led3.textContent   = d.leds[2];               // arreglo: posicion 2 = LED 3
		umbral.textContent = d.config.umbral;         // objeto adentro de objeto
	});

fetch('/historial')
	.then(r => r.json())
	.then(lista => lista.forEach(x => console.log(x.n, x.pot1)));   // recorrer un arreglo
```

(`pot1.textContent` es un atajo de `document.getElementById('pot1').textContent`: el
navegador crea una variable por cada `id`.)

#### Página → ESP: leer JSON

```js
function enviar(direccion, objeto) {
	return fetch(direccion, {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		body: JSON.stringify(objeto)                  // objeto -> texto
	}).then(r => r.json());
}

enviar('/led',    { n: 2, v: true });                           // numero + bool
enviar('/config', { umbral: 1500, periodo: 250, modo: 'manual' });  // numero + texto
enviar('/config', { umbral: Number(caja.value) });              // de un <input id="caja">: convertir!
enviar('/leds',   { leds: [1, 0, 0, 1] });                      // arreglo
enviar('/red',    { wifi: { ssid: 'MiRed', pass: '1234' } });   // objeto anidado
```

```c
/* POST /led  {"n":2,"v":true}  (numero + bool, con validacion) */
esp_err_t led_post(httpd_req_t *req)
{
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *n = cJSON_GetObjectItem(raiz, "n");      // NULL si no esta
	cJSON *v = cJSON_GetObjectItem(raiz, "v");
	if (!cJSON_IsNumber(n) || !cJSON_IsBool(v) || n->valueint < 1 || n->valueint > LED_COUNT) {
		cJSON_Delete(raiz);                         // ¡tambien al salir por error!
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Faltan n (1-4) y v (true/false)");
	}
	salida_set(leds[n->valueint - 1], cJSON_IsTrue(v));
	cJSON_Delete(raiz);
	return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* POST /config  {"umbral":1500,"periodo":250,"modo":"manual"}
 * Cada campo es opcional: se cambia solo lo que vino. Escribe un struct compartido -> mutex. */
esp_err_t config_post(httpd_req_t *req)
{
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *umbral  = cJSON_GetObjectItem(raiz, "umbral");
	cJSON *periodo = cJSON_GetObjectItem(raiz, "periodo");
	cJSON *modo    = cJSON_GetObjectItem(raiz, "modo");

	xSemaphoreTake(mutexConfig, portMAX_DELAY);
	if (cJSON_IsNumber(umbral))  { config.umbral = umbral->valueint; }       // valuedouble si es float
	if (cJSON_IsNumber(periodo)) { config.periodo_ms = periodo->valueint; }
	if (cJSON_IsString(modo)) {                                              // texto: copiar, no guardar el puntero
		strncpy(config.modo, modo->valuestring, sizeof(config.modo) - 1);
		config.modo[sizeof(config.modo) - 1] = '\0';
	}
	xSemaphoreGive(mutexConfig);

	cJSON_Delete(raiz);
	return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* POST /leds  {"leds":[1,0,0,1]}  (arreglo) */
esp_err_t leds_post(httpd_req_t *req)
{
	cJSON *raiz = leer_json(req);
	if (raiz == NULL) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
	}
	cJSON *arr = cJSON_GetObjectItem(raiz, "leds");
	if (!cJSON_IsArray(arr)) {
		cJSON_Delete(raiz);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta leds: [..]");
	}
	int cant = cJSON_GetArraySize(arr);
	for (int i = 0; i < cant && i < LED_COUNT; i++) {            // no pasarse de los LEDs que hay
		cJSON *item = cJSON_GetArrayItem(arr, i);
		if (cJSON_IsNumber(item)) {
			salida_set(leds[i], item->valueint != 0);
		}
	}
	cJSON_Delete(raiz);
	return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* POST /red  {"wifi":{"ssid":"MiRed","pass":"1234"}}  (objeto anidado) */
	cJSON *wifi = cJSON_GetObjectItem(raiz, "wifi");    // primero el de afuera
	cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");    // despues el de adentro (si wifi es NULL, da NULL)
	if (cJSON_IsString(ssid)) { ... ssid->valuestring ... }
```

Los datos compartidos de los ejemplos:

```c
typedef struct {
	int  umbral;
	int  periodo_ms;
	char modo[16];
} config_t;

config_t config = { 2000, 500, "auto" };
SemaphoreHandle_t mutexConfig;          // en app_main: mutexConfig = xSemaphoreCreateMutex();
```

| Para | Armar (ESP → página) | Leer (página → ESP) |
|---|---|---|
| objeto `{}` | `cJSON_CreateObject()` | `cJSON_Parse(texto)` |
| número (int o float) | `cJSON_AddNumberToObject(o, "k", x)` | `cJSON_IsNumber(i)` → `i->valueint` / `i->valuedouble` |
| bool | `cJSON_AddBoolToObject(o, "k", b)` | `cJSON_IsBool(i)` → `cJSON_IsTrue(i)` |
| texto | `cJSON_AddStringToObject(o, "k", "txt")` | `cJSON_IsString(i)` → `i->valuestring` (copiarlo) |
| arreglo `[]` | `cJSON_AddArrayToObject(o, "k")` + `cJSON_AddItemToArray(a, cJSON_CreateNumber(x))` | `cJSON_IsArray(a)`, `cJSON_GetArraySize(a)`, `cJSON_GetArrayItem(a, i)` |
| objeto adentro | `cJSON_AddObjectToObject(o, "k")` | `cJSON_GetObjectItem(o, "k")` dos veces |
| campo por nombre | — | `cJSON_GetObjectItem(o, "k")` (NULL si no está) |
| a texto / liberar | `cJSON_PrintUnformatted(o)` + `free()` | `cJSON_Delete(raiz)` |

Reglas:
- Siempre **validar el tipo** (`cJSON_IsNumber`...) antes de usar `valueint`: si el campo
  no vino, `GetObjectItem` da NULL y `n->valueint` reinicia la placa.
- `cJSON_Delete` en **todos** los caminos de salida, también en los de error.
- `valuestring` desaparece con el `cJSON_Delete`: copialo (`strncpy`) si lo necesitás después.
- Desde un `<input>`, JavaScript manda texto: `Number(caja.value)` para que llegue como número.
- Los floats salen con todos los decimales (`"volt":0.891013431013431`): redondealos en la
  página con `d.volt.toFixed(2)`.
- `httpd_resp_send_err` responde **texto**, no JSON: en la página `r.json()` falla con un
  error. Si importa, mirá `r.ok` antes (`r.ok ? r.json() : ...`).
- Probado en la placa (2026-10-25): los 6 ejemplos, los casos de error y 300 pedidos
  seguidos sin perder memoria.
- Para probar sin página: abrí `http://IP/estado` en el navegador (se ve el JSON), o desde la
  consola del navegador (F12): `enviar('/led', {n:1, v:true})`.

### Elementos de la página (HTML + JS)

Todos siguen el mismo esquema: **el HTML pone el control**, **el JS lee su valor** y lo
manda con `enviar()` (ver arriba), y **el handler en C lo lee del JSON**. Cada elemento
lleva un `id`; en el JS, `caja.value` es un atajo de `document.getElementById('caja').value`.

Hay una página de práctica con todos estos ejemplos en `practicas/elementos.html`: se
abre con doble clic en la PC (sin la placa) y muestra el JSON que mandaría cada control.

#### Mostrar datos

```html
<h2>Titulo</h2>                          <!-- titulos: h1 (el mas grande) .. h6 -->
<p>Pote 1: <span id="pot1">?</span></p>  <!-- p = parrafo; span = un pedazo para cambiar -->
<progress id="barra" max="4095"></progress>               <!-- barra de 0 a max -->
<meter id="nivel" min="0" max="4095" low="1000" high="3000"></meter>  <!-- cambia de color -->
<pre id="log"></pre>                     <!-- texto tal cual (respeta los saltos de linea) -->
```

```js
pot1.textContent = d.pot1;                   // escribir texto
barra.value = d.pot1;                        // mover la barra
nivel.value = d.pot1;
log.textContent += 'llego ' + d.pot1 + '\n'; // agregar una linea
pot1.style.color = d.pot1 > 2000 ? 'red' : 'green';   // color segun el valor
```

#### Botones

```html
<button onclick="enviar('/led', {n: 2, v: true})">ON</button>
<button id="btnGuardar" onclick="guardar()">Guardar</button>
```

```js
btnGuardar.disabled = true;      // deshabilitarlo (gris, no se puede apretar)
btnGuardar.textContent = 'Guardando...';   // cambiarle el texto
```

#### Caja de número o texto

```html
<input id="umbral" type="number" min="0" max="4095" value="2000">
<input id="nombre" type="text" maxlength="15" placeholder="Nombre">
<button onclick="enviar('/config', {umbral: Number(umbral.value), nombre: nombre.value})">Guardar</button>
```

`.value` **siempre es texto**: `Number(...)` para mandarlo como número (si no,
`cJSON_IsNumber` lo rechaza). En C: `umbral->valueint`, `nombre->valuestring` (copiar con
`strncpy`).

#### Slider (deslizador), por ejemplo el brillo de un PWM

```html
<input id="brillo" type="range" min="0" max="100" value="0"
       oninput="brilloTxt.textContent = brillo.value"
       onchange="enviar('/pwm', {duty: Number(brillo.value)})">
<span id="brilloTxt">0</span> %
```

- `oninput`: **mientras** lo movés (actualizar el número en pantalla).
- `onchange`: **al soltarlo** (mandar una sola vez; con `oninput` mandaría decenas de
  pedidos por segundo y saturaría el ESP).

```c
cJSON *duty = cJSON_GetObjectItem(raiz, "duty");
if (cJSON_IsNumber(duty)) { pwm_set_duty(LED_PWM_GPIO, duty->valuedouble); }
```

#### Checkbox (casilla): habilitar o deshabilitar algo

```html
<label><input id="habilitar" type="checkbox" onchange="enviar('/config', {habilitado: habilitar.checked})"> Alarma habilitada</label>
```

`.checked` es `true` o `false` (un bool de verdad: llega como `cJSON_IsBool`). El `<label>`
hace que también se pueda tocar el texto.

```c
cJSON *h = cJSON_GetObjectItem(raiz, "habilitado");
if (cJSON_IsBool(h)) {
	if (cJSON_IsTrue(h)) { xEventGroupSetBits(eventos, EV_HABILITADO); }    // bit de estado
	else                 { xEventGroupClearBits(eventos, EV_HABILITADO); }
}
```

#### Lista desplegable (select): elegir un modo

```html
<select id="modo" onchange="enviar('/modo', {modo: Number(modo.value)})">
	<option value="0">Apagado</option>
	<option value="1">Fijo</option>
	<option value="2">Parpadeo</option>
</select>
```

`value` es lo que se manda (conviene un número: va directo a un `enum` en C);
el texto de adentro es lo que ve el usuario.

```c
typedef enum { MODO_APAGADO, MODO_FIJO, MODO_PARPADEO, MODO_CANTIDAD } modo_t;
cJSON *m = cJSON_GetObjectItem(raiz, "modo");
if (cJSON_IsNumber(m) && m->valueint >= 0 && m->valueint < MODO_CANTIDAD) {
	xTaskNotify(hModo, m->valueint, eSetValueWithOverwrite);    // ej: avisar a la tarea
}
```

#### Botones de opción (radio): una de varias, todas a la vista

```html
<label><input type="radio" name="vel" value="100" onchange="enviar('/vel', {ms: 100})"> Rapido</label>
<label><input type="radio" name="vel" value="500" onchange="enviar('/vel', {ms: 500})" checked> Normal</label>
<label><input type="radio" name="vel" value="1000" onchange="enviar('/vel', {ms: 1000})"> Lento</label>
```

Los que tienen el mismo `name` son un grupo: solo uno puede estar marcado.

#### Selector de color: el LED RGB

```html
<input id="color" type="color" value="#000000" onchange="mandarColor()">
```

```js
function mandarColor() {
	const c = color.value;                        // "#ff8000"
	enviar('/rgb', {
		r: parseInt(c.substr(1, 2), 16),          // "ff" -> 255  (16 = hexadecimal)
		g: parseInt(c.substr(3, 2), 16),          // "80" -> 128
		b: parseInt(c.substr(5, 2), 16)           // "00" -> 0
	});
}
```

```c
cJSON *r = cJSON_GetObjectItem(raiz, "r");    // igual g y b
if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
	rgb_set(r->valueint / 8, g->valueint / 8, b->valueint / 8);    // /8: 255 encandila
}
```

#### Tabla armada desde un arreglo (historial, lista de lecturas)

```html
<table border="1">
	<thead><tr><th>N</th><th>Pote 1</th></tr></thead>
	<tbody id="tabla"></tbody>
</table>
```

```js
fetch('/historial')                               // [{"n":0,"pot1":1970},{"n":1,"pot1":1972}]
	.then(r => r.json())
	.then(lista => {
		tabla.innerHTML = '';                     // borrar las filas viejas
		lista.forEach(x => {
			tabla.innerHTML += '<tr><td>' + x.n + '</td><td>' + x.pot1 + '</td></tr>';
		});
	});
```

`innerHTML` escribe **HTML** (crea elementos); `textContent` escribe **texto** (lo muestra
tal cual). Para un número, `textContent`; para armar filas, `innerHTML`.

#### Mostrar u ocultar una parte

```html
<div id="alarma" style="display: none; color: red">¡ALARMA!</div>
```

```js
alarma.style.display = d.pot1 > d.umbral ? 'block' : 'none';   // block = se ve, none = oculto
```

`<div>` es una caja para agrupar cosas (como `<p>`, pero sin significado de párrafo).

#### Que los controles arranquen con los valores reales

Si la página carga con `value="2000"` fijo pero el ESP tiene otro umbral, el control
miente. Al cargar, pedir la configuración y ponerla en los controles:

```js
fetch('/estado')                    // o un GET /config aparte
	.then(r => r.json())
	.then(d => {
		umbral.value = d.umbral;          // caja de numero
		habilitar.checked = d.habilitado; // checkbox
		modo.value = d.modo;              // select: elige la opcion con ese value
	});
```

Va **una sola vez** al final del `<script>` (no dentro del `setInterval`: si no, te pisa lo
que estás escribiendo cada medio segundo).

#### Resumen

| Elemento | HTML | Leer en JS | Llega a C como |
|---|---|---|---|
| texto a mostrar | `<span id="x">` | `x.textContent = ...` | — |
| barra | `<progress id="x" max="4095">` | `x.value = ...` | — |
| botón | `<button onclick="...">` | — | lo que mande `enviar()` |
| número | `<input type="number">` | `Number(x.value)` | `cJSON_IsNumber` → `valueint` |
| texto | `<input type="text">` | `x.value` | `cJSON_IsString` → `valuestring` |
| slider | `<input type="range">` | `Number(x.value)` + `onchange` | `valueint` / `valuedouble` |
| checkbox | `<input type="checkbox">` | `x.checked` | `cJSON_IsBool` → `cJSON_IsTrue` |
| select | `<select><option value="0">` | `Number(x.value)` | `valueint` → `enum` |
| radio | `<input type="radio" name="g">` | el `value` del marcado | `valueint` |
| color | `<input type="color">` | `x.value` → `parseInt(..., 16)` | 3 números r, g, b |
| tabla | `<tbody id="x">` | `x.innerHTML += '<tr>...'` | — (viene de un arreglo JSON) |
| ocultar | `<div id="x">` | `x.style.display = 'none'` | — |

Eventos: `onclick` (botón), `onchange` (al terminar de cambiar: checkbox, select, slider
al soltar), `oninput` (mientras se escribe o se mueve).

---

## 6. Recetas

### A. Timer de hardware: hacer algo cada X µs exactos

La ISR solo avisa; una tarea hace el trabajo (`temporizador.h`).

```c
TaskHandle_t taskMuestreo;

bool IRAM_ATTR al_vencer(void *arg)                 // ISR: corta, solo ...FromISR
{
	BaseType_t despertada = pdFALSE;
	vTaskNotifyGiveFromISR(taskMuestreo, &despertada);
	return despertada == pdTRUE;
}

void task_muestreo(void *pvParam)
{
	while (1) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);    // espera el aviso del timer
		int pot = analogica_get_crudo(POT1_GPIO);   // el ADC se lee acá, no en la ISR
		// ...
	}
}

// app_main:
xTaskCreate(task_muestreo, "task muestreo", 4096, NULL, tskIDLE_PRIORITY + 5, &taskMuestreo);
temporizador_init(10000, al_vencer, NULL);          // cada 10 ms (DESPUÉS de crear la tarea)
```

Hay 4 timers de hardware (`TEMPORIZADOR_MAX`). Para algo cada ≥ 10 ms alcanza con una
tarea y `xTaskDelayUntil`.

### B. Pulsador por interrupción (con antirrebote)

```c
#define ANTIRREBOTE_MS 50
QueueHandle_t colaPulsos;

void IRAM_ATTR al_apretar(void *arg)                // ISR: manda a la cola qué pin fue
{
	gpio_num_t pin = (gpio_num_t)(int)arg;
	BaseType_t despertada = pdFALSE;
	xQueueSendFromISR(colaPulsos, &pin, &despertada);
	if (despertada) {
		portYIELD_FROM_ISR();
	}
}

void task_pulsos(void *pvParam)
{
	bool suelto[GPIO_NUM_MAX];                      // último estado confirmado de cada pin
	for (int i = 0; i < GPIO_NUM_MAX; i++) {
		suelto[i] = true;
	}
	gpio_num_t pin;
	while (1) {
		xQueueReceive(colaPulsos, &pin, portMAX_DELAY);
		vTaskDelay(pdMS_TO_TICKS(ANTIRREBOTE_MS));  // esperar que termine el rebote
		xQueueReset(colaPulsos);                    // descartar los flancos del rebote
		bool nivel = entrada_get(pin);
		if (nivel == suelto[pin]) {
			continue;                               // mismo estado que antes: era rebote
		}
		suelto[pin] = nivel;
		if (!nivel && pin == SW1_GPIO) {            // SW1 recién apretado
			salida_set(LED2_GPIO, !salida_get(LED2_GPIO));
		}
	}
}

// app_main (después de init_hardware, que configura los SW como entrada con pull-up):
colaPulsos = xQueueCreate(10, sizeof(gpio_num_t));  // ANTES de habilitar la interrupción
xTaskCreate(task_pulsos, "task pulsos", 4096, NULL, tskIDLE_PRIORITY + 5, NULL);
entrada_interrupcion(SW1_GPIO, GPIO_INTR_ANYEDGE, al_apretar, (void *)SW1_GPIO);
entrada_interrupcion(SW2_GPIO, GPIO_INTR_ANYEDGE, al_apretar, (void *)SW2_GPIO);  // misma ISR
```

Por qué así (probado en la placa): un pulsador rebota **al apretar y al soltar**. Filtrando
solo por tiempo contó 9 en 5 apretadas; confirmando el nivel sin recordar el estado, 7 en 5;
con estado por pin y `ANYEDGE`, 5 en 5.

### C. PWM (brillo de un LED, servo)

```c
// init_hardware: el LED 1 como PWM en vez de salida digital (un pin no puede ser las dos cosas)
pwm_init(LED_PWM_GPIO, LED_PWM_FREC_HZ);            // 1000 Hz
pwm_set_duty(LED_PWM_GPIO, 37.5);                   // 0..100 %

// servo SG90: 50 Hz, pulso de 500 a 2500 us
pwm_init(GPIO_NUM_15, 50);
pwm_set_pulso_us(GPIO_NUM_15, 1500);                // centro
```

### D. Mandar algo a la consola sin que lo pida

`enviar()` es **solo para responder adentro de `execute()`** (manda a la consola que
escribió el comando). Desde otra tarea (una alarma, un aviso):

- Solo al cliente TCP: `enviar_tcp("ALARMA: pote sobre el umbral\r\n");` (si no hay nadie
  conectado no hace nada).
- A las dos consolas: copiar `consola_avisar()` de `practicas/implementacionUartMicrorl.c`
  (toma `mutexConsola` para no cortar un comando a medio responder y manda a TCP y UART).
  **No llamarla desde `execute()`**: el mutex ya está tomado y se traba. Si un comando tiene
  que disparar un aviso, que le avise a la tarea (task notify), como el comando `avisar` de
  ese ejemplo.

### E. Una UART (otro dispositivo)

La consola UART de `main.c` es para una persona con una terminal. Para hablar con **otro
dispositivo** (otro ESP32, un módulo) se usa el driver de ESP-IDF directo (`driver/uart.h`,
ya incluido; `esp_driver_uart` ya está en `PRIV_REQUIRES`). Ejemplo completo, con cola de
eventos: `GUIA_FREERTOS.md`, sección 12. Usá UART1 en pines libres (TX 17, RX 18): **son los
mismos que la opción B de la consola UART**, no usar las dos cosas a la vez. Para probar sin
otro dispositivo, un cable de GPIO 17 a GPIO 18.

### F. Cliente UNER (otro equipo por TCP)

Con `CLIENTE_UNER 1`, `task_uner` se conecta al destino (`wifi.html`, comando
`destino <ip> <puerto>` o lo guardado en NVS) y reintenta sola si se corta.

```
Trama:  'U' 'N' 'E' 'R' | LEN | ':' | carga[0] = comando, carga[1..] = datos | XOR
        LEN = largo de la carga + 1. Números de varios bytes: little-endian (primero el byte bajo).
```

- **Recibir:** cada trama válida llega a `procesar_uner(carga, n)` en `main.c`: un `switch`
  por `carga[0]` y los datos con `uner_leer16`, `uner_leer32` o `uner_leer_float(&carga[i])`.
- **Mandar:** desde cualquier tarea o comando, `uner_mandar(carga, n)` (la encola; la manda
  `task_uner`, la única que escribe en el socket). Armar la carga con
  `uner_escribir16`, `uner_escribir32` o `uner_escribir_float(&carga[i], valor)`.
- **Probar:** consola `uner D1 2A` manda una trama; `destino` muestra el estado y la última
  trama recibida. Sin el equipo real: `python practicas/emulador_valvula.py` en la PC.
- `UNER_PERIODO_MS`, `UNER_CMD_PERIODICO` y `UNER_LATIDO_MS` (constantes de `main.c`): pedir
  algo cada X ms y marcar falla si no responde. Ejemplos de todos los tipos de dato y la
  válvula completa: `practicas/implementacionTcpUner.c`.

**¿Se mezcla con la consola TCP?** No. La consola es un **servidor** (el ESP escucha en su
puerto 1234) y UNER es un **cliente** (el ESP se conecta a la PC). Son dos sockets, dos
tareas y dos buffers; aunque el destino también use el puerto 1234, no chocan (probado).

---

## 7. Problemas comunes

| Síntoma | Causa | Solución |
|---|---|---|
| `idf.py flash` falla en "Serial port COM6" | el monitor o MobaXterm tiene el COM6 abierto | cerrarlo y volver a flashear |
| La página no carga | el ESP32 cambió de IP o no está conectado | ver la IP en el monitor, o usar el AP (`192.168.4.1`) |
| Cambié el HTML y no se ve | `www/` se graba al flashear | `idf.py -p COM6 flash`; en el navegador Ctrl+F5 |
| MobaXterm no muestra nada | ya hay otro cliente TCP conectado | cerrar el otro (un cliente a la vez; uno colgado se libera en ~11 s) |
| MobaXterm muestra todo doble | sesión *Raw* en vez de *Telnet* | usar *Telnet* |
| Un comando no responde | `execute()` no llamó a `enviar()` en ese camino | revisar todos los `if` |
| `Guru Meditation` al usar una cola | la cola se usó antes de crearla | crear colas/semáforos antes de las tareas e interrupciones |
| `stack overflow in task` | stack chico (`printf`, `snprintf`) | 4096 o más |
| `fatal error: X.h: No such file` | falta el componente en `main/CMakeLists.txt` | agregarlo a `PRIV_REQUIRES` (ej. `esp_timer`) |
| Copié un ejemplo sobre `main.c` y sigue corriendo lo viejo | la copia conservó la fecha vieja y `idf.py build` no recompiló | abrir `main.c`, guardarlo y compilar de nuevo (o `idf.py fullclean`) |
| UNER no conecta (`destino` dice `conectado: false`) | IP/puerto mal, el programa de la PC no está escuchando o el firewall de Windows lo bloquea | revisar con `destino`; permitir Python/el programa en el firewall |
| La consola UART muestra logs mezclados | opción A: UART0 es la misma del monitor | es normal; para separarlos, opción B (UART1 + CP2102) |
| Se traba la consola al avisar | se llamó `consola_avisar()` desde `execute()` | avisar a otra tarea (task notify) y que ella mande |

---

## 8. Límites

| Qué | Valor | Dónde se cambia |
|---|---|---|
| Clientes TCP a la vez | 1 | `task_tcp` en `main.c` |
| Puerto TCP | 1234 | `PUERTO_TCP` en `main.c` |
| Largo de un comando | 100 caracteres | `_COMMAND_LINE_LEN` en `config.h` |
| Palabras por comando | 8 | `_COMMAND_TOKEN_NMB` en `config.h` |
| Navegadores a la vez | 4 | `WEB_MAX_CONEXIONES` en `web.c` |
| Timers de hardware | 4 | `TEMPORIZADOR_MAX` |
| Muestras que toma `analogica_get` (descarta la menor y la mayor) | 5 (tarda 50 ms, minimo 3) | `ANALOGICA_MUESTRAS` en `ianalogica.h` |
| Carga de una trama UNER | 64 bytes | `UNER_MAX_CARGA` en `uner.h` |
| Tramas UNER esperando para salir | 10 | `xQueueCreate(10, ...)` de `colaUner` en `main.c` |
| Espera de `tcp_conectar` | 3 s | `CONECTAR_ESPERA_MS` en `tcp.c` |
| Clave de NVS | 15 caracteres | (fijo de ESP-IDF) |
