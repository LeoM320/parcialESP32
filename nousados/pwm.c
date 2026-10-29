/**
 * @file pwm.c
 * @brief Implementacion de pwm.h con el driver MCPWM de ESP-IDF.
 *
 * Por cada pin: timer -> operador -> comparador + generador (igual que el
 * ejemplo del curso). Se guarda el comparador, el generador y el periodo
 * para poder cambiar el ciclo de trabajo despues.
 *
 * Como funciona:
 *   - el timer cuenta de 0 a "periodo" (en us) y vuelve a 0, una y otra vez;
 *   - al pasar por 0 el generador pone el pin en ALTO;
 *   - cuando la cuenta llega al valor del comparador, lo pone en BAJO.
 *   Entonces el tiempo en alto = valor del comparador (en us).
 *
 *        comparador
 *      ┌─────┐     ┌─────┐
 *      │     │     │     │
 *  ────┘     └─────┘     └──
 *      0   cmp  periodo
 */

#include "pwm.h"
#include "driver/mcpwm_prelude.h"       /* todo el driver MCPWM */

#define PWM_RESOLUCION_HZ   1000000     /* 1 MHz -> 1 tick = 1 us */
#define PWM_TIMERS_GRUPO    3           /* timers por grupo MCPWM en ESP32-S3 */
#define PWM_MAX_CANALES     (2 * PWM_TIMERS_GRUPO)  /* 2 grupos x 3 timers = 6 pines PWM */
#define PWM_PERIODO_MAX     65535       /* contador de 16 bits */

/* Lo que hay que recordar de cada pin PWM para cambiarle el duty despues */
typedef struct {
    gpio_num_t pin;
    mcpwm_cmpr_handle_t comparador;
    mcpwm_gen_handle_t generador;
    uint32_t periodo;                   /* en ticks de 1 us */
} canal_pwm_t;

static canal_pwm_t s_canales[PWM_MAX_CANALES];  /* los pines ya inicializados */
static int s_cant = 0;                          /* cuantos hay en s_canales */

/* Busca el canal de un pin; NULL si ese pin no paso por pwm_init */
static canal_pwm_t *buscar(gpio_num_t pin)
{
    for (int i = 0; i < s_cant; i++) {
        if (s_canales[i].pin == pin) {
            return &s_canales[i];       /* direccion del elemento, para modificarlo */
        }
    }
    return NULL;
}

/* Aplica el valor de comparacion. Con 0 el comparador y el cero del timer
 * coinciden, asi que se fuerza la salida en BAJO para tener 0 % limpio. */
static void aplicar(canal_pwm_t *c, uint32_t ticks)
{
    if (ticks > c->periodo) {
        ticks = c->periodo;             /* no mas de 100 % */
    }
    if (ticks == 0) {
        mcpwm_generator_set_force_level(c->generador, 0, true);     /* pin fijo en 0 */
    } else {
        mcpwm_comparator_set_compare_value(c->comparador, ticks);   /* nuevo tiempo en alto */
        mcpwm_generator_set_force_level(c->generador, -1, true);   /* quita el forzado */
    }
}

void pwm_init(gpio_num_t pin, uint32_t frecuencia_hz)
{
    /* Validaciones: si no se cumplen, ESP_ERROR_CHECK reinicia y muestra el error */
    ESP_ERROR_CHECK(s_cant < PWM_MAX_CANALES ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(frecuencia_hz > 0 ? ESP_OK : ESP_ERR_INVALID_ARG);

    uint32_t periodo = PWM_RESOLUCION_HZ / frecuencia_hz;  /* ej. 1000 Hz -> 1000 us; 50 Hz -> 20000 us */
    ESP_ERROR_CHECK(periodo >= 2 && periodo <= PWM_PERIODO_MAX ? ESP_OK : ESP_ERR_INVALID_ARG);
                                                            /* entre ~16 Hz y 500 kHz */

    canal_pwm_t *c = &s_canales[s_cant];    /* el proximo lugar libre */
    int grupo = s_cant / PWM_TIMERS_GRUPO;  /* canales 0..2 -> grupo 0, 3..5 -> grupo 1 */

    /* 1. Timer: la cuenta de 0 a periodo */
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t timer_cfg = {
        .group_id      = grupo,
        .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = PWM_RESOLUCION_HZ,
        .period_ticks  = periodo,
        .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,     /* cuenta para arriba */
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &timer));

    /* 2. Operador: une el timer con comparador y generador */
    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t oper_cfg = {
        .group_id = grupo,              /* tiene que ser el mismo grupo que el timer */
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_cfg, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    /* 3. Comparador: el valor donde baja la salida (el duty) */
    mcpwm_comparator_config_t cmp_cfg = {
        .flags.update_cmp_on_tez = true,    /* cambia el duty al inicio del periodo: sin glitches */
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &cmp_cfg, &c->comparador));

    /* 4. Generador: el que maneja el pin */
    mcpwm_generator_config_t gen_cfg = {
        .gen_gpio_num = pin,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gen_cfg, &c->generador));

    /* ALTO al llegar a cero, BAJO al coincidir con el comparador */
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(c->generador,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(c->generador,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, c->comparador, MCPWM_GEN_ACTION_LOW)));

    /* Guardar el canal para pwm_set_duty / pwm_set_pulso_us */
    c->pin = pin;
    c->periodo = periodo;
    s_cant++;

    aplicar(c, 0);                      /* arranca en 0 % */

    /* 5. Arrancar el timer */
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));  /* corre sin parar */
}

void pwm_set_duty(gpio_num_t pin, float porcentaje)
{
    canal_pwm_t *c = buscar(pin);
    if (c == NULL) {
        return;                         /* ese pin no es PWM: no hace nada */
    }
    /* Limitar a 0..100 */
    if (porcentaje < 0.0f) {
        porcentaje = 0.0f;
    } else if (porcentaje > 100.0f) {
        porcentaje = 100.0f;
    }
    /* % -> us en alto. El + 0.5f redondea al pasar de float a entero */
    aplicar(c, (uint32_t)(c->periodo * porcentaje / 100.0f + 0.5f));
}

void pwm_set_pulso_us(gpio_num_t pin, uint32_t us)
{
    canal_pwm_t *c = buscar(pin);
    if (c == NULL) {
        return;
    }
    aplicar(c, us);     /* 1 tick = 1 us */
}
