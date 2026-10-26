/* ejemploC.c: herramientas de C que se usan todo el tiempo (para estudiar).
 *
 * Para probarlo: copiarlo sobre main/main.c, flashear y mirar el monitor.
 * Cada ejemplo_xxx() imprime lo que hace; leer el codigo al lado de la salida.
 *
 *   ejemplo_tipos()       tamanios, rangos, desborde, division entera, float, char, bool, enum
 *   ejemplo_casts()       de un tipo a otro: redondeo, ADC a mV y %, recortes, punto fijo, hexa
 *   ejemplo_cadenas()     copiar, concatenar, comparar, buscar, armar textos
 *   ejemplo_convertir()   texto <-> numero (atoi, strtol, strtof, sscanf, snprintf)
 *   ejemplo_separar()     partir un texto en palabras (strtok_r)
 *   ejemplo_memoria()     memset, memcpy, memmove, memcmp, copiar structs
 *   ejemplo_dinamica()    malloc, calloc, realloc, free, strdup (y ver que no pierde memoria)
 *   ejemplo_punteros()    punteros, arreglos, structs, punteros a funcion, static
 *   ejemplo_utilidades()  macros, min/max/promedio, qsort, math.h, aleatorios, medir tiempo
 *   ejemplo_bits()        operadores bitwise, mascaras, campos, flags, trucos, bitfields
 */

/* ======================= Includes ======================= */

#include <stdint.h>				// uint8_t, uint16_t, uint32_t
#include <stdbool.h>			// bool, true, false
#include <inttypes.h>			// PRIu32, PRIX32 (printf de uint32_t)
#include <stdio.h>				// printf(), snprintf(), sscanf()
#include <stdlib.h>				// atoi(), strtol(), strtof(), malloc(), free()
#include <string.h>				// strlen(), strcpy(), strcmp(), memcpy()...
#include <strings.h>			// strcasecmp() (comparar sin mayusculas/minusculas)
#include <ctype.h>				// toupper(), isdigit(), isspace()
#include <limits.h>				// INT_MAX, CHAR_BIT...
#include <float.h>				// FLT_MAX, FLT_DIG
#include <math.h>				// sqrtf(), powf(), roundf(), fabsf(), M_PI
#include "esp_system.h"			// esp_get_free_heap_size(): memoria dinamica libre
#include "esp_timer.h"			// esp_timer_get_time(): microsegundos desde el arranque
#include "esp_random.h"			// esp_random(): numero al azar por hardware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"		// vTaskDelay(), pvPortMalloc()

/* ======================= Macros ======================= */

/* Una macro es un reemplazo de texto. Los parentesis evitan sorpresas con 2 * MAXIMO(a, b) */
#define CANT(arr)			(sizeof(arr) / sizeof((arr)[0]))	// elementos de un arreglo (NO de un puntero)
#define MINIMO(a, b)		((a) < (b) ? (a) : (b))
#define MAXIMO(a, b)		((a) > (b) ? (a) : (b))
#define LIMITAR(x, lo, hi)	((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

#define BIT_PRENDER(x, n)	((x) |= (1u << (n)))
#define BIT_APAGAR(x, n)	((x) &= ~(1u << (n)))
#define BIT_INVERTIR(x, n)	((x) ^= (1u << (n)))
#define BIT_LEER(x, n)		(((x) >> (n)) & 1u)

#define FLAG_ALARMA		(1u << 0)			// cada aviso en un bit distinto
#define FLAG_WIFI		(1u << 1)
#define FLAG_GUARDAR	(1u << 2)

/* ======================= Tipos ======================= */

typedef struct {
	char nombre[16];			// el texto vive DENTRO del struct (se copia con el struct)
	int edad;
	float nota;
} alumno_t;

typedef enum {
	MODO_APAGADO,				// 0
	MODO_FIJO,					// 1
	MODO_PARPADEO				// 2
} modo_t;

typedef struct {				// bitfields: cuantos bits usa cada campo
	uint8_t led1 : 1;			// bit 0
	uint8_t led2 : 1;			// bit 1
	uint8_t modo : 2;			// bits 2-3 (0..3)
	uint8_t nivel : 4;			// bits 4-7 (0..15)
} registro_t;					// entra todo en 1 byte

/* ======================= Prototipos ======================= */

void ejemplo_tipos(void);
void ejemplo_casts(void);
void ejemplo_cadenas(void);
void ejemplo_convertir(void);
void ejemplo_separar(void);
void ejemplo_memoria(void);
void ejemplo_dinamica(void);
void ejemplo_punteros(void);
void ejemplo_utilidades(void);
void ejemplo_bits(void);

const char *modo_texto(modo_t m);					// enum -> texto con switch
int mapear(int x, int in_min, int in_max, int out_min, int out_max);	// cambia de rango
void entero_a_texto(int n, char *s);				// como snprintf "%d", pero a mano
int comparar_int(const void *a, const void *b);		// para qsort: de menor a mayor
int comparar_nota(const void *a, const void *b);	// para qsort: alumnos por nota
int contar_bits(uint32_t x);						// cuantos bits en 1

bool copiar(char *destino, size_t tam, const char *origen);	// copia segura (snprintf)
void a_mayusculas(char *s);							// cambia el texto que le pasan
bool es_numero(const char *s);						// "123" -> true, "12a" -> false
bool texto_a_int(const char *s, int *valor);		// convierte validando (mejor que atoi)
bool texto_a_float(const char *s, float *valor);	// igual, para numeros con punto ("3.75")
void intercambiar(int *a, int *b);					// por puntero: cambia las variables de afuera
int sumar_arreglo(const int *v, int n);				// un arreglo llega como puntero: hace falta n
int buscar(const char *const *lista, int n, const char *palabra);	// indice o -1
int suma(int a, int b);
int resta(int a, int b);
int operar(int a, int b, int (*f)(int, int));		// recibe una funcion como parametro
int contar(void);									// variable static: recuerda entre llamadas
void imprimir_binario(const char *nombre, uint8_t x);	// "nombre   01011010"

/* ======================= Arranque ======================= */

void app_main(void)
{
	vTaskDelay(pdMS_TO_TICKS(500));					// dar tiempo a que abra el monitor

	ejemplo_tipos();
	ejemplo_casts();
	ejemplo_cadenas();
	ejemplo_convertir();
	ejemplo_separar();
	ejemplo_memoria();
	ejemplo_dinamica();
	ejemplo_punteros();
	ejemplo_utilidades();
	ejemplo_bits();

	printf("\n=== FIN ===\n");
}

/* ======================= Tipos de variables ======================= */

void ejemplo_tipos(void)
{
	printf("\n=== TIPOS ===\n");

	/* Cuanto ocupa cada tipo en el ESP32 (procesador de 32 bits) */
	printf("char=%d short=%d int=%d long=%d long long=%d\n",
		   (int)sizeof(char), (int)sizeof(short), (int)sizeof(int), (int)sizeof(long), (int)sizeof(long long));
	printf("float=%d double=%d bool=%d puntero=%d\n",
		   (int)sizeof(float), (int)sizeof(double), (int)sizeof(bool), (int)sizeof(void *));

	/* stdint.h: mismo tamanio en cualquier procesador -> usar estos en embebidos */
	printf("int8_t  %d..%d   uint8_t  0..%d\n", INT8_MIN, INT8_MAX, UINT8_MAX);
	printf("int16_t %d..%d   uint16_t 0..%d\n", INT16_MIN, INT16_MAX, UINT16_MAX);
	printf("int32_t %" PRId32 "..%" PRId32 "   uint32_t 0..%" PRIu32 "\n", INT32_MIN, INT32_MAX, UINT32_MAX);
	printf("int64_t hasta %" PRId64 "\n", INT64_MAX);
	printf("float: %d cifras, hasta %g   double: %d cifras\n", FLT_DIG, FLT_MAX, DBL_DIG);

	/* Desborde: al pasarse del maximo (o del minimo) da la vuelta */
	uint8_t u8 = 255;
	u8++;										// 0
	uint8_t resta = 3;
	resta -= 5;									// 254: un unsigned no tiene negativos
	int8_t s8 = 127;
	s8++;										// -128
	printf("uint8 255+1=%d   uint8 3-5=%d   int8 127+1=%d\n", u8, resta, s8);

	/* Comparar con signo contra sin signo: el -1 se convierte en 4294967295 */
	int negativo = -1;
	unsigned int uno = 1;
	printf("-1 < 1u da %d (!)\n", negativo < uno);	// 0

	/* Division entre enteros: descarta los decimales. % es el resto */
	printf("7/2=%d  7%%2=%d  -7/2=%d  7/2.0=%.1f  (float)7/2=%.1f\n",
		   7 / 2, 7 % 2, -7 / 2, 7 / 2.0, (float)7 / 2);

	/* Los decimales no son exactos: nunca comparar con == */
	double suma = 0.1 + 0.2;
	printf("0.1+0.2==0.3? %d   con tolerancia? %d   0.1f vale en realidad %.10f\n",
		   suma == 0.3, fabs(suma - 0.3) < 0.0001, 0.1f);
	float f_grande = 16777217;					// float tiene 24 bits de mantisa
	printf("float 16777217 -> %.0f\n", f_grande);	// 16777216

	/* char es un numero chico: las letras son codigos ASCII */
	char letra = 'A';
	printf("'A'=%d  'A'+1='%c'  '7'-'0'=%d  'a'-'A'=%d\n", letra, letra + 1, '7' - '0', 'a' - 'A');

	/* bool: 0 es false, cualquier otro numero es true */
	bool b = 5;									// queda en 1 (true)
	printf("bool b=5 -> %d   !!7=%d   (3 > 2)=%d   !(3 > 2)=%d\n", b, !!7, 3 > 2, !(3 > 2));

	/* enum: nombres para numeros. Ideal para estados y modos */
	modo_t modo = MODO_PARPADEO;
	printf("modo=%d (%s)\n", modo, modo_texto(modo));

	/* Modificadores:
	 *   const int x = 5;      no se puede cambiar (el compilador avisa si lo intentas)
	 *   volatile bool flag;   la cambia una ISR u otra tarea: leerla siempre de memoria
	 *   static int x;         afuera de funciones: solo se ve en este archivo
	 *                         adentro de una funcion: recuerda su valor (ver contar())
	 *   #define TAM 10        reemplazo de texto, sin tipo
	 *   size_t                lo que devuelven sizeof y strlen (sin signo)
	 * Alcance: una variable local existe solo dentro de sus { }; una global, siempre. */
	const int tam = 10;
	size_t largo = strlen("hola");
	printf("const tam=%d   size_t largo=%u   CHAR_BIT=%d   INT_MAX=%d\n",
		   tam, (unsigned)largo, CHAR_BIT, INT_MAX);
}

const char *modo_texto(modo_t m)
{
	switch (m) {								// con enum el compilador avisa si falta un caso
	case MODO_APAGADO:
		return "apagado";
	case MODO_FIJO:
		return "fijo";
	case MODO_PARPADEO:
		return "parpadeo";
	}
	return "?";
}

/* ======================= Conversiones entre tipos ======================= */

void ejemplo_casts(void)
{
	printf("\n=== CONVERSIONES DE TIPO ===\n");

	/* Promedio: si todo es int, la division corta -> convertir uno a float con (float) */
	int total = 10, cantidad = 4;
	printf("promedio int=%d   con (float)=%.2f\n", total / cantidad, (float)total / cantidad);	// 2 y 2.50

	/* float -> int CORTA, no redondea. Para redondear: roundf / lroundf */
	float x = 3.7f, y = -3.7f;
	printf("(int)3.7=%d (int)-3.7=%d   roundf(3.5)=%.0f lroundf(2.5)=%ld   floorf(3.7)=%.0f ceilf(3.2)=%.0f\n",
		   (int)x, (int)y, roundf(3.5f), lroundf(2.5f), floorf(x), ceilf(3.2f));
	printf("sin math.h: (int)(3.7 + 0.5)=%d\n", (int)(x + 0.5f));	// redondeo para positivos

	/* ADC (0..4095) a milivolts y porcentaje: primero multiplicar, despues dividir */
	int crudo = 2048;
	int mv = crudo * 3300 / 4095;				// 1650
	int mal = crudo / 4095 * 3300;				// 0: 2048 / 4095 ya dio 0
	int pct = crudo * 100 / 4095;				// 50
	float volts = crudo * 3.3f / 4095.0f;
	printf("crudo=%d -> %d mV (al reves: %d)   %d%%   %.3f V   mapear a 0..255: %d\n",
		   crudo, mv, mal, pct, volts, mapear(crudo, 0, 4095, 0, 255));

	/* Achicar el tipo: se queda con los bits de abajo (300 -> 44). Limitar antes */
	int grande = 300;
	uint8_t recortado = (uint8_t)grande;
	uint8_t limitado = (uint8_t)LIMITAR(grande, 0, 255);
	printf("(uint8_t)300=%d   limitado=%d   (uint8_t)-1=%d   (uint32_t)-1=%" PRIu32 "\n",
		   recortado, limitado, (uint8_t)-1, (uint32_t)-1);

	/* Numero <-> caracter */
	int digito = 7;
	char c = '0' + digito;
	const char *hexa = "0123456789ABCDEF";
	printf("'0'+7='%c'   '9'-'0'=%d   cifra hexa de 11='%c'\n", c, '9' - '0', hexa[11]);

	/* Entero a texto "a mano" (lo que hace snprintf por dentro) */
	char t[12];
	entero_a_texto(-1234, t);
	printf("entero_a_texto(-1234)=\"%s\"\n", t);

	/* Punto fijo: guardar 25.37 como el entero 2537 (centesimas) y mostrarlo sin float */
	float temp = 25.37f;
	int centesimas = (int)lroundf(temp * 100);
	printf("punto fijo: %d -> %d.%02d\n", centesimas, centesimas / 100, centesimas % 100);

	/* Bytes -> texto hexa (una MAC, un paquete) y al reves */
	uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0xAB, 0x01, 0xFF };
	char mac_txt[18];
	snprintf(mac_txt, sizeof(mac_txt), "%02X:%02X:%02X:%02X:%02X:%02X",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	printf("bytes -> texto: %s\n", mac_txt);

	const char *hex_txt = "1A2BFF";
	uint8_t bytes[3];
	for (int i = 0; i < 3; i++) {
		unsigned int valor;
		sscanf(hex_txt + 2 * i, "%2x", &valor);	// lee 2 letras hexa desde la posicion 2*i
		bytes[i] = (uint8_t)valor;
	}
	printf("texto -> bytes: %d %d %d\n", bytes[0], bytes[1], bytes[2]);	// 26 43 255

	/* bool <-> texto */
	bool encendido = true;
	const char *recibido = "on";
	printf("bool -> texto: %s   texto -> bool: %d\n",
		   encendido ? "true" : "false", strcmp(recibido, "on") == 0);
}

/* Pasa x del rango [in_min, in_max] al rango [out_min, out_max] (como map() de Arduino) */
int mapear(int x, int in_min, int in_max, int out_min, int out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/* Entero a texto sin snprintf: saca las cifras de atras para adelante con % 10 */
void entero_a_texto(int n, char *s)
{
	char aux[12];
	int i = 0, j = 0;
	unsigned int v = (n < 0) ? -(unsigned int)n : (unsigned int)n;	// sin signo: anda hasta con INT_MIN
	do {
		aux[i++] = '0' + v % 10;				// la ultima cifra
		v /= 10;
	} while (v > 0);
	if (n < 0) {
		s[j++] = '-';
	}
	while (i > 0) {
		s[j++] = aux[--i];						// se copian al reves
	}
	s[j] = '\0';
}

/* ======================= Cadenas ======================= */

/* Una cadena en C es un arreglo de char terminado en '\0'.
 * "Hola" ocupa 5 bytes: 'H' 'o' 'l' 'a' '\0'. */
void ejemplo_cadenas(void)
{
	printf("\n=== CADENAS ===\n");

	/* sizeof = lugar reservado; strlen = letras que hay (sin el '\0') */
	char saludo[16] = "Hola";
	printf("sizeof=%d strlen=%d\n", (int)sizeof(saludo), (int)strlen(saludo));	// 16 y 4

	/* Copiar. strcpy NO controla el tamanio: si src es mas largo, pisa memoria.
	 * La forma segura y facil: snprintf con sizeof del destino. */
	char destino[8];
	const char *largo = "Texto demasiado largo";
	if (!copiar(destino, sizeof(destino), largo)) {	// copia 7 letras + '\0'
		printf("no entraba entero: ");
	}
	printf("copia recortada: \"%s\"\n", destino);		// "Texto d"

	/* strncpy: tambien sirve, pero si se llena NO pone el '\0': hay que ponerlo */
	strncpy(destino, "Chau", sizeof(destino) - 1);
	destino[sizeof(destino) - 1] = '\0';
	printf("strncpy: \"%s\"\n", destino);

	/* Concatenar (pegar al final): strncat con el lugar que queda */
	char frase[32] = "Hola";
	strncat(frase, " mundo", sizeof(frase) - strlen(frase) - 1);
	printf("strncat: \"%s\"\n", frase);

	/* Armar un texto de a pedazos: snprintf devuelve cuanto escribio */
	char lista[64];
	int largo_lista = 0;
	int valores[] = { 10, 20, 30 };
	for (int i = 0; i < 3; i++) {
		largo_lista += snprintf(lista + largo_lista, sizeof(lista) - largo_lista,
								"%s%d", (i > 0) ? "," : "", valores[i]);	// coma salvo al primero
	}
	printf("armado: \"%s\" (largo %d)\n", lista, largo_lista);	// "10,20,30"

	/* Comparar: devuelven 0 si son iguales (por eso "== 0") */
	printf("strcmp(\"led\",\"led\")=%d\n", strcmp("led", "led"));		// 0
	printf("strcmp(\"abc\",\"abd\")<0: %d\n", strcmp("abc", "abd") < 0);	// 1: "abc" va antes
	printf("strncmp primeras 3 de \"led2\" y \"led\": %d\n", strncmp("led2", "led", 3));	// 0
	printf("strcasecmp(\"ON\",\"on\")=%d\n", strcasecmp("ON", "on"));	// 0: ignora mayusculas

	/* Buscar: devuelven un PUNTERO a donde lo encontro, o NULL */
	const char *config = "ssid=MiRed";
	const char *igual = strchr(config, '=');			// busca un caracter
	if (igual != NULL) {
		printf("strchr: '=' en la posicion %d, despues dice \"%s\"\n",
			   (int)(igual - config), igual + 1);		// resta de punteros = posicion
	}
	if (strstr(config, "Red") != NULL) {				// busca un texto dentro de otro
		printf("strstr: contiene \"Red\"\n");
	}

	/* Sacar el Enter del final (lo que llega por TCP/UART trae \r\n) */
	char linea[32] = "hola\r\n";
	linea[strcspn(linea, "\r\n")] = '\0';				// strcspn: posicion del primer \r o \n
	printf("sin enter: \"%s\"\n", linea);

	/* Letra por letra: toupper, isdigit */
	char nombre[16] = "esp32-s3";
	a_mayusculas(nombre);
	printf("mayusculas: \"%s\"\n", nombre);				// "ESP32-S3"
	printf("es_numero(\"123\")=%d es_numero(\"12a\")=%d\n", es_numero("123"), es_numero("12a"));
}

/* Copia segura: nunca se pasa de tam y siempre deja el '\0'.
 * Devuelve false si el texto no entraba entero (quedo recortado). */
bool copiar(char *destino, size_t tam, const char *origen)
{
	int necesita = snprintf(destino, tam, "%s", origen);	// devuelve el largo que HUBIERA querido
	return necesita < (int)tam;
}

void a_mayusculas(char *s)
{
	for (int i = 0; s[i] != '\0'; i++) {				// recorre hasta el '\0'
		s[i] = toupper((unsigned char)s[i]);			// 'a' -> 'A'; lo demas queda igual
	}
}

bool es_numero(const char *s)
{
	if (*s == '-') {
		s++;											// se permite un signo adelante
	}
	if (*s == '\0') {
		return false;									// vacio o solo "-"
	}
	for (; *s != '\0'; s++) {							// avanza el puntero letra por letra
		if (!isdigit((unsigned char)*s)) {
			return false;
		}
	}
	return true;
}

/* ======================= Texto <-> numero ======================= */

void ejemplo_convertir(void)
{
	printf("\n=== CONVERTIR ===\n");

	/* atoi: facil, pero si el texto no es numero devuelve 0 sin avisar */
	printf("atoi(\"123\")=%d atoi(\"abc\")=%d atoi(\"12abc\")=%d\n",
		   atoi("123"), atoi("abc"), atoi("12abc"));	// 123, 0, 12

	/* strtol: avisa donde dejo de leer -> se puede validar */
	int v;
	printf("texto_a_int(\"250\"): %s", texto_a_int("250", &v) ? "ok" : "ERROR");
	printf(" -> %d\n", v);
	printf("texto_a_int(\"25x\"): %s\n", texto_a_int("25x", &v) ? "ok" : "ERROR");

	/* Otras bases: 16 = hexa, 2 = binario, 0 = la detecta ("0x" -> hexa) */
	printf("strtol(\"FF\",16)=%ld strtol(\"1010\",2)=%ld strtol(\"0x1F\",0)=%ld\n",
		   strtol("FF", NULL, 16), strtol("1010", NULL, 2), strtol("0x1F", NULL, 0));

	/* Con coma: strtof (float) */
	float f = strtof("3.75", NULL);
	printf("strtof(\"3.75\")=%.2f\n", f);

	/* Validando: el separador es el PUNTO. "3,75" es error (con NULL daria 3.0 sin avisar) */
	printf("texto_a_float(\"3.75\"): %s", texto_a_float("3.75", &f) ? "ok" : "ERROR");
	printf(" -> %.2f\n", f);
	printf("texto_a_float(\"-0.5\"): %s", texto_a_float("-0.5", &f) ? "ok" : "ERROR");
	printf(" -> %.2f\n", f);
	printf("texto_a_float(\"3,75\"): %s\n", texto_a_float("3,75", &f) ? "ok" : "ERROR");
	printf("texto_a_float(\"abc\"): %s\n", texto_a_float("abc", &f) ? "ok" : "ERROR");

	/* sscanf: lee varios valores con un formato; devuelve cuantos pudo leer */
	int r, g, b;
	int leidos = sscanf("20,0,5", "%d,%d,%d", &r, &g, &b);
	printf("sscanf leyo %d valores: r=%d g=%d b=%d\n", leidos, r, g, b);

	char clave[16];
	int numero;
	if (sscanf("umbral=1500", "%15[^=]=%d", clave, &numero) == 2) {	// %15[^=]: hasta el '=' (max 15)
		printf("sscanf clave=\"%s\" numero=%d\n", clave, numero);
	}

	/* Numero -> texto: snprintf con formatos */
	char t[64];
	uint32_t grande = 4000000000u;
	snprintf(t, sizeof(t), "[%d] [%5d] [%-5d] [%05d]", 42, 42, 42, 42);
	printf("%s\n", t);								// [42] [   42] [42   ] [00042]
	snprintf(t, sizeof(t), "[%.1f] [%8.3f] [%x] [%02X] [%c] [%%]", 3.14159, 2.5, 255, 10, 'A');
	printf("%s\n", t);								// [3.1] [   2.500] [ff] [0A] [A] [%]
	printf("uint32: %" PRIu32 " hexa: 0x%08" PRIX32 "\n", grande, grande);	// PRIu32 para uint32_t
}

/* Convierte validando todo el texto. Devuelve false si no es un numero entero. */
bool texto_a_int(const char *s, int *valor)
{
	char *fin;
	long n = strtol(s, &fin, 10);					// fin queda apuntando a lo que no leyo
	if (fin == s || *fin != '\0') {
		return false;								// no leyo nada, o sobro algo ("25x")
	}
	*valor = (int)n;								// se escribe en la variable de afuera
	return true;
}

/* Igual que texto_a_int pero con strtof. Acepta "3.75", "-0.5", "10", "1e3".
 * Devuelve false si no es un numero ("abc", "3,75", "2.5x"). */
bool texto_a_float(const char *s, float *valor)
{
	char *fin;
	float n = strtof(s, &fin);
	if (fin == s || *fin != '\0') {
		return false;
	}
	*valor = n;
	return true;
}

/* ======================= Separar en palabras ======================= */

void ejemplo_separar(void)
{
	printf("\n=== SEPARAR ===\n");

	/* strtok_r CAMBIA el texto (pone '\0' en los separadores): trabajar sobre una copia
	 * en un arreglo, nunca sobre un literal "..." */
	char linea[] = "led 2 on";
	char *resto;									// strtok_r guarda aca por donde va
	char *palabras[8];
	int n = 0;

	char *p = strtok_r(linea, " ", &resto);			// primera vez: el texto
	while (p != NULL && n < 8) {
		palabras[n++] = p;
		p = strtok_r(NULL, " ", &resto);			// las siguientes: NULL
	}
	printf("%d palabras:", n);						// asi arma microrl argc/argv
	for (int i = 0; i < n; i++) {
		printf(" [%s]", palabras[i]);
	}
	printf("\n");

	/* Dos niveles: "a=1&b=22&c=333" -> pares por '&', despues clave/valor por '=' */
	char datos[] = "a=1&b=22&c=333";
	char *resto1, *resto2;
	for (char *par = strtok_r(datos, "&", &resto1); par != NULL; par = strtok_r(NULL, "&", &resto1)) {
		char *clave = strtok_r(par, "=", &resto2);
		char *valor = strtok_r(NULL, "=", &resto2);
		if (clave != NULL && valor != NULL) {
			printf("clave=%s valor=%d\n", clave, atoi(valor));
		}
	}
}

/* ======================= Memoria (bytes) ======================= */

void ejemplo_memoria(void)
{
	printf("\n=== MEMORIA ===\n");

	/* memset: llena bytes con un valor (casi siempre 0 = limpiar) */
	uint8_t buf[8];
	memset(buf, 0, sizeof(buf));
	memset(buf, 0xAA, 3);							// los 3 primeros en 0xAA
	printf("memset:");
	for (int i = 0; i < 8; i++) {
		printf(" %02X", buf[i]);
	}
	printf("\n");

	/* Cantidad de elementos de un arreglo: sizeof total / sizeof de uno */
	int a[] = { 1, 2, 3, 4, 5 };
	int cant = sizeof(a) / sizeof(a[0]);
	printf("elementos de a[]: %d  suma: %d\n", cant, sumar_arreglo(a, cant));

	/* Los arreglos NO se copian con '=': memcpy con el tamanio en bytes */
	int b[5];
	memcpy(b, a, sizeof(a));
	printf("memcpy: b[4]=%d\n", b[4]);

	/* memcmp: compara bytes, 0 = iguales */
	printf("memcmp(a,b)=%d\n", memcmp(a, b, sizeof(a)));

	/* memmove: como memcpy pero se puede pisar a si mismo (zonas superpuestas).
	 * Ejemplo tipico: borrar el elemento 1 corriendo los de atras una posicion */
	memmove(&a[1], &a[2], (cant - 2) * sizeof(a[0]));
	cant--;
	printf("memmove (borrar a[1]):");
	for (int i = 0; i < cant; i++) {
		printf(" %d", a[i]);						// 1 3 4 5
	}
	printf("\n");

	/* Los structs SI se copian con '=' (incluido el arreglo de adentro) */
	alumno_t x = { "Ana", 21, 8.5f };
	alumno_t y = x;									// copia completa, independiente
	snprintf(y.nombre, sizeof(y.nombre), "Beto");
	printf("struct: x=%s y=%s (cambiar y no toca x)\n", x.nombre, y.nombre);
	printf("sizeof(alumno_t)=%d\n", (int)sizeof(alumno_t));	// 16 + 4 + 4 = 24
}

/* El arreglo llega como puntero: adentro sizeof(v) es 4 (un puntero), no el arreglo */
int sumar_arreglo(const int *v, int n)
{
	int total = 0;
	for (int i = 0; i < n; i++) {
		total += v[i];								// v[i] es lo mismo que *(v + i)
	}
	return total;
}

/* ======================= Memoria dinamica ======================= */

/* Reglas: cada malloc tiene su free; verificar NULL; no usar despues del free;
 * no hacer free dos veces. En embebidos, si el tamanio se conoce, mejor arreglo fijo. */
void ejemplo_dinamica(void)
{
	printf("\n=== MEMORIA DINAMICA ===\n");
	uint32_t libre_antes = esp_get_free_heap_size();

	/* malloc: reserva bytes (con basura adentro) */
	int n = 5;
	int *v = malloc(n * sizeof(int));				// n enteros
	if (v == NULL) {
		printf("sin memoria\n");
		return;
	}
	for (int i = 0; i < n; i++) {
		v[i] = i * i;								// se usa como un arreglo
	}
	printf("malloc: v[4]=%d\n", v[4]);

	/* realloc: cambia el tamanio conservando lo que habia.
	 * Usar un puntero aparte: si falla devuelve NULL y v sigue siendo valido */
	int *nuevo = realloc(v, 10 * sizeof(int));
	if (nuevo == NULL) {
		free(v);
		return;
	}
	v = nuevo;
	for (int i = n; i < 10; i++) {
		v[i] = i * i;
	}
	printf("realloc: v[9]=%d\n", v[9]);
	free(v);
	v = NULL;										// costumbre: asi no se usa por error

	/* calloc: como malloc pero todo en 0 (cantidad, tamanio de cada uno) */
	int *ceros = calloc(4, sizeof(int));
	if (ceros != NULL) {
		printf("calloc: %d %d %d %d\n", ceros[0], ceros[1], ceros[2], ceros[3]);
		free(ceros);
	}

	/* strdup: copia un texto en memoria nueva (largo justo). Despues: free */
	char *copia = strdup("texto copiado");
	if (copia != NULL) {
		a_mayusculas(copia);
		printf("strdup: %s\n", copia);
		free(copia);
	}

	/* Un struct dinamico: con puntero se usa '->' en vez de '.' */
	alumno_t *al = malloc(sizeof(alumno_t));
	if (al != NULL) {
		snprintf(al->nombre, sizeof(al->nombre), "Carla");
		al->edad = 22;
		al->nota = 9.25f;
		printf("struct dinamico: %s %d %.2f\n", al->nombre, al->edad, al->nota);
		free(al);
	}

	/* FreeRTOS tiene las suyas: pvPortMalloc / vPortFree (en el ESP32 es lo mismo) */
	char *texto = pvPortMalloc(32);
	if (texto != NULL) {
		snprintf(texto, 32, "con pvPortMalloc");
		printf("%s\n", texto);
		vPortFree(texto);
	}

	/* Si todo lo que se reservo se libero, la memoria libre vuelve a ser la misma */
	uint32_t libre_despues = esp_get_free_heap_size();
	printf("heap libre antes=%" PRIu32 " despues=%" PRIu32 " -> %s\n",
		   libre_antes, libre_despues, (libre_antes == libre_despues) ? "sin perdidas" : "PERDIDA");
}

/* ======================= Punteros y funciones ======================= */

void ejemplo_punteros(void)
{
	printf("\n=== PUNTEROS ===\n");

	/* Un puntero guarda una direccion. &x = direccion de x; *p = lo que hay ahi */
	int x = 10;
	int *p = &x;
	*p = 20;										// cambia x a traves del puntero
	printf("x=%d\n", x);							// 20

	/* Pasar por puntero para que la funcion cambie variables de afuera */
	int a = 1, b = 2;
	intercambiar(&a, &b);
	printf("intercambiar: a=%d b=%d\n", a, b);		// 2 1

	/* Aritmetica: p + 1 avanza un elemento (no un byte) */
	int v[] = { 100, 200, 300 };
	int *q = v;										// el nombre del arreglo = direccion del primero
	printf("*(q+2)=%d", *(q + 2));					// 300
	q++;
	printf("  despues de q++: *q=%d\n", *q);		// 200

	/* Arreglo de textos (como argv de microrl) */
	const char *colores[] = { "rojo", "verde", "azul" };
	int cant = sizeof(colores) / sizeof(colores[0]);
	printf("buscar \"verde\"=%d buscar \"negro\"=%d\n",
		   buscar(colores, cant, "verde"), buscar(colores, cant, "negro"));	// 1, -1

	/* Puntero a funcion: pasar "que hacer" como parametro (como el execute de microrl) */
	printf("operar(5,3,suma)=%d operar(5,3,resta)=%d\n", operar(5, 3, suma), operar(5, 3, resta));

	/* static dentro de una funcion: se crea una vez y recuerda su valor */
	contar();
	contar();
	printf("contar() por tercera vez=%d\n", contar());	// 3

	/* const char *: se puede leer pero no cambiar el texto (los "..." son de solo lectura).
	 * Nunca devolver la direccion de una variable local: deja de existir al salir. */
}

void intercambiar(int *a, int *b)
{
	int aux = *a;
	*a = *b;
	*b = aux;
}

int buscar(const char *const *lista, int n, const char *palabra)
{
	for (int i = 0; i < n; i++) {
		if (strcmp(lista[i], palabra) == 0) {
			return i;
		}
	}
	return -1;										// no esta
}

int suma(int a, int b)
{
	return a + b;
}

int resta(int a, int b)
{
	return a - b;
}

int operar(int a, int b, int (*f)(int, int))
{
	return f(a, b);									// llama a la funcion que le pasaron
}

int contar(void)
{
	static int veces = 0;							// sin static volveria a 0 en cada llamada
	veces++;
	return veces;
}

/* ======================= Utilidades ======================= */

void ejemplo_utilidades(void)
{
	printf("\n=== UTILIDADES ===\n");

	/* Macros (definidas arriba) */
	int v[] = { 40, 7, 93, 15, 62 };
	printf("CANT=%d  MINIMO(3,8)=%d  MAXIMO(3,8)=%d  LIMITAR(120,0,100)=%d\n",
		   (int)CANT(v), MINIMO(3, 8), MAXIMO(3, 8), LIMITAR(120, 0, 100));
	/* Cuidado: MAXIMO(i++, 3) hace i++ dos veces (la macro copia el texto tal cual) */

	/* Recorrer un arreglo: menor, mayor, promedio */
	int menor = v[0], mayor = v[0], suma = 0;
	for (int i = 0; i < (int)CANT(v); i++) {
		menor = MINIMO(menor, v[i]);
		mayor = MAXIMO(mayor, v[i]);
		suma += v[i];
	}
	printf("menor=%d mayor=%d promedio=%.1f\n", menor, mayor, (float)suma / CANT(v));

	/* Ordenar con qsort: se le pasa una funcion que compara dos elementos */
	qsort(v, CANT(v), sizeof(v[0]), comparar_int);
	printf("qsort:");
	for (int i = 0; i < (int)CANT(v); i++) {
		printf(" %d", v[i]);
	}
	printf("\n");

	alumno_t curso[] = { { "Ana", 21, 8.5f }, { "Beto", 23, 6.0f }, { "Carla", 22, 9.25f } };
	qsort(curso, CANT(curso), sizeof(curso[0]), comparar_nota);
	printf("por nota: %s %s %s\n", curso[0].nombre, curso[1].nombre, curso[2].nombre);

	/* Valor absoluto y math.h (las que terminan en f son para float) */
	printf("abs(-5)=%d  fabsf(-2.5)=%.1f  sqrtf(2)=%.4f  powf(2,10)=%.0f  fmodf(7.5,2)=%.1f\n",
		   abs(-5), fabsf(-2.5f), sqrtf(2.0f), powf(2.0f, 10.0f), fmodf(7.5f, 2.0f));
	printf("M_PI=%.5f  sinf(90 grados)=%.2f\n", M_PI, sinf(90.0f * (float)M_PI / 180.0f));	// en radianes

	/* Aleatorios: rand() repite la secuencia segun la semilla; esp_random() usa hardware */
	srand(1234);
	int dado = rand() % 6 + 1;					// 1..6
	printf("dado=%d   esp_random() %% 100=%" PRIu32 "\n", dado, esp_random() % 100);

	/* Medir cuanto tarda algo: esp_timer_get_time() en microsegundos (int64_t) */
	int64_t t0 = esp_timer_get_time();
	volatile float acum = 0;					// volatile: que el compilador no borre el bucle
	for (int i = 0; i < 1000; i++) {
		acum += sqrtf(i);
	}
	int64_t t1 = esp_timer_get_time();
	printf("1000 sqrtf tardaron %" PRId64 " us\n", t1 - t0);
	TickType_t ticks = xTaskGetTickCount();		// en ticks de FreeRTOS (1 tick = 10 ms)
	printf("ticks=%" PRIu32 " = %" PRIu32 " ms desde el arranque\n",
		   (uint32_t)ticks, (uint32_t)(ticks * portTICK_PERIOD_MS));

	/* Operador ternario: un if que devuelve un valor */
	int pote = 3000;
	printf("ternario: %s\n", (pote > 2000) ? "ALTO" : "bajo");
}

int comparar_int(const void *a, const void *b)
{
	int x = *(const int *)a;					// qsort pasa punteros genericos: convertirlos
	int y = *(const int *)b;
	return (x > y) - (x < y);					// negativo: a va antes; 0: iguales; positivo: despues
}

int comparar_nota(const void *a, const void *b)
{
	const alumno_t *x = a;
	const alumno_t *y = b;
	return (y->nota > x->nota) - (y->nota < x->nota);	// al reves: mayor nota primero
}

/* ======================= Bits (bitwise) ======================= */

void ejemplo_bits(void)
{
	printf("\n=== BITS ===\n");

	/* Los 6 operadores, bit a bit */
	uint8_t a = 0b11001100;						// 0b = escrito en binario (0xCC)
	uint8_t b = 0b10101010;						// 0xAA
	imprimir_binario("a", a);
	imprimir_binario("b", b);
	imprimir_binario("a & b (AND)", a & b);		// 1 si los dos son 1
	imprimir_binario("a | b (OR)", a | b);		// 1 si alguno es 1
	imprimir_binario("a ^ b (XOR)", a ^ b);		// 1 si son distintos
	imprimir_binario("~a (NOT)", ~a);			// invierte todos
	imprimir_binario("a << 2", a << 2);			// corre a la izquierda: los de arriba se pierden
	imprimir_binario("a >> 2", a >> 2);			// corre a la derecha: entran ceros

	/* & no es && : el bitwise mira bit por bit, el logico solo verdadero/falso.
	 * Precedencia: escribir (x & 1) == 0 con parentesis; x & 1 == 0 hace x & (1 == 0) */
	printf("2 & 1 = %d   2 && 1 = %d   ~0 = %d   !0 = %d\n", 2 & 1, 2 && 1, ~0, !0);

	/* Un bit a la vez (con las macros de arriba; ESP-IDF tambien trae BIT0..BIT31 y BIT(n)) */
	uint8_t estado = 0;
	BIT_PRENDER(estado, 0);						// estado |= (1 << 0)
	BIT_PRENDER(estado, 3);
	imprimir_binario("prender 0 y 3", estado);
	BIT_APAGAR(estado, 0);						// estado &= ~(1 << 0)
	imprimir_binario("apagar 0", estado);
	BIT_INVERTIR(estado, 7);					// estado ^= (1 << 7)
	imprimir_binario("invertir 7", estado);
	printf("bit 3=%d  bit 2=%d\n", (int)BIT_LEER(estado, 3), (int)BIT_LEER(estado, 2));	// (estado >> 3) & 1

	/* Varios bits juntos con una mascara */
	uint8_t reg = 0;
	reg |= 0x0F;								// prende los 4 de abajo
	imprimir_binario("reg |= 0x0F", reg);
	reg &= ~0x03;								// apaga los bits 0 y 1
	imprimir_binario("reg &= ~0x03", reg);
	printf("estan todos los de 0x0C? %d   alguno de 0x30? %d\n", (reg & 0x0C) == 0x0C, (reg & 0x30) != 0);

	/* Campo de varios bits: los bits 4..6 guardan un numero de 0 a 7 */
	uint8_t conf = 0b10000001;
	uint8_t campo = 5;
	conf = (conf & ~(0x07 << 4)) | ((campo & 0x07) << 4);	// limpiar ese lugar y escribir
	imprimir_binario("campo=5 en 4..6", conf);	// 1 101 0001
	printf("leer el campo: %d\n", (conf >> 4) & 0x07);

	/* Flags: cada bit es un aviso (es lo que hacen los event groups) */
	uint32_t flags = 0;
	flags |= FLAG_ALARMA | FLAG_GUARDAR;		// dos avisos de una
	if (flags & FLAG_ALARMA) {
		printf("hay alarma\n");
	}
	flags &= ~FLAG_ALARMA;						// atendido: borrarlo
	printf("flags=0x%02" PRIX32 "  wifi? %d  guardar? %d\n",
		   flags, (flags & FLAG_WIFI) != 0, (flags & FLAG_GUARDAR) != 0);

	/* Recorrer los bits (por ejemplo, pasar una mascara a los 4 LEDs) */
	uint8_t mascara = 0x05;						// 0101: LEDs 1 y 3
	printf("mascara 0x05 ->");
	for (int i = 0; i < 4; i++) {
		printf(" LED%d=%d", i + 1, (mascara >> i) & 1);	// salida_set(leds[i], (mascara >> i) & 1)
	}
	printf("\n");

	/* Trucos */
	uint32_t n = 40;							// 101000
	printf("bits en 1 de 40: %d (a mano) %d (__builtin_popcount)\n", contar_bits(n), __builtin_popcount(n));
	printf("potencia de 2? 40: %d   64: %d   (x & (x - 1)) == 0\n", (n & (n - 1)) == 0, (64 & (64 - 1)) == 0);
	printf("primer bit prendido de 40: %d (__builtin_ctz)\n", __builtin_ctz(n));
	printf("40 << 1 = %d (x2)   40 >> 2 = %d (/4)   par? 40 & 1 = %d  41 & 1 = %d\n",
		   (int)(n << 1), (int)(n >> 2), (int)(n & 1), 41 & 1);
	printf("-8 >> 1 = %d (con signo copia el signo: para bits usar unsigned)\n", -8 >> 1);

	/* Rotar: el bit que sale por un lado entra por el otro */
	uint8_t r = 0b10010001;
	imprimir_binario("r", r);
	imprimir_binario("rotar 1 a la izq", (uint8_t)((r << 1) | (r >> 7)));

	/* Partir un numero en bytes, armarlo de nuevo, invertir el orden */
	uint16_t valor = 0x1234;
	uint8_t alto = valor >> 8;					// 0x12
	uint8_t bajo = valor & 0xFF;				// 0x34
	uint16_t armado = ((uint16_t)alto << 8) | bajo;
	uint16_t invertido = (uint16_t)((valor << 8) | (valor >> 8));
	printf("0x%04X -> alto=0x%02X bajo=0x%02X -> armado=0x%04X   invertido=0x%04X\n",
		   valor, alto, bajo, armado, invertido);

	/* Los bytes en memoria: el ESP32 es little endian (primero el byte bajo) */
	uint32_t numero = 0x11223344;
	uint8_t bytes[4];
	memcpy(bytes, &numero, sizeof(numero));
	printf("0x11223344 en memoria: %02X %02X %02X %02X\n", bytes[0], bytes[1], bytes[2], bytes[3]);	// 44 33 22 11

	/* XOR: checksum de un paquete, y aplicar dos veces la misma clave vuelve al original */
	uint8_t paquete[] = { 0x01, 0x10, 0xFF, 0x22 };
	uint8_t chk = 0;
	for (int i = 0; i < (int)CANT(paquete); i++) {
		chk ^= paquete[i];
	}
	uint8_t dato = 0x5A, clave = 0x3C;
	uint8_t cifrado = dato ^ clave;
	printf("checksum XOR=0x%02X   0x5A^0x3C=0x%02X  ^0x3C otra vez=0x%02X\n", chk, cifrado, cifrado ^ clave);

	/* Bitfields: el compilador arma las mascaras solo */
	registro_t regf = { 0 };
	regf.led1 = 1;
	regf.modo = 2;
	regf.nivel = 9;
	uint8_t byte_regf;
	memcpy(&byte_regf, &regf, 1);
	printf("bitfield: sizeof=%d  modo=%d nivel=%d  el byte=0x%02X\n",
		   (int)sizeof(regf), regf.modo, regf.nivel, byte_regf);	// 1001 10 0 1 = 0x99
}

int contar_bits(uint32_t x)
{
	int n = 0;
	while (x != 0) {
		n += x & 1;								// mira el bit de abajo
		x >>= 1;								// y corre todo uno a la derecha
	}
	return n;
}

void imprimir_binario(const char *nombre, uint8_t x)
{
	printf("%-18s ", nombre);					// %-18s: texto alineado a la izquierda en 18 lugares
	for (int i = 7; i >= 0; i--) {				// del bit mas alto al mas bajo
		printf("%d", (x >> i) & 1);
	}
	printf("\n");
}
