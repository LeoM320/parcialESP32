# Guía UML para embebidos

UML es una forma **estándar** de dibujar un sistema con cajas y flechas. Cada tipo de diagrama responde una pregunta distinta. En embebidos se usan sobre todo estos:

| Diagrama | Pregunta que responde | Cuándo lo piden |
|---|---|---|
| **Estados** (máquina de estados) | ¿En qué situaciones puede estar el sistema y qué lo hace cambiar? | **Casi siempre**: modos, alarmas, protocolos |
| **Secuencia** | ¿Quién le habla a quién, y en qué orden, cuando pasa X? | ISR → tarea, comando TCP, pedido web |
| **Actividad** (flujo) | ¿Qué pasos sigue un algoritmo y dónde decide? | Lógica de un handler o de una tarea |
| **Componentes / clases** | ¿De qué módulos está hecho y quién usa a quién? | Estructura de archivos y librerías |
| **Tareas** (no es UML puro, pero lo piden) | ¿Qué tareas hay y cómo se comunican? | Arquitectura FreeRTOS |

> Regla de oro: **los nombres del diagrama son los nombres del código.** Si el estado se llama `PARPADEO`, el `enum` dice `PARPADEO`, y si el evento es `EV_SW1`, la cola lleva `EV_SW1`.

Los ejemplos de esta guía usan otro sistema distinto al del simulacro: un **LED con modos**. SW1 pasa por APAGADO → FIJO → PARPADEO, y el comando `apagar` lo apaga desde cualquier modo.

---

## 1. Diagrama de estados (el más importante)

### Elementos

```
  ●                     estado inicial (un punto lleno): por donde arranca
  ◉                     estado final (casi nunca en embebidos: el sistema no termina)

  ╭───────────────╮
  │   NOMBRE      │     estado: caja con esquinas redondeadas
  │───────────────│
  │ entry / acción│     al ENTRAR al estado (una vez)
  │ do / actividad│     MIENTRAS esta en el estado
  │ exit / acción │     al SALIR del estado (una vez)
  ╰───────────────╯

  ────────────────▶     transición, con una etiqueta:   evento [guarda] / acción
```

**La etiqueta de la transición** tiene tres partes, y todas son opcionales salvo el evento:
- **evento**: lo que pasa (`SW1`, `cmd_apagar`, `timeout`, `nivel_alto`);
- **[guarda]**: condición que tiene que ser cierta para que la transición se tome (`[modo == AUTO]`, `[pot > umbral]`);
- **/ acción**: lo que se hace al cruzar la flecha (`/ timer_start(500)`).

Ejemplo de etiqueta completa: `SW2 [modo == MANUAL] / bomba = !bomba`.

### Ejemplo: LED con modos

```
              ●
              │
              ▼
     ╭─────────────────╮        SW1          ╭─────────────────╮
     │     APAGADO     │ ──────────────────▶ │      FIJO       │
     │─────────────────│                     │─────────────────│
     │ entry / LED = 0 │                     │ entry / LED = 1 │
     ╰─────────────────╯                     ╰─────────────────╯
        ▲         ▲                                   │
        │         │ cmd_apagar                        │ SW1 / timer_start(500 ms)
        │         │ (desde FIJO y PARPADEO)           ▼
        │         │                          ╭──────────────────╮
        │         └───────────────────────── │    PARPADEO      │ ───┐
        │      SW1 / timer_stop              │──────────────────│    │ tick_timer / invertir LED
        └─────────────────────────────────── │ exit / LED = 0   │ ◀──┘ (auto-transición:
                                             ╰──────────────────╯       sale y vuelve al mismo)
```

Cómo leerlo en voz alta: *"Arranca en APAGADO. Con SW1 pasa a FIJO y prende el LED. Con otro SW1 arranca el timer y pasa a PARPADEO, donde cada tick invierte el LED. Con SW1 o `apagar` vuelve a APAGADO."*

### Histéresis y guardas

Cuando el cambio depende de un valor que se mide, se usan guardas y dos umbrales distintos. Así no oscila cuando el valor está justo en el límite:

```
     ╭──────────╮   medicion [valor >= ALTO]    ╭──────────╮
     │  NORMAL  │ ────────────────────────────▶ │  ALERTA  │
     │          │ ◀──────────────────────────── │          │
     ╰──────────╯   medicion [valor < ALTO - 5] ╰──────────╯
```

### Del diagrama al código

Cada estado es un valor del `enum` y cada flecha es un `if` dentro de un `case`. Los eventos llegan por una cola, que la puede llenar una ISR, `execute()`, un handler web o un timer:

```c
typedef enum { APAGADO, FIJO, PARPADEO } estado_t;
typedef enum { EV_SW1, EV_APAGAR, EV_TICK } evento_t;

void task_modo(void *pvParam)
{
	estado_t estado = APAGADO;					// el ● del diagrama
	evento_t ev;
	salida_set(LED1_GPIO, 0);					// entry de APAGADO
	while (1) {
		xQueueReceive(colaEventos, &ev, portMAX_DELAY);	// espera el proximo evento
		switch (estado) {
		case APAGADO:
			if (ev == EV_SW1) {
				salida_set(LED1_GPIO, 1);		// entry de FIJO
				estado = FIJO;
			}
			break;
		case FIJO:
			if (ev == EV_SW1) {
				xTimerStart(timerLed, 0);		// accion de la flecha
				estado = PARPADEO;
			} else if (ev == EV_APAGAR) {
				salida_set(LED1_GPIO, 0);
				estado = APAGADO;
			}
			break;
		case PARPADEO:
			if (ev == EV_TICK) {
				salida_set(LED1_GPIO, !salida_get(LED1_GPIO));	// auto-transicion
			} else if (ev == EV_SW1 || ev == EV_APAGAR) {
				xTimerStop(timerLed, 0);
				salida_set(LED1_GPIO, 0);		// exit de PARPADEO
				estado = APAGADO;
			}
			break;
		}
	}
}
```

### Errores típicos (te bajan puntos)
- Falta el estado inicial `●`.
- Hay **flechas sin evento**. Toda transición tiene un disparador.
- Hay **estados que en realidad son acciones** ("prender LED" no es un estado; "ENCENDIDO" sí lo es).
- Hay guardas que se pisan: con un mismo evento, dos flechas desde el mismo estado con guardas que pueden ser ciertas a la vez.
- Falta decir qué pasa con un evento en cada estado. Si se ignora, está bien no dibujarlo, pero tiene que ser una decisión tuya.

---

## 2. Diagrama de secuencia

Muestra **el orden de los mensajes** entre participantes. El tiempo corre **hacia abajo**.

### Elementos

```
  Participante          caja arriba: tarea, ISR, modulo, usuario, navegador
       │                linea de vida (punteada): el participante existe
       █                activacion (rectangulito): esta ejecutando

  ──────────▶           mensaje SINCRONICO (llamada a funcion: espera que vuelva)
  ──────────▷           mensaje ASINCRONICO (punta abierta: xQueueSend, notify, Give)
  ◀ ─ ─ ─ ─ ─           respuesta / retorno (punteada)

  ┌ alt ──────────────┐
  │ [condicion A]     │   alternativas (if / else)
  ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
  │ [si no]           │
  └───────────────────┘
  ┌ loop [cada 100 ms] ┐   repeticion
  ┌ opt [condicion]    ┐   opcional (if sin else)
```

### Ejemplo 1: comando `led 2 on` por TCP

```
  Usuario         task_tcp          microrl            execute()          iodigital
 (MobaXterm)         │                 │                   │                  │
     │ "led 2 on\r"  │                 │                   │                  │
     │──────────────▷│                 │                   │                  │
     │               █ insert_char()   │                   │                  │
     │               █────────────────▶█                   │                  │
     │               █                 █ execute(3, argv)  │                  │
     │               █                 █──────────────────▶█                  │
     │               █                 █                   █ salida_set(LED2,1)
     │               █                 █                   █─────────────────▶█
     │               █                 █                   █◀ ─ ─ ─ ─ ─ ─ ─ ─ │
     │               █   enviar("LED 2 on\r\n")            █                  │
     │◀──────────────█◀────────────────────────────────────█                  │
     │               █                 █◀ ─ ─ return 0 ─ ─ █                  │
```

### Ejemplo 2: pulsador por interrupción hasta la tarea

```
    SW1          ISR (IRAM)        colaEventos         task_modo          timerLed
     │ flanco ↓      │                  │                  │ (bloqueada en    │
     │──────────────▷█                  │                  │  xQueueReceive)  │
     │               █ xQueueSendFromISR(EV_SW1)            │                  │
     │               █─────────────────▷│                  │                  │
     │               █ portYIELD_FROM_ISR(despertar)        │                  │
     │               │                  │ ev = EV_SW1      │                  │
     │               │                  │─────────────────▷█                  │
     │               │                  │   ┌ alt ─────────█──────────────────┐
     │               │                  │   │ [estado == FIJO]                 │
     │               │                  │   │              █ xTimerStart()    │
     │               │                  │   │              █─────────────────▷│
     │               │                  │   ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
     │               │                  │   │ [estado == APAGADO]              │
     │               │                  │   │              █ salida_set(LED,1)│
     │               │                  │   └──────────────█──────────────────┘
```

Fijate qué muestra el diagrama: la ISR **no hace la lógica**. Solo manda el evento, con una flecha asincrónica. La lógica la hace la tarea.

### Ejemplo 3: POST web con validación (fragmento `alt`)

```
  Navegador          httpd (handler)        mutexConfig          memoria (NVS)
     │ POST /config {"umbral":1500}│             │                     │
     │────────────────────────────▷█             │                     │
     │                             █ leer_json() │                     │
     │   ┌ alt ────────────────────█─────────────────────────────────────┐
     │   │ [JSON valido y en rango]█             │                     │ │
     │   │                         █ Take ──────▶│                     │ │
     │   │                         █ config.umbral = 1500              │ │
     │   │                         █ Give ──────▶│                     │ │
     │   │                         █ memoria_set_u16("umbral",1500) ──▶│ │
     │   │◀ ─ ─ {"ok":true} ─ ─ ─ ─█             │                     │ │
     │   ├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
     │   │ [invalido]              █             │                     │ │
     │   │◀ ─ ─ 400 {"ok":false} ─ █             │                     │ │
     │   └─────────────────────────────────────────────────────────────────┘
```

---

## 3. Diagrama de actividad (flujo)

Es el diagrama de flujo de siempre, con la notación UML.

```
   ●                      inicio
   ╭──────────╮           accion (esquinas redondeadas)
   ◇                      decision (rombo); cada salida lleva [guarda]
   ━━━━━━━━━━             fork / join: barra gruesa (cosas en paralelo)
   ◉                      fin
```

Ejemplo, el handler `POST /config`:

```
                 ●
                 │
        ╭─────────────────╮
        │ leer_json(req)  │
        ╰─────────────────╯
                 │
                 ◇───────[NULL]──────────▶ ╭────────────────╮
                 │ [objeto ok]             │ responder 400  │──▶ ◉
                 ▼                         ╰────────────────╯
        ╭─────────────────╮                        ▲
        │ leer "umbral"   │                        │
        ╰─────────────────╯                        │
                 │                                 │
                 ◇───[no es numero o fuera de rango]┘
                 │ [0..4095]
                 ▼
        ╭─────────────────╮
        │ Take / copiar / │
        │ Give            │
        ╰─────────────────╯
                 │
        ╭─────────────────╮
        │ guardar en NVS  │
        ╰─────────────────╯
                 │
        ╭─────────────────╮
        │ cJSON_Delete    │
        │ responder ok    │──▶ ◉
        ╰─────────────────╯
```

---

## 4. Diagrama de componentes / clases (módulos)

En C no hay clases, pero cada **módulo** (`.c` + `.h`) se dibuja como una clase:
- nombre arriba;
- variables internas (`static`) con `-`;
- funciones públicas (las del `.h`) con `+`;
- la flecha **punteada** `- - ->` significa "usa a" (`#include` y llamadas).

```
                       ╭──────────────────────────╮
                       │         main.c           │
                       │──────────────────────────│
                       │ - config : config_t      │
                       │ - mutexConfig            │
                       │──────────────────────────│
                       │ + app_main()             │
                       │ - task_tcp()             │
                       │ - execute(argc, argv)    │
                       ╰──────────────────────────╯
              «use» ╱       │ «use»       │ «use»   ╲ «use»
                   ▼        ▼             ▼          ▼
 ╭──────────────────╮ ╭──────────────────╮ ╭───────────────╮ ╭─────────────────────╮
 │    iodigital     │ │   ianalogica     │ │    memoria    │ │        web          │
 │──────────────────│ │──────────────────│ │───────────────│ │─────────────────────│
 │ + salida_init()  │ │ - s_mutex        │ │ + memoria_init│ │ + web_init(uris, n) │
 │ + salida_set()   │ │ + analogica_init │ │ + get/set_str │ │ + web_leer_cuerpo() │
 │ + salida_get()   │ │ + analogica_get()│ │ + get/set_u16 │ ╰─────────────────────╯
 │ + entrada_...()  │ ╰──────────────────╯ ╰───────────────╯
 ╰──────────────────╯
```

---

## 5. Diagrama de tareas (arquitectura FreeRTOS)

No es un diagrama UML estándar, pero en embebidos es **el que más se pide junto con el de estados**. Si el enunciado no dice otra cosa, usá esta convención y explicala con una leyenda:

```
  ╭───────────────╮
  │ task_xxx      │   tarea: nombre, prioridad, periodo o que la despierta
  │ prio 2 · 100ms│
  ╰───────────────╯
  ┌───────────────┐
  │ «ISR» isr_sw  │   interrupcion
  └───────────────┘
  ═[ cola ]═══▶       cola: con el tipo y el largo (orden_t x 4)
  ( mutex )           mutex, unido con una linea al recurso que protege
  ⏱ timer 500 ms      software timer, con su callback
  ──── texto ───▶     flecha: que viaja (dato, evento, notify, llamada)
```

Ejemplo, `practicas/ejemploMicrorl.c`:

```
                          TCP :1234
   Usuario ─────────────────────────────▶ ╭────────────────────╮
                                          │ task_tcp           │
                                          │ prio 2 · por datos │
                                          │ (microrl/execute)  │
                                          ╰────────────────────╯
                                             │             │
                       Take/Give (umbral)    │             │ xQueueSend(orden_parpadeo_t)
                     ┌───────────────────────┘             ▼
                     ▼                           ═[ colaParpadeo · 4 x 8 B ]═
              ( mutexConfig ) ── protege ── [ config.umbral ]        │
                     ▲                                               │ xQueueReceive
                     │ Take/Give                                     ▼
          ╭────────────────────╮                          ╭─────────────────────╮
          │ task_umbral        │                          │ task_parpadeo       │
          │ prio 1 · 100 ms    │                          │ prio 1 · por cola o │
          ╰────────────────────╯                          │ timeout             │
            │ analogica_get(POT1)                         ╰─────────────────────╯
            │ salida_set(LED3)                                 │ salida_set(LEDn)
            ▼                                                  ▼
          hardware                                          hardware
```

Con este dibujo se ve enseguida:
- **quién comparte qué**: `config` lo usan dos tareas, por eso necesita un mutex;
- **quién despierta a quién**: `task_parpadeo` espera en la cola.

---

## 6. Cómo dibujarlo

| Dónde | Cómo |
|---|---|
| **Papel (parcial)** | Lápiz y regla. Primero las cajas, después las flechas, al final las etiquetas. Dejá espacio entre cajas para las etiquetas largas |
| **draw.io** (diagrams.net, tiene app de escritorio offline) | Panel izquierdo → "UML": están el estado inicial, el estado, la línea de vida y los fragmentos. Exportar a PNG |
| **PlantUML** (texto → dibujo; en VS Code o en plantuml.com) | Ver el ejemplo de abajo |
| **Mermaid** (texto; lo dibujan GitHub y muchos editores de Markdown) | Ver el ejemplo de abajo |
| **ASCII** (como esta guía) | Va bien en un comentario del código o en un `.md` |

El LED con modos en **PlantUML**:

```
@startuml
[*] --> APAGADO
APAGADO : entry / LED = 0
FIJO : entry / LED = 1
PARPADEO : exit / LED = 0
APAGADO --> FIJO : SW1
FIJO --> PARPADEO : SW1 / timer_start(500)
PARPADEO --> PARPADEO : tick / invertir LED
PARPADEO --> APAGADO : SW1 / timer_stop
FIJO --> APAGADO : cmd_apagar
PARPADEO --> APAGADO : cmd_apagar
@enduml
```

Y una secuencia en **Mermaid**:

```
sequenceDiagram
    participant SW as SW1
    participant ISR
    participant Q as colaEventos
    participant T as task_modo
    SW-)ISR: flanco
    ISR-)Q: xQueueSendFromISR(EV_SW1)
    Q-)T: xQueueReceive
    alt estado == FIJO
        T->>T: xTimerStart
    else estado == APAGADO
        T->>T: salida_set(LED, 1)
    end
```

---

## 7. Receta para el parcial

1. **Primero el diagrama de tareas (5 min).** Listá las tareas, las ISR, los timers y los handlers. Por cada dato compartido, preguntate quién lo escribe y quién lo lee: si son dos o más, va un **mutex**. Por cada aviso entre dos partes, elegí cola, notify o semáforo.
2. **Después el de estados (5–10 min).** Uno por cada cosa que tenga modos: la alarma, el modo AUTO/MANUAL. Escribí la etiqueta completa en cada flecha.
3. **Secuencia solo si la piden.** Elegí el caso que muestra la sincronización (ISR → tarea, web → mutex → NVS).
4. **Programá copiando los nombres del diagrama.** El `enum` sale de los estados, los `case` de las flechas, las colas y los mutex del diagrama de tareas.
5. **Al final, revisá que el código y el dibujo coincidan.** Si cambiaste algo en el código, cambialo en el dibujo.
