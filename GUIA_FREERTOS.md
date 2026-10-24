# Guía de FreeRTOS para el parcial (ESP32-S3, ESP-IDF v6.0.2)

Todo lo de FreeRTOS que puede entrar, con ejemplos sobre **esta** placa (LEDs 4–7,
pulsadores 11–14, potes 1–2) y **este** proyecto (`var_get` / `var_set`, consola,
`temporizador_init`, `entrada_interrupcion`). Los ejemplos están compilados con el
proyecto.

> Lo que pide el profe (criterios del TP2): *uso correcto de colas, semáforos/mutex,
> prioridades de tareas, y ausencia de bloqueos mediante `vTaskDelay` no deterministas*.
> Traducido: **cada tarea espera algo concreto** (una cola, un bit, una notificación, un
> período exacto con `xTaskDelayUntil`), no "duerme un rato a ver si pasó algo".

Índice:

0. [Datos de esta placa que importan](#0-datos-de-esta-placa-que-importan)
1. [Tareas](#1-tareas)
2. [Tiempo: vTaskDelay vs xTaskDelayUntil](#2-tiempo-vtaskdelay-vs-xtaskdelayuntil)
3. [Colas](#3-colas)
4. [Semáforos binarios y contadores](#4-semáforos-binarios-y-contadores)
5. [Mutex](#5-mutex)
6. [Event groups](#6-event-groups)
7. [Notificaciones de tarea](#7-notificaciones-de-tarea)
8. [Timers de software (FreeRTOS)](#8-timers-de-software-freertos)
9. [Interrupciones (ISR)](#9-interrupciones-isr)
10. [Secciones críticas](#10-secciones-críticas)
11. [Message buffers](#11-message-buffers)
12. [UART con cola de eventos](#12-uart-con-cola-de-eventos)
13. [Medir tiempo y diagnosticar](#13-medir-tiempo-y-diagnosticar)
14. [Patrones de ejercicio (combinados con WiFi)](#14-patrones-de-ejercicio-combinados-con-wifi)
15. [¿Qué herramienta uso?](#15-qué-herramienta-uso)
16. [Problemas clásicos y errores típicos](#16-problemas-clásicos-y-errores-típicos)

---

## 0. Datos de esta placa que importan

| Dato | Valor | Consecuencia |
|---|---|---|
| Versión del kernel | FreeRTOS V10.5.1 (versión SMP de Espressif) | API estándar + `...PinnedToCore` |
| Núcleos | **2** (core 0 y core 1) | Dos tareas pueden correr **a la vez de verdad**: los mutex no son opcionales |
| Tick (`CONFIG_FREERTOS_HZ`) | **100 Hz → 1 tick = 10 ms** | `pdMS_TO_TICKS(5)` da **0**. Menos de 10 ms: usar `temporizador_init` (gptimer) o `esp_timer` |
| Prioridades | 0 (IDLE) a 24 (`configMAX_PRIORITIES` = 25) | Número más alto = más prioridad |
| Stack | en **bytes** (en FreeRTOS "puro" es en palabras) | `4096` = 4 KB |
| `configMINIMAL_STACK_SIZE` | ~1,5 KB | Alcanza para una tarea que solo prende un LED. Con `printf`, `float` o `var_*`: **4096** |
| Watchdog de tareas | 5 s, vigila las tareas IDLE | Una tarea que nunca se bloquea deja sin CPU a IDLE → `Task watchdog got triggered` |
| Desborde de stack | detectado (canary) | Panic con `stack overflow in task X` → subir el stack de esa tarea |

Tareas que ya corren en el sistema (para elegir tus prioridades):

| Tarea | Prioridad | Qué es |
|---|---|---|
| IDLE0 / IDLE1 | 0 | ociosas, una por núcleo |
| `Tmr Svc` | 1 | corre los callbacks de los timers de software (sección 8) |
| `main` | 1 | `app_main`; termina al salir de `app_main` |
| `consola_tcp`, `consola_uart`, `httpd` | 5 | consola y servidor web |
| `esp_timer` | 22 | callbacks de `esp_timer` |
| `wifi` | 23 | WiFi (no competir con ella) |

**Regla práctica:** tus tareas entre **2 y 10**. Lo que tiene que responder rápido (control,
muestreo) más alto que la consola (5); lo que es "de fondo" (logs, reportes) más bajo.

Includes que vas a usar:

```c
#include "freertos/FreeRTOS.h"      // siempre primero
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "freertos/message_buffer.h"
#include "esp_timer.h"              // esp_timer_get_time()  (ver nota abajo)
#include "esp_attr.h"               // IRAM_ATTR
```

> **`esp_timer.h` necesita agregar el componente:** en `main/CMakeLists.txt`, sumar
> `esp_timer` a `PRIV_REQUIRES`. Si no, el error es
> `fatal error: esp_timer.h: No such file or directory`. Lo mismo vale para cualquier
> "includes X.h, provided by Y component": agregar Y a `PRIV_REQUIRES`.

---

## 1. Tareas

Una tarea es una función con un `for (;;)` que **nunca retorna**, y que en cada vuelta
**se bloquea esperando algo**.

```c
static TaskHandle_t s_tarea_led;        // handle: para suspenderla, notificarla, etc.

static void tarea_led(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(int)arg;          // el parámetro que se pasó al crearla
    bool encendido = false;
    for (;;) {
        encendido = !encendido;
        var_set(pin == LED2_GPIO ? "led2" : "led3", encendido ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(500));              // se bloquea: le deja la CPU a otras
    }
}

// en app_main:
xTaskCreate(tarea_led,             // función
            "led",                 // nombre (para depurar)
            4096,                  // stack en BYTES
            (void *)LED2_GPIO,     // parámetro (void *)
            4,                     // prioridad
            &s_tarea_led);         // handle de salida, o NULL si no lo necesitás
```

### Pasar varios datos a una tarea

El parámetro es un puntero: pasá un `struct` que **siga existiendo** mientras la tarea corre
(`static` o global; nunca una variable local de `app_main`, que desaparece).

```c
typedef struct {
    const char *led;       // id de la variable
    uint32_t periodo_ms;
} parpadeo_t;

static void tarea_parpadeo(void *arg)
{
    const parpadeo_t *p = arg;
    bool on = false;
    for (;;) {
        on = !on;
        var_set(p->led, on ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(p->periodo_ms));
    }
}

// la misma función, dos tareas distintas:
static const parpadeo_t s_rapido = { "led2", 200 };
static const parpadeo_t s_lento  = { "led3", 1000 };
xTaskCreate(tarea_parpadeo, "rapido", 4096, (void *)&s_rapido, 3, NULL);
xTaskCreate(tarea_parpadeo, "lento",  4096, (void *)&s_lento,  3, NULL);
```

### Elegir el núcleo

```c
xTaskCreatePinnedToCore(tarea_control, "control", 4096, NULL, 6, NULL, 1);   // core 1
// tskNO_AFFINITY = que el scheduler elija (lo que hace xTaskCreate)
```

Por qué: el WiFi corre en el core 0. Una tarea de control con tiempos exigentes en el
core 1 no compite con él. Para saber en qué núcleo está corriendo: `xPortGetCoreID()`.

### Controlar tareas desde afuera (con el handle)

| Función | Qué hace |
|---|---|
| `vTaskSuspend(h)` / `vTaskResume(h)` | la congela / la despierta (ej. comando `pausa`) |
| `vTaskDelete(h)` | la borra. `vTaskDelete(NULL)` = la tarea se borra a sí misma (una tarea que termina **tiene** que hacer esto, nunca `return`) |
| `vTaskPrioritySet(h, p)` / `uxTaskPriorityGet(h)` | cambia / lee la prioridad |
| `eTaskGetState(h)` | `eRunning`, `eReady`, `eBlocked`, `eSuspended`, `eDeleted` |
| `pcTaskGetName(h)` | nombre (con `NULL` = la tarea actual) |

> `vTaskSuspend` para "pausar un modo" es tentador pero peligroso: si la tarea estaba con
> un mutex tomado, queda tomado para siempre. Mejor un bit de event group o una variable
> de estado que la tarea consulte (sección 14.A).

### Tarea que hace algo una sola vez

```c
static void tarea_saludo(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    var_set("estado", "arrancado");
    vTaskDelete(NULL);              // nunca "return" en una tarea
}
```

---

## 2. Tiempo: vTaskDelay vs xTaskDelayUntil

**Esto es lo que el profe llama "vTaskDelay no determinista".**

```c
// MAL para algo periódico: el período real es 20 ms + lo que tarde el trabajo.
for (;;) {
    leer_y_procesar();                      // tarda 3 ms, a veces 8...
    vTaskDelay(pdMS_TO_TICKS(20));          // espera 20 ms DESDE AHORA
}                                           // → período 23, 28... y se va corriendo

// BIEN: el período es 20 ms exactos, contado desde el inicio de la vuelta anterior.
TickType_t ultimo = xTaskGetTickCount();
for (;;) {
    xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(20));   // espera hasta ultimo + 20 ms
    leer_y_procesar();
}
```

- `xTaskDelayUntil` actualiza `ultimo` solo. Devuelve `pdFALSE` si ya se pasó la hora
  (el trabajo tardó más que el período: sirve para detectar que no llegás).
- `vTaskDelayUntil` es lo mismo sin valor de retorno (versión vieja).
- **Usá `vTaskDelay` solo para pausas que no son un período:** "esperar 50 ms de rebote",
  "darle 1 s al WiFi", un parpadeo sin exigencia.

**Resolución:** con tick de 10 ms, los períodos son múltiplos de 10 ms. Para 1 ms o
para muestrear a frecuencia exacta: timer de hardware (`temporizador_init`, sección 9 y
receta H de `GUIA.md`). Convertir:

```c
pdMS_TO_TICKS(250)          // ms → ticks (25)
pdTICKS_TO_MS(ticks)        // ticks → ms
xTaskGetTickCount()         // ticks desde el arranque (en ISR: xTaskGetTickCountFromISR)
esp_timer_get_time()        // MICROsegundos desde el arranque (int64_t), precisión real
```

---

## 3. Colas

Una cola es un **buzón de mensajes por copia**: el que manda copia el dato adentro, el que
recibe se lo lleva. Sirve para **pasar datos** entre tareas (o de una ISR a una tarea) y a la
vez **despertar** al que espera.

```c
typedef struct {
    float x;
    float y;
} destino_t;

static QueueHandle_t s_destinos;

// crear (antes de crear las tareas que la usan):
s_destinos = xQueueCreate(5, sizeof(destino_t));        // 5 lugares de destino_t

// productor (ej. un comando de consola):
destino_t d = { 120.0f, 45.0f };
if (xQueueSend(s_destinos, &d, 0) != pdTRUE) {          // 0 = no esperar si está llena
    consola_printf("ERROR: cola llena\r\n");
}

// consumidor (tarea):
static void tarea_mover(void *arg)
{
    destino_t d;
    for (;;) {
        xQueueReceive(s_destinos, &d, portMAX_DELAY);    // duerme hasta que llegue algo
        // ... mover hacia d.x, d.y
    }
}
```

| Función | Qué hace |
|---|---|
| `xQueueSend(q, &dato, espera)` | agrega al final (= `xQueueSendToBack`). Si está llena espera hasta `espera` ticks |
| `xQueueSendToFront(q, &dato, espera)` | agrega adelante (un mensaje urgente) |
| `xQueueReceive(q, &dato, espera)` | saca el primero. `pdFALSE` si venció la espera |
| `xQueuePeek(q, &dato, espera)` | lo lee **sin sacarlo** |
| `xQueueOverwrite(q, &dato)` | **solo colas de 1 lugar**: pisa lo que haya. Nunca falla |
| `uxQueueMessagesWaiting(q)` | cuántos hay |
| `uxQueueSpacesAvailable(q)` | cuántos lugares libres |
| `xQueueReset(q)` | vacía la cola |
| `xQueueSendFromISR` / `xQueueReceiveFromISR` / `xQueueOverwriteFromISR` | versiones para ISR (sección 9) |

**Tiempos de espera:** `0` = no esperar, `pdMS_TO_TICKS(100)` = hasta 100 ms,
`portMAX_DELAY` = para siempre.

### Cola de 1 lugar = "último valor" (buzón)

Para pasar **el valor más nuevo** (posición de un pote, consigna) sin acumular atraso:

```c
static QueueHandle_t s_posicion;                    // xQueueCreate(1, sizeof(float))

// productor, periódico: siempre pisa
xQueueOverwrite(s_posicion, &angulo);

// consumidor que necesita esperar al dato nuevo:
xQueueReceive(s_posicion, &angulo, portMAX_DELAY);
// o consumidor que solo quiere "el último que haya", sin sacarlo:
xQueuePeek(s_posicion, &angulo, 0);
```

Es lo que el TP2 llama `Queue_MasterPos` / `Queue_TargetPos`, y lo que usa
`consola_tcp.c` para el puerto.

### Mandar texto por una cola

Una cola copia bytes de tamaño fijo: mandá un `struct` con un arreglo, **no un puntero**
a un buffer local (cuando el receptor lo lea, ese buffer ya cambió).

```c
typedef struct { char texto[32]; } mensaje_t;
mensaje_t m;
snprintf(m.texto, sizeof(m.texto), "pote %.1f", pot);
xQueueSend(s_mensajes, &m, 0);
```

Para textos de largo variable, mejor un message buffer (sección 11).

---

## 4. Semáforos binarios y contadores

### Binario: "avisar que pasó algo"

Vale 0 o 1. Uno hace `Give` (avisa), otro hace `Take` (espera el aviso). Ideal de ISR a
tarea cuando no hay dato que pasar.

```c
static SemaphoreHandle_t s_listo;

s_listo = xSemaphoreCreateBinary();         // nace en 0 (tomado)

// el que avisa (tarea):    xSemaphoreGive(s_listo);
// el que avisa (ISR):      xSemaphoreGiveFromISR(s_listo, &despertada);

static void tarea_espera(void *arg)
{
    for (;;) {
        if (xSemaphoreTake(s_listo, pdMS_TO_TICKS(2000)) == pdTRUE) {
            var_set("estado", "llego el aviso");
        } else {
            var_set("estado", "2 s sin aviso");     // timeout: también es información
        }
    }
}
```

Si llegan dos `Give` antes del `Take`, **se pierde uno** (vale 1 como máximo). Si importa
contar, usá un contador.

### Contador: "contar eventos" o "administrar N recursos"

```c
// a) Contar eventos sin perder ninguno (máx 10 pendientes, empieza en 0):
s_eventos = xSemaphoreCreateCounting(10, 0);
//    productor: xSemaphoreGive(s_eventos);   consumidor: xSemaphoreTake(..., portMAX_DELAY)

// b) N recursos iguales (ej. 3 "canales" disponibles, empieza en 3):
s_canales = xSemaphoreCreateCounting(3, 3);
if (xSemaphoreTake(s_canales, 0) == pdTRUE) {
    // ... usar un canal
    xSemaphoreGive(s_canales);                  // devolverlo
} else {
    consola_printf("ERROR: no hay canales libres\r\n");
}

uxSemaphoreGetCount(s_canales);                 // cuántos quedan
```

---

## 5. Mutex

Protege un **recurso compartido** (una variable, un `struct`, un periférico) para que lo use
una tarea por vez. Diferencias con el semáforo binario:

| | Semáforo binario | Mutex |
|---|---|---|
| Para qué | **avisar** (sincronizar) | **proteger** (exclusión mutua) |
| Quién lo devuelve | cualquiera (otra tarea, una ISR) | **solo la tarea que lo tomó** |
| Desde una ISR | sí (`GiveFromISR`) | **nunca** |
| Herencia de prioridad | no | **sí** (evita la inversión de prioridad, sección 16) |
| Estado inicial | tomado (0) | libre |

### Estado compartido con mutex (patrón del TP2: `Mutex_RobotState`)

```c
typedef struct {
    float angulo1;
    float angulo2;
    int modo;
} estado_t;

static estado_t s_estado;
static SemaphoreHandle_t s_mutex_estado;       // xSemaphoreCreateMutex() en app_main

// Funciones get/set: nadie toca s_estado directo, así nunca se olvida el mutex.
static void estado_set_angulos(float a1, float a2)
{
    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    s_estado.angulo1 = a1;
    s_estado.angulo2 = a2;
    xSemaphoreGive(s_mutex_estado);
}

static estado_t estado_get(void)
{
    xSemaphoreTake(s_mutex_estado, portMAX_DELAY);
    estado_t copia = s_estado;                  // copiar y soltar rápido
    xSemaphoreGive(s_mutex_estado);
    return copia;
}
```

Reglas:
- **Tomarlo el menor tiempo posible:** copiar el dato y soltarlo. Nada de `vTaskDelay`,
  `printf` largos ni esperar una cola con el mutex tomado.
- Cada `Take` con su `Give`, también en los `return` de error.
- Si una tarea necesita dos mutex, **todas** los toman en el mismo orden (si no: deadlock).
- **No hace falta para lo del registro:** `var_get` / `var_set` ya tienen su mutex adentro.
  Hace falta para **tus** variables compartidas entre tareas (y entre comandos de consola,
  que corren en otra tarea).

### Mutex recursivo

Para una función con mutex que se llama a sí misma o llama a otra que toma el mismo mutex
(con uno normal se bloquearía sola):

```c
s_m = xSemaphoreCreateRecursiveMutex();
xSemaphoreTakeRecursive(s_m, portMAX_DELAY);
xSemaphoreGiveRecursive(s_m);                   // tantos Give como Take
```

---

## 6. Event groups

Un `EventGroupHandle_t` es un conjunto de **24 banderas** (bits). Varias tareas los prenden,
y una tarea puede esperar **uno**, **cualquiera de varios (OR)** o **todos (AND)**. Ideal
para **modos de funcionamiento** y para "esperar a que pasen varias cosas".

```c
#define BIT_ESPEJO      (1 << 0)
#define BIT_GRABANDO    (1 << 1)
#define BIT_REPRODUCE   (1 << 2)
#define BIT_SENSOR_A    (1 << 3)
#define BIT_SENSOR_B    (1 << 4)

static EventGroupHandle_t s_eventos;            // xEventGroupCreate()

xEventGroupSetBits(s_eventos, BIT_ESPEJO);                      // prender
xEventGroupClearBits(s_eventos, BIT_GRABANDO | BIT_REPRODUCE);  // apagar
EventBits_t bits = xEventGroupGetBits(s_eventos);               // leer sin esperar
if (bits & BIT_ESPEJO) { /* modo espejo activo */ }

// Esperar:
bits = xEventGroupWaitBits(s_eventos,
                           BIT_SENSOR_A | BIT_SENSOR_B,  // qué bits
                           pdTRUE,       // limpiarlos al salir (evento "de un uso")
                           pdTRUE,       // pdTRUE = TODOS (AND), pdFALSE = cualquiera (OR)
                           pdMS_TO_TICKS(5000));         // o portMAX_DELAY
if ((bits & (BIT_SENSOR_A | BIT_SENSOR_B)) == (BIT_SENSOR_A | BIT_SENSOR_B)) {
    // pasaron los dos
} else {
    // venció la espera: bits dice cuáles sí llegaron
}
```

**Bit de modo vs bit de evento:**
- **Modo** (`BIT_ESPEJO`): queda prendido mientras dure el modo. Se espera con
  `xClearOnExit = pdFALSE` (o se consulta con `xEventGroupGetBits`).
- **Evento** (`BIT_SENSOR_A`): "pasó algo". Se espera con `xClearOnExit = pdTRUE` para que
  la próxima espera aguarde un evento nuevo.

### Esperar que un modo se active (sin consumir CPU)

```c
static void tarea_led_modo(void *arg)
{
    for (;;) {
        // duerme mientras el modo esté apagado; pdFALSE: NO limpia el bit (es un modo)
        xEventGroupWaitBits(s_eventos, BIT_ESPEJO, pdFALSE, pdFALSE, portMAX_DELAY);
        var_set("led2", "toggle");                      // parpadea mientras dure el modo
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
```

Para una tarea **periódica** que se activa con un modo, ver 14.A: al despertar hay que
reiniciar `ultimo = xTaskGetTickCount();`, porque si no `xTaskDelayUntil` "recupera" de
golpe todas las vueltas que se perdió mientras dormía.

### Punto de encuentro (rendezvous): `xEventGroupSync`

Cada tarea prende su bit y espera a que **todas** hayan llegado (ej. "arrancar las 3 tareas
a la vez después de calibrar"):

```c
#define LLEGO_A (1 << 5)
#define LLEGO_B (1 << 6)
#define TODAS   (LLEGO_A | LLEGO_B)
// en la tarea A:
xEventGroupSync(s_eventos, LLEGO_A, TODAS, portMAX_DELAY);
// en la tarea B:
xEventGroupSync(s_eventos, LLEGO_B, TODAS, portMAX_DELAY);
// ninguna sigue hasta que llegaron las dos
```

Desde una ISR: `xEventGroupSetBitsFromISR(eg, bits, &despertada)` (lo usa la receta H).

---

## 7. Notificaciones de tarea

Cada tarea trae de fábrica un **valor de 32 bits de notificación**. Otra tarea o una ISR le
"escribe" directo, **sin crear ningún objeto**. Es lo más rápido y liviano, pero hay **un
solo receptor** (la tarea dueña) y hace falta su handle.

### Como semáforo (contador) — el uso más común

```c
static TaskHandle_t s_tarea_adc;                // lo llena xTaskCreate(..., &s_tarea_adc)

// el que avisa (tarea):  xTaskNotifyGive(s_tarea_adc);
// el que avisa (ISR):    vTaskNotifyGiveFromISR(s_tarea_adc, &despertada);

static void tarea_adc(void *arg)
{
    for (;;) {
        // pdTRUE: pone el contador en 0 al salir (como semáforo binario)
        // pdFALSE: le resta 1 (como semáforo contador)
        uint32_t cuantos = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // ... leer el ADC (cuantos = avisos acumulados desde la última vez)
    }
}
```

### Con valor: mandar bits o un número

```c
// Mandar un número (pisa el anterior):
xTaskNotify(s_tarea_adc, 42, eSetValueWithOverwrite);
// Prender bits (como un event group privado):
xTaskNotify(s_tarea_adc, (1 << 2), eSetBits);
// Otras acciones: eIncrement (sumar 1), eSetValueWithoutOverwrite (falla si había uno), eNoAction

// Recibir:
uint32_t valor;
xTaskNotifyWait(0,              // bits a borrar al ENTRAR (0 = ninguno)
                0xFFFFFFFF,     // bits a borrar al SALIR (todos)
                &valor,
                portMAX_DELAY);
```

| Usar notificación si... | Usar cola / event group si... |
|---|---|
| hay un solo receptor y conocés su handle | hay varios receptores o no sabés quién espera |
| el dato es un aviso, un contador, bits o un número | el dato es un `struct` o hay que guardar varios |
| querés lo más rápido (ISR → tarea) | |

---

## 8. Timers de software (FreeRTOS)

Llaman a una función (**callback**) después de un tiempo, una vez o periódicamente. No usan
hardware: los maneja la tarea `Tmr Svc` (prioridad 1, stack 2 KB). Resolución: el tick (10 ms).

```c
#include "freertos/timers.h"

static TimerHandle_t s_apagado;

/* Callback: corre en la tarea Tmr Svc. Corto y SIN bloquear
 * (nada de vTaskDelay ni esperas largas: frena a todos los demás timers). */
static void al_vencer_apagado(TimerHandle_t t)
{
    var_set("led4", "off");
}

// crear (en app_main):
s_apagado = xTimerCreate("apagado",                 // nombre
                         pdMS_TO_TICKS(5000),       // período
                         pdFALSE,                   // pdFALSE = una vez, pdTRUE = periódico
                         NULL,                      // ID (para compartir callback, ver abajo)
                         al_vencer_apagado);        // callback

// usar (desde cualquier tarea; el último parámetro es cuánto esperar si la cola de
// comandos del timer está llena: 0 está bien):
xTimerStart(s_apagado, 0);          // arranca (si ya corría, reinicia la cuenta)
xTimerReset(s_apagado, 0);          // reinicia la cuenta: "5 s desde AHORA"
xTimerStop(s_apagado, 0);
xTimerChangePeriod(s_apagado, pdMS_TO_TICKS(2000), 0);   // OJO: también lo ARRANCA
xTimerIsTimerActive(s_apagado);     // pdTRUE si está corriendo
// desde una ISR: xTimerStartFromISR / xTimerResetFromISR (t, &despertada)
```

### Ejemplo: "apagar el LED 4 si pasan 5 s sin tocar SW1" (watchdog de inactividad)

```c
// cada vez que se aprieta SW1 (en la tarea del pulsador):
var_set("led4", "on");
xTimerReset(s_apagado, 0);          // si no lo vuelven a apretar en 5 s → callback
```

### Un callback para varios timers (con el ID)

```c
static void al_vencer_led(TimerHandle_t t)
{
    const char *led = pvTimerGetTimerID(t);     // el ID que se pasó al crearlo
    var_set(led, "toggle");
}
xTimerCreate("t2", pdMS_TO_TICKS(300), pdTRUE, (void *)"led2", al_vencer_led);
xTimerCreate("t3", pdMS_TO_TICKS(700), pdTRUE, (void *)"led3", al_vencer_led);
```

### ¿Timer de software, `esp_timer` o timer de hardware?

| | Timer FreeRTOS | `esp_timer` | `temporizador_init` (gptimer) |
|---|---|---|---|
| Resolución | 10 ms (tick) | 1 µs | 1 µs |
| Callback corre en | tarea `Tmr Svc` (prio 1) | tarea `esp_timer` (prio 22) | **ISR** (solo `...FromISR`) |
| Puede llamar `var_set` | sí (cortito) | sí (cortito) | **no**: avisar a una tarea |
| Uso típico | timeouts, parpadeos, apagados | períodos < 10 ms con callback normal | muestreo exacto, alta frecuencia |

`esp_timer` periódico, por si hace falta:

```c
static void cada_2ms(void *arg) { /* corto */ }
esp_timer_handle_t t;
const esp_timer_create_args_t args = { .callback = cada_2ms, .name = "rapido" };
esp_timer_create(&args, &t);
esp_timer_start_periodic(t, 2000);          // en MICROsegundos
```

---

## 9. Interrupciones (ISR)

Detalle completo en `GUIA.md` (recetas H e I). Lo esencial:

1. La ISR es **corta**, marcada **`IRAM_ATTR`**, y usa **solo funciones `...FromISR`**.
2. **Nunca** en una ISR: `printf`, `ESP_LOG`, `var_set`, `var_get`, ADC, `vTaskDelay`, tomar
   un mutex, nada que espere.
3. La ISR **avisa**, una tarea **hace el trabajo**.
4. El `despertada` (`BaseType_t`, arranca en `pdFALSE`): las funciones `FromISR` lo ponen en
   `pdTRUE` si despertaron a una tarea más prioritaria; entonces se pide el cambio de tarea
   al salir.

```c
// ISR de GPIO (entrada_interrupcion): avisa con notificación
static void IRAM_ATTR al_apretar(void *arg)
{
    BaseType_t despertada = pdFALSE;
    vTaskNotifyGiveFromISR(s_tarea_boton, &despertada);
    if (despertada) {
        portYIELD_FROM_ISR();
    }
}

// ISR de timer (temporizador_init): devuelve el "despertada" en vez de hacer el yield
static bool IRAM_ATTR al_vencer(void *arg)
{
    BaseType_t despertada = pdFALSE;
    vTaskNotifyGiveFromISR(s_tarea_adc, &despertada);
    return despertada == pdTRUE;
}
```

Qué usar para avisar desde la ISR:

| Quiero... | En la ISR | En la tarea |
|---|---|---|
| avisar a **una** tarea (lo más liviano) | `vTaskNotifyGiveFromISR` | `ulTaskNotifyTake` |
| avisar con un dato (qué pin, una lectura) | `xQueueSendFromISR` | `xQueueReceive` |
| avisar "pasó X" a quien sea | `xSemaphoreGiveFromISR` | `xSemaphoreTake` |
| prender un bit de evento / modo | `xEventGroupSetBitsFromISR` | `xEventGroupWaitBits` |
| reiniciar un timeout | `xTimerResetFromISR` | (callback del timer) |

---

## 10. Secciones críticas

Para un dato **muy chico** compartido entre una **ISR y una tarea** (un contador, una
bandera), donde no se puede usar mutex. En el ESP32 (2 núcleos) se usa un **spinlock**:

```c
static portMUX_TYPE s_spin = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_pulsos = 0;      // volatile: lo cambia una ISR

static void IRAM_ATTR al_pulso(void *arg)
{
    taskENTER_CRITICAL_ISR(&s_spin);        // versión _ISR dentro de la interrupción
    s_pulsos++;
    taskEXIT_CRITICAL_ISR(&s_spin);
}

static uint32_t pulsos_leer_y_borrar(void)  // desde una tarea
{
    taskENTER_CRITICAL(&s_spin);            // apaga interrupciones en este núcleo
    uint32_t n = s_pulsos;
    s_pulsos = 0;
    taskEXIT_CRITICAL(&s_spin);
    return n;
}
```

- Adentro: **solo unas pocas instrucciones**. Nada de funciones de FreeRTOS, `printf`, ni
  `var_*`. Se bloquean las interrupciones: si tarda, salta el watchdog de interrupciones.
- Si no hay ISR de por medio (solo tareas), usá un **mutex**.
- `volatile` le dice al compilador "esto cambia solo, no lo guardes en un registro". No
  reemplaza a la sección crítica: `s_pulsos++` igual son 3 pasos (leer, sumar, escribir).

Ejemplo de uso: medir **frecuencia** de una señal por GPIO. La ISR cuenta flancos y una
tarea, cada 1 s exacto (`xTaskDelayUntil`), lee y borra el contador: eso son Hz.

---

## 11. Message buffers

Como una cola, pero para **mensajes de largo variable** (texto). Uno escribe, uno lee.

```c
#include "freertos/message_buffer.h"

static MessageBufferHandle_t s_log;         // xMessageBufferCreate(1024) = 1 KB en total

// escribir sin bloquear (desde una tarea de control: nunca espera)
static void log_async(const char *texto)
{
    xMessageBufferSend(s_log, texto, strlen(texto), 0);    // si no entra, se descarta
}

// tarea de baja prioridad que saca los textos y los manda lento
static void tarea_log(void *arg)
{
    char buf[128];
    for (;;) {
        size_t n = xMessageBufferReceive(s_log, buf, sizeof(buf) - 1, portMAX_DELAY);
        buf[n] = '\0';
        printf("%s", buf);
    }
}
```

Es lo que hace el `uart.c` del TP2 (`UART_EnviarCadenaAsincrona`): una tarea de control
rápida no se frena esperando a que salgan los bytes por la UART.

- **Un solo escritor y un solo lector.** Si escriben varias tareas, protegé el `Send` con un
  mutex.
- `xStreamBufferCreate` es la variante para un **chorro de bytes** sin separación en mensajes.

---

## 12. UART con cola de eventos

La consola por UART0 ya está hecha (`consola_uart.c`, lectura simple). Si el profe pide
**otra UART** (comunicarse con otro dispositivo, otro ESP32, un módulo) o "UART con eventos",
el driver de ESP-IDF entrega los eventos por una **cola de FreeRTOS**:

```c
#include "driver/uart.h"

#define UART_EXT    UART_NUM_1
#define PIN_TX      GPIO_NUM_17     // pines libres en esta placa
#define PIN_RX      GPIO_NUM_18

static QueueHandle_t s_uart_eventos;

static void tarea_uart(void *arg)
{
    uart_event_t ev;
    char buf[128];
    for (;;) {
        xQueueReceive(s_uart_eventos, &ev, portMAX_DELAY);
        switch (ev.type) {
        case UART_DATA: {                       // llegaron ev.size bytes
            int n = uart_read_bytes(UART_EXT, buf, sizeof(buf) - 1, 0);
            buf[n] = '\0';
            // ... procesar (ej. var_set("estado", buf))
            break;
        }
        case UART_PATTERN_DET: {                // llegó el carácter de fin ('\n')
            int pos = uart_pattern_pop_pos(UART_EXT);
            if (pos >= 0 && pos < (int)sizeof(buf) - 1) {
                int n = uart_read_bytes(UART_EXT, buf, pos + 1, 0);   // la línea completa
                buf[n] = '\0';
                // ... procesar la línea
            } else {
                uart_flush_input(UART_EXT);     // línea demasiado larga
            }
            break;
        }
        case UART_FIFO_OVF:                     // se perdieron datos: vaciar todo
        case UART_BUFFER_FULL:
            uart_flush_input(UART_EXT);
            xQueueReset(s_uart_eventos);
            break;
        default:                                // UART_FRAME_ERR, UART_PARITY_ERR, UART_BREAK...
            break;
        }
    }
}

static void uart_ext_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // rx 1024, tx 0 (escribe bloqueando), cola de 20 eventos → s_uart_eventos
    ESP_ERROR_CHECK(uart_driver_install(UART_EXT, 1024, 0, 20, &s_uart_eventos, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_EXT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_EXT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // opcional: avisar cada '\n' (detección de patrón)
    uart_enable_pattern_det_baud_intr(UART_EXT, '\n', 1, 9, 0, 0);
    uart_pattern_queue_reset(UART_EXT, 20);
    xTaskCreate(tarea_uart, "uart_ext", 4096, NULL, 6, NULL);
}

// enviar: uart_write_bytes(UART_EXT, "hola\n", 5);
```

- Probar sin otro dispositivo: un cable de **GPIO 17 a GPIO 18** (loopback): lo que mandás
  te vuelve.
- Requiere `esp_driver_uart` en `PRIV_REQUIRES` (ya está, por la consola UART).
- Con detección de patrón, usá **o** `UART_DATA` **o** `UART_PATTERN_DET` para leer (si
  leés todo en `UART_DATA`, te comés el `\n` antes de que llegue el evento de patrón). Lo
  más simple: en `UART_DATA` no leer nada y leer solo en `UART_PATTERN_DET`.

---

## 13. Medir tiempo y diagnosticar

```c
// Cuánto tarda algo (µs):
int64_t t0 = esp_timer_get_time();
hacer_algo();
int64_t dt_us = esp_timer_get_time() - t0;

// Cuánto stack le SOBRÓ como mínimo a una tarea (bytes). Si da < 500: subir el stack.
UBaseType_t libre = uxTaskGetStackHighWaterMark(NULL);   // NULL = la tarea actual
UBaseType_t libre_otra = uxTaskGetStackHighWaterMark(s_tarea_adc);

// Memoria libre (heap):
uint32_t heap = esp_get_free_heap_size();               // #include "esp_system.h"
uint32_t minimo = esp_get_minimum_free_heap_size();     // el peor momento desde el arranque

// Cantidad de tareas, núcleo actual:
UBaseType_t n = uxTaskGetNumberOfTasks();
BaseType_t core = xPortGetCoreID();
```

Un buen comando de consola para el parcial (`estado` o `info`):

```c
if (strcmp(argv[0], "info") == 0) {
    consola_printf("tareas: %u  heap libre: %lu  minimo: %lu\r\n",
                   (unsigned)uxTaskGetNumberOfTasks(),
                   (unsigned long)esp_get_free_heap_size(),
                   (unsigned long)esp_get_minimum_free_heap_size());
    consola_printf("stack libre adc: %u bytes\r\n",
                   (unsigned)uxTaskGetStackHighWaterMark(s_tarea_adc));
    return true;
}
```

> `vTaskList()` (tabla de todas las tareas con estado y stack) y `vTaskGetRunTimeStats()`
> (% de CPU) **están apagadas** en este `sdkconfig`: necesitan
> `CONFIG_FREERTOS_USE_TRACE_FACILITY` y `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS`
> (y `GENERATE_RUN_TIME_STATS` para el %), activables en `idf.py menuconfig` →
> Component config → FreeRTOS → Kernel. Si el profe lo pide, es cambiar eso y llamar
> `vTaskList(buf)` con un `buf` de ~1 KB.

---

## 14. Patrones de ejercicio (combinados con WiFi)

Estos son los esqueletos que más probablemente aparezcan. Todos se integran con el sistema:
los comandos van en `comandos()`, lo que se ve en la web/Qt son variables del registro.

### A. Modos de funcionamiento por comando (event group)

```c
#define MODO_ESPEJO     (1 << 0)
#define MODO_GRABANDO   (1 << 1)
#define MODO_REPRODUCE  (1 << 2)
#define TODOS_LOS_MODOS (MODO_ESPEJO | MODO_GRABANDO | MODO_REPRODUCE)

static EventGroupHandle_t s_modos;

static void cambiar_modo(EventBits_t modo, const char *nombre)
{
    xEventGroupClearBits(s_modos, TODOS_LOS_MODOS);
    if (modo != 0) {
        xEventGroupSetBits(s_modos, modo);
    }
    var_set("estado", nombre);                  // se ve en la web y en Qt
}

// en comandos():
//   "modo espejo" → cambiar_modo(MODO_ESPEJO, "espejo");
//   "modo stop"   → cambiar_modo(0, "detenido");

static void tarea_espejo(void *arg)
{
    for (;;) {
        xEventGroupWaitBits(s_modos, MODO_ESPEJO, pdFALSE, pdFALSE, portMAX_DELAY);
        TickType_t ultimo = xTaskGetTickCount();            // recién despertó
        while (xEventGroupGetBits(s_modos) & MODO_ESPEJO) {
            float pot;
            char txt[16];
            var_get("pot1", &pot);
            snprintf(txt, sizeof(txt), "%.0f", pot);
            var_set("led1", txt);                           // pote → brillo
            xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(20));
        }
    }
}
```

Bonus: si querés que el modo también se pueda cambiar desde la web, registrá un `var_txt`
o `var_num` "modo" y que una tarea lo mire; pero el comando de consola es lo que el profe
suele pedir.

### B. Comando → cola → tarea (la consola nunca se bloquea)

Un comando que dispara algo largo (un movimiento de 3 s) **no** lo hace él: lo encola y
contesta al toque. Así la consola sigue respondiendo.

```c
typedef struct { float destino; uint32_t duracion_ms; } orden_t;
static QueueHandle_t s_ordenes;                         // xQueueCreate(4, sizeof(orden_t))

// en comandos():  "mover 80 2000"
if (strcmp(argv[0], "mover") == 0) {
    if (argc != 3) {
        consola_printf("uso: mover <0-100> <ms>\r\n");
        return true;
    }
    orden_t o = { strtof(argv[1], NULL), strtoul(argv[2], NULL, 10) };
    if (o.destino < 0 || o.destino > 100) {
        consola_printf("ERROR: fuera de rango\r\n");
    } else if (xQueueSend(s_ordenes, &o, 0) != pdTRUE) {
        consola_printf("ERROR: ocupado\r\n");
    } else {
        consola_printf("OK\r\n");
    }
    return true;
}

// la tarea interpola linealmente de donde está al destino:
static void tarea_mover(void *arg)
{
    orden_t o;
    float actual = 0;
    char txt[16];
    for (;;) {
        xQueueReceive(s_ordenes, &o, portMAX_DELAY);
        int pasos = o.duracion_ms / 20;
        if (pasos < 1) {
            pasos = 1;
        }
        float inicio = actual;
        TickType_t ultimo = xTaskGetTickCount();
        for (int i = 1; i <= pasos; i++) {
            actual = inicio + (o.destino - inicio) * i / pasos;
            snprintf(txt, sizeof(txt), "%.1f", actual);
            var_set("led1", txt);
            xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(20));
        }
    }
}
```

### C. Muestreo periódico + filtro + "último valor"

```c
static QueueHandle_t s_pot_filtrado;                    // xQueueCreate(1, sizeof(float))

static void tarea_muestreo(void *arg)
{
    float filtrado = 0;
    TickType_t ultimo = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(20));    // 50 Hz exactos
        float crudo;
        var_get("pot1", &crudo);
        filtrado = 0.8f * filtrado + 0.2f * crudo;      // filtro pasa bajos (EMA)
        xQueueOverwrite(s_pot_filtrado, &filtrado);     // el consumidor ve siempre el último
    }
}
```

Filtro de **media móvil** de N muestras, por si lo piden:

```c
#define N 8
static float s_ventana[N];
static int s_idx = 0;
static float s_suma = 0;

static float media_movil(float nueva)
{
    s_suma += nueva - s_ventana[s_idx];     // saco la más vieja, sumo la nueva
    s_ventana[s_idx] = nueva;
    s_idx = (s_idx + 1) % N;
    return s_suma / N;
}
```

### D. Grabar y reproducir (teach & playback)

```c
#define MUESTRAS_MAX 500

typedef struct {
    float valor;
    uint32_t dt_ms;                 // tiempo desde la muestra anterior
} muestra_t;

static muestra_t s_grabacion[MUESTRAS_MAX];
static int s_cant_grabada = 0;
static SemaphoreHandle_t s_mutex_grab;          // xSemaphoreCreateMutex()

static void tarea_grabar(void *arg)
{
    for (;;) {
        xEventGroupWaitBits(s_modos, MODO_GRABANDO, pdFALSE, pdFALSE, portMAX_DELAY);
        xSemaphoreTake(s_mutex_grab, portMAX_DELAY);
        s_cant_grabada = 0;
        xSemaphoreGive(s_mutex_grab);
        int64_t antes = esp_timer_get_time();
        TickType_t ultimo = xTaskGetTickCount();
        while ((xEventGroupGetBits(s_modos) & MODO_GRABANDO) && s_cant_grabada < MUESTRAS_MAX) {
            xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(50));
            int64_t ahora = esp_timer_get_time();
            muestra_t m = { .dt_ms = (ahora - antes) / 1000 };
            antes = ahora;
            var_get("pot1", &m.valor);
            xSemaphoreTake(s_mutex_grab, portMAX_DELAY);
            s_grabacion[s_cant_grabada++] = m;
            xSemaphoreGive(s_mutex_grab);
        }
        cambiar_modo(0, "grabado");             // terminó (por stop o por memoria llena)
    }
}

static void tarea_reproducir(void *arg)
{
    char txt[16];
    for (;;) {
        xEventGroupWaitBits(s_modos, MODO_REPRODUCE, pdFALSE, pdFALSE, portMAX_DELAY);
        xSemaphoreTake(s_mutex_grab, portMAX_DELAY);
        int cant = s_cant_grabada;
        xSemaphoreGive(s_mutex_grab);
        // leer s_grabacion sin mutex está bien acá: los modos son excluyentes
        // (cambiar_modo apaga GRABANDO), así que nadie escribe mientras se reproduce
        for (int i = 0; i < cant && (xEventGroupGetBits(s_modos) & MODO_REPRODUCE); i++) {
            vTaskDelay(pdMS_TO_TICKS(s_grabacion[i].dt_ms));   // respetar los tiempos grabados
            snprintf(txt, sizeof(txt), "%.0f", s_grabacion[i].valor);
            var_set("led1", txt);
        }
        cambiar_modo(0, "reproducido");
    }
}
```

(Acá `vTaskDelay` está bien: el tiempo a esperar **es** el dato grabado. Si querés
precisión sin acumular error, guardá el tiempo absoluto desde el inicio y usá
`xTaskDelayUntil` con `ultimo` arrancado al comenzar.)

### E. Pulsador por interrupción → notificación → acción

Igual que la receta I de `GUIA.md` pero con notificación (un solo pulsador):

```c
static TaskHandle_t s_tarea_boton;

static void IRAM_ATTR al_apretar_sw2(void *arg)
{
    BaseType_t despertada = pdFALSE;
    vTaskNotifyGiveFromISR(s_tarea_boton, &despertada);
    if (despertada) {
        portYIELD_FROM_ISR();
    }
}

static void tarea_boton(void *arg)
{
    bool suelto = true;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(50));          // antirrebote
        ulTaskNotifyTake(pdTRUE, 0);            // descartar los avisos del rebote
        bool nivel = entrada_get(SW2_GPIO);
        if (nivel == suelto) {
            continue;
        }
        suelto = nivel;
        if (!nivel) {                           // recién apretado
            var_set("led3", "toggle");
        }
    }
}

// app_main (después de sistema_init y con el pin registrado con var_din):
xTaskCreate(tarea_boton, "boton", 4096, NULL, 6, &s_tarea_boton);    // ANTES de la interrupción
entrada_interrupcion(SW2_GPIO, GPIO_INTR_ANYEDGE, al_apretar_sw2, NULL);
```

### F. Gatekeeper: una sola tarea dueña de un recurso

Si varias tareas quieren usar algo que no es seguro compartir (una UART externa, una
pantalla, un buzzer con secuencias), **una sola tarea lo maneja** y las demás le mandan
pedidos por una cola. No hace falta mutex: el recurso tiene un único dueño.

```c
typedef struct { char linea[48]; } pedido_t;
static QueueHandle_t s_pedidos;

static void tarea_gatekeeper(void *arg)
{
    pedido_t p;
    for (;;) {
        xQueueReceive(s_pedidos, &p, portMAX_DELAY);
        uart_write_bytes(UART_EXT, p.linea, strlen(p.linea));   // solo esta tarea escribe
    }
}
```

### G. Alarma por umbral con histéresis

```c
static void tarea_alarma(void *arg)
{
    bool alarma = false;
    TickType_t ultimo = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&ultimo, pdMS_TO_TICKS(100));
        float pot, consigna;
        var_get("pot1", &pot);
        var_get("consigna", &consigna);         // la cambian desde la web / Qt / consola
        if (!alarma && pot > consigna + 2) {    // entra por arriba de consigna + 2
            alarma = true;
            var_set("led4", "on");
            var_set("estado", "ALARMA");
        } else if (alarma && pot < consigna - 2) {   // sale por debajo de consigna - 2
            alarma = false;
            var_set("led4", "off");
            var_set("estado", "normal");
        }
    }
}
```

### H. Productor–consumidor con prioridades

Un ejercicio clásico para ver si entendés prioridades: un productor rápido, un consumidor
lento, y qué pasa cuando la cola se llena.

```c
static QueueHandle_t s_datos;                   // xQueueCreate(10, sizeof(int))

static void productor(void *arg)
{
    int n = 0;
    for (;;) {
        if (xQueueSend(s_datos, &n, pdMS_TO_TICKS(10)) != pdTRUE) {
            // cola llena: el consumidor no da abasto → se pierde (o se cuenta como error)
        }
        n++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void consumidor(void *arg)
{
    int dato;
    for (;;) {
        xQueueReceive(s_datos, &dato, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(100));         // "procesar" tarda el doble
    }
}
```

Para explicar en el parcial: si el consumidor tiene **más** prioridad, saca cada dato apenas
llega (la cola casi siempre vacía). Si tiene **menos**, igual procesa (porque el productor se
bloquea en `vTaskDelay`), pero la cola se va llenando porque produce más rápido de lo que se
consume.

---

## 15. ¿Qué herramienta uso?

| Necesito... | Herramienta | Funciones clave |
|---|---|---|
| hacer algo cada X ms exactos | tarea + `xTaskDelayUntil` | `xTaskGetTickCount`, `xTaskDelayUntil` |
| hacer algo cada < 10 ms o a frecuencia exacta | timer de hardware → aviso a tarea | `temporizador_init`, `vTaskNotifyGiveFromISR` |
| pasar **datos** de A a B | cola | `xQueueSend`, `xQueueReceive` |
| pasar **el último valor** (sin atraso) | cola de 1 + overwrite | `xQueueOverwrite`, `xQueuePeek` |
| pasar **texto** de largo variable | message buffer | `xMessageBufferSend/Receive` |
| **avisar** a una tarea (sin datos) | notificación | `xTaskNotifyGive`, `ulTaskNotifyTake` |
| avisar a quien esté esperando | semáforo binario | `xSemaphoreGive`, `xSemaphoreTake` |
| contar eventos sin perder ninguno | semáforo contador / notificación | `xSemaphoreCreateCounting` |
| **modos** de funcionamiento | event group (bits de modo) | `SetBits`, `ClearBits`, `GetBits`, `WaitBits` |
| esperar que pasen **varias** cosas | event group (AND) | `xEventGroupWaitBits(..., pdTRUE, ...)` |
| proteger datos compartidos entre tareas | mutex | `xSemaphoreCreateMutex`, `Take`/`Give` |
| proteger un dato entre ISR y tarea | sección crítica | `taskENTER_CRITICAL(_ISR)` |
| timeout / apagar algo después de X s | timer de software one-shot | `xTimerCreate(..., pdFALSE, ...)`, `xTimerReset` |
| parpadeo / acción periódica liviana | timer de software periódico | `xTimerCreate(..., pdTRUE, ...)` |
| que la consola no se trabe con algo largo | cola de órdenes a una tarea (14.B) | |
| un recurso que no se puede compartir | tarea gatekeeper (14.F) | |

---

## 16. Problemas clásicos y errores típicos

### Conceptos que te pueden preguntar

- **Condición de carrera (race condition):** dos tareas leen-modifican-escriben el mismo
  dato a la vez y una pisa a la otra (`contador++` desde dos tareas). Solución: mutex
  (o sección crítica si hay una ISR).
- **Deadlock (abrazo mortal):** A tiene el mutex 1 y espera el 2; B tiene el 2 y espera el 1.
  Las dos quedan bloqueadas para siempre. Solución: tomar siempre en el mismo orden, o usar
  timeout en el `Take` y soltar todo si falla.
- **Inversión de prioridad:** una tarea baja (L) tiene el recurso, la alta (H) lo espera, y
  una media (M) que no lo usa le saca la CPU a L → H espera a M. Solución: **mutex** (hereda
  prioridad: L sube a la prioridad de H mientras lo tiene). Por eso para proteger se usa
  mutex y no semáforo binario.
- **Inanición (starvation):** una tarea de alta prioridad que nunca se bloquea no deja correr
  a las de abajo (en el ESP32 además dispara el watchdog de IDLE).
- **Planificación:** FreeRTOS es **apropiativo con prioridades fijas**: siempre corre la
  tarea lista de mayor prioridad; entre iguales, se turnan cada tick (round robin). En el
  ESP32 hay un scheduler por núcleo.
- **Estados de una tarea:** *Running* (corriendo), *Ready* (lista, esperando CPU), *Blocked*
  (esperando algo: delay, cola, semáforo, con o sin timeout), *Suspended* (`vTaskSuspend`,
  solo sale con `vTaskResume`).

### Errores típicos y qué significan

| Síntoma | Causa | Solución |
|---|---|---|
| `Task watchdog got triggered ... IDLE` | una tarea con un `for` que nunca se bloquea | que espere algo (`vTaskDelay`, cola, notificación) |
| `stack overflow in task X` | stack chico (`printf` y `float` piden mucho) | subir a 4096 o más; medir con `uxTaskGetStackHighWaterMark` |
| `Guru Meditation ... LoadProhibited` | puntero NULL: usaste una cola/semáforo/handle **antes de crearlo** | crear los objetos **antes** de las tareas y de habilitar interrupciones |
| La tarea nunca corre | prioridad más baja que una que no se bloquea | revisar prioridades y bloqueos |
| `vTaskDelay(pdMS_TO_TICKS(5))` no espera | con tick de 10 ms, 5 ms = 0 ticks | usar ≥ 10 ms, o timer de hardware / `esp_timer` |
| El período se va corriendo | `vTaskDelay` en una tarea periódica | `xTaskDelayUntil` |
| Crash en una ISR | `printf`, `var_set`, mutex o algo que no es `FromISR` dentro de la ISR | la ISR solo avisa; el trabajo en la tarea |
| `xTimerChangePeriod` arrancó un timer que estaba parado | así funciona: cambiar el período lo arranca | llamar `xTimerStop` después si no lo querías andando |
| Un dato de la cola llega "cambiado" | mandaste un puntero a un buffer que después se modificó | mandar el dato por copia (struct con arreglo) |
| Se pierden avisos | semáforo binario o `ulTaskNotifyTake(pdTRUE)` cuando llegan varios | semáforo contador o `ulTaskNotifyTake(pdFALSE)` |
| Una tarea que termina hace crash | `return` en una tarea | `vTaskDelete(NULL)` al final |
| Mutex que queda tomado | un `return` de error entre `Take` y `Give`, o `vTaskSuspend` de la dueña | `Give` en todos los caminos; no suspender tareas con recursos |

### Checklist antes de entregar

- [ ] Ninguna tarea periódica con `vTaskDelay`: todas con `xTaskDelayUntil`.
- [ ] Ninguna tarea "mira cada tanto" una variable: esperan una cola, bit o notificación.
- [ ] Cada dato compartido entre tareas tiene su mutex (o va por cola).
- [ ] Las ISR son cortas, `IRAM_ATTR`, solo `FromISR`, con `despertada`.
- [ ] Colas, semáforos y event groups creados **antes** de las tareas que los usan.
- [ ] Prioridades justificadas (control > consola > fondo) y stacks de 4096 con `printf`.
- [ ] Los comandos validan `argc` y rangos, y contestan `ERROR: ...` si algo está mal.
- [ ] Lo importante se ve en la web y en Qt (variables del registro, `estado`).
