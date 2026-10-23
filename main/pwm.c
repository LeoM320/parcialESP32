/**
 * @file pwm.c
 * @brief Implementacion de pwm.h con el driver MCPWM de ESP-IDF.
 *
 * Por cada pin: timer -> operador -> comparador + generador (igual que el
 * ejemplo del curso). Se guarda el comparador, el generador y el periodo
 * para poder cambiar el ciclo de trabajo despues.
 */

#include "pwm.h"
#include "driver/mcpwm_prelude.h"

#define PWM_RESOLUCION_HZ   1000000     /* 1 MHz -> 1 tick = 1 us */
#define PWM_TIMERS_GRUPO    3           /* timers por grupo MCPWM en ESP32-S3 */
#define PWM_MAX_CANALES     (2 * PWM_TIMERS_GRUPO)
#define PWM_PERIODO_MAX     65535       /* contador de 16 bits */

typedef struct {
    gpio_num_t pin;
    mcpwm_cmpr_handle_t comparador;
    mcpwm_gen_handle_t generador;
    uint32_t periodo;                   /* en ticks de 1 us */
} canal_pwm_t;

static canal_pwm_t s_canales[PWM_MAX_CANALES];
static int s_cant = 0;

static canal_pwm_t *buscar(gpio_num_t pin)
{
    for (int i = 0; i < s_cant; i++) {
        if (s_canales[i].pin == pin) {
            return &s_canales[i];
        }
    }
    return NULL;
}

/* Aplica el valor de comparacion. Con 0 el comparador y el cero del timer
 * coinciden, asi que se fuerza la salida en BAJO para tener 0 % limpio. */
static void aplicar(canal_pwm_t *c, uint32_t ticks)
{
    if (ticks > c->periodo) {
        ticks = c->periodo;
    }
    if (ticks == 0) {
        mcpwm_generator_set_force_level(c->generador, 0, true);
    } else {
        mcpwm_comparator_set_compare_value(c->comparador, ticks);
        mcpwm_generator_set_force_level(c->generador, -1, true);   /* quita el forzado */
    }
}

void pwm_init(gpio_num_t pin, uint32_t frecuencia_hz)
{
    ESP_ERROR_CHECK(s_cant < PWM_MAX_CANALES ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(frecuencia_hz > 0 ? ESP_OK : ESP_ERR_INVALID_ARG);

    uint32_t periodo = PWM_RESOLUCION_HZ / frecuencia_hz;
    ESP_ERROR_CHECK(periodo >= 2 && periodo <= PWM_PERIODO_MAX ? ESP_OK : ESP_ERR_INVALID_ARG);

    canal_pwm_t *c = &s_canales[s_cant];
    int grupo = s_cant / PWM_TIMERS_GRUPO;

    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t timer_cfg = {
        .group_id      = grupo,
        .clk_src       = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = PWM_RESOLUCION_HZ,
        .period_ticks  = periodo,
        .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &timer));

    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t oper_cfg = {
        .group_id = grupo,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_cfg, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    mcpwm_comparator_config_t cmp_cfg = {
        .flags.update_cmp_on_tez = true,    /* cambia el duty al inicio del periodo: sin glitches */
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &cmp_cfg, &c->comparador));

    mcpwm_generator_config_t gen_cfg = {
        .gen_gpio_num = pin,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &gen_cfg, &c->generador));

    /* ALTO al llegar a cero, BAJO al coincidir con el comparador */
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(c->generador,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(c->generador,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, c->comparador, MCPWM_GEN_ACTION_LOW)));

    c->pin = pin;
    c->periodo = periodo;
    s_cant++;

    aplicar(c, 0);

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
}

void pwm_set_duty(gpio_num_t pin, float porcentaje)
{
    canal_pwm_t *c = buscar(pin);
    if (c == NULL) {
        return;
    }
    if (porcentaje < 0.0f) {
        porcentaje = 0.0f;
    } else if (porcentaje > 100.0f) {
        porcentaje = 100.0f;
    }
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
