# Guía de comunicaciones — ParcialESP32

Cómo está armado todo lo que habla con el ESP32 (WiFi, web, app Qt, consola),
cómo funciona por dentro y cómo usarlo para resolver un ejercicio.

---

## 1. El mapa completo

```
                        ┌─────────────────────── ESP32 ───────────────────────┐
                        │                                                     │
  Celular / PC ──WiFi── │ AP "ESP32-Parcial"  192.168.4.1                      │
  (red del ESP32)       │   └─ GET /  → config.html (SSID, clave, puerto)      │
                        │                                                     │
  Router ──────WiFi──── │ STA (IP que da el router, ej. 192.168.123.38)        │
                        │   │                                                 │
                        │   ├─ HTTP :80 ───── web.c ──┐                        │
   Navegador ───────────┼───┤   /  → panel.html       │                        │
   App Qt  ─────────────┼───┤   /api/def              ├──► variables.c ──► GPIO │
                        │   │   /api/valores          │    (registro,         │
                        │   │   /api/set              │     con mutex)        │
                        │   │                         │                        │
   MobaXterm ───────────┼───┴─ TCP :1234 ─ consola ───┘                        │
   (Telnet)             │      tcp → telnet → microrl → ejecutar()            │
                        └─────────────────────────────────────────────────────┘
   Monitor / MobaXterm ── USB (COM6, UART0 115200) ─ consola ─ microrl → ejecutar()
   (Serial)               (misma consola que por TCP, sin WiFi)
```

Dos ideas para no perderse:

1. **Hay un solo lugar donde viven los datos: el registro de variables** (`variables.c`).
   La web, la app Qt y la consola son tres "ventanas" al mismo registro. Ninguna guarda
   el estado: muestran lo que el ESP32 confirma.
2. **Cada canal tiene un solo uso:**
   - **HTTP (puerto 80)** → para programas: la página web y la app Qt. Hablan JSON.
   - **TCP (puerto 1234) y UART (USB)** → para personas: MobaXterm o el monitor. Hablan
     texto, con los mismos comandos.

---

## 2. Arranque: qué pasa al prender el ESP32

### Lo tuyo y lo del sistema

El código se divide en dos:

- **`main.c` es lo tuyo**, lo único que tocás en el parcial. Tiene cuatro secciones
  numeradas: 1. variables, 2. comandos de consola (vacía, la hacés vos), 3. endpoints HTTP
  (opcional), 4. `app_main` y tus tareas.
- **`sistema.c` es todo lo demás**: NVS, WiFi, la consola (sin comandos), el servidor web,
  la API y la página de configuración. No hace falta tocarlo.

`app_main()` queda así:

```c
void app_main(void)
{
    registrar_variables();                  /* 1. antes de sistema_init */
    sistema_init(comandos, NULL, 0);        /* 2. arranca todo */
    xTaskCreate(tarea_control, "control", 4096, NULL, 4, NULL);   /* 3. tus tareas */
}
```

Los parámetros de `sistema_init`:

| Parámetro | Qué es | Si no lo usás |
|---|---|---|
| `comandos` | tu función de comandos de consola (sección 7) | `NULL` |
| `endpoints`, `cantidad` | tu tabla de endpoints HTTP (receta E) | `NULL, 0` |

### Qué hace `sistema_init()` por dentro

En este orden (el orden importa):

| Paso | Llamada | Por qué va ahí |
|---|---|---|
| 0 | (tu `registrar_variables()`, antes) | Antes de crear tareas: el registro no se modifica después, así nadie lo lee a medio armar. |
| 1 | `var_init()` | Crea el mutex del registro, por si no registraste ninguna variable. Si ya existe, no hace nada. |
| 1b | `consola_uart_init(UART_NUM_0, 115200, ...)` | Consola por el USB (COM). Primero porque no depende de nada: anda aunque falle el WiFi. |
| 2 | `memoria_init()` | Abre la NVS. WiFi la necesita y nosotros leemos la config de ahí. |
| 3 | `memoria_get_*()` | Lee SSID, clave y puerto guardados. Si nunca se guardó nada, quedan los valores por defecto (`CONFIG_ESP_WIFI_*`, puerto 1234). |
| 4 | `wifi_init(...)` | Levanta el AP y, si hay SSID, conecta la STA al router. |
| 5 | `consola_tcp_init(puerto, ejecutar)` | Crea la tarea de la consola. Después de WiFi porque usa sockets. |
| 6 | `web_init(...)` | Arranca el servidor HTTP con los endpoints del sistema más los tuyos. Al final porque `config_post` usa `consola_tcp_puerto()`. |

Por el monitor serie (`idf.py -p COM6 monitor`) se ve:

```
Config de arranque: SSID "MiRed", puerto 1234
AP "ESP32-Parcial" activo en 192.168.4.1, conectando STA a MiRed
STA conectada, IP: 192.168.123.38        ← esta es la IP para la web, Qt y MobaXterm
Servidor HTTP listo en el puerto 80
```

---

## 3. WiFi: AP + STA

**Archivos:** `wifi.h/.c`, `memoria.h/.c`, `www/config.html`, `www/config.js`.

### Los dos modos a la vez

- **AP (Access Point):** el ESP32 crea su propia red, `ESP32-Parcial` (clave `12345678`,
  configurable en `menuconfig`). Su IP siempre es `192.168.4.1`. Acepta hasta 4
  dispositivos. Sirve para configurar.
- **STA (Station):** el ESP32 se conecta al router como un celular más. El router le da
  una IP. Sirve para usarlo desde tu PC en la red de siempre.

### Primera configuración

1. Conectate desde el celular o la PC a la red `ESP32-Parcial`.
2. Abrí `http://192.168.4.1` → aparece el formulario de configuración.
3. Cargá SSID y clave del router y el puerto de la consola → **Guardar**.
4. El ESP32 lo guarda en NVS, se conecta al router y abre la consola en ese puerto.
5. Mirá en el monitor serie la línea `STA conectada, IP: ...`.

### Por qué la misma dirección `/` muestra dos páginas distintas

`raiz_get()` pregunta **por qué red entró el pedido**:

```c
static bool por_ap(httpd_req_t *req)
{
    return web_ip_local(req) == wifi_ip_ap();   /* ¿me hablaron a 192.168.4.1? */
}
```

- Por el AP → `config.html`.
- Por el router → `panel.html`.

`config_post` hace la misma pregunta y responde **403** si no vino por el AP: nadie en
la red del router puede cambiarte la configuración.

### NVS (memoria que sobrevive al apagado)

`memoria.h` guarda pares clave → valor en el espacio `"config"`:

```c
memoria_set_str("ssid", "MiRed");          // guarda (y hace commit)
memoria_get_str("ssid", buf, sizeof(buf)); // si no existe, NO toca buf
memoria_set_u16("puerto", 1234);
```

- `idf.py flash` **no borra** la NVS: la config sobrevive a reprogramar.
- Para empezar de cero: `idf.py -p COM6 erase-flash` y después `flash`.

---

## 4. HTTP: el servidor web

**Archivos:** `web.h/.c`, carpeta `www/`, handlers del sistema en `sistema.c`.

### Cómo está armado

- `web_init()` monta **SPIFFS** (los archivos de `www/` quedan grabados en la flash),
  registra los endpoints de la app y al final `/*`, que sirve cualquier archivo de `www/`.
- Hasta `WEB_MAX_CONEXIONES` (4) conexiones a la vez. Si llega una quinta, se cierra
  la más vieja (`lru_purge_enable`).
- Todos los handlers corren **en la misma tarea** del servidor, de a uno. Por eso
  `s_json_web` puede ser un buffer `static` compartido sin mutex.

> Si cambiás algo en `www/`, hay que volver a compilar y flashear: la carpeta se
> convierte en una imagen SPIFFS al compilar (`spiffs_create_partition_image` en
> `main/CMakeLists.txt`).

### La API (lo que usan la web y Qt)

| Pedido | Respuesta OK | Errores |
|---|---|---|
| `GET /api/def` | `{"vars":[{"id":"led2","tipo":"dout","etq":"LED 2","fila":0,"col":1}, ...]}` | 500 si no entra en el buffer |
| `GET /api/valores` | `{"led1":50,"led2":1,"sw1":0,"pot1":49.4,"consigna":25,"estado":"listo"}` | 500 |
| `POST /api/set` cuerpo `{"id":"led2","valor":"toggle"}` | los valores actualizados (como `/api/valores`) | 400 valor inválido · 403 solo lectura · 404 no existe |
| `POST /api/config` (formulario) `ssid=..&pass=..&puerto_int=..` | `OK` | 400 campos · 403 si no es por el AP · 500 NVS |

- `def` dice **qué hay y cómo mostrarlo**. Se pide una vez, al conectar.
- `valores` dice **cuánto vale cada cosa ahora**. Se pide cada 1 s (polling).
- `set` cambia algo y devuelve los valores nuevos, así el cliente se actualiza enseguida.
- En los errores, el cuerpo de la respuesta es el motivo en texto (`"Valor invalido"`).
  Los clientes lo muestran tal cual.

### Probar la API sin clientes

Desde una terminal de la PC:

```bash
curl http://192.168.123.38/api/valores
```

```bash
curl -X POST -d "{\"id\":\"led2\",\"valor\":\"on\"}" http://192.168.123.38/api/set
```

### Cómo es un handler

```c
/* GET /api/valores */
static esp_err_t valores_get(httpd_req_t *req)
{
    int largo = var_json_valores(s_json_web, sizeof(s_json_web));  // arma el JSON
    return responder_json(req, largo);   // Content-Type: application/json + envío
}
```

Y se registra en la tabla:

```c
static const httpd_uri_t endpoints[] = {
    { .uri = "/api/valores", .method = HTTP_GET, .handler = valores_get },
    ...
};
```

---

## 5. JSON: armar y leer

### Armar (ESP32 → cliente): `snprintf`

No hace falta una librería. `variables.c` va agregando pedazos a un buffer con
`agregar()` (un `vsnprintf` que avanza y marca si no entró):

```c
agregar(&t, "{");
agregar(&t, "\"%s\":%d", "led2", 1);      // "led2":1
agregar(&t, ",\"%s\":\"%s\"", "estado", "listo");
agregar(&t, "}");
return terminar(&t);                       // largo, o -1 si no entró
```

Reglas: los textos van entre comillas (`\"%s\"`), los números no. Por eso los textos
no pueden tener `"` ni `\`: `var_set` los rechaza.

### Leer (cliente → ESP32): `json_valor()`

```c
char id[32], valor[64];
json_valor(cuerpo, "id", id, sizeof(id));          // {"id":"led2",...}  → "led2"
json_valor(cuerpo, "valor", valor, sizeof(valor)); // {"valor":50}       → "50"
```

Siempre devuelve **texto**: un número llega como `"50"` y se convierte después
(`strtof`, `atoi`). Sirve para objetos planos, sin listas ni objetos adentro.

---

## 6. El registro de variables (el corazón)

**Archivos:** `variables.h/.c`, `registrar_variables()` en `main.c`.

### Los 6 tipos

| Tipo | Registro | Valor | Se escribe con | Hardware |
|---|---|---|---|---|
| `dout` | `var_dout(id, etq, pin, fila, col)` | 0 / 1 | `on`, `off`, `1`, `0`, `toggle` | `salida_set` |
| `din` | `var_din(id, etq, pin, activo_bajo, fila, col)` | 0 / 1 | — (solo lectura) | `entrada_get`, al pedir el valor |
| `ain` | `var_ain(id, etq, pin, min, max, unidad, fila, col)` | min..max | — (solo lectura) | `analogica_get_crudo` escalado |
| `pwm` | `var_pwm(id, etq, pin, frec_hz, fila, col)` | 0..100 % | un número | `pwm_set_duty` |
| `num` | `var_num(id, etq, min, max, inicial, fila, col)` | min..max | un número | ninguno (dato del programa) |
| `texto` | `var_txt(id, etq, inicial, fila, col)` | texto ≤ 31 | cualquier texto sin `"` ni `\` | ninguno |

`fila` y `col` dicen dónde aparece el ladrillo en la grilla de **ambos** clientes.

### Agregar algo = una línea

```c
static void registrar_variables(void)
{
    var_dout("led2", "LED 2", LED2_GPIO, 0, 1);
    var_num ("vel", "Velocidad", 0, 10, 5, 3, 0);   // ← nueva: fila 3, columna 0
}
```

Compilar, flashear y listo: la web y Qt la muestran solas (con tu ladrillo, o con
`Desconocido` si todavía no lo hiciste). `var_init()` no hace falta: se llama sola.

### Usarlo desde tu programa

```c
float x;
var_get("pot1", &x);          // lee (las entradas se leen del hardware en ese momento)
var_set("led2", "on");        // escribe: mismo camino que la web y la consola

char txt[16];
snprintf(txt, sizeof(txt), "%d", contador);
var_set("vueltas", txt);      // var_set siempre recibe texto

char estado[VAR_TEXTO_MAX];
var_texto("estado", estado, sizeof(estado));   // valor como texto ("on", "42.5 %", "listo")
```

Todas estas funciones tienen **mutex adentro**: se pueden llamar desde cualquier tarea.

---

## 7. La consola: TCP (MobaXterm) y UART (USB)

**Archivos:** `tcp.h/.c`, `telnet.h/.c`, `microrl.c/.h` + `config.h` (del profe),
`consola.h/.c`, `consola_tcp.h/.c`, `consola_uart.h/.c`, `ejecutar()` en `sistema.c`
(que llama a tus `comandos()` de `main.c`).

Hay **dos consolas a la vez**, con los mismos comandos (tu `comandos()`): una por WiFi
(TCP) y otra por el cable USB (UART0). Cada una tiene su propia línea e historial, y
`consola_printf()` contesta por la que recibió el comando.

### Conectarse

- **TCP:** MobaXterm → **Session → Telnet** → Remote host: la IP del ESP32 → Port: `1234`.
  En PuTTY: tipo **Telnet** (con *Raw* se ve doble eco).
- **UART:** `idf.py -p COM6 monitor`, o MobaXterm → **Session → Serial** → COM6, 115200.
  Por ahí también salen los logs (`I (28160) tcp: Cliente conectado...`), que se mezclan
  con la consola: si te tapan el prompt, apretá Enter. Si MobaXterm tiene tomado el COM6,
  `idf.py flash` no puede grabar: cerrá la sesión serie antes de flashear.

```
Consola ESP32

ESP32> hola
ERROR: comando desconocido: hola
```

La consola no trae comandos: todos los escribís vos en `comandos()` de `main.c`. Lo que
el sistema ya resuelve es todo lo demás (conexión, Telnet, eco, edición de la línea,
historial con las flechas y separar la línea en palabras). Cuando tu función devuelve
`false`, el sistema contesta `ERROR: comando desconocido`.

### Un cliente a la vez

`consola_tcp.c` hace un bucle simple:

```
abrir servidor en el puerto
  └─ esperar cliente (accept, vence cada 1 s para mirar si cambió el puerto)
        └─ atenderlo: recibir → filtrar Telnet → consola_recibir()
           hasta que se desconecte (o cambie el puerto)
     volver a esperar
```

Si se conecta un segundo cliente, **queda esperando sin respuesta** hasta que el primero
se va. Un cliente que desaparece sin cerrar (se cortó el WiFi) se libera en ~11 s
gracias al **keepalive** de `tcp_aceptar()` (5 s quieto + 3 sondeos de 2 s).

### El camino de una tecla

```
MobaXterm manda "l"
  → tcp_recibir()        bytes crudos del socket (0 = venció la espera, -1 = se fue)
  → telnet_filtrar()     saca los comandos Telnet (bytes 255 ...) que manda MobaXterm
  → consola_recibir()    convierte \r, \n o \r\n en un solo Enter
  → microrl              hace el eco, la edición, el historial (flechas)
  → al Enter: ejecutar(argc, argv)   con la línea separada en palabras
  → consola_printf()     la respuesta vuelve por enviar() → tcp_enviar()
```

Por UART es igual pero más corto: `uart_read_bytes()` → `consola_recibir()` → microrl →
`ejecutar()` → `consola_printf()` → `uart_write_bytes()`. No hay Telnet: la terminal serie
ya manda cada tecla al toque y no hace eco propio.

### Cómo se reparten las dos consolas

`consola.c` tiene un arreglo de `CONSOLA_MAX` (2) consolas; cada transporte crea la suya
con `consola_crear(ejecutar, enviar)` y le pasa los bytes con `consola_recibir(c, ...)`.
`consola_recibir` toma un **mutex** y anota cuál es la consola actual: así los comandos
corren de a uno (uno que llega por UART espera al de TCP) y `consola_printf()` sabe a
quién contestar. Por eso `consola_printf()` solo sirve dentro de `comandos()`: fuera de
un comando no hay consola actual y no se manda nada.

Para usar otra UART (ej. un módulo USB-serie en otros pines), cambiá en `sistema.c`
`UART_CONSOLA`, `UART_BAUDIOS` y los pines (`UART_PIN_NO_CHANGE` = los de siempre).

**¿Por qué Telnet?** Al conectarse, `telnet_inicio()` le dice a MobaXterm "el eco lo
hago yo y mandá cada tecla al toque" (`IAC WILL ECHO`, `IAC WILL SGA`). Sin eso, MobaXterm
manda la línea entera al final y muestra todo dos veces.

### Escribir los comandos

Todo va en `comandos()` de `main.c` (sección 2). Se llama con cada línea que llega:

- `argv[0]` es el comando, `argv[1]`... los argumentos, `argc` cuántas palabras hay
  (microrl acepta como mucho 8 por línea).
- Respondés con `consola_printf(...)`, terminando las líneas en `\r\n`. Corta a 256
  caracteres; para algo más largo usá `consola_escribir(texto)`.
- Devolvés `true` si reconociste el comando y `false` si no.

```c
static bool comandos(int argc, const char *const *argv)
{
    if (strcmp(argv[0], "hola") == 0) {
        consola_printf("Hola!\r\n");
        return true;
    }
    if (strcmp(argv[0], "led") == 0 && argc == 3) {         /* led 2 on */
        char id[8];
        snprintf(id, sizeof(id), "led%s", argv[1]);
        if (var_set(id, argv[2]) != VAR_OK) {
            consola_printf("ERROR: no se pudo\r\n");
        }
        return true;
    }
    return false;
}
```

Funciones del registro que te sirven para la consola:

| Para | Usá |
|---|---|
| cambiar una variable | `var_set(id, "texto")`. Devuelve `VAR_OK`, `VAR_NO_EXISTE`, `VAR_SOLO_LECTURA` o `VAR_VALOR_INVALIDO`. |
| mostrar una variable | `var_texto(id, buf, tam)` → `"on"`, `"42.5 %"`, `"listo"` |
| usar un número | `var_get(id, &f)` |
| listar todas | `var_listar(buf, tam)` → una línea por variable, lista para `consola_escribir` |

Si querés un comando genérico tipo `<id> <valor>` (`led2 on`, `consigna 12.5`): probá
`var_set(argv[0], argv[1])` y, según lo que devuelva, mostrá `var_texto(argv[0], ...)` o el
error. Para textos con espacios (`estado hola mundo`), uní `argv[1]`..`argv[argc-1]` con
`strlcat` antes de llamar a `var_set`.

Consejos: empezá los errores con `ERROR: ` para que se reconozcan, y hacé un `help` con
`consola_escribir` (la ayuda suele pasar de 256 caracteres).

---

## 8. Los clientes: web y Qt (espejos)

**Web:** `www/conexion.js`, `www/ladrillos.js`, `www/app.js`, `www/panel.html`, `www/style.css`.
**Qt:** `qt/conexion.py`, `qt/ladrillos.py`, `qt/app.py` (se abre con `qt/abrir.bat`).

Los dos tienen **los mismos tres archivos con el mismo algoritmo**. Si entendés uno,
entendés el otro.

| Archivo | Qué hace | Web | Qt |
|---|---|---|---|
| conexion | Las 3 operaciones HTTP | `fetch` + `await`; si falla tira `Error` | `QNetworkAccessManager`; llama a `listo(datos)` o `fallo(mensaje)` |
| ladrillos | Una clase por tipo | elementos HTML | widgets |
| app | Grilla + polling + mensajes | `setTimeout` encadenado | `QTimer` de un disparo |

### El algoritmo (igual en los dos)

```
1. Conectar  → pedir def → crear un ladrillo por variable en (fila, col)
2. Polling   → pedir valores → cada ladrillo.actualizar(valor) → esperar 1 s → repetir
              (encadenado: el próximo pedido sale cuando terminó el anterior)
3. Clic      → ladrillo.set(valor) → POST /api/set → mostrar los valores que devuelve
4. Error     → mensaje en rojo 4 s (si se repite, se queda mientras siga fallando)
```

Por qué polling y no "el ESP avisa": es lo más simple y aguanta todo (cortes, varios
clientes, cambios desde la consola). Un cambio hecho en MobaXterm aparece en la web y
en Qt en menos de 1 s.

### Los ladrillos (los widgets los hacés vos)

`ladrillos.js` y `ladrillos.py` traen solo la base:

- `Ladrillo`: arma el recuadro con el título (`etq`) y te da `this.set(valor)` /
  `self.set(valor)` para mandar un valor al ESP32 y `agregar(...)` para sumar elementos.
- `Desconocido`: muestra `tipo: valor`. Mientras un tipo no tenga su clase, se ve así,
  así que la grilla siempre funciona.
- `TIPOS`: vacía. Ahí va cada clase que escribas.

Cada clase define solo dos cosas. Ejemplo para `dout`:

```js
class SalidaDigital extends Ladrillo {
    crear(def) {                       // arma sus elementos con la definicion
        this.boton = this.agregar('button');
        this.boton.onclick = () => this.set('toggle');   // manda al ESP32
    }
    actualizar(valor) {                // muestra lo que confirmo el ESP32
        this.boton.textContent = valor ? 'ON' : 'OFF';
    }
}
```

```python
class SalidaDigital(Ladrillo):
    def crear(self, d):
        self.boton = self.agregar(QPushButton())
        self.boton.clicked.connect(lambda: self.set("toggle"))

    def actualizar(self, valor):
        self.boton.setText("ON" if valor else "OFF")
```

Y después se suma a la tabla, en los dos clientes:

```js
const TIPOS = {
    dout: SalidaDigital,
};
```

```python
TIPOS = {
    "dout": SalidaDigital,
}
```

### Qué manda el ESP32 para cada tipo

Lo que tu ladrillo recibe en `def` (al crearse) y en `actualizar(valor)`, y qué acepta `set`:

| Tipo | En `def`, además de id/etq/fila/col | `valor` que llega | `set(...)` acepta | Idea de widget |
|---|---|---|---|---|
| `dout` | — | `0` / `1` | `"on"`, `"off"`, `"toggle"` | botón (HTML `button` / `QPushButton`) |
| `din` | — | `0` / `1` | — (solo lectura) | indicador (`p` / `QLabel` con color) |
| `ain` | `min`, `max`, `unidad` | número | — (solo lectura) | barra (`progress` / `QProgressBar`) + texto |
| `pwm` | `min` (0), `max` (100), `unidad` (`"%"`) | número | número entre min y max | deslizador (`input type=range` / `QSlider`) |
| `num` | `min`, `max`, `unidad` (`""`) | número | número entre min y max | campo + botón Enviar (`input` / `QLineEdit`) |
| `texto` | — | texto | texto de hasta 31 caracteres | campo + botón Enviar |

Consejos que vas a necesitar:

- **Mostrá solo lo que confirma el ESP32.** En el clic llamá a `set` y no cambies nada:
  la respuesta llega enseguida y pasa por `actualizar`.
- **Deslizadores:** si en `actualizar` le cambiás la posición mientras el usuario
  arrastra, el polling se la pisa. En la web, marcá una bandera en `oninput`, mandá en
  `onchange` y no actualices mientras esté marcada. En Qt, `setTracking(False)` hace que
  `valueChanged` salga recién al soltar, y en `actualizar` salteá si `isSliderDown()`.
  Además, rodeá el `setValue` con `blockSignals(True/False)` para que no se reenvíe.
- **Campos de texto:** no les pongas el valor en `actualizar` (te borraría lo que estás
  escribiendo cada 1 s). Mostrá el valor en un texto aparte.
- **Barras:** la fracción es `(valor - min) / (max - min)`. `QProgressBar` usa enteros:
  `setRange(0, 1000)` y `setValue(round(fraccion * 1000))`.
- Los estilos de la web van en `style.css`, debajo de la línea marcada.

---

## 9. Sincronización: quién corre dónde

| Tarea | Qué hace | Qué toca |
|---|---|---|
| `main` | arranca todo y termina | registra variables (antes de que existan las demás tareas) |
| servidor HTTP | atiende la web y Qt, de a un pedido | `var_*` (con mutex), `s_json_web` (solo ella) |
| `consola_tcp` | atiende MobaXterm | `var_*` (con mutex), su microrl (mutex de consola) |
| `consola_uart` | atiende el COM del USB | `var_*` (con mutex), su microrl (mutex de consola) |
| (tus tareas) | la lógica del ejercicio | `var_get` / `var_set` (con mutex) |

Las herramientas de FreeRTOS que se usan:

- **Mutex en `variables.c`:** `var_set` escribe el pin y el valor juntos con el mutex tomado.
  Nadie lee un valor que todavía no se aplicó, aunque la web, la consola y tu tarea toquen
  la misma variable a la vez. El mutex tiene herencia de prioridad.
- **Cola de 1 elemento para el puerto:** `config_post` (tarea HTTP) hace `xQueueOverwrite`
  con el puerto nuevo y la tarea de la consola lo mira con `xQueuePeek` cada 1 s. Así
  se pasa un dato entre tareas sin variables compartidas.
- **Mutex en `consola.c`:** las dos consolas ejecutan comandos de a uno, y mientras tanto
  queda anotada a cuál contesta `consola_printf()`.
- **Sin mutex donde una sola tarea usa algo:** los buffers `static` de cada tarea.
- **Mutex en `ianalogica.c`:** un `analogica_get()` a la vez (el ADC es uno solo).

> Regla práctica: si un dato lo tocan dos tareas, que viva en el registro de variables
> (o protegelo con un mutex o pasalo por una cola). Si lo toca una sola, no hace falta nada.

---

## 10. Recetas para el parcial

### A. "Mostrar un dato nuevo y poder cambiarlo desde la PC"

Una línea en `registrar_variables()`:

```c
var_num("limite", "Limite", 0, 100, 50, 3, 0);
```

### B. "Que el ESP32 haga algo solo" (lógica del ejercicio)

Una tarea que lee y escribe variables. Ejemplo: si el pote supera la consigna, prender el LED 2.

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void tarea_control(void *arg)
{
    for (;;) {
        float pot, consigna;
        var_get("pot1", &pot);
        var_get("consigna", &consigna);
        var_set("led2", pot > consigna ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// al final de app_main():
xTaskCreate(tarea_control, "control", 4096, NULL, 4, NULL);
```

Como `var_set` es el mismo camino que usan los clientes, la web y Qt muestran
el LED cambiando sin tocar nada más. Ojo: si la tarea escribe `led2` cada 100 ms, gana
siempre ella. Lo que pongas desde la web dura hasta la próxima vuelta.

### C. "Contar algo y mostrarlo"

```c
var_num("cuenta", "Pulsaciones", 0, 100000, 0, 3, 1);   // en registrar_variables()

// en una tarea:
static void tarea_contador(void *arg)
{
    float sw, anterior = 0;
    int cuenta = 0;
    char txt[16];
    for (;;) {
        var_get("sw1", &sw);
        if (sw && !anterior) {                  // flanco: recién apretado
            snprintf(txt, sizeof(txt), "%d", ++cuenta);
            var_set("cuenta", txt);
        }
        anterior = sw;
        vTaskDelay(pdMS_TO_TICKS(20));          // ~20 ms tambien filtra el rebote
    }
}
```

### D. "Un comando nuevo en la consola"

Ver la sección 7: un `if` en `comandos()` que devuelva `true`.

### E. "Un endpoint nuevo"

En `main.c`, sección 3 (la plantilla ya está comentada ahí):

```c
/* GET /api/hola -> {"mensaje":"hola"} */
static esp_err_t hola_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"mensaje\":\"hola\"}");
}

static const httpd_uri_t endpoints[] = {
    { .uri = "/api/hola", .method = HTTP_GET, .handler = hola_get },
};

// en app_main, en vez de NULL, 0:
sistema_init(comandos, endpoints, sizeof(endpoints) / sizeof(endpoints[0]));
```

Para un POST: `web_leer_cuerpo()` + `json_valor()` (como `set_post` en `sistema.c`).
Podés agregar hasta `SISTEMA_ENDPOINTS_MAX` (8). No uses rutas que ya tiene el sistema
(`/`, `/api/config`, `/api/def`, `/api/valores`, `/api/set`).

### F. "Mostrar un tipo con un widget" (ladrillo nuevo)

Si el tipo ya existe en el ESP32, escribí la clase en `ladrillos.js` y en
`ladrillos.py` y sumala a `TIPOS` (sección 8 tiene la tabla de qué llega y qué se manda).

Si es un **tipo nuevo**, también hay que tocar el ESP32. Los pasos están en el
comentario de arriba de `variables.c`:

1. sumarlo a `tipo_t` y `NOMBRE_TIPO`
2. escribir su `var_<tipo>()` (y declararla en `variables.h`)
3. agregar su `case` en `leer()`, `var_set()`, `agregar_valor_json()` y `agregar_valor_texto()`
4. una clase en cada cliente + agregarla a `TIPOS`

### G. "Cambiar la red o el puerto"

Desde la página de configuración (por el AP), sin reprogramar. Los valores por defecto
están en `menuconfig` (`CONFIG_ESP_WIFI_SSID`, etc.) y `PUERTO_INICIAL` en `sistema.c`.

### H. "Muestrear cada X ms" (timer por hardware)

**Librería:** `temporizador.h` → `temporizador_init(periodo_us, isr, arg)`.

**Regla de oro de las ISR:** la ISR solo **avisa**; el trabajo lo hace una tarea. En una ISR
no van `printf`, `var_set`, `var_get` ni el ADC (`adc_oneshot` usa un mutex y una ISR no
puede esperar). Solo funciones `...FromISR`.

Patrón del profe (timer → event group → tarea):

```c
#include "freertos/event_groups.h"
#include "esp_attr.h"               // IRAM_ATTR
#include "temporizador.h"

#define BIT_MUESTREAR   (1 << 0)
static EventGroupHandle_t s_eventos;

/* ISR: corre cada 10 ms. Solo levanta un bit. */
static bool IRAM_ATTR al_vencer(void *arg)
{
    BaseType_t despertada = pdFALSE;
    xEventGroupSetBitsFromISR(s_eventos, BIT_MUESTREAR, &despertada);
    return despertada == pdTRUE;     // true = cambiar de tarea al salir de la ISR
}

/* Tarea: espera el bit y hace el trabajo */
static void tarea_muestreo(void *arg)
{
    for (;;) {
        xEventGroupWaitBits(s_eventos, BIT_MUESTREAR,
                            pdTRUE,          // borrar el bit al despertar
                            pdFALSE,         // con uno de los bits alcanza
                            portMAX_DELAY);
        float pot;
        var_get("pot1", &pot);               // el ADC se lee aca, no en la ISR
        // ... tu logica (promedio, umbral, etc.)
    }
}

// en app_main, despues de sistema_init:
s_eventos = xEventGroupCreate();                 // ANTES de arrancar el timer
xTaskCreate(tarea_muestreo, "muestreo", 4096, NULL, 5, NULL);
temporizador_init(10000, al_vencer, NULL);       // 10000 us = 10 ms
```

- Creá el event group (o la cola) **antes** de `temporizador_init`: si la ISR salta y
  `s_eventos` todavía es `NULL`, el ESP32 se reinicia.
- `IRAM_ATTR` deja la ISR en RAM, así puede correr aunque la flash esté ocupada.
- En vez del event group sirve igual un semáforo binario (`xSemaphoreGiveFromISR` /
  `xSemaphoreTake`) o una notificación (`vTaskNotifyGiveFromISR` / `ulTaskNotifyTake`).
- Hasta 4 timers (`TEMPORIZADOR_MAX`).

Probado en la placa: a 10 ms da ~100 muestras por segundo.

### I. "Reaccionar a un pulsador al instante" (interrupción de GPIO)

**Librería:** `iodigital.h` → `entrada_interrupcion(pin, flanco, isr, arg)`.

Patrón: ISR → cola → tarea (la cola lleva **qué** pasó, ej. qué pulsador):

```c
#include "freertos/queue.h"
#include "esp_attr.h"
#include "iodigital.h"

#define ANTIRREBOTE_MS  50
static QueueHandle_t s_pulsos;

/* ISR: solo manda a la cola que pin fue */
static void IRAM_ATTR al_apretar(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(int)arg;
    BaseType_t despertada = pdFALSE;
    xQueueSendFromISR(s_pulsos, &pin, &despertada);
    if (despertada) {
        portYIELD_FROM_ISR();        // cambiar de tarea al salir de la ISR
    }
}

/* Tarea: recibe, filtra el rebote y hace el trabajo */
static void tarea_pulsos(void *arg)
{
    static bool suelto[GPIO_NUM_MAX];               // ultimo estado confirmado de cada pin
    for (int i = 0; i < GPIO_NUM_MAX; i++) {
        suelto[i] = true;                           // con pull-up, suelto = 1
    }
    gpio_num_t pin;
    for (;;) {
        xQueueReceive(s_pulsos, &pin, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(ANTIRREBOTE_MS));   // esperar que termine el rebote
        xQueueReset(s_pulsos);                        // descartar los flancos del rebote
        bool nivel = entrada_get(pin);
        if (nivel == suelto[pin]) {
            continue;        // mismo estado que antes: era rebote
        }
        suelto[pin] = nivel;
        if (nivel) {
            continue;        // paso a suelto: no se cuenta
        }
        // ... apretado de verdad: tu logica. Ej:
        if (pin == SW1_GPIO) {
            var_set("led2", "toggle");
        }
    }
}

// en app_main, despues de registrar_variables y sistema_init:
s_pulsos = xQueueCreate(10, sizeof(gpio_num_t));   // ANTES de habilitar la interrupcion
xTaskCreate(tarea_pulsos, "pulsos", 4096, NULL, 5, NULL);
entrada_interrupcion(SW1_GPIO, GPIO_INTR_ANYEDGE, al_apretar, (void *)SW1_GPIO);
entrada_interrupcion(SW2_GPIO, GPIO_INTR_ANYEDGE, al_apretar, (void *)SW2_GPIO);  // misma ISR
```

- El pin ya tiene que estar configurado como entrada: `var_din("sw1", ...)` lo hace (con
  pull-up). Si no lo registrás como variable, usá `entrada_init(pin, GPIO_PULLUP_ONLY)`.
- `GPIO_INTR_NEGEDGE` = al apretar (con pull-up el pin baja a 0), `POSEDGE` = al soltar,
  `ANYEDGE` = los dos. Acá se usa `ANYEDGE` para que la tarea también se entere del soltar.
- **Rebote:** un pulsador mecánico genera varios flancos en pocos ms, **al apretar y
  también al soltar**. La tarea espera `ANTIRREBOTE_MS`, tira los flancos acumulados, lee el
  nivel y **cuenta solo el cambio de suelto a apretado**: para contar otra apretada tiene que
  haber visto el pin suelto antes. Probado en la placa:
  - Solo por tiempo ("ignorar lo que llegue dentro de 50 ms"): 9 pulsos en 5 apretadas
    (el rebote al soltar también da flancos de bajada).
  - Esperar + confirmar que siga en 0, sin recordar el estado: 7 en 5 (rebotes de más de
    50 ms con el botón todavía apretado se contaban dos veces).
  - Con estado por pin y `ANYEDGE`: 5 en 5.
- El `arg` es el propio pin: una sola ISR sirve para los 4 pulsadores y la tarea sabe cuál
  leer. Si dos pulsadores se aprietan con menos de 50 ms de diferencia, se pierde uno
  (el `xQueueReset` descarta todo).

### Qué herramienta usar para avisar

| Necesito... | Usá | Desde la ISR | En la tarea |
|---|---|---|---|
| avisar "pasó algo" | semáforo binario | `xSemaphoreGiveFromISR` | `xSemaphoreTake` |
| avisar y mandar un dato (qué pulsador, una lectura) | cola | `xQueueSendFromISR` | `xQueueReceive` |
| avisar varias cosas distintas, o esperar que pasen varias | event group | `xEventGroupSetBitsFromISR` | `xEventGroupWaitBits` |
| avisar a una tarea puntual, lo más liviano | notificación | `vTaskNotifyGiveFromISR` | `ulTaskNotifyTake` |
| proteger un dato que tocan varias tareas | mutex (nunca en una ISR) | — | `xSemaphoreTake` / `xSemaphoreGive` |

---

## 11. Problemas comunes

| Síntoma | Causa | Solución |
|---|---|---|
| `Could not open COM6 ... Acceso denegado` | El monitor o MobaXterm (serie) tiene el puerto | Cerrarlos y volver a flashear |
| No sé la IP del ESP32 | — | Monitor serie: `STA conectada, IP: ...` |
| La IP no responde | Todavía se está conectando al router, o la PC está en otra red | Esperar unos segundos; revisar la red |
| `/` por el router no muestra la config | Es a propósito | Entrar por la red `ESP32-Parcial` a `192.168.4.1` |
| MobaXterm conecta pero no muestra nada | Hay otro cliente en la consola | Cerrar el otro (o esperar ~11 s si se colgó) |
| Todo aparece dos veces en PuTTY | Sesión *Raw* | Usar *Telnet* |
| Cambié `www/` y no se ve | La imagen SPIFFS se arma al compilar | `idf.py -p COM6 flash` (compila y graba) |
| El navegador muestra la página vieja | Caché | Ctrl+F5 |
| "Valor invalido" | Fuera de rango, o texto con `"` o `\` | Ver el rango en `registrar_variables()` |
| El ESP se reinicia al arrancar con `ESP_ERR_NO_MEM` | Más de `VAR_MAX` variables | Subir `VAR_MAX` en `variables.h` |
| La config quedó mal guardada | NVS | `idf.py -p COM6 erase-flash` y flashear |

## 12. Límites (constantes)

| Constante | Dónde | Valor | Qué limita |
|---|---|---|---|
| `VAR_MAX` | `variables.h` | 24 | variables registradas |
| `VAR_TEXTO_MAX` | `variables.h` | 32 | largo de un texto (31 + `\0`) |
| `JSON_MAX` | `sistema.c` | 2048 | tamaño de las respuestas `def` / `valores` |
| `WEB_MAX_CONEXIONES` | `web.c` | 4 | conexiones HTTP abiertas a la vez |
| `PRINTF_MAX` | `consola.c` | 256 | largo de un `consola_printf` |
| `CONSOLA_MAX` | `consola.h` | 2 | consolas a la vez (TCP + UART) |
| `_COMMAND_TOKEN_NMB` | `config.h` | 8 | palabras por línea de consola |
| `ESPERA_MS` | `consola_tcp.c` | 1000 | cada cuánto se revisa el cambio de puerto |
| `KEEPALIVE_*` | `tcp.c` | 5 s / 2 s / 3 | detección de cliente caído (~11 s) |
| `TEMPORIZADOR_MAX` | `temporizador.h` | 4 | timers por hardware |
| `REFRESCO_MS` | `app.js`, `app.py` | 1000 | polling de los clientes |
| `ERROR_MS` | `app.js`, `app.py` | 4000 | cuánto se ve un error |
