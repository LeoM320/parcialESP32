/* main.c: tu programa.
 *
 * Las librerias solo ponen en marcha cosas (GPIO, ADC, NVS, WiFi, servidor web,
 * sockets TCP, microrl). Todo lo demas esta aca, a la vista:
 *
 *   - task_tcp:    atiende un cliente TCP (MobaXterm, Telnet) con microrl.
 *   - execute():   los comandos que llegan por TCP.
 *   - handlers web: lo que hace cada direccion de la pagina (/wifi).
 */

/* ======================= Includes ======================= */

#include <stdint.h>
#include <stdio.h>				// snprintf()
#include <stdlib.h>				// atoi()
#include <string.h>				// strcmp(), strlen()
#include <unistd.h>				// close()
#include "freertos/FreeRTOS.h"	// siempre primero de los de FreeRTOS
#include "freertos/FreeRTOSConfig_arch.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"		// xTaskCreate(), vTaskDelete()
#include "hardware.h"			// pines: leds[], switches[], pots[], LED_COUNT...
#include "iodigital.h"			// salida_init/set/get, entrada_init/get
#include "ianalogica.h"			// analogica_init/get
#include "memoria.h"			// NVS: memoria_init, memoria_get/set_str
#include "portmacro.h"
#include "wifi.h"				// wifi_init, wifi_sta_conectar
#include "web.h"				// web_init, web_parametro, web_leer_cuerpo, web_campo
#include "tcp.h"				// tcp_servidor, tcp_aceptar, tcp_recibir, tcp_enviar
#include "telnet.h"				// telnet_inicio, telnet_filtrar
#include "microrl.h"			// la linea de comandos del profe

/* ======================= Constantes ======================= */

#define PUERTO_TCP		1234		// puerto de la consola (MobaXterm -> Telnet -> IP:1234)
#define TCP_ESPERA_MS	1000		// cada cuanto vuelven tcp_aceptar() y tcp_recibir() sin datos

#define FLAG_SW1 (1 << 0)
#define FLAG_SW2 (1 << 1)
#define FLAG_SW3 (1 << 2)
#define FLAG_SW4 (1 << 3)

/* ======================= Variables globales ======================= */

microrl_t rl;					// estado de microrl: la linea que se esta escribiendo, historial
int sockTcp = -1;				// cliente TCP conectado, -1 = ninguno (lo usa enviar())

/* ======================= Prototipos ======================= */

void init_hardware(void);							// configura LEDs, switches y potes
void task_tcp(void *pvParam);						// tarea de la consola TCP
void enviar(const char *texto);						// manda texto al cliente TCP
int execute(int argc, const char *const *argv);		// tus comandos (la llama microrl)
esp_err_t wifi_post(httpd_req_t *req);				// handler de POST /wifi

/* Direcciones de la pagina que atiende tu codigo. El resto (/, /wifi.html...)
 * son archivos de la carpeta www/. */
const httpd_uri_t uris[] = {
	{ .uri = "/wifi",   .method = HTTP_POST, .handler = wifi_post },	// formulario de wifi.html
};

/* ======================= Arranque ======================= */

//Voy a tener un task_switches y un task_leds

typedef struct {
	uint8_t sw0;
	uint8_t sw1;
	uint8_t sw2;
	uint8_t sw3;
}pulsadores_t;

typedef enum {
	PANEO_PRIMERO,
	PANEO_SEGUNDO,
	PANEO_TERCERO,
	PANEO_CUARTO,
	PANEO_CANT
}paneo_t;

typedef struct {
	uint32_t pot1, pot2;
}valorAdc_t;

valorAdc_t valorAdc = {0};

paneo_t paneo = PANEO_PRIMERO;

const char *nPaneo_t[PANEO_CANT] = {"1do", "2do", "3ro", "4to"};

QueueHandle_t colaLeds;

EventGroupHandle_t prender;

SemaphoreHandle_t accesoAdc;

//prototipos
void task_switches(void *pvParam);
void task_leds(void *pvParam);

void task_paneo(void *pvParam);

void task_banderita(void *pvParam);

void task_interruptores(void *pvParam);

void task_leerAdc(void *pvParam);

void task_printAdc(void *pvParam);

void task_callSoftTimer(TimerHandle_t xTimer);

void task_callTaskNotification(void *pvParam);

TaskHandle_t llamada = NULL;

void IRAM_ATTR isr_pulsadores(void *arg);

void app_main(void)
{
	/* Red guardada en NVS; si nunca se guardo, la de menuconfig */
	char ssid[33] = CONFIG_ESP_WIFI_SSID;		// 32 caracteres max + '\0'
	char pass[65] = CONFIG_ESP_WIFI_PASSWORD;	// 64 caracteres max + '\0'

	init_hardware();							// pines antes que nada

	memoria_init();								// NVS: tiene que ir antes que wifi_init
	memoria_get_str("ssid", ssid, sizeof(ssid));	// si la clave no existe, ssid no cambia
	memoria_get_str("pass", pass, sizeof(pass));
	wifi_init(CONFIG_ESP_AP_SSID, CONFIG_ESP_AP_PASSWORD, ssid, pass);	// AP propio + conexion al router

	web_init(uris, sizeof(uris) / sizeof(uris[0]));	// servidor HTTP: tus handlers + archivos de www/
													// sizeof/sizeof = cantidad de elementos de uris[]
	colaLeds = xQueueCreate(1,sizeof(pulsadores_t));
	prender = xEventGroupCreate();
	
	accesoAdc = xSemaphoreCreateMutex();
	
	TimerHandle_t despertar = NULL;
	
	xTaskCreate(task_tcp,
				"task tcp",
				4096,			// microrl + snprintf: el minimo no alcanza
				NULL,
				tskIDLE_PRIORITY + 2,
				NULL);
	// app_main termina aca; las tareas, el WiFi y el servidor web siguen corriendo
	
	/*xTaskCreate(task_switches,
				"task switches",
				configMINIMAL_STACK_SIZE,
				NULL,
				tskIDLE_PRIORITY+3,
				NULL	
			);
			
	xTaskCreate(task_leds,
					"task leds",
					configMINIMAL_STACK_SIZE,
					NULL,
					tskIDLE_PRIORITY+3,
					NULL	
				);*/
	
	/*xTaskCreate(task_paneo,
	"task paneo",
configMINIMAL_STACK_SIZE,
NULL,
tskIDLE_PRIORITY+3,
NULL);*/

	/*xTaskCreate(task_banderita
	,"task banderita",
	configMINIMAL_STACK_SIZE,
	NULL,
	tskIDLE_PRIORITY+3,
	NULL);
	
	xTaskCreate(task_interruptores, 
		"task interruptores", 
		configMINIMAL_STACK_SIZE, 
		NULL, 
		tskIDLE_PRIORITY+3, 
		NULL);*/
		
	//xTaskCreate(task_leerAdc, "leer adc", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+3, NULL);
	//xTaskCreate(task_printAdc, "print adc", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+3, NULL);
	xTaskCreate(task_callTaskNotification, "llamada", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+3, &llamada);
	despertar = xTimerCreate("despertar", pdMS_TO_TICKS(500), pdTRUE, NULL, task_callSoftTimer);
	xTimerStart(despertar, 0);
}

void task_callSoftTimer(TimerHandle_t xTimer){
	printf("MundanoTemporizador\r\n");
	xTaskNotifyGive(llamada);
}

void task_callTaskNotification(void *pvParam){
	while(1){
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);     // duerme hasta que el timer avise
		printf("Tarea: me avisaron\r\n");
	}
}

void task_leerAdc(void *pvParam){
	uint32_t pot1, pot2;
	while(1){
		//Cada tanto obtener acceso a la estructura y actualizarla
		pot1 = analogica_get(POT1_GPIO);
		pot2 = analogica_get(POT2_GPIO);
		xSemaphoreTake(accesoAdc, portMAX_DELAY);
		valorAdc.pot1 = pot1;
		valorAdc.pot2 = pot2;
		xSemaphoreGive(accesoAdc);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void task_printAdc(void *pvParam){
	TickType_t xLastWakeTime = xTaskGetTickCount();
	uint32_t pot1, pot2;
	while(1){
		//Cada tanto obtener acceso a la estructura y actualizarla
		xSemaphoreTake(accesoAdc, portMAX_DELAY);
		pot1 = valorAdc.pot1;
		pot2 = valorAdc.pot2;
		xSemaphoreGive(accesoAdc);
		printf("Pot1:%04lu Pot2:%04lu\r\n",(unsigned long)pot1,(unsigned long)pot2);
		xTaskDelayUntil(&xLastWakeTime,pdMS_TO_TICKS(200));
	}
}

//Implementacion
void task_switches(void *pvParam){
	pulsadores_t pulsadores, pulsadoresAnterior;
	while(1){
		pulsadores.sw0 = !entrada_get(SW1_GPIO);
		pulsadores.sw1 = !entrada_get(SW2_GPIO);
		pulsadores.sw2 = !entrada_get(SW3_GPIO);
		pulsadores.sw3 = !entrada_get(SW4_GPIO);
		
		if(pulsadores.sw0 != pulsadoresAnterior.sw0 ||
		   pulsadores.sw1 != pulsadoresAnterior.sw1 ||
		   pulsadores.sw2 != pulsadoresAnterior.sw2 ||
		   pulsadores.sw3 != pulsadoresAnterior.sw3){
			xQueueSend(colaLeds, &pulsadores, 0);
			pulsadoresAnterior.sw0 = pulsadores.sw0;
			pulsadoresAnterior.sw1 = pulsadores.sw1;
			pulsadoresAnterior.sw2 = pulsadores.sw2;
			pulsadoresAnterior.sw3 = pulsadores.sw3;
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void task_leds(void *pvParam){
	pulsadores_t pulsadores;
	while(1){
		xQueueReceive(colaLeds, &pulsadores, portMAX_DELAY);
		
		salida_set(LED1_GPIO,pulsadores.sw0);
		salida_set(LED2_GPIO,pulsadores.sw1);
		salida_set(LED3_GPIO,pulsadores.sw2);
		salida_set(LED4_GPIO,pulsadores.sw3);
		
		printf("Switches %d %d %d %d\n", pulsadores.sw0, pulsadores.sw1, pulsadores.sw2, pulsadores.sw3);
	}
}

void task_paneo(void *pvParam){
	while(1){
		paneo_t paneo_safe = paneo;
		switch (paneo_safe) {
			case PANEO_PRIMERO:
				salida_set(LED1_GPIO, 1);
				salida_set(LED2_GPIO, 0);
				salida_set(LED3_GPIO, 0);
				salida_set(LED4_GPIO, 0);
			break;
			case PANEO_SEGUNDO:
				salida_set(LED1_GPIO, 0);
				salida_set(LED2_GPIO, 1);
				salida_set(LED3_GPIO, 0);
				salida_set(LED4_GPIO, 0);
			break;
			case PANEO_TERCERO:
				salida_set(LED1_GPIO, 0);
				salida_set(LED2_GPIO, 0);
				salida_set(LED3_GPIO, 1);
				salida_set(LED4_GPIO, 0);
			break;
			case PANEO_CUARTO:
				salida_set(LED1_GPIO, 0);
				salida_set(LED2_GPIO, 0);
				salida_set(LED3_GPIO, 0);
				salida_set(LED4_GPIO, 1);
			break;
			case PANEO_CANT:
			break;
			default:
			break;
		}
		printf("%s", nPaneo_t[paneo]);
		paneo = (paneo + 1) % PANEO_CANT;
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

void task_banderita(void *pvParam){
	uint8_t contador=0;
	while(1){
		xEventGroupWaitBits(prender, FLAG_SW1 | FLAG_SW2 | FLAG_SW3 | FLAG_SW4, pdTRUE, pdFALSE, portMAX_DELAY);
		printf("Cambio %d \r\n", contador++);
	}
}

void task_interruptores(void *pvParam){
	TickType_t xLastWakeTime = xTaskGetTickCount();
	uint8_t swPasado=0;
	while(1){
		//Aca lo que quiero es leer y establecer las banderas de los interruptores
		uint8_t swActual = 0;
		swActual |= !entrada_get(SW1_GPIO)<<0;
		swActual |= !entrada_get(SW2_GPIO)<<1;
		swActual |= !entrada_get(SW3_GPIO)<<2;
		swActual |= !entrada_get(SW4_GPIO)<<3;
		if(swPasado!=swActual){
			xEventGroupSetBits(prender, swActual);
			swPasado = swActual;
		}
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
	}
}

void init_hardware(void)
{
	for (int i = 0; i < LED_COUNT; i++) {
		salida_init(leds[i]);							// LED como salida, arranca apagado
	}
	for (int i = 0; i < SW_COUNT; i++) {
		entrada_init(switches[i], GPIO_PULLUP_ONLY);	// apretado = 0
	}
	for (int i = 0; i < POT_COUNT; i++) {
		analogica_init(pots[i]);						// canal del ADC1 de ese pin
	}
}

/* ======================= Comandos (TCP + microrl) ======================= */

/* Microrl la llama al apretar Enter, con la linea ya separada en palabras:
 * argv[0] es el comando, argc cuantas palabras hay. */
int execute(int argc, const char *const *argv)
{
	char respuesta[64];			// para armar textos con numeros (snprintf)

	/* Tus comandos van aca, antes del mensaje de error:
	 *   if (strcmp(argv[0], "xxx") == 0) {		// strcmp devuelve 0 si son iguales
	 *       ...
	 *       return 0;
	 *   }
	 */

	snprintf(respuesta, sizeof(respuesta), "ERROR: comando desconocido: %s\r\n", argv[0]);
	enviar(respuesta);
	return 0;					// microrl pide que devuelva int
}

/* Manda texto al cliente TCP (si hay uno). Lo usa microrl para el eco y el
 * prompt, y vos para responder. */
void enviar(const char *texto)
{
	if (sockTcp >= 0) {								// hay alguien conectado
		tcp_enviar(sockTcp, texto, strlen(texto));	// strlen: largo sin el '\0'
	}
}

void task_tcp(void *pvParam)
{
	char buf[64];				// aca deja tcp_recibir los bytes que llegan
	telnet_t telnet;			// estado del filtro Telnet (un comando puede llegar partido)

	int servidor = tcp_servidor(PUERTO_TCP, TCP_ESPERA_MS);	// abre el puerto y escucha
	if (servidor < 0) {
		vTaskDelete(NULL);		// no se pudo abrir: la tarea se borra (nunca return)
	}

	while (1) {											// atiende un cliente tras otro
		int sock = tcp_aceptar(servidor);			// -1 = nadie se conecto en TCP_ESPERA_MS
		if (sock < 0) {
			continue;								// vuelve a esperar
		}

		/* Se conecto un cliente: preparar la consola */
		sockTcp = sock;								// desde ahora enviar() le manda a el
		telnet_inicio(&telnet, sock);				// MobaXterm: el eco lo hace microrl
		microrl_init(&rl, enviar);					// microrl escribe con enviar()
		microrl_set_execute_callback(&rl, execute);	// al Enter llama a execute()
		enviar("\r\nConsola ESP32\r\n");			// mensaje de bienvenida
		microrl_insert_char(&rl, KEY_CR);			// muestra el prompt

		/* Mientras siga conectado: lo que llega va a microrl */
		while (1) {
			int n = tcp_recibir(sock, buf, sizeof(buf));	// 0 = vencio la espera
			if (n < 0) {
				break;								// se desconecto
			}
			n = telnet_filtrar(&telnet, buf, n);	// saca los comandos Telnet
			for (int i = 0; i < n; i++) {
				microrl_insert_char(&rl, buf[i]);	// eco, edicion; al Enter llama a execute
			}
		}

		/* Se fue el cliente: liberar y esperar otro */
		sockTcp = -1;								// enviar() ya no manda nada
		close(sock);								// cierra el socket de ese cliente
	}
}

/* ======================= Pagina web ======================= */

/* POST /wifi (formulario de wifi.html): guarda la red en NVS y se conecta */
esp_err_t wifi_post(httpd_req_t *req)
{
	char cuerpo[200], ssid[33], pass[65];

	/* El cuerpo llega como "ssid=MiRed&pass=clave": se lee y se separa en campos */
	if (web_leer_cuerpo(req, cuerpo, sizeof(cuerpo)) < 0 ||
		!web_campo(cuerpo, "ssid", ssid, sizeof(ssid)) ||
		!web_campo(cuerpo, "pass", pass, sizeof(pass))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Datos invalidos");
	}
	memoria_set_str("ssid", ssid);		// queda guardado para el proximo arranque
	memoria_set_str("pass", pass);
	wifi_sta_conectar(ssid, pass);		// se conecta ahora, sin reiniciar
	return httpd_resp_sendstr(req, "Guardado. Conectando... (la IP nueva sale en el monitor)");
}
