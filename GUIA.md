# Guía del proyecto ParcialESP32

Esqueleto para el parcial: librerías finitas que **ponen en marcha** cada cosa (GPIO, ADC,
PWM, timer, NVS, WiFi, servidor web, sockets TCP, microrl) y un `main.c` donde está
**todo lo demás a la vista**: tus tareas, tus comandos y lo que hace la página web.

Para FreeRTOS (tareas, colas, mutex, event groups...) ver `GUIA_FREERTOS.md`.

---

## 1. Mapa

```
                         ESP32-S3
   Router ──WiFi── STA (IP del router, ej. 192.168.123.38)
   Celular ─WiFi── AP  "ESP32-Parcial" (192.168.4.1)
                    │
                    ├─ HTTP :80  ── web.c ── archivos de www/ (index.html, wifi.html)
                    │                     └─ tus handlers en main.c (/led, /estado, /wifi)
                    │
                    └─ TCP :1234 ── task_tcp (main.c) ── telnet ── microrl ──► execute() (main.c)

   USB (COM6): flashear y ver los logs (idf.py -p COM6 monitor)
```

| Archivo | Qué hace | Funciones |
|---|---|---|
| `hardware.h` | pines de la placa y tablas `leds[]`, `switches[]`, `pots[]` | — |
| `iodigital` | GPIO | `salida_init/set/get`, `entrada_init/get`, `entrada_interrupcion` |
| `ianalogica` | ADC1 | `analogica_init`, `analogica_get` (promedio de 5), `analogica_get_crudo` (1 lectura) |
| `pwm` | PWM (MCPWM) | `pwm_init`, `pwm_set_duty` (0–100 %), `pwm_set_pulso_us` (servos) |
| `temporizador` | interrupción periódica (gptimer) | `temporizador_init` |
| `memoria` | NVS: guardar y leer datos que sobreviven al reinicio | `memoria_init`, `memoria_get/set_str`, `memoria_get/set_u16` |
| `wifi` | AP + STA | `wifi_init`, `wifi_sta_conectar` |
| `web` | servidor HTTP + archivos de `www/` | `web_init`, `web_parametro`, `web_leer_cuerpo`, `web_campo` |
| `tcp` | sockets TCP | `tcp_servidor`, `tcp_aceptar`, `tcp_recibir`, `tcp_enviar` |
| `telnet` | que MobaXterm (Telnet) no duplique el eco | `telnet_inicio`, `telnet_filtrar` |
| `microrl` + `config.h` | línea de comandos (la del profe) | `microrl_init`, `microrl_set_execute_callback`, `microrl_insert_char` |
| **`main.c`** | **todo tu programa** | `app_main`, `task_tcp`, `execute`, `enviar`, handlers web |
| `www/` | la página (HTML sin estilos) | `index.html`, `wifi.html` |

Cada `.h` tiene la documentación de sus funciones.

---

## 2. El arranque (`app_main`)

```c
init_hardware();                    // salida_init de los LEDs, entrada_init de los SW, analogica_init de los potes

memoria_init();                     // NVS
memoria_get_str("ssid", ...);       // la red guardada (si nunca se guardó: la de menuconfig)
memoria_get_str("pass", ...);
wifi_init(AP_SSID, AP_PASS, ssid, pass);

web_init(uris, cantidad);           // servidor HTTP: tus handlers + archivos de www/

xTaskCreate(task_tcp, ...);         // consola por TCP
// ... tus tareas
```

El orden importa en dos lugares: `memoria_init` antes de `wifi_init` (el WiFi usa la NVS) y
`wifi_init` antes de `web_init` y `task_tcp` (los dos usan la red).

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
memoria_set_u16("umbral", 3000);            // guardar
uint16_t umbral = 2000;                     // valor por defecto
memoria_get_u16("umbral", &umbral);         // si no existe, queda el por defecto
```

Las claves tienen hasta 15 caracteres. Para otros tipos, mirá `memoria.h` (o usá
`nvs_set_i32` / `nvs_get_i32` de ESP-IDF directo).

---

## 4. La consola TCP y `execute()`

Lo que escribís en la consola TCP termina en **`execute(argc, argv)`** de `main.c`, y vos
contestás con **`enviar(texto)`**.

### Conectarse

MobaXterm → *Session → Telnet* → la IP del ESP32 → puerto `1234`. Un cliente a la vez.

(Por el USB, `idf.py -p COM6 monitor`, solo salen los logs del sistema: ahí no hay consola.
Mientras algo tenga el COM6 abierto, **no se puede flashear**.)

### El camino de una tecla

```
task_tcp:  tcp_recibir() → telnet_filtrar() → microrl_insert_char()  (eco, edición, historial)
                                                      │ al Enter
                                                      ▼
                                              execute(argc, argv)   ← TUS COMANDOS
                                                      │
                                                      ▼
                                              enviar(texto) → tcp_enviar
```

Microrl separa la línea en palabras: `argv[0]` es el comando, `argc` cuántas palabras hay.
`enviar()` es también la función que usa microrl para el eco y el prompt.

### Agregar un comando

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

`execute()` corre en `task tcp`. Si un comando modifica datos que también usa otra tarea o
un handler web (un `struct`, un arreglo, un contador), protegelos con un **mutex**
(`GUIA_FREERTOS.md`, sección 5).

---

## 5. La página web

### Cómo funciona

1. El navegador pide `/` → el servidor manda `www/index.html` (es un archivo).
2. Los botones llaman a `fetch('/led?n=2&v=1')` → eso lo atiende **tu** handler
   `led_get()` en `main.c`.
3. Cada 1 s la página pide `/estado` → tu handler `estado_get()` arma un texto y la
   página lo muestra tal cual en un `<pre>`.

```
index.html                          main.c
──────────                          ──────
<button onclick="led(2,1)">  ──►  GET /led?n=2&v=1  ──►  led_get(): salida_set(leds[1], 1)
setInterval(actualizar, 1000) ──►  GET /estado      ──►  estado_get(): "LED 1: apagado\n..."
<form action="/wifi">        ──►  POST /wifi       ──►  wifi_post(): NVS + conectar
```

Los archivos de `www/` se graban en la flash con `idf.py flash` (partición SPIFFS). Si
cambiás un `.html`, hay que volver a flashear.

### Agregar algo a la página

**Para ver un dato nuevo:** agregalo al texto de `estado_get()`:

```c
largo += snprintf(txt + largo, sizeof(txt) - largo, "Umbral: %d\n", s_umbral);
```

**Para controlar algo nuevo:** un handler + una línea en la tabla `uris[]` + un botón.

```c
// main.c: GET /umbral?v=3000
esp_err_t umbral_get(httpd_req_t *req)
{
	char v[8];
	if (!web_parametro(req, "v", v, sizeof(v))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falta v");
	}
	s_umbral = atoi(v);
	return httpd_resp_sendstr(req, "OK");
}

// en la tabla uris[] (y su prototipo arriba):
	{ .uri = "/umbral", .method = HTTP_GET, .handler = umbral_get },
```

```html
<!-- www/index.html -->
<p>Umbral <input id="umbral"> <button onclick="fetch('/umbral?v=' + umbral.value)">Enviar</button></p>
```

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

### D. Mandar algo al cliente TCP sin que lo pida

`enviar(texto)` se puede llamar desde cualquier tarea: si no hay nadie conectado no hace
nada. Ej. una alarma:

```c
if (pot > umbral) {
	enviar("ALARMA: pote sobre el umbral\r\n");
}
```

Ojo: si otra tarea manda mientras `task tcp` contesta un comando, los textos se pueden
mezclar. Si pasa, un mutex alrededor de `enviar()` o una tarea dueña del socket
(gatekeeper, `GUIA_FREERTOS.md` 14.F).

### E. Una UART (otro dispositivo)

El proyecto no trae librería de UART. Si la piden, se usa el driver de ESP-IDF directo
(`driver/uart.h`) y se agrega `esp_driver_uart` a `PRIV_REQUIRES` en `main/CMakeLists.txt`.
Ejemplo completo, con cola de eventos: `GUIA_FREERTOS.md`, sección 12. Usá UART1 en pines
libres (TX 17, RX 18); para probar sin otro dispositivo, un cable de GPIO 17 a GPIO 18.

---

## 7. Problemas comunes

| Síntoma | Causa | Solución |
|---|---|---|
| `idf.py flash` falla en "Serial port COM6" | el monitor o MobaXterm tiene el COM6 abierto | cerrarlo y volver a flashear |
| La página no carga | el ESP32 cambió de IP o no está conectado | ver la IP en el monitor, o usar el AP (`192.168.4.1`) |
| La página carga pero dice "Sin conexion" | el ESP32 se reinició o se cayó el WiFi | esperar unos segundos |
| Cambié el HTML y no se ve | `www/` se graba al flashear | `idf.py -p COM6 flash`; en el navegador Ctrl+F5 |
| MobaXterm no muestra nada | ya hay otro cliente TCP conectado | cerrar el otro (un cliente a la vez; uno colgado se libera en ~11 s) |
| MobaXterm muestra todo doble | sesión *Raw* en vez de *Telnet* | usar *Telnet* |
| Un comando no responde | `execute()` no llamó a `enviar()` en ese camino | revisar todos los `if` |
| `Guru Meditation` al usar una cola | la cola se usó antes de crearla | crear colas/semáforos antes de las tareas e interrupciones |
| `stack overflow in task` | stack chico (`printf`, `snprintf`) | 4096 o más |
| `fatal error: X.h: No such file` | falta el componente en `main/CMakeLists.txt` | agregarlo a `PRIV_REQUIRES` (ej. `esp_timer`) |

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
| Muestras que promedia `analogica_get` | 5 (tarda 50 ms) | `ANALOGICA_MUESTRAS` en `ianalogica.h` |
