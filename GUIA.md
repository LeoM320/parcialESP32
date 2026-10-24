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
```

Dos ideas para no perderse:

1. **Hay un solo lugar donde viven los datos: el registro de variables** (`variables.c`).
   La web, la app Qt y la consola son tres "ventanas" al mismo registro. Ninguna guarda
   el estado: muestran lo que el ESP32 confirma.
2. **Cada canal tiene un solo uso:**
   - **HTTP (puerto 80)** → para programas: la página web y la app Qt. Hablan JSON.
   - **TCP (puerto 1234)** → para personas: MobaXterm. Habla texto.

---

## 2. Arranque: qué pasa al prender el ESP32

`app_main()` en `main.c`, en este orden (el orden importa):

| Paso | Llamada | Por qué va ahí |
|---|---|---|
| 1 | `registrar_variables()` | Antes de crear tareas: el registro no se modifica después, así nadie lo lee a medio armar. |
| 2 | `memoria_init()` | Abre la NVS. WiFi la necesita y nosotros leemos la config de ahí. |
| 3 | `memoria_get_*()` | Lee SSID, clave y puerto guardados. Si nunca se guardó nada, quedan los valores por defecto (`CONFIG_ESP_WIFI_*`, puerto 1234). |
| 4 | `wifi_init(...)` | Levanta el AP y, si hay SSID, conecta la STA al router. |
| 5 | `consola_tcp_init(puerto, ejecutar)` | Crea la tarea de la consola. Después de WiFi porque usa sockets. |
| 6 | `web_init(endpoints, n)` | Arranca el servidor HTTP. Al final porque `config_post` usa `consola_tcp_puerto()`. |

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

**Archivos:** `web.h/.c`, carpeta `www/`, handlers en `main.c`.

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
    var_init();
    var_dout("led2", "LED 2", LED2_GPIO, 0, 1);
    var_num ("vel", "Velocidad", 0, 10, 5, 3, 0);   // ← nueva: fila 3, columna 0
}
```

Compilar, flashear y listo: la web, Qt y la consola la muestran solas.

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

## 7. La consola TCP (MobaXterm)

**Archivos:** `tcp.h/.c`, `telnet.h/.c`, `microrl.c/.h` + `config.h` (del profe),
`consola.h/.c`, `consola_tcp.h/.c`, `ejecutar()` en `main.c`.

### Conectarse

MobaXterm → **Session → Telnet** → Remote host: la IP del ESP32 → Port: `1234`.
En PuTTY: tipo **Telnet** (con *Raw* se ve doble eco).

```
Consola ESP32 - 'help' para ver los comandos

ESP32> vars
  led1       LED 1 (brillo)   pwm    0 %
  led2       LED 2            dout   off
  sw1        SW 1             din    off
  pot1       Pote 1           ain    49.35 %
  ...
ESP32> led2 on
led2 = on
ESP32> consigna 99
ERROR: valor invalido para consigna: 99
```

Comandos: `help`, `vars`, `<id>` (ver) y `<id> <valor>` (cambiar).

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

**¿Por qué Telnet?** Al conectarse, `telnet_inicio()` le dice a MobaXterm "el eco lo
hago yo y mandá cada tecla al toque" (`IAC WILL ECHO`, `IAC WILL SGA`). Sin eso, MobaXterm
manda la línea entera al final y muestra todo dos veces.

### Agregar un comando

En `ejecutar()` de `main.c`, antes del `else` final:

```c
} else if (strcmp(argv[0], "reset") == 0) {
    var_set("consigna", "25");
    consola_printf("Consigna reiniciada\r\n");
} else {
    cmd_variable(argc, argv);      // <id> / <id> <valor>
}
```

- `argv[0]` es el comando, `argv[1]`... los argumentos, `argc` cuántas palabras hay.
  microrl acepta como mucho 8 palabras por línea.
- Las líneas terminan en `\r\n`.
- `consola_printf` corta a 256 caracteres. Para algo más largo, usá `consola_escribir`.
- Empezá los errores con `ERROR: ` para que se reconozcan fácil.
- Acordate de sumarlo al texto de `help`.

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

### Los ladrillos

Cada clase define solo dos cosas:

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

Y la tabla `TIPOS` elige la clase según el `tipo` que mandó el ESP32. Un tipo que el
cliente no conoce cae en `Desconocido`, que muestra el valor tal cual: nunca se rompe.

| Tipo | Clase | Se ve como |
|---|---|---|
| `dout` | `SalidaDigital` | botón ON/OFF |
| `din` | `EntradaDigital` | indicador ACTIVO / inactivo |
| `ain` | `EntradaAnalogica` | barra + valor con unidad |
| `pwm` | `Pwm` | deslizador (manda al soltar; mientras arrastrás, el polling no lo pisa) |
| `num` | `Numero` (hereda de `Editable`) | valor + campo + Enviar |
| `texto` | `Texto` (hereda de `Editable`) | valor + campo + Enviar |

---

## 9. Sincronización: quién corre dónde

| Tarea | Qué hace | Qué toca |
|---|---|---|
| `main` | arranca todo y termina | registra variables (antes de que existan las demás tareas) |
| servidor HTTP | atiende la web y Qt, de a un pedido | `var_*` (con mutex), `s_json_web` (solo ella) |
| `consola_tcp` | atiende MobaXterm | `var_*` (con mutex), microrl y `s_resp` (solo ella) |
| (tus tareas) | la lógica del ejercicio | `var_get` / `var_set` (con mutex) |

Las herramientas de FreeRTOS que se usan:

- **Mutex en `variables.c`:** `var_set` escribe el pin y el valor juntos con el mutex tomado.
  Nadie lee un valor que todavía no se aplicó, aunque la web, la consola y tu tarea toquen
  la misma variable a la vez. El mutex tiene herencia de prioridad.
- **Cola de 1 elemento para el puerto:** `config_post` (tarea HTTP) hace `xQueueOverwrite`
  con el puerto nuevo y la tarea de la consola lo mira con `xQueuePeek` cada 1 s. Así
  se pasa un dato entre tareas sin variables compartidas.
- **Sin mutex donde una sola tarea usa algo:** la consola, microrl y los buffers `static`
  de cada tarea.
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

Como `var_set` es el mismo camino que usan los clientes, la web, Qt y la consola muestran
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

Ver la sección 7: un `else if` en `ejecutar()` + una línea en `help`.

### E. "Un endpoint nuevo"

```c
/* GET /api/hola -> {"mensaje":"hola"} */
static esp_err_t hola_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"mensaje\":\"hola\"}");
}

// en la tabla endpoints[]:
{ .uri = "/api/hola", .method = HTTP_GET, .handler = hola_get },
```

Para un POST: `web_leer_cuerpo()` + `json_valor()` (ver `set_post`). No hay que
configurar nada más: `web_init()` agranda solo el límite de endpoints según la tabla.

### F. "Una forma nueva de mostrar algo" (ladrillo nuevo)

Si el tipo ya existe y solo querés otra forma de verlo, escribí la clase en
`ladrillos.js` y en `ladrillos.py` y cambiala en `TIPOS`.

Si es un **tipo nuevo**, también hay que tocar el ESP32. Los pasos están en el
comentario de arriba de `variables.c`:

1. sumarlo a `tipo_t` y `NOMBRE_TIPO`
2. escribir su `var_<tipo>()` (y declararla en `variables.h`)
3. agregar su `case` en `leer()`, `var_set()`, `agregar_valor_json()` y `agregar_valor_texto()`
4. una clase en cada cliente + agregarla a `TIPOS`

### G. "Cambiar la red o el puerto"

Desde la página de configuración (por el AP), sin reprogramar. Los valores por defecto
están en `menuconfig` (`CONFIG_ESP_WIFI_SSID`, etc.) y `PUERTO_INICIAL` en `main.c`.

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
| `JSON_MAX` | `main.c` | 2048 | tamaño de `def` / `valores` / `vars` |
| `WEB_MAX_CONEXIONES` | `web.c` | 4 | conexiones HTTP abiertas a la vez |
| `PRINTF_MAX` | `consola.c` | 256 | largo de un `consola_printf` |
| `_COMMAND_TOKEN_NMB` | `config.h` | 8 | palabras por línea de consola |
| `ESPERA_MS` | `consola_tcp.c` | 1000 | cada cuánto se revisa el cambio de puerto |
| `KEEPALIVE_*` | `tcp.c` | 5 s / 2 s / 3 | detección de cliente caído (~11 s) |
| `REFRESCO_MS` | `app.js`, `app.py` | 1000 | polling de los clientes |
| `ERROR_MS` | `app.js`, `app.py` | 4000 | cuánto se ve un error |
