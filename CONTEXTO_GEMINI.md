# Contexto para seguir el proyecto (leer completo antes de tocar nada)

Este archivo resume todo lo que hace falta saber para continuar el firmware del
parcial. Lo escribió Claude al pasar la posta. Leelo entero, después leé
`main/main.c` completo, y recién ahí proponé el próximo paso.

---

## 1. Quién es el usuario y cómo trabaja

- Estudiante de Ingeniería en Mecatrónica (UNER), materia **Sistemas Embebidos**.
  Está preparando el **parcial final**: firmware para un SCARA 2DOF simulado.
- **Respondé siempre en español** (rioplatense, informal está bien).
- Trabaja **paso a paso**: él dice qué etapa sigue, vos la hacés, compilás y
  esperás a que la pruebe. **No avances etapas por tu cuenta.**
- En el examen **no puede usar IA**: el código tiene que ser simple y entendible
  por él. Preferí pocas funciones, nada de capas de abstracción.
- Las pruebas temporales se marcan `// PRUEBA TEMPORAL` y **se sacan** cuando
  él confirma que salió bien ("después la sacás").
- **No cambies `sdkconfig`, particiones, tick rate, frecuencia de CPU ni nada
  fuera de lo pedido** sin preguntar antes. Ya se enojó por eso.
- No hagas commits salvo que te lo pida.

## 2. Proyecto, herramientas y comandos

- Proyecto: `C:\Espressif\ParcialESP32` (ESP-IDF **v6.0.2**, chip **esp32s3**,
  placa DevKitC-1). La copia que está en `ClaudeProjects\ParcialEmbebidos\ParcialESP32` es vieja: ignorarla.
- Enunciado: `C:\Users\meola\Downloads\Parcial Sistemas Embebidos 26102026.pdf`
  (resumen en la sección 4).
- Simulador del profe: `C:\Users\meola\Desktop\MyScara\MyScara.exe` (Qt6, solo
  puerto serie). Va por **COM6 a 115200**, el mismo puerto por el que se flashea.
- Compilar (PowerShell):
  ```
  . C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1 *> $null; cd C:\Espressif\ParcialESP32; idf.py build
  ```
- Flashear (tenés permiso): `idf.py -p COM6 flash`. Si dice "Acceso denegado"
  el puerto lo tiene MyScara o un monitor abierto: **pedile que lo cierre**, no
  mates procesos. Después de flashear él vuelve a abrir MyScara.
- **Siempre compilar después de cada cambio** y reportar errores/warnings.
- Python con pypdf etc.: `C:\Espressif\tools\python\v6.0.2\venv\Scripts\python.exe`.
- La ESP en la red de prueba: `192.168.123.38`, servidor TCP en el puerto 30000.

### Trampa conocida (le costó un parcial)
El autocompletado del IDE (Espressif-IDE / clangd) **inserta solo** includes como
`freertos/FreeRTOSConfig_arch.h`, `freertos/projdefs.h`, `portmacro.h` arriba
de `main.c`, antes de `freertos/FreeRTOS.h`, y eso rompe toda la compilación.
Si aparece un error raro de includes, revisá primero el principio de `main.c` y
leé **la primera línea `error:`** del build.

## 3. Estilo de código (obligatorio en `main.c`)

- Estilo del profe para FreeRTOS: tareas `void task_xxx(void *pvParam)` con
  `while(1)`; prototipos antes de `app_main`, cuerpos después;
  `xTaskCreatePinnedToCore(...)` con **un argumento por línea**; prioridades
  `tskIDLE_PRIORITY + n`; constantes con `#define`.
- Estilo propio del usuario: llave en la misma línea (`void f(){`, `if(x){`),
  salvo `app_main` que la lleva abajo. Nombres simples en español/camelCase
  (`colaRx`, `accesoScara`, `valorAdc`). **Sin líneas en blanco dentro de las
  funciones.** Explicaciones en un bloque **Doxygen** (`/** @brief ... @param ... */`)
  arriba de **cada** función, no muchos comentarios sueltos. Los `#define` y
  variables globales llevan comentario corto al costado.
- Las librerías de `main/*.c` mantienen su propio estilo (4 espacios) y también
  llevan Doxygen.
- Tabs para indentar en `main.c`.

## 4. Enunciado (resumen fiel)

**Hardware**: ESP32, 2 potes (ADC), 4 pulsadores SW0..SW3, 4 LEDs LD0..LD3,
UART0 115200 8N1 al simulador Qt, WiFi SoftAP + Station.

**Protocolo UNER (UART0)** — trama `'U' 'N' 'E' 'R' LEN ':' CMD datos CHK`
(ya implementado en `uner.c`):

| Cmd | Nombre | Sentido | Datos | Respuesta |
|---|---|---|---|---|
| 0xF0 | ALIVE | ESP→Qt | – | F0 0D |
| 0xD0 | GET STATE | ESP→Qt | – | int32 angle1, angle2, pwr1, pwr2 + uint8 stateArm |
| 0xD1 | SET POWER | ESP→Qt | int32 pwr1, pwr2 | lo mismo |
| 0xD2 | SET PEN | ESP→Qt | uint8 (0/1) | lo mismo |
| 0xD3 | CLEAR | ESP→Qt | – | D3 0D |
| 0xD4 | LIMIT REACHED | Qt→ESP | uint8 stateArm | el ESP responde D4 stateArm |

stateArm: bit0 PEN, bit1 LIMIT_SW, bit2 ARM1_L, bit3 ARM1_R, bit4 ARM2_L, bit5 ARM2_R.

**Módulo A (FreeRTOS)** — justificar afinidad de núcleos, prioridades y
sincronización (colas, mutex, event groups).
1. ADC de los 2 potes con filtro (promedio móvil).
2. LEDs: LD0=ARM1_L, LD1=ARM1_R, LD2=ARM2_L, LD3=ARM2_R al recibir 0xD4. Con
   LIMIT_SW: los 4 parpadean 3 veces (100 ms on / 100 ms off) y vuelven al estado anterior.
3. **Una única tarea Rx** (UART0 + TCP Server + TCP Client, una cola) y **una
   única tarea Tx** (una cola).
4. Modos:
   - **Modo A manual hardware**: posición gruesa por potes; SW0 +angle1, SW1
     −angle1, SW2 +angle2, SW3 −angle2, ajuste fino de **0.5°**.
   - **Modo A manual comandos**: por microrl en el servidor TCP.
   - **Modo B autónomo**: una tarea escribe **números de 3 dígitos**; ADC y
     pulsadores deshabilitados.

**Módulo B**: driver UART (`driver/uart.h`) con máquina de estados no bloqueante (hecho).

**Módulo C (redes)**:
- SoftAP con **servidor TCP en el puerto 30000** que aloja página web (HTTP
  REST: ángulos, potencias, finales de carrera, PEN en tiempo real) **y**
  recibe comandos de texto con microrl desde PuTTY.
- Comandos microrl (se pueden agregar más):

| Comando | Parámetros | Respuesta |
|---|---|---|
| STATE | – | pwr1, pwr2, angle1, angle2, state |
| MOVE | pwr1 angle1 pwr2 angle2 | angle1 angle2 (mueve hasta el ángulo) |
| NUMERO | número | número (modo B) |
| SETMODE | A o B (sin parám.: devuelve el modo) | mode |
| WIFI | SSID PASSWORD (sin parám.: estado y SSID) | ssid password |
| SCADA | IP PORT (sin parám.: estado e IP:PORT) | ip port |
| STOP | – | detiene los movimientos |

- Station: con WIFI se conecta a la red; con SCADA se conecta al servidor y
  manda telemetría **no bloqueante** sin perturbar el control:
  ```
  POST /scara/telemetry HTTP/1.1\r\n
  Content-Type: application/json\r\n
  Content-Length: nBytes\r\n\r\n
  {"NAME":"MyName","ADC1":v1,"ADC2":v2,"PWR1":p1,"PWR2":p2,"MODO":"A","STATE":state}
  ```
  Respuesta: `HTTP/1.1 200 OK` … `{"ACK":"OK"}` (Content-Length 13) o `{"ACK":"ERROR"}` (16).

**Interpretación acordada**: el MyScara corre local por UART0 (COM6). La "PC del
profe" es el servidor SCADA HTTP al que el ESP llega por WiFi como **TCP
client**. Por eso el origen TCP Client transporta texto HTTP (POST y el ACK), no UNER.

**Pendiente de decidir**: cómo compartir el puerto 30000 entre HTTP y microrl
(el enunciado pide las dos cosas en el mismo puerto). Preguntale al usuario.

## 5. Pinout

- LEDs: GPIO 4, 5, 6, 7 (activo alto).
- Potes: GPIO 1 y 2 (ADC1 CH0 / CH1).
- Switches: GPIO 11–14 a GND con pull-up interno (suelto = 1, apretado = 0).
- UART0 = consola USB = MyScara. **No usar `printf` en el firmware**: sale por
  UART0 y ensucia la línea del simulador (la máquina UNER lo descarta, pero no
  conviene).

## 6. Lo que se sabe del simulador MyScara (medido)

- Ángulos en **cuentas 0..4095** (como potes). **656 = 0°, 3428 = 180°** en los
  dos brazos.
- Origen en el hombro; +x a la derecha, +y hacia adelante (para abajo en
  pantalla); ángulos desde +x hacia +y.
- Brazo 1 muestra q1 = grados(ang1). Brazo 2 muestra el ángulo **relativo del
  codo, invertido**: q2 = 90° − grados(ang2).
- Largos: **L1 = 160** (hombro→codo), **L2 = 190** (codo→lápiz).
- Potencia útil ±100 (satura arriba). Velocidad ≈ **16.4 cuentas/s por unidad
  de pwr**, lineal, sin zona muerta. **+pwr1 sube ang1, pero +pwr2 BAJA ang2.**
- Rampa de aceleración ≈ 0.6 s; frena en < 100 ms con pwr 0.
- Ruido de lectura ≈ ±10 cuentas (≈ ±3 unidades en x-y).
- LIMIT_SW: lápiz detrás de la línea del hombro (y < ~0); bloquea el movimiento.
- **Espacio de trabajo alcanzable**: anillo de radio **~248 a 350** alrededor
  del hombro (el codo solo llega a 90° de doblez porque sus grados van 0..180).
  Un pentágono de radio 60 en (0,300) falló; radio 40 dibujó bien. **Los
  dígitos del modo B tienen que entrar en ese anillo** (zona cómoda: centro
  (0, 300), ±40 en cada eje aprox.).
- El Qt dibuja la posición real del lápiz.

## 7. Estado actual de `main/main.c` (~900 líneas, compila sin warnings)

Librerías propias en `main/`: `iodigital`, `ianalogica`, `pwm`, `temporizador`,
`memoria` (NVS), `serie` (UART), `wifi`, `tcp`, `web`, `microrl` (+`config.h`),
`telnet` (no se usa: la consola va en **Raw**), `uner`, `rgb`. Todo se incluye
por `includes.h`. Componente `espressif/cjson` disponible.

### Sincronización
- `colaSw` (isr → antirrebote), `colaValido` (antirrebote → switches).
- `colaRx`, `colaTx` de `bloque_t {medio, largo, datos[64]}`; `medio_t` =
  `MEDIO_UART0`, `MEDIO_SERVER`, `MEDIO_CLIENT`.
- Mutex `accesoAdc` (protege `valorAdc`) y `accesoScara` (protege `scara`).
- Event group `eventos`: `BIT_MODO_A` (1<<0), `BIT_EN_POSICION` (1<<1),
  `BIT_PARPADEO` (1<<2).
- `scara_t scara`: `ang[2]`, `pwr[2]`, `stateArm`, `pen`, `obj[2]` (objetivo en
  cuentas), `avance[2]` (pwr de feedforward mientras dibuja, 0 = quieto).

### Tareas (core 0 = WiFi/TCP, core 1 = resto)
| Tarea | Prio | Core | Qué hace |
|---|---|---|---|
| task_adc | +4 | 1 | potes cada 20 ms, promedio móvil de 10, a `valorAdc` (duerme en modo B) |
| task_antirrebote | +4 | 1 | valida switches (50 ms) |
| task_switches | +3 | 1 | **por ahora solo vacía `colaValido`** (falta modo A) |
| task_rxUart0 | +5 | 1 | UART0 → colaRx |
| task_rxTcpServer | +5 | 0 | servidor TCP 30000 → colaRx (`socketServer`) |
| task_rxTcpClient | +5 | 0 | cliente TCP a `scadaIp:scadaPuerto`, reintenta cada 2 s (`socketClient`) |
| task_rx | +4 | 1 | única Rx: UART0 → máquina UNER → `procesar_uner`; **SERVER y CLIENT se descartan (falta microrl y ACK)** |
| task_tx | +4 | 1 | única Tx: manda cada bloque por su medio |
| task_control | +5 | 1 | lazo de 50 ms (ver abajo) |
| task_parpadeo | +2 | 1 | espera `BIT_PARPADEO`, 3 parpadeos, restaura LEDs |

### Funciones clave
- `enviar_uner`, `cmd_alive`, `cmd_getState`, `cmd_setPower`, `cmd_setPen`, `cmd_clear`.
- `procesar_uner`: GET STATE guarda ang/pwr/stateArm y llama `mostrar_topes`;
  LIMIT (D4) guarda stateArm, si hay LIMIT_SW prende `BIT_PARPADEO`, actualiza
  LEDs y responde D4.
- `mostrar_topes(stateArm)`: pone LD0..LD3 (no hace nada si está parpadeando).
  Se llama también con cada GET STATE, así los LEDs se apagan al salir del tope.
- `bool mover(x, y, avance)`: cinemática inversa → cuentas → `scara.obj`;
  devuelve false si está fuera de alcance o de 0..180°.
- `task_control`: cada 50 ms pide GET STATE, filtra la lectura (EMA 0.5) y
  calcula `pwr = avance + KP*error` (KP 0.3, saturado a ±100). Mientras se
  mueve, el error es contra el objetivo de hace `RETRASO` = 8 vueltas (el Qt va
  ~400 ms atrasado); quieto, contra el actual con banda muerta ±6. Invierte el
  signo de pwr2, no empuja contra un final de carrera activo, y prende
  `BIT_EN_POSICION` cuando las dos pwr dan 0.
- `esperar_posicion(quieto_ms)`: espera `BIT_EN_POSICION` (timeout 8 s).
- `mover_recto(x, y, velocidad)`: va en **línea recta** desde el último punto
  con perfil coseno (arranca y frena suave) y espera llegar. **No toca el
  lápiz.** `VELOCIDAD` = 10 u/s para dibujar, `VELOCIDAD_VIAJE` = 40 u/s con el
  lápiz arriba.

### Resultados del control (no tocar sin motivo)
- Dibujando a 10 u/s el error es del orden del ruido (≈1.4 medio).
- A 40 u/s hay desvío lateral de hasta ~9 en algunos tramos: es atraso dinámico
  (baja a ~3.8 a 10 u/s), no calibración. Con lápiz arriba no importa.
- Probado y **peor**: RETRASO 3, demorar el codo una vuelta. RETRASO 8 es lo mejor.

### Cosas de prueba que siguen en el código
- `STA_SSID_PRUEBA` / `STA_PASS_PRUEBA` (red de la casa del usuario) y
  `scadaIp = "192.168.123.37"`, `scadaPuerto = 5000`: se reemplazan cuando
  existan los comandos WIFI (guardar en NVS) y SCADA.
- `ANG1_MIN/MAX`, `ANG2_MIN/MAX` = ±90 en task_adc: rango de los potes,
  ajustar al hacer el modo A (el Qt usa 0..180°).

## 8. Qué está hecho y qué falta

Hecho (probado en MyScara):
1. UART0 + UNER (ALIVE responde), Rx/Tx únicas con colas.
2. LEDs por 0xD4, parpadeo con LIMIT_SW, se apagan al salir del tope.
3. Polling de GET STATE cada 50 ms.
4. Control de posición y dibujo en línea recta preciso.

Falta (el usuario elige el orden):
5. **Modo A hardware**: potes = posición gruesa, SW0..SW3 = ±0.5° fino sobre
   cada articulación → objetivo en `scara.obj` (sin avance). Conectar
   `task_switches`.
6. **microrl por TCP 30000** en modo Raw (sin telnet): en `task_rx`, caso
   `MEDIO_SERVER`, cada byte a `microrl_insert_char`; el callback execute
   procesa STATE, MOVE, NUMERO, SETMODE, WIFI, SCADA, STOP; las respuestas van
   por `colaTx` con `MEDIO_SERVER`. PuTTY: Connection type **Raw**, Local echo
   **Force off**, Local line editing **Force off**.
7. **Panel HTTP** con el estado en tiempo real (JSON con snprintf o cJSON).
   Decidir con el usuario lo del puerto 30000.
8. **STA + SCADA**: WIFI guarda SSID/pass en NVS y conecta; SCADA guarda IP:PORT;
   una tarea manda el POST de telemetría periódico por `colaTx` con
   `MEDIO_CLIENT`; en `task_rx`, caso `MEDIO_CLIENT`, leer el `{"ACK":...}`.
9. **Modo B**: comando NUMERO → cola → tarea de dibujo que traza 3 dígitos con
   `cmd_setPen` + `mover_recto` (estilo 7 segmentos funcionó antes: 30×60,
   paso 45, centrados en (0,300) — verificar que entran en el anillo
   alcanzable). En modo B se apaga `BIT_MODO_A` (ADC y switches dejan de mandar).
10. **Justificación del módulo A** (núcleos, prioridades, colas/mutex/event groups).

## 9. Cómo arrancar

1. Leé `main/main.c` entero y `main/includes.h`.
2. Si vas a usar una librería, leé su `.h` antes (las APIs son propias y chicas).
3. Preguntale al usuario qué etapa sigue y explicá el plan corto antes de codear.
4. Codeá en el estilo de la sección 3, compilá, flasheá si corresponde y pedile
   que pruebe con MyScara.
