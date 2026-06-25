/*
 * pulse_counter.c  –  ESP32-S3 PCNT-based Geiger tube pulse counter.
 *
 * Hardware:
 *   SBM-20 tube → open-collector NPN → GPIO1 pull-up (active-LOW, falling edge)
 *
 * Architecture:
 *   PCNT hardware counts falling edges autonomously.
 *   The CPU may enter Light Sleep between calls; PCNT keeps counting.
 *   A software accumulator extends the 16-bit hardware counter to 32 bits.
 *   A PCNT watchpoint at PCNT_HIGH_LIMIT (32 767) fires an ISR on overflow.
 *
 * Overflow accuracy:
 *   After a hardware overflow the counter wraps to low_limit (-1), not 0.
 *   The transient -1 state is clamped to 0 in pulse_counter_read_and_reset().
 *   Resulting error: ≤ 1 count per 32 767 counts (~0.003 %) – negligible.
 *
 * Thread safety:
 *   s_overflow_accum is protected by a portMUX spinlock shared between
 *   the PCNT ISR (portENTER_CRITICAL_ISR) and task context (portENTER_CRITICAL).
 *   pcnt_unit_get_count() / pcnt_unit_clear_count() are NOT IRAM_ATTR and
 *   must be called outside the critical section. The brief window between
 *   the two calls is ~microseconds; any pulses in that window are counted
 *   in the next 1-second interval.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

#include "pulse_counter.h"
#include "geiger_config.h"

static const char *TAG = "PULSE_CTR";

/* ── Module state ─────────────────────────────────────────────────────────── */

static pcnt_unit_handle_t    s_pcnt_unit = NULL;
static pcnt_channel_handle_t s_pcnt_chan = NULL;

/* 32-bit software accumulator – updated in the watchpoint ISR. */
static volatile uint32_t s_overflow_accum = 0;

/* Spinlock shared between ISR and task context. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* ── Overflow watchpoint ISR ──────────────────────────────────────────────── */

static bool IRAM_ATTR pcnt_on_reach(pcnt_unit_handle_t             unit,
                                     const pcnt_watch_event_data_t *edata,
                                     void                          *user_ctx)
{
    (void)unit;
    (void)edata;
    (void)user_ctx;

    /*
     * Counter reached PCNT_HIGH_LIMIT (32 767) and will wrap to
     * PCNT_LOW_LIMIT (-1) on the next pulse. Record the high-limit
     * count in the accumulator.
     */
    portENTER_CRITICAL_ISR(&s_mux);
    s_overflow_accum += (uint32_t)PCNT_HIGH_LIMIT;
    portEXIT_CRITICAL_ISR(&s_mux);

    return false;   /* no high-priority task wake needed */
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t pulse_counter_init(void)
{
    esp_err_t ret;

    /* 1. PCNT unit ──────────────────────────────────────────────────────── */
    pcnt_unit_config_t unit_cfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,   /* -1: minimum required by API */
        .flags = {
            .accum_count = 0,           /* manual accumulation via watchpoint */
        },
    };
    ret = pcnt_new_unit(&unit_cfg, &s_pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pcnt_new_unit: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Glitch filter (APB-clocked; does not operate during Light Sleep) ─ */
    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = PCNT_GLITCH_FILTER_NS,
    };
    ret = pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_glitch_filter: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. Channel on GPIO_GEIGER_PULSE ──────────────────────────────────── */
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = GPIO_GEIGER_PULSE,
        .level_gpio_num = -1,           /* no level-control GPIO needed */
    };
    ret = pcnt_new_channel(s_pcnt_unit, &chan_cfg, &s_pcnt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pcnt_new_channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 4. Edge / level actions ───────────────────────────────────────────── */
    /*
     * Geiger pulse: active-LOW open-collector → count on falling edge.
     * Level action: KEEP so count direction stays fixed at +1.
     */
    pcnt_channel_set_edge_action(s_pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_HOLD,       /* rising  edge → hold    */
        PCNT_CHANNEL_EDGE_ACTION_INCREASE);  /* falling edge → count+1 */

    pcnt_channel_set_level_action(s_pcnt_chan,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP);

    /* 5. Overflow watchpoint ────────────────────────────────────────────── */
    ret = pcnt_unit_add_watch_point(s_pcnt_unit, PCNT_HIGH_LIMIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add_watch_point: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 6. ISR callback ───────────────────────────────────────────────────── */
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_on_reach,
    };
    ret = pcnt_unit_register_event_callbacks(s_pcnt_unit, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_event_callbacks: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 7. Enable, clear, start ───────────────────────────────────────────── */
    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

    ESP_LOGI(TAG, "ready – GPIO%d falling-edge, high_limit=%d, glitch=%dns",
             GPIO_GEIGER_PULSE, PCNT_HIGH_LIMIT, PCNT_GLITCH_FILTER_NS);
    return ESP_OK;
}

uint32_t pulse_counter_read_and_reset(void)
{
    /*
     * Operation order to minimise the race window:
     *   1. pcnt_unit_get_count()   – snapshot hardware counter now.
     *   2. Critical section        – atomically read + zero s_overflow_accum.
     *      Any overflow ISR firing between (1) and (2) is captured here. ✓
     *   3. pcnt_unit_clear_count() – reset hardware counter.
     *      Pulses arriving between (1) and (3) are counted next call. ✓
     */
    int hw_raw = 0;
    pcnt_unit_get_count(s_pcnt_unit, &hw_raw);

    portENTER_CRITICAL(&s_mux);
    uint32_t accum   = s_overflow_accum;
    s_overflow_accum = 0;
    portEXIT_CRITICAL(&s_mux);

    pcnt_unit_clear_count(s_pcnt_unit);

    /* hw_raw is in [PCNT_LOW_LIMIT, PCNT_HIGH_LIMIT] = [-1, 32767].
     * Clamp the transient -1 wrap state to 0. */
    uint32_t hw = (hw_raw > 0) ? (uint32_t)hw_raw : 0u;
    return accum + hw;
}
