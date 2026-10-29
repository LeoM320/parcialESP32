/**
 * @file config.h
 * @brief Configuracion de la libreria microrl (largo de linea, tokens, prompt e historial).
 */

#ifndef _MICRORL_CONFIG_H_
#define _MICRORL_CONFIG_H_

#define MICRORL_LIB_VER "1.5.1"                         ///< Version de la libreria microrl.
#define _COMMAND_LINE_LEN (1 + 100)                     ///< Largo maximo de linea (100 caracteres + '\0').
#define _COMMAND_TOKEN_NMB 8                            ///< Cantidad maxima de palabras por comando.
#define _PROMPT_DEFAULT "\033[32mESP32>\033[0m "        ///< Prompt en color verde ANSI.
#define _PROMPT_LEN 7                                   ///< Largo visible del prompt ("ESP32> ").
#define _USE_COMPLETE                                   ///< Habilita soporte de autocompletado con TAB.
#define _USE_HISTORY                                    ///< Habilita historial circular con flechas arriba/abajo.
#define _RING_HISTORY_LEN 64                            ///< Tamanio del buffer circular de historial en bytes.
#define _USE_ESC_SEQ                                    ///< Habilita secuencias de escape ANSI.
#define _USE_LIBC_STDIO                                 ///< Usa snprintf de la libreria estandar de C.
#define _USE_CTLR_C                                     ///< Habilita callback para Ctrl+C.
#undef _ENABLE_INIT_PROMPT                              ///< No imprime prompt en microrl_init.
#define _ENDL_CR                                        ///< Fin de linea al recibir '\r'.
#define ENDL "\r\n"                                     ///< Secuencia de salto de linea enviada a la terminal.

#if _RING_HISTORY_LEN > 256
#error "This history implementation (ring buffer with 1 byte iterator) allow 256 byte buffer size maximum"
#endif

#endif /* _MICRORL_CONFIG_H_ */
