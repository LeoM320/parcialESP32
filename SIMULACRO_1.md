# Simulacro de parcial 1 — Control de nivel de un tanque

**Sistemas Embebidos · ESP32-S3 · FreeRTOS · ESP-IDF**
**Tiempo: 3 horas · Total: 100 puntos (+10 de bonus)**

**Permitido:** tus librerías (`main/`), tus guías (`GUIA.md`, `GUIA_FREERTOS.md`), tus ejemplos (`practicas/`), la documentación oficial de FreeRTOS y de ESP-IDF.
**No permitido:** IA.

**Punto de partida:** el `main.c` pelado y el `www/index.html` pelado. Hacé todo en esos dos archivos.

---

## Situación

Un tanque se llena con una bomba. Un sensor mide el nivel. El sistema tiene que:
- controlar la bomba solo (modo **AUTO**) o a mano (modo **MANUAL**);
- avisar si el tanque está por rebalsar;
- poder configurarse desde una consola TCP y desde una página web;
- recordar la configuración aunque se corte la luz.

## Hardware (el de tu placa)

| Elemento | Función |
|---|---|
| POT1 | Sensor de nivel: 0..4095 → **nivel 0..100 %** |
| LED1 (PWM) | **Bomba**. Encendida: brillo = velocidad configurada (%). Apagada: 0 % |
| LED2 | Encendido = modo **MANUAL** |
| LED3 | *Heartbeat*: invierte su estado cada 500 ms mientras el sistema funciona |
| LED4 | Alarma: **parpadea cada 250 ms** si la alarma está activa; fijo si está reconocida; apagado si no hay alarma |
| LED RGB | Verde = normal · Rojo = alarma activa · Amarillo = alarma reconocida · Azul = modo MANUAL sin alarma |
| SW1 | Cambia entre AUTO y MANUAL |
| SW2 | En MANUAL, prende o apaga la bomba. En AUTO no hace nada |
| SW3 | Reconoce la alarma |
| SW4 | (bonus) Si se mantiene apretado 2 s, vuelve a la configuración de fábrica |

## Configuración

| Parámetro | Por defecto | Validación |
|---|---|---|
| `min` | 30 | 0 ≤ min < max |
| `max` | 80 | min < max < alarma |
| `alarma` | 90 | max < alarma ≤ 100 |
| `vel` | 80 | 10 ≤ vel ≤ 100 |

---

## Consignas

### 1. Adquisición y control (15 pts)
- Una tarea **periódica de 100 ms** (con `xTaskDelayUntil`) lee POT1 y calcula el nivel en %.
- **Modo AUTO**, con histéresis:
  - si nivel ≤ `min`, la bomba se prende;
  - si nivel ≥ `max`, la bomba se apaga;
  - entre los dos, la bomba queda como estaba.
- **Modo MANUAL:** la bomba hace lo que se le ordene (SW2, consola o web).
- **Seguridad, en cualquier modo:** si la alarma está activa o reconocida, la bomba se apaga y no se puede prender.
- El estado del sistema y la configuración se leen y escriben desde varias tareas: **protegelos con un mutex**.

### 2. Alarma (10 pts)
- **Activación:** si nivel ≥ `alarma`, la alarma pasa a **activa**.
- **Reconocimiento:** con SW3, el comando `reconocer` o el botón de la web, una alarma activa pasa a **reconocida**.
- **Vuelta a normal:** cuando nivel < `alarma` − 5 (histéresis), la alarma vuelve a **normal**, desde activa o desde reconocida.
- El parpadeo de LED4 y el heartbeat de LED3 se hacen con **software timers**, no con tareas que hagan `vTaskDelay`.
- El LED RGB muestra el estado según la tabla de hardware.

### 3. Pulsadores por interrupción (10 pts)
- SW1, SW2 y SW3 se atienden con **interrupción** (`entrada_interrupcion`).
- La ISR no hace la lógica: avisa a una tarea con **cola o task notify** (`...FromISR` + `portYIELD_FROM_ISR`).
- Hace falta un antirrebote: ignorar el mismo pulsador si vuelve a disparar antes de 50 ms.
- Nada de `printf`, mutex ni NVS dentro de la ISR.

### 4. Consola TCP con microrl (10 pts)
Todos los comandos validan la cantidad de argumentos y los rangos. Ante un error responden `ERROR: ...` y no cambian nada.

| Comando | Respuesta / efecto |
|---|---|
| `estado` | Una línea con el mismo JSON que `GET /estado` |
| `modo auto` · `modo manual` | Cambia el modo |
| `bomba on` · `bomba off` | Solo en MANUAL y sin alarma. Si no se puede: `ERROR: ...` |
| `config` | Muestra `min=30 max=80 alarma=90 vel=80` |
| `config <min> <max> <alarma> <vel>` | Valida todo junto. Si está bien, aplica y guarda en NVS |
| `reconocer` | Reconoce la alarma (`ERROR` si no hay alarma activa) |
| `help` | Lista de comandos |

### 5. Página web (15 pts)
Handlers en `main.c`, que **siempre responden JSON** (cJSON).

| Dirección | Método | Cuerpo | Respuesta |
|---|---|---|---|
| `/estado` | GET | — | `{"nivel":57,"bomba":true,"vel":80,"modo":"auto","alarma":"normal","min":30,"max":80,"lim":90}` |
| `/modo` | POST | `{"modo":"manual"}` | `{"ok":true}` |
| `/bomba` | POST | `{"on":true}` | `{"ok":true}` o `{"ok":false,"error":"..."}` |
| `/config` | POST | `{"min":30,"max":80,"alarma":90,"vel":80}` | `{"ok":true}` o `{"ok":false,"error":"..."}` |
| `/reconocer` | POST | `{}` | `{"ok":true}` |

Valores posibles: `"modo"` es `"auto"` o `"manual"`; `"alarma"` es `"normal"`, `"activa"` o `"reconocida"`.

Errores:
- un JSON mal formado o con tipos incorrectos → **400**;
- una orden que no se puede cumplir (bomba en AUTO, config inválida) → `{"ok":false,"error":"..."}`.

La página (`index.html`) tiene que:
- actualizarse sola **cada 500 ms**;
- mostrar el nivel con número y barra (`<progress>`), el estado de la bomba, el modo y la alarma en texto con color (rojo si está activa);
- tener un `<select>` o radios para el modo, botones **Bomba ON / OFF** y un botón **Reconocer alarma** que solo se ve si la alarma está activa;
- tener un formulario de configuración que se llena con los valores actuales al cargar la página, y que muestra el error si el ESP lo rechaza.

### 6. Persistencia NVS (10 pts)
- `min`, `max`, `alarma`, `vel` y el modo se guardan en NVS **solo cuando cambian**.
- Al arrancar se cargan. Si no existen, se usan los valores por defecto.
- **Prueba:** cambiá la config, reiniciá la placa (botón RST) y verificá que se mantiene.

### 7. Preguntas teóricas (10 pts, 2 c/u)
Respondelas en un comentario al final de `main.c`.
1. ¿Por qué no se puede tomar un mutex dentro de una ISR? ¿Qué usarías en su lugar?
2. ¿Qué diferencia hay entre `vTaskDelay(100)` y `xTaskDelayUntil(&t, 100)` en una tarea que tarda 30 ms en hacer su trabajo?
3. Si el callback de un software timer hace `vTaskDelay(1000)`, ¿qué pasa con los demás timers?
4. ¿Por qué en un handler web que lee una variable compartida conviene copiarla bajo mutex y armar el JSON afuera?
5. ¿Qué pasa si escribís en NVS dentro del `while(1)` de una tarea de 100 ms? ¿Cómo lo evitás?

### 8. Calidad (5 pts)
- La sincronización es correcta: no hay variables compartidas sin proteger y no hay espera bloqueante en ISRs ni en callbacks.
- Todo se valida antes de aplicarse. No hay memoria perdida (cada `cJSON_Parse`/`Create` con su `Delete`, cada `Print` con su `free`).
- Estilo de la cátedra: prototipos arriba, `task_xxx` con `while(1)`, `tskIDLE_PRIORITY + n`.

### 9. Diseño en UML (15 pts)
Entregalo en `UML.md` (en ASCII), en draw.io (exportado a PNG dentro de `uml/`) o en papel con foto. Usá la notación de `GUIA_UML.md`. **Conviene hacerlo antes de programar.**

- **a) Diagramas de estados (6 pts)**
  - Uno para la **alarma** (normal / activa / reconocida) y otro para el **modo** (AUTO / MANUAL, con el estado de la bomba).
  - Cada transición lleva la etiqueta completa `evento [guarda] / acción`.
  - Tienen que aparecer: el estado inicial, la histéresis, y lo que pasa con LED4, el RGB y la bomba.
- **b) Diagramas de secuencia (5 pts)**
  1. Desde que se aprieta SW3 hasta que el RGB cambia a amarillo: ISR, mecanismo de aviso, tarea, mutex, timer.
  2. `POST /config`, con un fragmento `alt` para config válida (mutex + NVS + respuesta) y config inválida (respuesta con error).
- **c) Diagrama de tareas (4 pts)**
  - Todas las tareas con prioridad y período (o qué las despierta), las ISR, las colas o notify (con el tipo de dato), el mutex y el recurso que protege, los software timers con su callback y los handlers web (que corren en la tarea `httpd`).
  - Una leyenda con la convención que usaste.
- **Coherencia:** si el código no coincide con los diagramas (otros estados, otro mecanismo de aviso), se descuenta.

### Bonus (+10)
- **(+5)** SW4 mantenido 2 s vuelve a la config de fábrica y la guarda en NVS. Soltarlo antes no hace nada.
- **(+5)** Historial de los últimos 10 eventos, en un buffer circular: cambio de modo, alarma activada, alarma reconocida, cambio de config, cada uno con su tiempo en segundos desde el arranque.
  - Comando `historial`.
  - `GET /historial` devuelve un arreglo JSON.
  - La página lo muestra en una tabla.

---

## Cómo lo vamos a corregir

Cuando termines (o se te acabe el tiempo), decime **"corregí el simulacro"**. Voy a:
1. leer tu `main.c`, tu `index.html` y tus diagramas, y puntuar cada consigna con esta grilla;
2. si me dejás, compilar, flashear y probar consola y web en la placa;
3. marcarte los errores de sincronización o de memoria con la línea exacta.

**Consejo de estrategia:** primero los diagramas (9, unos 20 min), después programá por partes probando cada una antes de seguir (1 → 2 → 3 → 4 → 5 → 6). Una parte que anda vale más que tres a medias. Anotá la hora de inicio.
