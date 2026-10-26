# Guía de C para el parcial

Lo de C que más se usa para armar la lógica: estados, banderas, mensajes. Cada tema con
**qué cuidar cuando lo comparten varias tareas** (eso es lo que el profe mira).

Para FreeRTOS ver `GUIA_FREERTOS.md`; para el proyecto, `GUIA.md`.

## Índice

1. [Tipos enteros (`stdint.h`)](#1-tipos-enteros-stdinth)
2. [`typedef` y `struct`](#2-typedef-y-struct)
3. [`enum` y máquinas de estado](#3-enum-y-máquinas-de-estado)
4. [Banderas con máscaras de bits](#4-banderas-con-máscaras-de-bits)
5. [Campos de bits (`struct` con `:1`)](#5-campos-de-bits-struct-con-1)
6. [`union`](#6-union)
7. [Compartir datos entre tareas sin romper nada](#7-compartir-datos-entre-tareas-sin-romper-nada)
8. [Otras cosas de C que aparecen siempre](#8-otras-cosas-de-c-que-aparecen-siempre)
9. [Errores típicos](#9-errores-típicos)

---

## 1. Tipos enteros (`stdint.h`)

En embebidos se usan tipos con **tamaño fijo** (`int` depende del procesador):

| Tipo | Bits | Rango |
|---|---|---|
| `uint8_t` / `int8_t` | 8 | 0..255 / -128..127 |
| `uint16_t` / `int16_t` | 16 | 0..65535 / -32768..32767 |
| `uint32_t` / `int32_t` | 32 | 0..4.294.967.295 / ±2.147.483.647 |
| `bool` (`stdbool.h`) | 8 | `true` / `false` |

En el ESP32 `int` es de 32 bits. `#include <stdint.h>` y `<stdbool.h>` ya vienen con
`FreeRTOS.h`.

**Desborde:** un `uint8_t` en 255 + 1 vuelve a 0, sin aviso. Un contador de apretadas en
`uint8_t` se "resetea" solo a la vuelta 256.

```c
uint8_t c = 250;
c += 10;            // c = 4 (260 - 256)
```

---

## 2. `typedef` y `struct`

`struct` agrupa datos; `typedef` le pone nombre al tipo para no escribir `struct` cada vez.
Convención: los tipos terminan en `_t`.

```c
typedef struct {
	uint8_t led;        // 0..3
	bool prendido;
	uint16_t tiempo_ms;
} orden_t;

orden_t o = { .led = 2, .prendido = true, .tiempo_ms = 500 };   // inicializar por nombre
orden_t vacia = {0};                                             // todo en cero

o.led = 1;              // acceso con punto
orden_t *p = &o;
p->led = 3;             // con puntero: flecha (es lo mismo que (*p).led)

orden_t copia = o;      // copiar con = SÍ se puede
// if (copia == o)      // comparar con == NO se puede: campo por campo o memcmp
```

Un `struct` es lo que normalmente viaja por una **cola**: `xQueueCreate(5, sizeof(orden_t))`.

---

## 3. `enum` y máquinas de estado

### `enum`

Le da nombre a una lista de valores enteros (0, 1, 2... si no decís otra cosa):

```c
typedef enum {
	ESTADO_APAGADO,         // 0
	ESTADO_PRENDIDO,        // 1
	ESTADO_PARPADEO,        // 2
} estado_t;

estado_t estado = ESTADO_APAGADO;
```

Ventaja sobre `#define`: el compilador sabe que son del mismo tipo, y en un `switch` te
avisa (`-Wswitch`) si te olvidaste de algún caso.

Truco: un último elemento cuenta cuántos hay.

```c
typedef enum { MODO_A, MODO_B, MODO_C, MODO_CANTIDAD } modo_t;   // MODO_CANTIDAD = 3

const char *nombres[MODO_CANTIDAD] = { "A", "B", "C" };          // nombre de cada modo
modo = (modo + 1) % MODO_CANTIDAD;                               // pasar al siguiente, en ronda
```

### Máquina de estados

Una máquina de estados es: **en qué estado estoy** + **qué evento llegó** → **a qué estado
paso y qué hago**. Se escribe con un `switch (estado)` y, adentro, qué hacer con cada evento.

Ejemplo: el LED 1 tiene tres modos y **SW1 los va rotando**: apagado → prendido → parpadeo →
apagado...

```
          SW1              SW1              SW1
APAGADO ───────► PRENDIDO ───────► PARPADEO ───────► APAGADO
```

```c
#define PERIODO_MS		20		// cada cuanto corre la maquina
#define PARPADEO_MS		250		// medio periodo del parpadeo

typedef enum {
	ESTADO_APAGADO,
	ESTADO_PRENDIDO,
	ESTADO_PARPADEO,
} estado_t;

void task_maquina(void *pvParam)
{
	estado_t estado = ESTADO_APAGADO;
	int anterior = 1;                       // SW1 suelto (pull-up)
	int cuenta = 0;                         // para el parpadeo
	TickType_t xLastWakeTime = xTaskGetTickCount();

	while (1) {
		/* 1. Evento: SW1 paso de suelto a apretado */
		int ahora = entrada_get(SW1_GPIO);
		bool apretado = (anterior == 1 && ahora == 0);
		anterior = ahora;

		/* 2. Transiciones: segun el estado y el evento, el estado nuevo */
		if (apretado) {
			switch (estado) {
			case ESTADO_APAGADO:  estado = ESTADO_PRENDIDO; break;
			case ESTADO_PRENDIDO: estado = ESTADO_PARPADEO; cuenta = 0; break;
			case ESTADO_PARPADEO: estado = ESTADO_APAGADO;  break;
			}
		}

		/* 3. Acciones: lo que se hace mientras se esta en cada estado */
		switch (estado) {
		case ESTADO_APAGADO:
			salida_set(LED1_GPIO, 0);
			break;
		case ESTADO_PRENDIDO:
			salida_set(LED1_GPIO, 1);
			break;
		case ESTADO_PARPADEO:
			cuenta += PERIODO_MS;
			if (cuenta >= PARPADEO_MS) {
				cuenta = 0;
				salida_set(LED1_GPIO, !salida_get(LED1_GPIO));
			}
			break;
		}

		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PERIODO_MS));
	}
}
```

Reglas para que quede prolija:
- **Un solo lugar cambia el estado** (esta tarea). Si otra tarea quiere cambiarlo, le
  **manda un evento** por cola o notificación; no escribe `estado` directamente.
- El estado es una **variable local de la tarea** → nadie más la toca → no hace falta
  mutex.
- Separar **transiciones** (cambio de estado) de **acciones** (qué hago en cada estado).
- Para tiempos (parpadeo, "esperar 2 s en este estado") se cuentan ticks/ms en la tarea
  periódica, **no** se pone un `vTaskDelay` largo adentro de un `case` (la máquina deja de
  ver eventos mientras duerme).

### Máquina de estados que espera eventos (sin polling)

Si los eventos llegan por una cola (de otra tarea, una ISR, un comando), la tarea duerme
hasta que llega uno:

```c
typedef enum { EV_BOTON, EV_TIMEOUT, EV_PARAR } evento_t;

QueueHandle_t colaEventos;      // xQueueCreate(10, sizeof(evento_t)) en app_main

void task_maquina(void *pvParam)
{
	estado_t estado = ESTADO_APAGADO;
	evento_t ev;

	while (1) {
		xQueueReceive(colaEventos, &ev, portMAX_DELAY);     // duerme hasta un evento

		switch (estado) {
		case ESTADO_APAGADO:
			if (ev == EV_BOTON) { estado = ESTADO_PRENDIDO; salida_set(LED1_GPIO, 1); }
			break;
		case ESTADO_PRENDIDO:
			if (ev == EV_BOTON || ev == EV_PARAR) { estado = ESTADO_APAGADO; salida_set(LED1_GPIO, 0); }
			break;
		default:
			break;
		}
	}
}

// otra tarea, o un comando:   evento_t ev = EV_BOTON;  xQueueSend(colaEventos, &ev, 0);
```

Si además necesitás un tiempo ("si en 5 s no apretás, apagar"), usá el timeout de la cola:
`xQueueReceive(colaEventos, &ev, pdMS_TO_TICKS(5000))` devuelve `pdFALSE` si no llegó nada
→ eso es tu `EV_TIMEOUT`.

---

## 4. Banderas con máscaras de bits

Una variable entera usada como **8, 16 o 32 interruptores**. Cada bit es una bandera.

### Definir las banderas

```c
#define FLAG_ALARMA		(1 << 0)		// 0b00000001
#define FLAG_GRABANDO	(1 << 1)		// 0b00000010
#define FLAG_WIFI_OK	(1 << 2)		// 0b00000100
#define FLAG_ERROR		(1 << 3)		// 0b00001000

uint8_t flags = 0;
```

ESP-IDF ya trae `BIT0`, `BIT1`... `BIT31` (en `esp_bit_defs.h`): `#define FLAG_ALARMA BIT0`.
Siempre entre paréntesis: `(1 << 3)`, por la precedencia de operadores.

### Las 4 operaciones

| Qué | Cómo | Operador |
|---|---|---|
| **Prender** un bit | `flags \|= FLAG_ALARMA;` | OR: pone en 1 sin tocar los demás |
| **Apagar** un bit | `flags &= ~FLAG_ALARMA;` | AND con la máscara invertida |
| **Invertir** un bit | `flags ^= FLAG_ALARMA;` | XOR |
| **Preguntar** si está | `if (flags & FLAG_ALARMA)` | AND: distinto de 0 = prendido |

Varias a la vez:

```c
flags |= FLAG_ALARMA | FLAG_ERROR;                      // prender dos
if ((flags & (FLAG_ALARMA | FLAG_ERROR)) == (FLAG_ALARMA | FLAG_ERROR)) { }  // ¿las dos?
if (flags & (FLAG_ALARMA | FLAG_ERROR)) { }             // ¿alguna de las dos?
```

Con un índice (bit n):

```c
flags |= (1 << n);          // prender el bit n
if (flags & (1 << n)) { }   // ¿está el bit n?
```

Ejemplo útil: el estado de los 4 switches en **un solo byte**, para mandarlo por una cola:

```c
uint8_t sw = 0;
for (int i = 0; i < SW_COUNT; i++) {
	if (entrada_get(switches[i]) == 0) {    // apretado
		sw |= (1 << i);
	}
}
// sw = 0b0101 -> apretados SW1 y SW3
```

### ¿Y si las banderas las usan varias tareas?

`flags |= X` **no es una sola instrucción**: es *leer → modificar → escribir*. Si dos tareas
lo hacen a la vez, una puede pisar el cambio de la otra:

```
task A: lee flags (0000)
                              task B: lee flags (0000)
                              task B: escribe 0010
task A: escribe 0001          ← se perdió el bit de B
```

Soluciones, de mejor a peor:
1. **Event group** (`GUIA_FREERTOS.md`, sección 6): son exactamente banderas de bits, pero
   `xEventGroupSetBits` / `ClearBits` son seguras entre tareas e ISR, y además una tarea
   puede **dormir esperando** que se prendan (`xEventGroupWaitBits`). Si las banderas las
   comparten tareas, casi siempre es la respuesta.
2. **Task notify con `eSetBits`**: si las banderas son para **una sola tarea** que las
   espera:
   ```c
   xTaskNotify(xTaskControl, FLAG_ALARMA, eSetBits);          // quien avisa (FromISR en ISR)

   uint32_t recibidas;
   xTaskNotifyWait(0, 0xFFFFFFFF, &recibidas, portMAX_DELAY); // la tarea: espera y limpia todo
   if (recibidas & FLAG_ALARMA) { ... }
   ```
3. **Variable propia + mutex o sección crítica** alrededor de cada `|=` / `&=`
   (sección 7).

---

## 5. Campos de bits (`struct` con `:1`)

Otra forma de escribir banderas: el compilador empaqueta los campos en bits.

```c
typedef struct {
	uint8_t alarma   : 1;      // 1 bit: 0 o 1
	uint8_t grabando : 1;
	uint8_t wifi_ok  : 1;
	uint8_t modo     : 2;      // 2 bits: 0..3
	uint8_t          : 3;      // relleno sin nombre
} estado_bits_t;               // ocupa 1 byte

estado_bits_t e = {0};
e.alarma = 1;                  // se lee como un campo normal
if (e.wifi_ok) { }
e.modo = 3;                    // 0..3; si ponés 4 se trunca a 0 (sin aviso)
```

Ventaja: se lee mucho más claro que las máscaras.

Desventajas (importan en el parcial):
- **No es atómico**: `e.alarma = 1` también es *leer el byte → cambiar un bit → escribir el
  byte*. Si otra tarea cambia `e.grabando` al mismo tiempo, se pisan **aunque sean campos
  distintos** (están en el mismo byte). Mismo problema que `flags |= X`, pero escondido.
- **No se puede tomar la dirección** de un campo: `&e.alarma` no compila.
- **El orden de los bits depende del compilador**: no sirve para armar un byte que se manda
  a otro equipo ni para mapear un registro (para eso, máscaras).
- No se pueden usar como bits de un event group.

**Cuándo usarlos:** banderas que maneja **una sola tarea** (variables locales) o datos que
viajan **copiados** por una cola. Si los comparten varias tareas: event group o mutex.

Se pueden combinar con una `union` para ver el byte entero (sección 6).

---

## 6. `union`

Todos los campos de una `union` **comparten la misma memoria**: es una sola cosa vista de
varias formas. Ocupa lo que el campo más grande.

```c
typedef union {
	float valor;
	uint8_t bytes[4];
} float_bytes_t;

float_bytes_t u;
u.valor = 3.5f;
// u.bytes[0..3] son los 4 bytes de ese float (el ESP32 es little-endian:
// bytes[0] es el menos significativo)
```

### Uso 1: ver un dato como bytes (mandar por UART/TCP)

```c
float_bytes_t u = { .valor = temperatura };
tcp_enviar(sock, u.bytes, 4);       // manda el float crudo, 4 bytes
```

### Uso 2: banderas por nombre y como byte entero

```c
typedef union {
	uint8_t todo;                    // el byte entero
	struct {
		uint8_t alarma   : 1;
		uint8_t grabando : 1;
		uint8_t wifi_ok  : 1;
		uint8_t          : 5;
	};                               // struct anónimo: se accede directo (f.alarma)
} flags_t;

flags_t f = { .todo = 0 };           // limpiar todo de una
f.alarma = 1;                        // un bit por nombre
if (f.todo != 0) { }                 // ¿alguna bandera prendida?
```

### Uso 3: mensajes de distinto tipo por **una sola cola** (la más útil)

Un `enum` dice **qué tipo** de mensaje es, y la `union` guarda **los datos de ese tipo**:

```c
typedef enum {
	MSG_LED,
	MSG_PWM,
	MSG_TEXTO,
} tipo_msg_t;

typedef struct {
	tipo_msg_t tipo;                 // qué campo de la union es válido
	union {
		struct { uint8_t n; bool prendido; } led;   // para MSG_LED
		float duty;                                   // para MSG_PWM
		char texto[16];                               // para MSG_TEXTO
	};
} mensaje_t;

QueueHandle_t colaMensajes;          // xQueueCreate(5, sizeof(mensaje_t))

// quien manda
mensaje_t m = { .tipo = MSG_LED, .led = { .n = 2, .prendido = true } };
xQueueSend(colaMensajes, &m, 0);

m.tipo = MSG_PWM;
m.duty = 37.5f;
xQueueSend(colaMensajes, &m, 0);

// la tarea que recibe
mensaje_t r;
xQueueReceive(colaMensajes, &r, portMAX_DELAY);
switch (r.tipo) {
case MSG_LED:   salida_set(leds[r.led.n], r.led.prendido); break;
case MSG_PWM:   pwm_set_duty(LED_PWM_GPIO, r.duty);         break;
case MSG_TEXTO: printf("%s\n", r.texto);                   break;
}
```

Así una tarea "dueña" del hardware recibe órdenes de muchas fuentes (comandos, web,
botones) por una sola cola, y nadie más toca ese hardware: **no hace falta mutex**.

Regla de la `union`: **leé el mismo campo que escribiste** (por eso el `tipo`). Si escribís
`duty` y leés `led`, leés basura.

---

## 7. Compartir datos entre tareas sin romper nada

### El problema

Dos tareas (o una tarea y una ISR) que tocan **la misma variable**, y al menos una escribe.
En el ESP32-S3 hay **2 núcleos**: pueden estar corriendo literalmente al mismo tiempo.

### Qué es atómico y qué no

| Operación | ¿Atómica? |
|---|---|
| Leer o escribir un `uint8_t`, `uint16_t`, `uint32_t`, `int`, `bool`, puntero | Sí (una instrucción) |
| `x++`, `x += 5`, `flags \|= X`, `e.alarma = 1` (campo de bits) | **No** (leer-modificar-escribir) |
| Leer o escribir un `struct`, un arreglo, un `float` + un `int` juntos | **No** (varias instrucciones) |
| `uint64_t`, `double` | **No** |

"Atómica" alcanza para **una** variable que **una** tarea escribe y otras solo leen. Para
todo lo demás hay que proteger.

### `volatile`

Le dice al compilador "esta variable puede cambiar por fuera de este código, no la guardes
en un registro". Hace falta en variables que cambia una **ISR** y lee una tarea en un loop:

```c
volatile uint32_t contador_isr = 0;     // la ISR hace contador_isr++
```

`volatile` **no** da exclusión mutua: `contador_isr++` sigue sin ser atómico. Solo evita
que el compilador "optimice" la lectura. Entre tareas que se sincronizan con colas/mutex no
hace falta (las funciones de FreeRTOS ya fuerzan a releer).

### Las 4 formas de hacerlo bien

**1. No compartir: mandar copias por una cola** (la mejor). Cada tarea tiene sus datos
locales; lo que necesita la otra se lo manda (sección 6, uso 3). Sin variables globales, no
hay nada que proteger.

**2. Mutex** (`GUIA_FREERTOS.md` sección 5): para un dato compuesto (struct, arreglo) que
leen y escriben varias tareas. **Nunca en una ISR.**

```c
SemaphoreHandle_t mutexConfig;          // xSemaphoreCreateMutex() en app_main
config_t config;                        // struct compartido

// escribir
xSemaphoreTake(mutexConfig, portMAX_DELAY);
config.umbral = 3000;
config.modo = MODO_B;
xSemaphoreGive(mutexConfig);

// leer: copiar adentro, usar afuera (el mutex se tiene el menor tiempo posible)
config_t copia;
xSemaphoreTake(mutexConfig, portMAX_DELAY);
copia = config;
xSemaphoreGive(mutexConfig);
```

**3. Sección crítica**: para 2 o 3 instrucciones, sobre todo si una **ISR** también toca el
dato. Bloquea las interrupciones (y al otro núcleo) mientras dura: tiene que ser
cortísima, sin llamadas a FreeRTOS que bloqueen, sin `printf`.

```c
portMUX_TYPE muxFlags = portMUX_INITIALIZER_UNLOCKED;   // global

// en una tarea
portENTER_CRITICAL(&muxFlags);
flags |= FLAG_ALARMA;
portEXIT_CRITICAL(&muxFlags);

// en una ISR
portENTER_CRITICAL_ISR(&muxFlags);
flags |= FLAG_ERROR;
portEXIT_CRITICAL_ISR(&muxFlags);
```

**4. Event group / task notify** para banderas (sección 4): ya son seguras y además permiten
**esperar** a que una bandera se prenda.

### ¿Qué uso?

| Lo que tenés | Herramienta |
|---|---|
| Un dato que una tarea genera y otra consume | **cola** |
| Mensajes de distinto tipo hacia una tarea | **cola** de `struct` con `enum` + `union` |
| Banderas que varias tareas prenden/esperan | **event group** |
| Avisos/banderas para **una** tarea (también desde ISR) | **task notify** (`eSetBits` o `Give/Take`) |
| Un `struct` de configuración que leen varias tareas | **mutex** + copia local |
| Un contador o flag que toca una ISR y una tarea | **sección crítica** (o notify) |
| Estado de una máquina de estados | **variable local** de su tarea; los demás mandan eventos |

---

## 8. Otras cosas de C que aparecen siempre

### `static`

```c
static int s_cuenta = 0;        // global "privada": solo se ve en este .c

void f(void)
{
	static int llamadas = 0;    // local que NO se borra al salir: conserva el valor
	llamadas++;
}
```

Ojo: una variable `static` adentro de una función que llaman **varias tareas** es
compartida (mismo problema de la sección 7).

### `const`

```c
const int MAX = 10;                 // no se puede cambiar
void mostrar(const char *texto);    // la función promete no modificar el texto
```

### Arreglos y `sizeof`

```c
int valores[8];
size_t n = sizeof(valores) / sizeof(valores[0]);   // 8 (cantidad de elementos)
```

Al pasar un arreglo a una función **llega un puntero**: adentro `sizeof` da el tamaño del
puntero (4), no del arreglo. Por eso se pasa el tamaño aparte: `f(valores, n)`.

### Strings

```c
char buf[32];
snprintf(buf, sizeof(buf), "Pote: %d", valor);     // armar texto (nunca sprintf)
if (strcmp(a, "on") == 0) { }                      // comparar: 0 = iguales
strlcpy(dest, origen, sizeof(dest));               // copiar sin pasarse
int n = atoi("123");                               // texto -> int
float f = strtof("3.5", NULL);                     // texto -> float
```

Un string siempre termina en `'\0'`: `char nombre[33]` guarda hasta **32** letras.

### División y `float`

```c
int a = 7 / 2;              // 3 (división entera: se descarta la parte decimal)
float b = 7 / 2;            // 3.0 (¡se dividió como int antes de convertir!)
float c = 7 / 2.0f;         // 3.5
int pct = valor * 100 / 4095;   // multiplicar ANTES de dividir para no perder precisión
```

`printf("%f")` y `%.2f` con `float` necesitan más stack: tarea con 4096.

### Punteros a función (callbacks)

```c
typedef void (*accion_t)(int);          // tipo: "función que recibe int y no devuelve nada"

void prender(int n) { salida_set(leds[n], 1); }
void apagar(int n)  { salida_set(leds[n], 0); }

accion_t acciones[] = { prender, apagar };
acciones[0](2);                         // llama a prender(2)
```

Así funcionan `microrl_set_execute_callback(&rl, execute)` y las ISR que se le pasan a
`entrada_interrupcion`.

### Operador ternario

```c
salida_set(LED1_GPIO, valor > umbral ? 1 : 0);
printf("%s\n", apretado ? "apretado" : "suelto");
```

### `switch`: no olvidar el `break`

```c
switch (modo) {
case MODO_A:
	hacer_a();
	break;          // sin break sigue de largo al case siguiente
case MODO_B:
	hacer_b();
	break;
default:            // si no es ninguno
	break;
}
```

### Macros con `#define`

```c
#define CUADRADO(x)   ((x) * (x))       // paréntesis en cada x y en todo
// sin paréntesis: CUADRADO(1+2) -> 1+2*1+2 = 5, no 9
```

---

## 9. Errores típicos

| Error | Qué pasa | Bien |
|---|---|---|
| `if (x = 5)` | asigna 5, siempre verdadero | `if (x == 5)` |
| `if (flags & FLAG_A == 0)` | `==` va antes que `&` | `if ((flags & FLAG_A) == 0)` |
| `flags &= FLAG_A` para apagar | apaga **todos menos** A | `flags &= ~FLAG_A` |
| `x++` compartido entre tareas | se pierden cuentas | cola, mutex o sección crítica |
| Campo de bits compartido | se pisan bits vecinos | event group o mutex |
| `xQueueSend(cola, dato, 0)` | pasa el valor como dirección | `&dato` |
| Comparar structs con `==` | no compila | campo por campo o `memcmp` |
| Leer de la `union` otro campo del que se escribió | basura | usar un `enum` de tipo |
| `char s[5] = "hola!";` | no entra el `'\0'` | `char s[6]` |
| `sizeof(arreglo)` dentro de una función | da 4 (puntero) | pasar el tamaño como parámetro |
| `uint8_t i; for (i = 0; i < 300; i++)` | `i` nunca llega a 300: loop infinito | `int i` |
| `case` sin `break` | ejecuta también el siguiente | `break` en cada `case` |
