/**
 * @file ejemploDoxygen.h
 * @brief Ejemplos de comentarios Doxygen para copiar: archivo, defines, enum, struct,
 *        funciones, tareas, ISR y callbacks.
 *
 * Doxygen arma documentacion (HTML/PDF) leyendo los comentarios que empiezan con
 * barra-doble-asterisco o con tres barras. Reglas rapidas:
 * - El comentario va ANTES de lo que documenta (o despues con "///<" en la misma linea).
 * - En el .h se documenta QUE hace y como se usa (es lo que lee el que llama).
 * - En el .c alcanza con un comentario normal de COMO lo hace (ver al final del archivo).
 * - Primera linea con @brief (una oracion); despues una linea en blanco y el detalle.
 *
 * Este archivo compila, pero no se usa en el proyecto: es solo para copiar.
 *
 * @author  Apellido, Nombre
 * @date    2026-10-26
 * @version 1.0
 */

#ifndef EJEMPLO_DOXYGEN_H_
#define EJEMPLO_DOXYGEN_H_

#include <stdbool.h>
#include <stddef.h>				// size_t
#include <stdint.h>

/* ======================= Defines ======================= */

#define SENSOR_MAX		4095	///< Lectura maxima del ADC (12 bits).
#define PERIODO_MS		100		///< Periodo de muestreo en milisegundos.

/**
 * @brief Convierte una lectura del ADC (0..SENSOR_MAX) a porcentaje (0..100).
 * @param x Lectura cruda.
 */
#define A_PORCENTAJE(x)	((x) * 100 / SENSOR_MAX)

/* ======================= Tipos ======================= */

/**
 * @brief Modo de funcionamiento del equipo.
 */
typedef enum{
	MODO_MANUAL = 0,		///< Lo maneja el usuario desde la consola o la web.
	MODO_AUTOMATICO,		///< Lo maneja el lazo de control segun el setpoint.
	MODO_FALLA				///< Se detecto un error: salidas apagadas hasta reconocerlo.
}modo_t;

/**
 * @brief Configuracion del control (se guarda entera en NVS como blob).
 *
 * Si se agrega o cambia un campo, lo guardado antes deja de cargarse (mide distinto)
 * y quedan los valores por defecto.
 */
typedef struct{
	uint8_t min;			///< Limite inferior en %, 0..100.
	uint8_t max;			///< Limite superior en %, min+1..100.
	uint16_t periodoMs;		///< Cada cuanto se ejecuta el control, en ms.
	modo_t modo;			///< Modo al arrancar.
}config_t;

/**
 * @brief Funcion que se llama cuando llega un evento.
 * @param codigo Que paso (depende de quien la llame).
 * @param ctx    Puntero que se paso al registrarla (puede ser NULL).
 */
typedef void (*evento_cb_t)(int codigo, void *ctx);

/* ======================= Funciones ======================= */

/**
 * @brief Inicializa el sensor. Llamar una vez, antes de sensor_leer().
 */
void sensor_init(void);

/**
 * @brief Lee el sensor y devuelve el valor en porcentaje.
 *
 * Promedia varias lecturas para sacar ruido.
 *
 * @param[in]  muestras Cuantas lecturas promediar (1..64).
 * @param[out] crudo    Donde dejar el promedio sin convertir (0..SENSOR_MAX). Puede ser NULL.
 * @return Valor en porcentaje, 0..100.
 *
 * @pre  sensor_init() ya se llamo.
 * @note Tarda unos 50 us por muestra: no llamar desde una ISR.
 * @see  A_PORCENTAJE
 */
uint8_t sensor_leer(uint8_t muestras, uint16_t *crudo);

/**
 * @brief Cambia el setpoint del control.
 *
 * @param valor Nuevo setpoint en %.
 * @retval true  Aceptado.
 * @retval false Fuera de [config.min, config.max]: no se cambio nada.
 */
bool control_setpoint(uint8_t valor);

/**
 * @brief Copia un texto a un buffer sin pasarse.
 *
 * @param[out]    dest   Buffer destino (siempre queda terminado en '\0').
 * @param[in]     origen Texto a copiar.
 * @param[in]     tamano sizeof(dest).
 * @param[in,out] largo  Entra: caracteres ya ocupados en dest. Sale: el total despues de copiar.
 * @return Cuantos caracteres se copiaron (puede ser menos si no entraba).
 *
 * @code
 *   char linea[32];
 *   size_t largo = 0;
 *   texto_agregar(linea, "pote=", sizeof(linea), &largo);
 *   texto_agregar(linea, "1234", sizeof(linea), &largo);	// linea = "pote=1234", largo = 9
 * @endcode
 */
size_t texto_agregar(char *dest, const char *origen, size_t tamano, size_t *largo);

/**
 * @brief Registra la funcion a llamar en cada evento.
 * @param cb  Funcion (NULL = dejar de avisar).
 * @param ctx Se le pasa tal cual a cb en cada llamada.
 * @warning cb se ejecuta en la tarea del modulo: tiene que volver rapido y no bloquearse.
 */
void evento_registrar(evento_cb_t cb, void *ctx);

/**
 * @brief Tarea de control: lee el sensor y actua cada config.periodoMs.
 *
 * Prioridad tskIDLE_PRIORITY + 5 (tiene plazo). Usa vTaskDelayUntil para que el periodo
 * no se corra. Se comunica con el resto por colaSetpoint (entrada) y mutexConfig.
 *
 * @param pvParam No se usa (NULL).
 */
void task_control(void *pvParam);

/**
 * @brief ISR del boton de parada: solo avisa a task_emergencia con xTaskNotifyFromISR.
 *
 * Va en IRAM (IRAM_ATTR en el .c). No usa printf, ESP_LOG, mutex ni NVS.
 *
 * @param arg Pin que interrumpio (casteado a void *).
 */
void isr_parada(void *arg);

/**
 * @brief Version vieja de sensor_leer() con una sola muestra.
 * @deprecated Usar sensor_leer(1, NULL).
 */
uint8_t sensor_leer_rapido(void);

/**
 * @brief Calibra el sensor contra dos puntos conocidos.
 * @todo Guardar la calibracion en NVS (ahora se pierde al reiniciar).
 */
void sensor_calibrar(void);

#endif /* EJEMPLO_DOXYGEN_H_ */

/* ======================= Y en el .c =======================
 *
 * El .c no repite el Doxygen del .h. Arriba de cada funcion, un comentario comun que
 * explique como lo hace, si no es obvio:
 *
 *	// Descarta la lectura mas alta y la mas baja antes de promediar
 *	uint8_t sensor_leer(uint8_t muestras, uint16_t *crudo)
 *	{
 *		...
 *	}
 *
 * Las funciones static (solo del .c) si se documentan en el .c, con el mismo formato.
 *
 * ======================= Generar la documentacion (opcional) =======================
 *
 *	doxygen -g            crea el archivo Doxyfile
 *	(en Doxyfile: INPUT = main   OPTIMIZE_OUTPUT_FOR_C = YES   EXTRACT_ALL = YES)
 *	doxygen               genera html/index.html
 */
