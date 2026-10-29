/**
 * @file main.c
 * @brief Firmware de control para el brazo robotico SCARA 2DOF en el simulador MyScara (Qt).
 *
 * OJO: todos los includes van en includes.h, aca tiene que haber UNO solo.
 * Si el editor agrega algun #include arriba de includes.h, borrarlo antes de compilar.
 *
 * =========================================================================================
 *                     GUIA DE ESTUDIO: FLUJO DE TRABAJO DEL SISTEMA
 * =========================================================================================
 *
 * 1. ARRANQUE DEL SISTEMA (app_main)
 * ----------------------------------
 *   a) Primero se crean los objetos de sincronizacion de FreeRTOS (colaSw, colaValido,
 *      colaNumero, accesoScara y eventos) y se prende el bit BIT_MODO_A para arrancar
 *      en Modo A (Manual). Se crean antes de configurar interrupciones para que ninguna
 *      ISR use una cola sin inicializar.
 *   b) init_hardware(): configura los 4 LEDs como salidas digitales, los 4 pulsadores
 *      con pull-up e interrupcion por flanco de bajada (isr_pulsadores) y los 2 canales
 *      del ADC1 para los potenciometros.
 *   c) init_comunicacion(): crea colaRx y colaTx, abre UART0 a 115200 8N1 (donde escucha
 *      el simulador MyScara), inicializa NVS (memoria flash no volatil), monta la particion
 *      SPIFFS en "/spiffs" (donde vive index.html), recupera de NVS las credenciales WiFi
 *      (sta_ssid, sta_pass) y del servidor SCADA (scada_ip, scada_port), inicializa la
 *      consola microrl y levanta el WiFi en modo doble SoftAP + Station (APSTA).
 *   d) Finalmente lanza las 12 tareas de FreeRTOS repartidas entre los dos nucleos.
 *
 * 2. REPARTO DE NUCLEOS (Core Affinity) Y PRIORIDADES
 * ---------------------------------------------------
 *   - CORE_WIFI (Core 0): aloja internamente el stack WiFi (prio 23) y lwIP TCP/IP (prio 18)
 *     de ESP-IDF. Por eso ubicamos en el Core 0 las tareas de red:
 *       * task_rxTcpServer (prio +5): escucha en el puerto 30000 (consola microrl y web HTTP).
 *       * task_rxTcpClient (prio +5): mantiene la conexion TCP saliente hacia el SCADA.
 *       * task_telemetria  (prio +2): arma y envia el POST HTTP JSON cada 1 segundo.
 *   - CORE_CONTROL (Core 1): queda aislado del trafico WiFi para garantizar tiempo real:
 *       * task_control     (prio +5): lazo cerrado de posicion cada 50 ms exactos.
 *       * task_rxUart0     (prio +5): lee bytes de UART0 y los vuelca en colaRx.
 *       * task_rx          (prio +4): UNICA tarea receptora (consume colaRx).
 *       * task_tx          (prio +4): UNICA tarea transmisora (consume colaTx).
 *       * task_adc         (prio +4): lee potes cada 20 ms y aplica promedio movil de 10 muestras.
 *       * task_antirrebote (prio +4): filtra rebotes mecanicos de los switches (50 ms).
 *       * task_switches    (prio +3): aplica el ajuste fino (+-0.5 grados) en Modo A.
 *       * task_modoB       (prio +3): traza los 3 digitos en 7 segmentos en Modo B.
 *       * task_parpadeo    (prio +2): parpadea 3 veces los LEDs (100 ms ON/OFF) con LIMIT_SW.
 *
 * 3. FLUJO DE COMUNICACIONES: COLA UNICA DE RX Y COLA UNICA DE TX
 * ---------------------------------------------------------------
 *   Todas las entradas y salidas pasan por bloques (bloque_t) que llevan la etiqueta del
 *   canal (MEDIO_UART0, MEDIO_SERVER o MEDIO_CLIENT), la cantidad de bytes y los datos:
 *
 *   - RECEPCION (3 productoras -> colaRx -> 1 consumidora task_rx):
 *       1) task_rxUart0 lee del simulador Qt por serie y mete el bloque con MEDIO_UART0.
 *       2) task_rxTcpServer lee del puerto 30000:
 *            - Si una conexion nueva empieza con "GET ", es un navegador web: llama a
 *              responder_http() para devolver index.html o el JSON /estado y cierra ese
 *              socket temporal sin cortar la sesion de PuTTY.
 *            - Si no empieza con "GET ", es la consola PuTTY: guarda el socket en
 *              socketServer y todo lo que llega lo manda a colaRx con MEDIO_SERVER.
 *       3) task_rxTcpClient se conecta al servidor SCADA (scadaIp:scadaPuerto) y todo lo
 *          que responde el servidor lo manda a colaRx con MEDIO_CLIENT.
 *       -> task_rx saca cada bloque de colaRx y segun b.medio:
 *            * MEDIO_UART0:  pasa byte por byte a la maquina de estados uner_rx_byte();
 *                            cuando cierra una trama valida con checksum XOR correcto,
 *                            llama a procesar_uner().
 *            * MEDIO_SERVER: pasa byte por byte a microrl_insert_char(); cuando el usuario
 *                            aprieta Enter, microrl invoca al callback execute(argc, argv).
 *            * MEDIO_CLIENT: acumula la respuesta HTTP del SCADA, busca el JSON {"ACK":...},
 *                            lo parsea con cJSON y actualiza scadaAck ("OK" o "ERROR").
 *
 *   - TRANSMISION (multiples productoras -> colaTx -> 1 consumidora task_tx):
 *       Cualquier tarea que quiera transmitir usa enviar_uner() (para tramas binarias UNER)
 *       o enviar_texto() (para texto hacia PuTTY o POST HTTP hacia el SCADA). Ambas arman
 *       el bloque_t y hacen xQueueSend(colaTx, ...).
 *       -> task_tx es la unica que toca los drivers de salida: saca de colaTx y llama a
 *          serie_enviar() o tcp_enviar() segun corresponda.
 *
 * 4. FLUJO DEL LAZO DE CONTROL Y PROTOCOLO UNER (task_control + procesar_uner)
 * ----------------------------------------------------------------------------
 *   El estado completo del robot vive en la estructura global "scara", protegida en todo
 *   momento por el mutex "accesoScara" (con herencia de prioridad):
 *     - scara.ang[0..1], scara.pwr[0..1], scara.stateArm y scara.pen son los valores reales
 *       que reporta el simulador Qt.
 *     - scara.obj[0..1] es el angulo objetivo en cuentas ADC (656 = 0°, 3428 = 180°).
 *     - scara.pwrMax[0..1] es el tope de potencia (100 por defecto, o el que fije MOVE).
 *     - scara.avance[0..1] es la potencia de prealimentacion (feedforward) al trazar rectas.
 *
 *   Ciclo de 50 ms en task_control:
 *     1) Envia CMD_GET_STATE (0xD0) por UART0 y duerme 50 ms exactos con xTaskDelayUntil.
 *     2) Mientras duerme, el Qt contesta y task_rx -> procesar_uner() actualiza scara.ang,
 *        scara.pwr, scara.stateArm y refresca los LEDs de tope con mostrar_topes().
 *     3) Al despertar, task_control toma una copia de "scara" con el mutex:
 *        - Filtra la lectura angular con media exponencial (FILTRO = 0.5) para atenuar el
 *          ruido de +-10 cuentas que introduce el simulador.
 *        - Si el brazo esta quieto (avance == 0), calcula el error contra el objetivo actual
 *          (copia.obj) y aplica banda muerta (BANDA = 6 cuentas) para que frene sin temblar.
 *        - Si esta dibujando una recta (avance != 0), calcula el error contra el objetivo de
 *          hace RETRASO = 8 ciclos (400 ms atras, guardado en el buffer circular "historia"),
 *          porque el simulador tiene inercia y va ~400 ms atrasado siguiendo la rampa.
 *        - Calcula la ley de control proporcional + feedforward:
 *              pwr = avance + KP * error
 *          y la satura entre -pwrMax y +pwrMax.
 *        - Invierte el signo de pwr[1] (en el Qt, potencia positiva en el motor 2 disminuye
 *          las cuentas de ang2) y anula la potencia si el brazo intenta empujar contra un
 *          final de carrera activo (ARM1_L, ARM1_R, ARM2_L, ARM2_R).
 *        - Envia CMD_SET_POWER (0xD1) con las potencias calculadas.
 *        - Si ambas potencias dieron 0 (llego al destino), prende BIT_EN_POSICION en el
 *          Event Group "eventos"; si sigue moviendose, lo apaga.
 *
 * 5. GESTION DE FINALES DE CARRERA Y LEDS (0xD4 LIMIT REACHED)
 * ------------------------------------------------------------
 *   Cuando el brazo toca un limite en el simulador, el Qt envia solo el comando 0xD4 con el
 *   byte stateArm:
 *     - procesar_uner() guarda stateArm, actualiza los 4 LEDs fisicos (LD0=ARM1_L, LD1=ARM1_R,
 *       LD2=ARM2_L, LD3=ARM2_R) mediante mostrar_topes() y devuelve el ACK (0xD4 stateArm).
 *     - Si el bit ARM_LIMIT_SW (desplazamiento maximo hacia atras) viene en 1, procesar_uner()
 *       prende el bit BIT_PARPADEO en "eventos".
 *     - Esto despierta a task_parpadeo(), que hace parpadear los 4 LEDs 3 veces (100 ms ON y
 *       100 ms OFF). Mientras BIT_PARPADEO esta en 1, mostrar_topes() ignora las actualizaciones
 *       normales para no cortar el parpadeo. Al terminar los 3 destellos, limpia BIT_PARPADEO
 *       y restaura en los LEDs el estado de los finales de carrera.
 *
 * 6. MODOS DE OPERACION DEL ROBOT
 * -------------------------------
 *   El bit BIT_MODO_A del Event Group "eventos" define el modo activo (1 = Modo A, 0 = Modo B):
 *
 *   A) MODO A - MANUAL HARDWARE (BIT_MODO_A == 1):
 *      - Posicion gruesa (task_adc): lee los 2 potes cada 20 ms, calcula la media movil de las
 *        ultimas 10 muestras y, si algun pote se movio mas de UMBRAL_POTE (40 cuentas), mapea
 *        el rango 0..4095 a 656..3428 cuentas (0..180°) y actualiza scara.obj. El umbral evita
 *        que el ruido del ADC pise los ajustes finos de los pulsadores o del comando MOVE
 *        cuando el usuario no esta tocando los potenciometros.
 *      - Ajuste fino (isr_pulsadores -> colaSw -> task_antirrebote -> colaValido -> task_switches):
 *        Al presionar un pulsador, la ISR envia su indice (0..3) a colaSw. task_antirrebote
 *        espera 50 ms, verifica que siga presionado, lo pasa a colaValido y espera a que se
 *        suelte. task_switches recibe el pulsador validado y suma o resta PASO_FINO_CUENTAS
 *        (8 cuentas = 0.5 grados) sobre scara.obj[0] (SW0/SW1) o scara.obj[1] (SW2/SW3).
 *
 *   B) MODO A - MANUAL COMANDOS (microrl -> execute):
 *      Desde PuTTY (Raw, puerto 30000) el usuario ingresa comandos que ejecuta execute():
 *        - STATE:   devuelve potencias, angulos en grados y cuentas, y stateArm.
 *        - MOVE:    fija los limites pwrMax y los nuevos objetivos scara.obj[0..1].
 *        - NUMERO:  pasa a Modo B y encola el numero de 3 cifras en colaNumero.
 *        - SETMODE: cambia entre Modo A y Modo B (o consulta el modo actual sin parametros).
 *        - WIFI:    guarda SSID y PASSWORD en NVS y reconecta la interfaz WiFi Station.
 *        - SCADA:   guarda IP y PORT en NVS y fuerza la reconexion del cliente TCP al SCADA.
 *        - STOP:    prende BIT_STOP, vacia colaNumero, iguala el objetivo al angulo actual,
 *                   levanta el lapiz y frena los motores inmediatamente.
 *
 *   C) MODO B - AUTONOMO: ESCRITURA DE 3 DIGITOS (task_modoB):
 *      Al entrar en Modo B se apaga BIT_MODO_A: task_adc queda bloqueada durmiendo en
 *      xEventGroupWaitBits() y task_switches ignora los botones.
 *      Cuando llega un numero (0..999) por colaNumero:
 *        1) Levanta el lapiz (cmd_setPen(0)) y limpia el lienzo del Qt (CMD_CLEAR).
 *        2) Separa el numero en centena, decena y unidad.
 *        3) Ubica los 3 digitos dentro del anillo alcanzable del SCARA (x de -40 a +40,
 *           y de 280 a 320). Para cada digito consulta tabla7seg y recorre los 7 segmentos:
 *           - Si el inicio del segmento no coincide con el punto donde quedo el lapiz, sube
 *             el lapiz, viaja rapido (VELOCIDAD_VIAJE = 40 u/s) hasta el inicio y baja el lapiz.
 *           - Si el segmento empieza donde termino el anterior, mantiene el lapiz abajo para
 *             hacer un trazo continuo.
 *           - Traza el segmento con mover_recto() a VELOCIDAD = 10 u/s: interpola puntos (x,y)
 *             cada 50 ms con perfil de velocidad cosenoidal (arranque y frenado suave), convierte
 *             cada punto a angulos por cinematica inversa en mover() (ley del coseno) y al
 *             final del segmento espera BIT_EN_POSICION antes de pasar al siguiente.
 *        4) Al terminar los 3 digitos, levanta el lapiz y estaciona el brazo al costado en
 *           (140, 260) para dejar el numero despejado en pantalla.
 *
 * 7. TELEMETRIA SCADA Y PANEL WEB (task_telemetria + responder_http)
 * ------------------------------------------------------------------
 *   - Telemetria SCADA: cada 1000 ms task_telemetria lee el estado del brazo con el mutex,
 *     arma el paquete HTTP POST /scara/telemetry con el JSON requerido y lo envia de forma
 *     no bloqueante a traves de colaTx (MEDIO_CLIENT). La respuesta {"ACK":"OK"} vuelve por
 *     task_rxTcpClient -> colaRx -> task_rx.
 *   - Panel Web: cuando el navegador pide http://<IP>:30000/, responder_http() envia el
 *     archivo /spiffs/index.html. El JavaScript de la pagina consulta cada 250 ms el endpoint
 *     GET /estado en el mismo puerto 30000 para graficar angulos, potencias, lapiz y finales
 *     de carrera en tiempo real.
 * =========================================================================================
 */

/* ======================= Includes ======================= */

#include "includes.h"

/* ======================= Constantes ======================= */

#define REBOTE_MS 50			// tiempo de validacion de los pulsadores
#define PERIODO_ADC_MS 20		// cada cuanto leo los potes
#define MEDIA_N 10				// muestras del promedio movil (10 x 20 ms = 200 ms)
#define PASO_FINO_CUENTAS 8		// cuentas por toque de switch (0.5 grados: (3428-656)/180 * 0.5 = 7.7)
#define UMBRAL_POTE 40			// cambio minimo del pote para pisar el objetivo actual

#define CORE_WIFI 0				// core 0: wifi, tcp, telemetria (junto a la tarea wifi de idf)
#define CORE_CONTROL 1			// core 1: control en tiempo real, hardware y protocolo uner

#define BIT_MODO_A (1 << 0)		// en eventos: prendido = modo A, apagado = modo B
#define BIT_EN_POSICION (1 << 1)	// en eventos: lo prende task_control cuando los 2 brazos llegaron
#define BIT_PARPADEO (1 << 2)	// en eventos: lo prende procesar_uner con LIMIT_SW
#define BIT_STOP (1 << 3)		// en eventos: lo prende STOP para frenar el dibujo en curso

#define BLOQUE_MAX 64			// bytes por bloque de colaRx y colaTx
#define LARGO_COLA 10			// bloques que entran en colaRx y en colaTx
#define PUERTO_SERVER 30000		// puerto del servidor TCP (consola microrl y pagina web HTTP)
#define TCP_TIMEOUT_MS 1000		// espera maxima de recibir en tcp
#define UART_TIMEOUT_MS 1000	// espera maxima de serie_recibir
#define AP_SSID "RedMeola"		// nombre de la red WiFi propia (SoftAP en 192.168.4.1)
#define AP_PASS "12345678"		// clave del SoftAP (minimo 8 caracteres)
// #define STA_SSID_PRUEBA "InternetPlus2.4G-1770"	// red WiFi STA de casa
// #define STA_PASS_PRUEBA "49161770"
#define STA_SSID_PRUEBA "FCAL"					// red WiFi STA de la facultad (se cambia con WIFI)
#define STA_PASS_PRUEBA "fcalconcordia.06-2019"
#define NOMBRE_ALUMNO "Meolans"	// campo NAME del JSON de telemetria
#define PERIODO_TELEMETRIA_MS 1000	// periodo de envio del POST de telemetria al SCADA

#define CMD_ALIVE 0xF0			// ESP -> Qt, sin datos. Responde F0 0D
#define CMD_GET_STATE 0xD0		// ESP -> Qt, sin datos. Responde angle1, angle2, pwr1, pwr2 (int32) + stateArm (uint8)
#define CMD_SET_POWER 0xD1		// ESP -> Qt, pwr1 pwr2 (int32). Responde lo mismo
#define CMD_SET_PEN 0xD2		// ESP -> Qt, statePEN (uint8, 0 o 1). Responde lo mismo
#define CMD_CLEAR 0xD3			// ESP -> Qt, sin datos. Responde D3 0D
#define CMD_LIMIT 0xD4			// Qt -> ESP, stateArm (uint8). Le respondo D4 stateArm
#define UNER_ACK 0x0D			// byte de confirmacion para ALIVE y CLEAR

#define ARM_PEN (1 << 0)		// bits de stateArm
#define ARM_LIMIT_SW (1 << 1)
#define ARM1_L (1 << 2)
#define ARM1_R (1 << 3)
#define ARM2_L (1 << 4)
#define ARM2_R (1 << 5)

#define PERIODO_CONTROL_MS 50	// periodo del lazo de control (GET STATE + SET POWER)
#define KP 0.3f					// ganancia proporcional (pwr por cuenta de error)
#define CUENTAS_POR_PWR 16.4f	// velocidad medida: cuentas/s por unidad de pwr
#define FILTRO 0.5f				// factor del filtro EMA de lectura angular
#define VELOCIDAD 10.0f			// velocidad del lapiz al dibujar (unidades/s)
#define VELOCIDAD_VIAJE 40.0f	// velocidad con el lapiz arriba entre trazos
#define RETRASO 8				// vueltas de control que el brazo va atrasado al seguir el avance
#define PWR_MAX 100				// potencia maxima admitida por el simulador Qt
#define BANDA 6					// banda muerta en cuentas cuando el objetivo esta quieto
#define CUENTAS_0 656.0f		// cuentas del simulador en 0 grados
#define CUENTAS_180 3428.0f		// cuentas del simulador en 180 grados
#define L1 160.0f				// largo del brazo 1 (hombro -> codo)
#define L2 190.0f				// largo del brazo 2 (codo -> lapiz)

/* ======================= Tipos ======================= */

typedef enum {
	MEDIO_UART0,				// simulador MyScara por serie
	MEDIO_SERVER,				// consola microrl conectada al puerto 30000
	MEDIO_CLIENT				// servidor SCADA remoto
}medio_t;

typedef struct {
	medio_t medio;				// rx: de donde vino / tx: a donde va
	uint8_t largo;				// bytes validos en datos
	uint8_t datos[BLOQUE_MAX];
}bloque_t;

typedef struct {
	int32_t ang[2];				// angulos leidos del Qt en cuentas ([0] hombro, [1] codo)
	int32_t pwr[2];				// potencias actuales reportadas por el Qt
	uint8_t stateArm;			// bitmap: ARM_PEN, ARM_LIMIT_SW, ARM1_L...
	uint8_t pen;				// ultimo estado del lapiz confirmado
	int32_t obj[2];				// angulo objetivo en cuentas para task_control
	int32_t pwrMax[2];			// limite de potencia de cada motor
	float avance[2];			// potencia de feedforward mientras dibuja (0 = quieto)
}scara_t;

/* ======================= Variables globales ======================= */

QueueHandle_t colaSw;			// isr_pulsadores -> task_antirrebote
QueueHandle_t colaValido;		// task_antirrebote -> task_switches
QueueHandle_t colaNumero;		// execute (NUMERO) -> task_modoB
QueueHandle_t colaRx;			// task_rxXxx -> task_rx
QueueHandle_t colaTx;			// cualquiera -> task_tx
SemaphoreHandle_t accesoScara;	// protege la estructura scara
EventGroupHandle_t eventos;		// flags de modo, posicion, parpadeo y stop

int socketServer = -1;			// cliente de consola conectado al puerto 30000 (-1 si no hay)
int socketClient = -1;			// socket conectado al SCADA (-1 si no hay)
char scadaIp[16] = "192.168.123.37";	// IP del servidor SCADA
uint16_t scadaPuerto = 5000;	// puerto del servidor SCADA
char staSsid[33] = STA_SSID_PRUEBA;	// SSID de la red WiFi Station
char staPass[65] = STA_PASS_PRUEBA;	// clave de la red WiFi Station
char scadaAck[16] = "-";		// ultimo ACK recibido del SCADA
microrl_t rl;					// estado de la consola microrl
scara_t scara = {.obj = {2048, 2048}, .pwrMax = {PWR_MAX, PWR_MAX}};	// estado compartido del robot

/* ======================= Prototipos ======================= */

void init_hardware(void);
void isr_pulsadores(void *arg);
void task_adc(void *pvParam);
void task_antirrebote(void *pvParam);
void task_switches(void *pvParam);
void init_comunicacion(void);
void task_rxUart0(void *pvParam);
void task_rxTcpServer(void *pvParam);
void task_rxTcpClient(void *pvParam);
void task_rx(void *pvParam);
void task_tx(void *pvParam);
void enviar_uner(medio_t medio, const uint8_t *carga, uint8_t n);
void enviar_texto(medio_t medio, const char *texto);
void enviar_server(const char *texto);
void cmd_setPower(int32_t pwr1, int32_t pwr2);
void cmd_setPen(uint8_t pen);
void procesar_uner(medio_t medio, const uint8_t *carga, uint8_t n);
void mostrar_topes(uint8_t stateArm);
void task_parpadeo(void *pvParam);
bool mover(float x, float y, bool avance);
void task_control(void *pvParam);
void mover_recto(float x, float y, float velocidad);
void task_modoB(void *pvParam);
void task_telemetria(void *pvParam);
void responder_http(int sock, const char *req);
int execute(int argc, const char *const *argv);

/**
 * @brief Crea los objetos de FreeRTOS, inicializa hardware y comunicaciones y lanza las tareas.
 */
void app_main(void)
{
	colaSw = xQueueCreate(10, sizeof(uint8_t));
	colaValido = xQueueCreate(10, sizeof(uint8_t));
	colaNumero = xQueueCreate(5, sizeof(int));
	accesoScara = xSemaphoreCreateMutex();
	eventos = xEventGroupCreate();
	xEventGroupSetBits(eventos, BIT_MODO_A);
	init_hardware();
	init_comunicacion();
	xTaskCreatePinnedToCore(task_adc,
				"task adc",
				2048,
				NULL,
				tskIDLE_PRIORITY + 4,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_antirrebote,
				"task antirrebote",
				2048,
				NULL,
				tskIDLE_PRIORITY + 4,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_switches,
				"task switches",
				2048,
				NULL,
				tskIDLE_PRIORITY + 3,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_rxUart0,
				"task rxUart0",
				3072,
				NULL,
				tskIDLE_PRIORITY + 5,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_rxTcpServer,
				"task rxTcpServer",
				4096,
				NULL,
				tskIDLE_PRIORITY + 5,
				NULL,
				CORE_WIFI);
	xTaskCreatePinnedToCore(task_rxTcpClient,
				"task rxTcpClient",
				3072,
				NULL,
				tskIDLE_PRIORITY + 5,
				NULL,
				CORE_WIFI);
	xTaskCreatePinnedToCore(task_rx,
				"task rx",
				3072,
				NULL,
				tskIDLE_PRIORITY + 4,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_tx,
				"task tx",
				3072,
				NULL,
				tskIDLE_PRIORITY + 4,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_control,
				"task control",
				3072,
				NULL,
				tskIDLE_PRIORITY + 5,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_parpadeo,
				"task parpadeo",
				2048,
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_modoB,
				"task modoB",
				3072,
				NULL,
				tskIDLE_PRIORITY + 3,
				NULL,
				CORE_CONTROL);
	xTaskCreatePinnedToCore(task_telemetria,
				"task telemetria",
				3072,
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL,
				CORE_WIFI);
}

/**
 * @brief Configura los 4 LEDs, los 4 switches con interrupcion por flanco de bajada y los 2 potes.
 */
void init_hardware(void){
	for(int i = 0; i < LED_COUNT; i++){
		salida_init(leds[i]);
	}
	for(int i = 0; i < SW_COUNT; i++){
		entrada_init(switches[i], GPIO_PULLUP_ONLY);
		entrada_interrupcion(switches[i], GPIO_INTR_NEGEDGE, isr_pulsadores, (void *)i);
	}
	analogica_init(POT1_GPIO);
	analogica_init(POT2_GPIO);
	rgb_init(RGB_GPIO);
	rgb_set(10, 0, 0);
}

/**
 * @brief ISR de los 4 pulsadores: envia el indice (0..3) a colaSw para que lo valide task_antirrebote.
 * @param arg numero de pulsador (0..3).
 */
void IRAM_ATTR isr_pulsadores(void *arg){
	uint8_t sw = (int)arg;
	BaseType_t despertar = pdFALSE;
	xQueueSendFromISR(colaSw, &sw, &despertar);
	portYIELD_FROM_ISR(despertar);
}

/**
 * @brief Lee los 2 potenciometros cada PERIODO_ADC_MS, filtra con media movil y actualiza scara.obj en modo A.
 *
 * En modo B duerme en xEventGroupWaitBits hasta que se active BIT_MODO_A.
 * Solo pisa scara.obj cuando alguno de los potes cambia mas de UMBRAL_POTE cuentas,
 * asi no anula el ajuste fino de los botones ni el comando MOVE si los potes estan quietos.
 * @param pvParam no se usa.
 */
void task_adc(void *pvParam){
	int muestras1[MEDIA_N] = {0}, muestras2[MEDIA_N] = {0};
	int suma1 = 0, suma2 = 0, i = 0, nuevo1, nuevo2, p1, p2;
	int potAnt1 = -1000, potAnt2 = -1000;
	bool inicio = false;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while(1){
		if(!(xEventGroupGetBits(eventos) & BIT_MODO_A)){
			xEventGroupWaitBits(eventos, BIT_MODO_A, pdFALSE, pdTRUE, portMAX_DELAY);
			xLastWakeTime = xTaskGetTickCount();
		}
		nuevo1 = analogica_get(POT1_GPIO);
		nuevo2 = analogica_get(POT2_GPIO);
		if(nuevo1 >= 0 && nuevo2 >= 0){
			if(!inicio){
				for(int k = 0; k < MEDIA_N; k++){
					muestras1[k] = nuevo1;
					muestras2[k] = nuevo2;
				}
				suma1 = nuevo1 * MEDIA_N;
				suma2 = nuevo2 * MEDIA_N;
				inicio = true;
			}
			suma1 = suma1 - muestras1[i] + nuevo1;
			suma2 = suma2 - muestras2[i] + nuevo2;
			muestras1[i] = nuevo1;
			muestras2[i] = nuevo2;
			i = (i + 1) % MEDIA_N;
			p1 = suma1 / MEDIA_N;
			p2 = suma2 / MEDIA_N;
			if(abs(p1 - potAnt1) > UMBRAL_POTE || abs(p2 - potAnt2) > UMBRAL_POTE){
				potAnt1 = p1;
				potAnt2 = p2;
				xSemaphoreTake(accesoScara, portMAX_DELAY);
				scara.avance[0] = 0;
				scara.avance[1] = 0;
				scara.pwrMax[0] = PWR_MAX;
				scara.pwrMax[1] = PWR_MAX;
				scara.obj[0] = lroundf(CUENTAS_0 + p1 * (CUENTAS_180 - CUENTAS_0) / 4095.0f);
				scara.obj[1] = lroundf(CUENTAS_0 + p2 * (CUENTAS_180 - CUENTAS_0) / 4095.0f);
				xSemaphoreGive(accesoScara);
			}
		}
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PERIODO_ADC_MS));
	}
}

/**
 * @brief Valida cada pulsacion de colaSw esperando REBOTE_MS al apretar y al soltar.
 * @param pvParam no se usa.
 */
void task_antirrebote(void *pvParam){
	uint8_t sw;
	while(1){
		xQueueReceive(colaSw, &sw, portMAX_DELAY);
		vTaskDelay(pdMS_TO_TICKS(REBOTE_MS));
		if(!entrada_get(switches[sw])){
			xQueueSend(colaValido, &sw, 0);
			while(!entrada_get(switches[sw])){
				vTaskDelay(pdMS_TO_TICKS(10));
			}
			vTaskDelay(pdMS_TO_TICKS(REBOTE_MS));
		}
		xQueueReset(colaSw);
	}
}

/**
 * @brief Ajuste fino en modo A: cada toque de SW0..SW3 corre +-0.5 grados (PASO_FINO_CUENTAS) el objetivo.
 * @param pvParam no se usa.
 */
void task_switches(void *pvParam){
	uint8_t sw;
	while(1){
		xQueueReceive(colaValido, &sw, portMAX_DELAY);
		if(xEventGroupGetBits(eventos) & BIT_MODO_A){
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			if(sw == 0){
				scara.obj[0] = (int32_t)fminf(CUENTAS_180, scara.obj[0] + PASO_FINO_CUENTAS);
			}else if(sw == 1){
				scara.obj[0] = (int32_t)fmaxf(CUENTAS_0, scara.obj[0] - PASO_FINO_CUENTAS);
			}else if(sw == 2){
				scara.obj[1] = (int32_t)fminf(CUENTAS_180, scara.obj[1] + PASO_FINO_CUENTAS);
			}else if(sw == 3){
				scara.obj[1] = (int32_t)fmaxf(CUENTAS_0, scara.obj[1] - PASO_FINO_CUENTAS);
			}
			scara.avance[0] = 0;
			scara.avance[1] = 0;
			scara.pwrMax[0] = PWR_MAX;
			scara.pwrMax[1] = PWR_MAX;
			xSemaphoreGive(accesoScara);
		}
	}
}

/**
 * @brief Inicializa colas de comunicacion, UART0, NVS, SPIFFS (para index.html), microrl y WiFi AP+STA.
 */
void init_comunicacion(void){
	esp_vfs_spiffs_conf_t spiffs = {
		.base_path = "/spiffs",
		.partition_label = "storage",
		.max_files = 2,
		.format_if_mount_failed = false,
	};
	colaRx = xQueueCreate(LARGO_COLA, sizeof(bloque_t));
	colaTx = xQueueCreate(LARGO_COLA, sizeof(bloque_t));
	serie_init(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, 115200, UART_TIMEOUT_MS);
	memoria_init();
	esp_vfs_spiffs_register(&spiffs);
	memoria_get_str("sta_ssid", staSsid, sizeof(staSsid));
	memoria_get_str("sta_pass", staPass, sizeof(staPass));
	memoria_get_str("scada_ip", scadaIp, sizeof(scadaIp));
	memoria_get_u16("scada_port", &scadaPuerto);
	microrl_init(&rl, enviar_server);
	microrl_set_execute_callback(&rl, execute);
	wifi_init(AP_SSID, AP_PASS, staSsid, staPass);
}

/**
 * @brief Recibe bytes de UART0 (simulador MyScara) y los encola en colaRx con MEDIO_UART0.
 * @param pvParam no se usa.
 */
void task_rxUart0(void *pvParam){
	bloque_t b;
	int n;
	b.medio = MEDIO_UART0;
	while(1){
		n = serie_recibir(UART_NUM_0, b.datos, BLOQUE_MAX);
		if(n > 0){
			b.largo = n;
			xQueueSend(colaRx, &b, portMAX_DELAY);
		}
	}
}

/**
 * @brief Servidor TCP en PUERTO_SERVER (30000): atiende peticiones HTTP REST y consola microrl.
 *
 * Si la conexion entrante empieza con "GET ", sirve la pagina web o el JSON /estado y cierra.
 * Si es una conexion de consola (PuTTY), la asigna a socketServer y envia los bytes a colaRx.
 * @param pvParam no se usa.
 */
void task_rxTcpServer(void *pvParam){
	bloque_t b;
	int n, nuevo;
	int servidor = tcp_servidor(PUERTO_SERVER, 100);
	b.medio = MEDIO_SERVER;
	while(1){
		nuevo = tcp_aceptar(servidor);
		if(nuevo >= 0){
			n = tcp_recibir(nuevo, b.datos, BLOQUE_MAX - 1);
			if(n > 0 && strncmp((char *)b.datos, "GET ", 4) == 0){
				b.datos[n] = '\0';
				responder_http(nuevo, (char *)b.datos);
				close(nuevo);
			}else{
				if(socketServer >= 0){
					close(socketServer);
				}
				socketServer = nuevo;
				microrl_init(&rl, enviar_server);
				microrl_set_execute_callback(&rl, execute);
				enviar_server("\r\nSCARA ESP32 - Consola microrl\r\n" _PROMPT_DEFAULT);
				if(n > 0){
					b.largo = n;
					xQueueSend(colaRx, &b, portMAX_DELAY);
				}
			}
		}
		if(socketServer >= 0){
			n = tcp_recibir(socketServer, b.datos, BLOQUE_MAX);
			if(n > 0){
				b.largo = n;
				xQueueSend(colaRx, &b, portMAX_DELAY);
			}else if(n < 0){
				close(socketServer);
				socketServer = -1;
			}
		}
	}
}

/**
 * @brief Cliente TCP hacia scadaIp:scadaPuerto: mantiene la conexion y manda lo recibido a colaRx.
 * @param pvParam no se usa.
 */
void task_rxTcpClient(void *pvParam){
	bloque_t b;
	int n, sock;
	b.medio = MEDIO_CLIENT;
	while(1){
		sock = tcp_conectar(scadaIp, scadaPuerto, TCP_TIMEOUT_MS);
		if(sock >= 0){
			socketClient = sock;
			n = 0;
			while(n >= 0){
				n = tcp_recibir(sock, b.datos, BLOQUE_MAX);
				if(n > 0){
					b.largo = n;
					xQueueSend(colaRx, &b, portMAX_DELAY);
				}
			}
			socketClient = -1;
			close(sock);
		}
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}

/**
 * @brief Unica tarea consumidora de colaRx: despacha bytes segun provengan de UART0, SERVER o CLIENT.
 * @param pvParam no se usa.
 */
void task_rx(void *pvParam){
	bloque_t b;
	uner_rx_t rxUart;
	char bufClient[256];
	int posClient = 0;
	uner_rx_init(&rxUart);
	while(1){
		xQueueReceive(colaRx, &b, portMAX_DELAY);
		switch(b.medio){
		case MEDIO_UART0:
			for(int i = 0; i < b.largo; i++){
				if(uner_rx_byte(&rxUart, b.datos[i])){
					procesar_uner(MEDIO_UART0, rxUart.carga, rxUart.n);
				}
			}
			break;
		case MEDIO_SERVER:
			for(int i = 0; i < b.largo; i++){
				microrl_insert_char(&rl, b.datos[i]);
			}
			break;
		case MEDIO_CLIENT:
			for(int i = 0; i < b.largo; i++){
				if(posClient < (int)sizeof(bufClient) - 1){
					bufClient[posClient++] = (char)b.datos[i];
				}
			}
			bufClient[posClient] = '\0';
			char *jsonStr = strstr(bufClient, "{\"ACK\"");
			if(jsonStr != NULL && strchr(jsonStr, '}') != NULL){
				cJSON *raiz = cJSON_Parse(jsonStr);
				if(raiz != NULL){
					cJSON *ack = cJSON_GetObjectItem(raiz, "ACK");
					if(cJSON_IsString(ack) && ack->valuestring != NULL){
						strncpy(scadaAck, ack->valuestring, sizeof(scadaAck) - 1);
						scadaAck[sizeof(scadaAck) - 1] = '\0';
					}
					cJSON_Delete(raiz);
				}
				posClient = 0;
			}else if(posClient >= (int)sizeof(bufClient) - 1){
				posClient = 0;
			}
			break;
		}
	}
}

/**
 * @brief Unica tarea consumidora de colaTx: envia cada bloque por UART0, socketServer o socketClient.
 * @param pvParam no se usa.
 */
void task_tx(void *pvParam){
	bloque_t b;
	while(1){
		xQueueReceive(colaTx, &b, portMAX_DELAY);
		switch(b.medio){
		case MEDIO_UART0:
			serie_enviar(UART_NUM_0, b.datos, b.largo);
			break;
		case MEDIO_SERVER:
			if(socketServer >= 0){
				tcp_enviar(socketServer, b.datos, b.largo);
			}
			break;
		case MEDIO_CLIENT:
			if(socketClient >= 0){
				tcp_enviar(socketClient, b.datos, b.largo);
			}
			break;
		}
	}
}

/**
 * @brief Empaqueta una trama UNER y la encola en colaTx hacia el medio indicado.
 * @param medio destino del bloque.
 * @param carga comando en carga[0] seguido de su payload.
 * @param n cantidad de bytes de carga.
 */
void enviar_uner(medio_t medio, const uint8_t *carga, uint8_t n){
	bloque_t b;
	if(n + 7 > BLOQUE_MAX){
		return;
	}
	b.medio = medio;
	b.largo = uner_armar(b.datos, carga, n);
	xQueueSend(colaTx, &b, portMAX_DELAY);
}

/**
 * @brief Envia una cadena de texto por colaTx hacia MEDIO_SERVER o MEDIO_CLIENT fragmentando en bloques.
 * @param medio MEDIO_SERVER o MEDIO_CLIENT.
 * @param texto cadena terminada en '\0'.
 */
void enviar_texto(medio_t medio, const char *texto){
	bloque_t b;
	int len = strlen(texto);
	if((medio == MEDIO_SERVER && socketServer < 0) || (medio == MEDIO_CLIENT && socketClient < 0)){
		return;
	}
	b.medio = medio;
	while(len > 0){
		b.largo = (len > BLOQUE_MAX) ? BLOQUE_MAX : len;
		memcpy(b.datos, texto, b.largo);
		xQueueSend(colaTx, &b, portMAX_DELAY);
		texto += b.largo;
		len -= b.largo;
	}
}

/**
 * @brief Callback de impresion de microrl: envia el texto al cliente de consola en MEDIO_SERVER.
 * @param texto cadena terminada en '\0'.
 */
void enviar_server(const char *texto){
	enviar_texto(MEDIO_SERVER, texto);
}

/**
 * @brief Envia el comando CMD_SET_POWER (0xD1) con las potencias de ambos motores en int32 little-endian.
 * @param pwr1 potencia del motor 1.
 * @param pwr2 potencia del motor 2.
 */
void cmd_setPower(int32_t pwr1, int32_t pwr2){
	uint8_t carga[9];
	carga[0] = CMD_SET_POWER;
	uner_escribir32(&carga[1], (uint32_t)pwr1);
	uner_escribir32(&carga[5], (uint32_t)pwr2);
	enviar_uner(MEDIO_UART0, carga, 9);
}

/**
 * @brief Envia el comando CMD_SET_PEN (0xD2) para subir (0) o bajar (1) el lapiz.
 * @param pen 0 arriba, 1 abajo.
 */
void cmd_setPen(uint8_t pen){
	uint8_t carga[2] = {CMD_SET_PEN, pen};
	enviar_uner(MEDIO_UART0, carga, 2);
}

/**
 * @brief Procesa las tramas UNER recibidas del simulador Qt y actualiza la estructura scara.
 * @param medio canal por el que llego la trama.
 * @param carga bytes de comando y datos.
 * @param n longitud de la carga.
 */
void procesar_uner(medio_t medio, const uint8_t *carga, uint8_t n){
	uint8_t resp[2];
	switch(carga[0]){
	case CMD_ALIVE:
		if(n == 1){
			resp[0] = CMD_ALIVE;
			resp[1] = UNER_ACK;
			enviar_uner(medio, resp, 2);
		}
		break;
	case CMD_GET_STATE:
		if(n == 18){
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			scara.ang[0] = (int32_t)uner_leer32(&carga[1]);
			scara.ang[1] = (int32_t)uner_leer32(&carga[5]);
			scara.pwr[0] = (int32_t)uner_leer32(&carga[9]);
			scara.pwr[1] = (int32_t)uner_leer32(&carga[13]);
			scara.stateArm = carga[17];
			xSemaphoreGive(accesoScara);
			mostrar_topes(carga[17]);
		}
		break;
	case CMD_SET_POWER:
		if(n == 9){
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			scara.pwr[0] = (int32_t)uner_leer32(&carga[1]);
			scara.pwr[1] = (int32_t)uner_leer32(&carga[5]);
			xSemaphoreGive(accesoScara);
		}
		break;
	case CMD_SET_PEN:
		if(n == 2){
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			scara.pen = carga[1];
			xSemaphoreGive(accesoScara);
		}
		break;
	case CMD_LIMIT:
		if(n == 2){
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			scara.stateArm = carga[1];
			xSemaphoreGive(accesoScara);
			if(carga[1] & ARM_LIMIT_SW){
				xEventGroupSetBits(eventos, BIT_PARPADEO);
			}
			mostrar_topes(carga[1]);
			resp[0] = CMD_LIMIT;
			resp[1] = carga[1];
			enviar_uner(medio, resp, 2);
		}
		break;
	default:
		break;
	}
}

/**
 * @brief Actualiza los 4 LEDs fisicos segun los bits de final de carrera de stateArm.
 * @param stateArm bitmap de estado del brazo.
 */
void mostrar_topes(uint8_t stateArm){
	if(xEventGroupGetBits(eventos) & BIT_PARPADEO){
		return;
	}
	salida_set(leds[0], (stateArm & ARM1_L) != 0);
	salida_set(leds[1], (stateArm & ARM1_R) != 0);
	salida_set(leds[2], (stateArm & ARM2_L) != 0);
	salida_set(leds[3], (stateArm & ARM2_R) != 0);
}

/**
 * @brief Ante un evento LIMIT_SW parpadea los 4 LEDs 3 veces (100 ms ON / 100 ms OFF) y restaura los topes.
 * @param pvParam no se usa.
 */
void task_parpadeo(void *pvParam){
	uint8_t stateArm;
	while(1){
		xEventGroupWaitBits(eventos, BIT_PARPADEO, pdFALSE, pdTRUE, portMAX_DELAY);
		for(int vez = 0; vez < 3; vez++){
			for(int i = 0; i < LED_COUNT; i++){
				salida_set(leds[i], 1);
			}
			vTaskDelay(pdMS_TO_TICKS(100));
			for(int i = 0; i < LED_COUNT; i++){
				salida_set(leds[i], 0);
			}
			vTaskDelay(pdMS_TO_TICKS(100));
		}
		xSemaphoreTake(accesoScara, portMAX_DELAY);
		stateArm = scara.stateArm;
		xSemaphoreGive(accesoScara);
		xEventGroupClearBits(eventos, BIT_PARPADEO);
		mostrar_topes(stateArm);
	}
}

/**
 * @brief Calcula la cinematica inversa para llevar el lapiz a (x, y) y actualiza scara.obj.
 * @param x coordenada x destino.
 * @param y coordenada y destino.
 * @param avance true para calcular feedforward de velocidad mientras traza.
 * @return true si el punto es alcanzable dentro de 0..180 grados en cada brazo.
 */
bool mover(float x, float y, bool avance){
	float c = (x * x + y * y - L1 * L1 - L2 * L2) / (2.0f * L1 * L2);
	float q1, q2, grados[2];
	int32_t cuentas;
	if(c > 1.0f || c < -1.0f){
		return false;
	}
	q2 = -acosf(c);
	q1 = atan2f(y, x) - atan2f(L2 * sinf(q2), L1 + L2 * cosf(q2));
	grados[0] = q1 * 180.0f / M_PI;
	grados[1] = 90.0f - q2 * 180.0f / M_PI;
	if(grados[0] < 0 || grados[0] > 180 || grados[1] < 0 || grados[1] > 180){
		return false;
	}
	xSemaphoreTake(accesoScara, portMAX_DELAY);
	for(int i = 0; i < 2; i++){
		cuentas = lroundf(CUENTAS_0 + grados[i] * (CUENTAS_180 - CUENTAS_0) / 180.0f);
		scara.avance[i] = 0;
		scara.pwrMax[i] = PWR_MAX;
		if(avance){
			scara.avance[i] = (cuentas - scara.obj[i]) * (1000.0f / PERIODO_CONTROL_MS) / CUENTAS_POR_PWR;
		}
		scara.obj[i] = cuentas;
	}
	xSemaphoreGive(accesoScara);
	return true;
}

/**
 * @brief Lazo cerrado de control cada PERIODO_CONTROL_MS: pide estado al Qt, filtra y manda potencias.
 * @param pvParam no se usa.
 */
void task_control(void *pvParam){
	uint8_t cmdAlive = CMD_ALIVE, cmdGet = CMD_GET_STATE;
	scara_t copia;
	int32_t historia[2][RETRASO], ref, error, pwr[2];
	int indice = 0;
	float filtrado[2] = {0, 0}, p;
	bool quieto;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	enviar_uner(MEDIO_UART0, &cmdAlive, 1);
	while(1){
		enviar_uner(MEDIO_UART0, &cmdGet, 1);
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PERIODO_CONTROL_MS));
		xSemaphoreTake(accesoScara, portMAX_DELAY);
		copia = scara;
		xSemaphoreGive(accesoScara);
		if(copia.ang[0] == 0 && copia.ang[1] == 0){
			continue;
		}
		quieto = copia.avance[0] == 0 && copia.avance[1] == 0;
		for(int i = 0; i < 2; i++){
			if(filtrado[i] == 0){
				filtrado[i] = copia.ang[i];
				for(int k = 0; k < RETRASO; k++){
					historia[i][k] = copia.obj[i];
				}
			}
			filtrado[i] += FILTRO * (copia.ang[i] - filtrado[i]);
			ref = historia[i][indice];
			historia[i][indice] = copia.obj[i];
			if(quieto){
				ref = copia.obj[i];
			}
			error = ref - lroundf(filtrado[i]);
			p = copia.avance[i] + KP * error;
			if(quieto && abs(error) <= BANDA){
				p = 0;
			}
			pwr[i] = lroundf(fmaxf(-copia.pwrMax[i], fminf(copia.pwrMax[i], p)));
		}
		indice = (indice + 1) % RETRASO;
		pwr[1] = -pwr[1];
		if(((copia.stateArm & ARM1_L) && pwr[0] < 0) || ((copia.stateArm & ARM1_R) && pwr[0] > 0)){
			pwr[0] = 0;
		}
		if(((copia.stateArm & ARM2_R) && pwr[1] > 0) || ((copia.stateArm & ARM2_L) && pwr[1] < 0)){
			pwr[1] = 0;
		}
		cmd_setPower(pwr[0], pwr[1]);
		if(pwr[0] == 0 && pwr[1] == 0){
			xEventGroupSetBits(eventos, BIT_EN_POSICION);
		}else{
			xEventGroupClearBits(eventos, BIT_EN_POSICION);
		}
	}
}

/**
 * @brief Interpola un segmento recto con perfil cosenoidal hasta (x, y) y espera a que el brazo llegue.
 * @param x coordenada x final.
 * @param y coordenada y final.
 * @param velocidad velocidad media en unidades por segundo.
 */
void mover_recto(float x, float y, float velocidad){
	static float xa = 0, ya = 0;
	static bool primera = true;
	float dx = x - xa, dy = y - ya;
	float xb = xa, yb = ya, xn, yn, s;
	int pasos = sqrtf(dx * dx + dy * dy) / (velocidad * PERIODO_CONTROL_MS / 1000.0f);
	TickType_t xLastWakeTime = xTaskGetTickCount();
	if(primera){
		pasos = 0;
		xb = x;
		yb = y;
		primera = false;
	}
	for(int k = 1; k <= pasos; k++){
		if(xEventGroupGetBits(eventos) & BIT_STOP){
			break;
		}
		s = (1 - cosf(M_PI * k / pasos)) / 2;
		xn = xa + dx * s;
		yn = ya + dy * s;
		if(!mover(xn, yn, true)){
			break;
		}
		xb = xn;
		yb = yn;
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PERIODO_CONTROL_MS));
	}
	if(!(xEventGroupGetBits(eventos) & BIT_STOP)){
		mover(xb, yb, false);
		xEventGroupClearBits(eventos, BIT_EN_POSICION);
		vTaskDelay(pdMS_TO_TICKS(2 * PERIODO_CONTROL_MS));
		xEventGroupWaitBits(eventos, BIT_EN_POSICION, pdFALSE, pdTRUE, pdMS_TO_TICKS(8000));
		vTaskDelay(pdMS_TO_TICKS(200));
	}
	xa = xb;
	ya = yb;
}

/**
 * @brief Tarea del modo B: espera un numero de 3 cifras en colaNumero, lo dibuja en 7 segmentos y estaciona el brazo.
 * @param pvParam no se usa.
 */
void task_modoB(void *pvParam){
	static const uint8_t tabla7seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
	const float sx1[7] = {0, 20.0f, 20.0f, 20.0f, 0, 0, 0};
	const float sy1[7] = {0, 0, 20.0f, 40.0f, 40.0f, 20.0f, 20.0f};
	const float sx2[7] = {20.0f, 20.0f, 20.0f, 0, 0, 0, 20.0f};
	const float sy2[7] = {0, 20.0f, 40.0f, 40.0f, 20.0f, 0, 20.0f};
	uint8_t cmdClr = CMD_CLEAR;
	int num, digitos[3];
	float x0, px, py, x1, y1, x2, y2;
	while(1){
		xQueueReceive(colaNumero, &num, portMAX_DELAY);
		xEventGroupClearBits(eventos, BIT_MODO_A | BIT_STOP);
		cmd_setPen(0);
		enviar_uner(MEDIO_UART0, &cmdClr, 1);
		vTaskDelay(pdMS_TO_TICKS(100));
		digitos[0] = (num / 100) % 10;
		digitos[1] = (num / 10) % 10;
		digitos[2] = num % 10;
		for(int i = 0; i < 3; i++){
			x0 = -40.0f + i * 30.0f;
			px = -1000.0f;
			py = -1000.0f;
			for(int s = 0; s < 7; s++){
				if(xEventGroupGetBits(eventos) & (BIT_MODO_A | BIT_STOP)){
					break;
				}
				if(tabla7seg[digitos[i]] & (1 << s)){
					x1 = x0 + sx1[s];
					y1 = 280.0f + sy1[s];
					x2 = x0 + sx2[s];
					y2 = 280.0f + sy2[s];
					if(x1 != px || y1 != py){
						cmd_setPen(0);
						vTaskDelay(pdMS_TO_TICKS(100));
						mover_recto(x1, y1, VELOCIDAD_VIAJE);
						if(xEventGroupGetBits(eventos) & (BIT_MODO_A | BIT_STOP)){
							break;
						}
						cmd_setPen(1);
						vTaskDelay(pdMS_TO_TICKS(100));
					}
					mover_recto(x2, y2, VELOCIDAD);
					px = x2;
					py = y2;
				}
			}
			cmd_setPen(0);
			vTaskDelay(pdMS_TO_TICKS(100));
		}
		if(!(xEventGroupGetBits(eventos) & (BIT_MODO_A | BIT_STOP))){
			mover_recto(140.0f, 260.0f, VELOCIDAD_VIAJE);
		}
	}
}

/**
 * @brief Tarea de telemetria: cada PERIODO_TELEMETRIA_MS envia un POST HTTP con JSON al servidor SCADA.
 * @param pvParam no se usa.
 */
void task_telemetria(void *pvParam){
	char body[160], post[320];
	int32_t a0, a1, p0, p1;
	uint8_t st;
	const char *modo;
	TickType_t xLastWakeTime = xTaskGetTickCount();
	while(1){
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PERIODO_TELEMETRIA_MS));
		if(socketClient >= 0){
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			a0 = scara.ang[0];
			a1 = scara.ang[1];
			p0 = scara.pwr[0];
			p1 = scara.pwr[1];
			st = scara.stateArm;
			xSemaphoreGive(accesoScara);
			modo = (xEventGroupGetBits(eventos) & BIT_MODO_A) ? "A" : "B";
			int nBytes = snprintf(body, sizeof(body),
				"{\"NAME\":\"%s\",\"ADC1\":%ld,\"ADC2\":%ld,\"PWR1\":%ld,\"PWR2\":%ld,\"MODO\":\"%s\",\"STATE\":%u}",
				NOMBRE_ALUMNO, (long)a0, (long)a1, (long)p0, (long)p1, modo, (unsigned)st);
			snprintf(post, sizeof(post),
				"POST /scara/telemetry HTTP/1.1\r\n"
				"Host: %s:%u\r\n"
				"Content-Type: application/json\r\n"
				"Content-Length: %d\r\n\r\n"
				"%s",
				scadaIp, scadaPuerto, nBytes, body);
			enviar_texto(MEDIO_CLIENT, post);
		}
	}
}

/**
 * @brief Responde un pedido HTTP GET (/ o /estado) recibido en el servidor TCP del puerto 30000.
 * @param sock socket del cliente HTTP.
 * @param req primera linea del pedido HTTP.
 */
void responder_http(int sock, const char *req){
	char cab[160], buf[256];
	if(strncmp(req, "GET /estado", 11) == 0){
		xSemaphoreTake(accesoScara, portMAX_DELAY);
		int32_t a0 = scara.ang[0], a1 = scara.ang[1];
		int32_t p0 = scara.pwr[0], p1 = scara.pwr[1];
		uint8_t st = scara.stateArm;
		xSemaphoreGive(accesoScara);
		float g0 = (a0 - CUENTAS_0) * 180.0f / (CUENTAS_180 - CUENTAS_0);
		float g1 = (a1 - CUENTAS_0) * 180.0f / (CUENTAS_180 - CUENTAS_0);
		const char *modo = (xEventGroupGetBits(eventos) & BIT_MODO_A) ? "A" : "B";
		uint32_t ip = wifi_ip_sta();
		int len = snprintf(buf, sizeof(buf),
			"{\"ang1\":%.1f,\"ang2\":%.1f,\"adc1\":%ld,\"adc2\":%ld,\"pwr1\":%ld,\"pwr2\":%ld,\"state\":%u,\"modo\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%u.%u.%u.%u\"}",
			g0, g1, (long)a0, (long)a1, (long)p0, (long)p1, (unsigned)st, modo, staSsid,
			(unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
		int n = snprintf(cab, sizeof(cab),
			"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
			len);
		tcp_enviar(sock, cab, n);
		tcp_enviar(sock, buf, len);
	}else{
		FILE *f = fopen("/spiffs/index.html", "r");
		if(f != NULL){
			fseek(f, 0, SEEK_END);
			long tam = ftell(f);
			fseek(f, 0, SEEK_SET);
			int n = snprintf(cab, sizeof(cab),
				"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
				tam);
			tcp_enviar(sock, cab, n);
			size_t leidos;
			while((leidos = fread(buf, 1, sizeof(buf), f)) > 0){
				tcp_enviar(sock, buf, leidos);
			}
			fclose(f);
		}
	}
}

/**
 * @brief Ejecuta los comandos de consola ingresados por microrl (STATE, MOVE, NUMERO, SETMODE, WIFI, SCADA, STOP).
 * @param argc cantidad de tokens en la linea.
 * @param argv arreglo de tokens.
 * @return 0 siempre.
 */
int execute(int argc, const char *const *argv){
	char resp[128];
	uint8_t cmdClr = CMD_CLEAR;
	if(strcasecmp(argv[0], "STATE") == 0){
		xSemaphoreTake(accesoScara, portMAX_DELAY);
		int32_t p0 = scara.pwr[0], p1 = scara.pwr[1];
		int32_t a0 = scara.ang[0], a1 = scara.ang[1];
		uint8_t st = scara.stateArm;
		xSemaphoreGive(accesoScara);
		float g0 = (a0 - CUENTAS_0) * 180.0f / (CUENTAS_180 - CUENTAS_0);
		float g1 = (a1 - CUENTAS_0) * 180.0f / (CUENTAS_180 - CUENTAS_0);
		snprintf(resp, sizeof(resp), "%ld, %ld\r\n%.1f, %.1f (%ld, %ld)\r\n%u\r\n", (long)p0, (long)p1, g0, g1, (long)a0, (long)a1, (unsigned)st);
		enviar_server(resp);
	}else if(strcasecmp(argv[0], "MOVE") == 0){
		if(argc >= 5){
			int32_t p1 = abs(atoi(argv[1])), p2 = abs(atoi(argv[3]));
			float a1 = atof(argv[2]), a2 = atof(argv[4]);
			p1 = (p1 < 1) ? 1 : ((p1 > PWR_MAX) ? PWR_MAX : p1);
			p2 = (p2 < 1) ? 1 : ((p2 > PWR_MAX) ? PWR_MAX : p2);
			int32_t c1 = (a1 > 180.0f) ? (int32_t)a1 : lroundf(CUENTAS_0 + a1 * (CUENTAS_180 - CUENTAS_0) / 180.0f);
			int32_t c2 = (a2 > 180.0f) ? (int32_t)a2 : lroundf(CUENTAS_0 + a2 * (CUENTAS_180 - CUENTAS_0) / 180.0f);
			c1 = (int32_t)fmaxf(CUENTAS_0, fminf(CUENTAS_180, c1));
			c2 = (int32_t)fmaxf(CUENTAS_0, fminf(CUENTAS_180, c2));
			xEventGroupClearBits(eventos, BIT_STOP);
			xSemaphoreTake(accesoScara, portMAX_DELAY);
			scara.pwrMax[0] = p1;
			scara.pwrMax[1] = p2;
			scara.avance[0] = 0;
			scara.avance[1] = 0;
			scara.obj[0] = c1;
			scara.obj[1] = c2;
			xSemaphoreGive(accesoScara);
			snprintf(resp, sizeof(resp), "%s\r\n%s\r\n", argv[2], argv[4]);
			enviar_server(resp);
		}else{
			enviar_server("ERROR: uso: MOVE pwr1 ang1 pwr2 ang2\r\n");
		}
	}else if(strcasecmp(argv[0], "STOP") == 0){
		xEventGroupSetBits(eventos, BIT_STOP);
		xQueueReset(colaNumero);
		xSemaphoreTake(accesoScara, portMAX_DELAY);
		scara.obj[0] = scara.ang[0];
		scara.obj[1] = scara.ang[1];
		scara.avance[0] = 0;
		scara.avance[1] = 0;
		scara.pwr[0] = 0;
		scara.pwr[1] = 0;
		xSemaphoreGive(accesoScara);
		cmd_setPen(0);
		cmd_setPower(0, 0);
		enviar_server("STOP OK\r\n");
	}else if(strcasecmp(argv[0], "SETMODE") == 0){
		if(argc == 1){
			snprintf(resp, sizeof(resp), "%s\r\n", (xEventGroupGetBits(eventos) & BIT_MODO_A) ? "A" : "B");
			enviar_server(resp);
		}else if(argc >= 2){
			if(argv[1][0] == 'A' || argv[1][0] == 'a'){
				xEventGroupSetBits(eventos, BIT_MODO_A | BIT_STOP);
				xQueueReset(colaNumero);
				cmd_setPen(0);
				enviar_server("A\r\n");
			}else if(argv[1][0] == 'B' || argv[1][0] == 'b'){
				xEventGroupClearBits(eventos, BIT_MODO_A | BIT_STOP);
				enviar_server("B\r\n");
			}else{
				enviar_server("ERROR: modo A o B\r\n");
			}
		}
	}else if(strcasecmp(argv[0], "WIFI") == 0){
		if(argc == 1){
			uint32_t ip = wifi_ip_sta();
			snprintf(resp, sizeof(resp), "%s (%s) IP:%u.%u.%u.%u\r\n", ip != 0 ? "CONECTADO" : "DESCONECTADO", staSsid, (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
			enviar_server(resp);
		}else if(argc >= 3){
			strncpy(staSsid, argv[1], sizeof(staSsid) - 1);
			staSsid[sizeof(staSsid) - 1] = '\0';
			strncpy(staPass, argv[2], sizeof(staPass) - 1);
			staPass[sizeof(staPass) - 1] = '\0';
			memoria_set_str("sta_ssid", staSsid);
			memoria_set_str("sta_pass", staPass);
			wifi_sta_conectar(staSsid, staPass);
			snprintf(resp, sizeof(resp), "%s\r\n%s\r\n", staSsid, staPass);
			enviar_server(resp);
		}else{
			enviar_server("ERROR: uso: WIFI [SSID PASS]\r\n");
		}
	}else if(strcasecmp(argv[0], "SCADA") == 0){
		if(argc == 1){
			snprintf(resp, sizeof(resp), "%s (%s:%u) ACK:%s\r\n", socketClient >= 0 ? "CONECTADO" : "DESCONECTADO", scadaIp, scadaPuerto, scadaAck);
			enviar_server(resp);
		}else if(argc >= 3){
			strncpy(scadaIp, argv[1], sizeof(scadaIp) - 1);
			scadaIp[sizeof(scadaIp) - 1] = '\0';
			scadaPuerto = atoi(argv[2]);
			memoria_set_str("scada_ip", scadaIp);
			memoria_set_u16("scada_port", scadaPuerto);
			if(socketClient >= 0){
				int s = socketClient;
				socketClient = -1;
				close(s);
			}
			snprintf(resp, sizeof(resp), "%s\r\n%u\r\n", scadaIp, scadaPuerto);
			enviar_server(resp);
		}else{
			enviar_server("ERROR: uso: SCADA [IP PORT]\r\n");
		}
	}else if(strcasecmp(argv[0], "NUMERO") == 0){
		if(argc >= 2){
			int num = atoi(argv[1]);
			if(num >= 0 && num <= 999){
				xEventGroupClearBits(eventos, BIT_MODO_A | BIT_STOP);
				xQueueSend(colaNumero, &num, 0);
				snprintf(resp, sizeof(resp), "%03d\r\n", num);
				enviar_server(resp);
			}else{
				enviar_server("ERROR: numero entre 0 y 999\r\n");
			}
		}else{
			enviar_server("ERROR: uso: NUMERO <0..999>\r\n");
		}
	}else if(strcasecmp(argv[0], "PEN") == 0){
		if(argc >= 2){
			uint8_t pen = atoi(argv[1]) ? 1 : 0;
			cmd_setPen(pen);
			snprintf(resp, sizeof(resp), "%u\r\n", pen);
			enviar_server(resp);
		}
	}else if(strcasecmp(argv[0], "CLEAR") == 0){
		enviar_uner(MEDIO_UART0, &cmdClr, 1);
		enviar_server("CLEAR OK\r\n");
	}else{
		enviar_server("Comando desconocido\r\n");
	}
	return 0;
}
