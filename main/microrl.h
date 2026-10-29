/**
 * @file microrl.h
 * @brief Interfaz publica de la libreria microrl para consola interactiva.
 */

#ifndef _MICRORL_H_
#define _MICRORL_H_

#include "config.h"

#define true  1
#define false 0

#define KEY_NUL 0   /**< ^@ Null character */
#define KEY_SOH 1   /**< ^A Start of heading */
#define KEY_STX 2   /**< ^B Start of text */
#define KEY_ETX 3   /**< ^C End of text */
#define KEY_EOT 4   /**< ^D End of transmission */
#define KEY_ENQ 5   /**< ^E Enquiry */
#define KEY_ACK 6   /**< ^F Acknowledge */
#define KEY_BEL 7   /**< ^G Bell */
#define KEY_BS  8   /**< ^H Backspace */
#define KEY_HT  9   /**< ^I Horizontal tab */
#define KEY_LF  10  /**< ^J Line Feed */
#define KEY_VT  11  /**< ^K Vertical tab */
#define KEY_FF  12  /**< ^L Form Feed */
#define KEY_CR  13  /**< ^M Carriage Return */
#define KEY_SO  14  /**< ^N Shift Out */
#define KEY_SI  15  /**< ^O Shift In */
#define KEY_DLE 16  /**< ^P Data link escape */
#define KEY_DC1 17  /**< ^Q XON */
#define KEY_DC2 18  /**< ^R Device control 2 */
#define KEY_DC3 19  /**< ^S XOFF */
#define KEY_DC4 20  /**< ^T Device control 4 */
#define KEY_NAK 21  /**< ^U Negative acknowledge */
#define KEY_SYN 22  /**< ^V Synchronous idle */
#define KEY_ETB 23  /**< ^W End transmission block */
#define KEY_CAN 24  /**< ^X Cancel line */
#define KEY_EM  25  /**< ^Y End of medium */
#define KEY_SUB 26  /**< ^Z Substitute */
#define KEY_ESC 27  /**< ^[ Escape */
#define KEY_FS  28  /**< ^\ File separator */
#define KEY_GS  29  /**< ^] Group separator */
#define KEY_RS  30  /**< ^^ Record separator */
#define KEY_US  31  /**< ^_ Unit separator */
#define KEY_DEL 127 /**< Delete */

#define IS_CONTROL_CHAR(x) ((x)<=31)

#define _HIST_UP   0
#define _HIST_DOWN 1
#define _ESC_BRACKET  1
#define _ESC_HOME     2
#define _ESC_END      3

#ifdef _USE_HISTORY
/** @brief Buffer circular para almacenar el historial de comandos. */
typedef struct {
	char ring_buf [_RING_HISTORY_LEN];
	int begin;
	int end;
	int cur;
} ring_history_t;
#endif

/** @brief Estructura de estado de una instancia de consola microrl. */
typedef struct {
#ifdef _USE_ESC_SEQ
	char escape_seq;
	char escape;
#endif
#if (defined(_ENDL_CRLF) || defined(_ENDL_LFCR))
	char tmpch;
#endif
#ifdef _USE_HISTORY
	ring_history_t ring_hist;
#endif
	char * prompt_str;
	char cmdline [_COMMAND_LINE_LEN];
	int cmdlen;
	int cursor;
	int (*execute) (int argc, const char * const * argv );
	char ** (*get_completion) (int argc, const char * const * argv );
	void (*print) (const char *);
#ifdef _USE_CTLR_C
	void (*sigint) (void);
#endif
} microrl_t;

/**
 * @brief Inicializa la instancia de microrl y asocia el callback de salida de texto.
 * @param pThis Puntero a la estructura microrl_t.
 * @param print Callback para enviar texto a la terminal.
 */
void microrl_init (microrl_t * pThis, void (*print)(const char*));

/**
 * @brief Configura el modo de eco (habilitado/deshabilitado).
 * @param echo Estado del eco.
 */
void microrl_set_echo (int echo);

/**
 * @brief Registra el callback de autocompletado al presionar TAB.
 * @param pThis          Puntero a la estructura microrl_t.
 * @param get_completion Funcion que devuelve posibles completados.
 */
void microrl_set_complete_callback (microrl_t * pThis, char ** (*get_completion)(int, const char* const*));

/**
 * @brief Registra el callback de ejecucion de comandos al presionar Enter.
 * @param pThis   Puntero a la estructura microrl_t.
 * @param execute Funcion que procesa (argc, argv).
 */
void microrl_set_execute_callback (microrl_t * pThis, int (*execute)(int, const char* const*));

/**
 * @brief Registra el callback invocado al presionar Ctrl+C.
 * @param pThis  Puntero a la estructura microrl_t.
 * @param sigintf Funcion callback para Ctrl+C.
 */
void microrl_set_sigint_callback (microrl_t * pThis, void (*sigintf)(void));

/**
 * @brief Procesa un caracter recibido desde el flujo de entrada de la terminal.
 * @param pThis Puntero a la estructura microrl_t.
 * @param ch    Caracter recibido.
 */
void microrl_insert_char (microrl_t * pThis, int ch);

#endif /* _MICRORL_H_ */
