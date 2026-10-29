/**
 * @file temporizador.c
 * @brief Implementacion de temporizador.h con el driver gptimer de ESP-IDF.
 *
 * gptimer llama a su callback con su propia firma; aca se guarda la ISR del
 * usuario y su arg, y un callback intermedio la llama.
 *
 * El timer de hardware cuenta a 1 MHz; al llegar a periodo_us dispara la
 * interrupcion y vuelve solo a 0 (periodico).
 */

#include "temporizador.h"
#include "esp_attr.h"               /* IRAM_ATTR */
#include "esp_err.h"                /* ESP_ERROR_CHECK */
#include "driver/gptimer.h"         /* driver del timer de uso general */

#define RESOLUCION_HZ   1000000     /* 1 MHz: 1 cuenta = 1 us */

/* Lo que paso el usuario en temporizador_init */
typedef struct {
    temporizador_isr_t isr;         /* su funcion */
    void *arg;                      /* su argumento */
} usuario_t;

static usuario_t s_usuarios[TEMPORIZADOR_MAX];  /* uno por timer creado */
static int s_cant = 0;

/* Callback de gptimer (en interrupcion): llama a la ISR del usuario.
 * IRAM_ATTR: el codigo queda en RAM interna, asi corre aunque la flash este ocupada.
 * ctx es el puntero que se le paso a gptimer_register_event_callbacks (el usuario_t).
 * Devuelve lo que devuelve la ISR: true = desperto una tarea de mas prioridad. */
static bool IRAM_ATTR al_vencer(gptimer_handle_t timer, const gptimer_alarm_event_data_t *evento,
                                void *ctx)
{
    usuario_t *u = ctx;
    return u->isr(u->arg);
}

void temporizador_init(uint32_t periodo_us, temporizador_isr_t isr, void *arg)
{
    if (s_cant >= TEMPORIZADOR_MAX) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);        /* no hay mas lugar: error */
    }
    usuario_t *u = &s_usuarios[s_cant++];       /* toma el proximo lugar y avanza el contador */
    u->isr = isr;
    u->arg = arg;

    /* 1. Crear el timer */
    gptimer_handle_t timer;
    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,      /* cuenta para arriba */
        .resolution_hz = RESOLUCION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));

    /* 2. Que llame a al_vencer(.., u) en cada alarma */
    gptimer_event_callbacks_t callbacks = { .on_alarm = al_vencer };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, u));
    ESP_ERROR_CHECK(gptimer_enable(timer));

    /* 3. La alarma: a las periodo_us cuentas, interrumpir y volver a 0 */
    gptimer_alarm_config_t alarma = {
        .alarm_count = periodo_us,              /* 1 cuenta = 1 us */
        .reload_count = 0,                      /* valor al que vuelve */
        .flags.auto_reload_on_alarm = true,     /* vuelve a 0 solo: periodico */
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarma));

    /* 4. Arrancar */
    ESP_ERROR_CHECK(gptimer_start(timer));
}
