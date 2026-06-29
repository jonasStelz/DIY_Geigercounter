/*
 * geiger_core.c  –  1-second measurement tick, CPM ring buffer, event posting.
 *
 * Every second:
 *   1. Read hardware pulse counter delta.
 *   2. Feed delta to statistics (rolling CPM, EMA, lifetime counts).
 *   3. Post APP_EVENT_PULSE if delta > 0.
 *   4. Check alarm threshold and post APP_EVENT_ALARM_TRIGGER or
 *      APP_EVENT_ALARM_CLEAR as needed.
 *
 * Alarm logic:
 *   - Reads alarm mode and threshold from ui_menu getters.
 *   - CPM mode:   rolling CPM  vs threshold
 *   - µSv/h mode: EMA value    vs threshold
 *   - Fires APP_EVENT_ALARM_TRIGGER every second while above threshold.
 *   - Fires APP_EVENT_ALARM_CLEAR once when value drops back below threshold.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"

#include "app_types.h"
#include "geiger_config.h"
#include "pulse_counter.h"
#include "statistics.h"
#include "ui_menu.h"
#include "geiger_core.h"

static const char *TAG = "GEIGER";

static bool s_was_alarming = false;

/* ── Public API ─────────────────────────────────────────────────────────── */

esp_err_t geiger_core_init(void)
{
    esp_err_t ret = pulse_counter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pulse_counter_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = statistics_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "statistics_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}

void geiger_core_task(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();
    ESP_LOGI(TAG, "task started");

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(GEIGER_CORE_TICK_MS));

        /* 1. Read pulse delta and update statistics */
        uint32_t delta = pulse_counter_read_and_reset();
        statistics_record_counts(delta);

        /* 2. Notify buzzer/UI of new pulses */
        if (delta > 0) {
            esp_err_t err = esp_event_post(APP_EVENTS,
                                           APP_EVENT_PULSE,
                                           &delta, sizeof(delta),
                                           pdMS_TO_TICKS(10));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "event_post PULSE failed: %s", esp_err_to_name(err));
            }
        }

        /* 3. Alarm check
         *
         * Read the current alarm configuration and measurement values,
         * then decide whether to fire ALARM_TRIGGER or ALARM_CLEAR.
         *
         * ALARM_TRIGGER is posted every second while above threshold so
         * the buzzer keeps sounding and the UI keeps blinking.
         * ALARM_CLEAR is posted once when the value drops below threshold,
         * resetting the acknowledged state so the alarm can fire again. */
        alarm_mode_t mode = ui_menu_get_alarm_mode();
        bool alarming     = false;

        if (mode == ALARM_MODE_CPM) {
            geiger_data_t snap;
            statistics_get_snapshot(&snap);
            alarming = (snap.cpm >= ui_menu_get_alarm_threshold_cpm());

        } else if (mode == ALARM_MODE_USVH) {
            geiger_data_t snap;
            statistics_get_snapshot(&snap);
            alarming = (snap.usvh_ema >= ui_menu_get_alarm_threshold_usvh());
        }
        /* ALARM_MODE_OFF: alarming stays false */

        if (alarming) {
            app_events_post(APP_EVENT_ALARM_TRIGGER);
            if (!s_was_alarming) {
                ESP_LOGW(TAG, "ALARM triggered (mode=%d)", (int)mode);
            }
        } else if (s_was_alarming) {
            app_events_post(APP_EVENT_ALARM_CLEAR);
            ESP_LOGI(TAG, "ALARM cleared");
        }

        s_was_alarming = alarming;
    }
}

void geiger_core_get_data(geiger_data_t *out)
{
    statistics_get_snapshot(out);
}
