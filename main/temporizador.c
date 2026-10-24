/**
 * @file temporizador.c
 * @brief Implementacion de temporizador.h con el driver gptimer de ESP-IDF.
 *
 * gptimer llama a su callback con su propia firma; aca se guarda la ISR del
 * usuario y su arg, y un callback intermedio la llama.
 */

#include "temporizador.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "driver/gptimer.h"

#define RESOLUCION_HZ   1000000     /* 1 MHz: 1 cuenta = 1 us */

typedef struct {
    temporizador_isr_t isr;
    void *arg;
} usuario_t;

static usuario_t s_usuarios[TEMPORIZADOR_MAX];
static int s_cant = 0;

/* Callback de gptimer (en interrupcion): llama a la ISR del usuario */
static bool IRAM_ATTR al_vencer(gptimer_handle_t timer, const gptimer_alarm_event_data_t *evento,
                                void *ctx)
{
    usuario_t *u = ctx;
    return u->isr(u->arg);
}

void temporizador_init(uint32_t periodo_us, temporizador_isr_t isr, void *arg)
{
    if (s_cant >= TEMPORIZADOR_MAX) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    usuario_t *u = &s_usuarios[s_cant++];
    u->isr = isr;
    u->arg = arg;

    gptimer_handle_t timer;
    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = RESOLUCION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));

    gptimer_event_callbacks_t callbacks = { .on_alarm = al_vencer };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, u));
    ESP_ERROR_CHECK(gptimer_enable(timer));

    gptimer_alarm_config_t alarma = {
        .alarm_count = periodo_us,              /* 1 cuenta = 1 us */
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,     /* vuelve a 0 solo: periodico */
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarma));
    ESP_ERROR_CHECK(gptimer_start(timer));
}
