/*
 * buzzer.c  –  Passive piezo buzzer driver via LEDC PWM.
 *
 * Hardware
 * ────────
 *   GPIO11 → NPN transistor base → piezo between collector and VCC.
 *   HIGH = transistor conducting = piezo active.
 *   The LEDC peripheral generates the square wave; the transistor
 *   amplifies it to drive the piezo element.
 *
 * Architecture
 * ────────────
 *   APP_EVENT_PULSE fires the event handler which posts to a
 *   depth-1 queue using xQueueOverwrite().  If the previous click
 *   has not finished yet, the old entry is replaced – this prevents
 *   audio lag from building up at high count rates.
 *
 *   buzzer_task() dequeues and plays each click:
 *     1. Check ui_menu_buzzer_enabled()  – skip if muted
 *     2. Acquire ESP_PM_NO_LIGHT_SLEEP lock  (Step 8 – stub for now)
 *     3. Set LEDC duty to 50 %  →  piezo starts clicking
 *     4. vTaskDelay(BUZZER_CLICK_MS)
 *     5. Set LEDC duty to 0    →  GPIO returns LOW
 *     6. Release PM lock
 *
 * LEDC configuration (from geiger_config.h)
 * ──────────────────────────────────────────
 *   Timer    LEDC_TIMER_0       (BUZZER_LEDC_TIMER_NUM)
 *   Channel  LEDC_CHANNEL_0     (BUZZER_LEDC_CHANNEL_NUM)
 *   Freq     750 Hz             (BUZZER_FREQ_HZ)
 *   Res      13 bit             (BUZZER_DUTY_RES_BITS)
 *   Duty50   4096               (BUZZER_DUTY_50_PCT)
 *   Click    10 ms              (BUZZER_CLICK_MS)
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_event.h"

/* PM lock – activated in Step 8.  Stub: include but do not acquire/release. */
/* #include "esp_pm.h" */

#include "app_types.h"
#include "geiger_config.h"
#include "ui_menu.h"
#include "buzzer.h"

static const char *TAG = "BUZ";

/* Tokens pushed to the queue – distinguish click from alarm */
#define CLICK_TOKEN  1u
#define ALARM_TOKEN  2u

static QueueHandle_t s_queue = NULL;

/*
 * PM lock handle – uncomment and initialise in buzzer_init() once
 * Step 8 (Power Optimization) adds esp_pm support.
 *
 * static esp_pm_lock_handle_t s_pm_lock = NULL;
 */

/* ── LEDC helpers ─────────────────────────────────────────────────────── */

static void ledc_setup(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = (ledc_timer_bit_t)BUZZER_DUTY_RES_BITS,
        .timer_num       = (ledc_timer_t)BUZZER_LEDC_TIMER_NUM,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num   = GPIO_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = (ledc_channel_t)BUZZER_LEDC_CHANNEL_NUM,
        .timer_sel  = (ledc_timer_t)BUZZER_LEDC_TIMER_NUM,
        .duty       = 0,    /* start silent */
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ledc_channel_config(&channel);
}

static void ledc_click_start(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE,
                  (ledc_channel_t)BUZZER_LEDC_CHANNEL_NUM,
                  BUZZER_DUTY_50_PCT);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,
                     (ledc_channel_t)BUZZER_LEDC_CHANNEL_NUM);
}

static void ledc_click_stop(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE,
                  (ledc_channel_t)BUZZER_LEDC_CHANNEL_NUM,
                  0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,
                     (ledc_channel_t)BUZZER_LEDC_CHANNEL_NUM);
}

/* ── Event handler ────────────────────────────────────────────────────── */

static void on_pulse(void *arg, esp_event_base_t base,
                     int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    uint8_t token = CLICK_TOKEN;
    /*
     * xQueueOverwrite: depth-1 queue always accepts the write.
     * If a click is already pending the old token is replaced – no lag.
     */
    xQueueOverwrite(s_queue, &token);
}


static void on_alarm_trigger(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    /* Skip if user acknowledged or buzzer is muted */
    if (ui_menu_is_alarm_acknowledged() || !ui_menu_buzzer_enabled()) {
        return;
    }

    uint8_t token = ALARM_TOKEN;
    xQueueOverwrite(s_queue, &token);
}

/* ── Public API ───────────────────────────────────────────────────────── */

esp_err_t buzzer_init(void)
{
    s_queue = xQueueCreate(1, sizeof(uint8_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    /*
     * PM lock – create here, acquire/release per click.
     * Uncomment once Step 8 adds power management:
     *
     * ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0,
     *                                    "buzzer", &s_pm_lock));
     */

    ledc_setup();

    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_PULSE,         on_pulse,         NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_ALARM_TRIGGER, on_alarm_trigger, NULL));

    ESP_LOGI(TAG, "initialized  GPIO%d  %d Hz  %d ms click",
             GPIO_BUZZER, BUZZER_FREQ_HZ, BUZZER_CLICK_MS);
    return ESP_OK;
}

void buzzer_task(void *arg)
{
    (void)arg;

    uint8_t token;

    while (1) {
        /* Block until a click is requested */
        if (xQueueReceive(s_queue, &token, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Honour the mute setting from the Audio & Wi-Fi screen */
        if (!ui_menu_buzzer_enabled()) {
            continue;
        }

        /*
         * PM lock – prevent Light Sleep while LEDC clock is active.
         * Uncomment once Step 8 adds power management:
         *
         * esp_pm_lock_acquire(s_pm_lock);
         */

        if (token == ALARM_TOKEN) {
            /* Alarm tone: 3 rapid beeps (10 ms on / 90 ms off × 3) */
            for (int i = 0; i < 3; i++) {
                ledc_click_start();
                vTaskDelay(pdMS_TO_TICKS(BUZZER_CLICK_MS));   /* 10 ms */
                ledc_click_stop();
                vTaskDelay(pdMS_TO_TICKS(90));                 /* 90 ms gap */
            }
        } else {
            /* Normal click: single 10 ms tone */
            ledc_click_start();
            vTaskDelay(pdMS_TO_TICKS(BUZZER_CLICK_MS));
            ledc_click_stop();
        }

        /*
         * esp_pm_lock_release(s_pm_lock);
         */

        ESP_LOGD(TAG, "click");
    }
}
