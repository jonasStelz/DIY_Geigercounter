/*
 * geiger_core.c  –  1-second measurement tick, CPM ring buffer, event posting.
 *
 * Responsibilities:
 *   - Calls pulse_counter_read_and_reset() every GEIGER_CORE_TICK_MS (1 s).
 *   - Feeds the pulse delta to statistics_record_counts().
 *   - Posts APP_EVENT_PULSE to the default event loop when delta > 0,
 *     carrying the delta count as event data (uint32_t).
 *     The buzzer task subscribes to this event and produces a click per pulse.
 *   - Provides geiger_core_get_data() as a thread-safe snapshot for any task.
 *
 * Timing:
 *   vTaskDelayUntil ensures a stable 1-second period regardless of how long
 *   the loop body takes. Drift accumulation is negligible for this use-case.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"

#include "geiger_core.h"
#include "geiger_config.h"
#include "pulse_counter.h"
#include "statistics.h"

static const char *TAG = "GEIGER_CORE";

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t geiger_core_init(void)
{
    esp_err_t ret;

    ret = pulse_counter_init();
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
        /* Precise 1-second period; compensates for loop execution time. */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(GEIGER_CORE_TICK_MS));

        /* Read and atomically reset the hardware pulse counter. */
        uint32_t delta = pulse_counter_read_and_reset();

        /* Feed the 1-second bucket to the statistics component. */
        statistics_record_counts(delta);

        /* Notify the buzzer (and any other subscribers) of new pulses.
         * The delta count is carried as event data so the buzzer can
         * produce one click per actual pulse. */
        if (delta > 0) {
            esp_err_t err = esp_event_post(APP_EVENTS,
                                           APP_EVENT_PULSE,
                                           &delta, sizeof(delta),
                                           pdMS_TO_TICKS(10));
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "event_post failed: %s", esp_err_to_name(err));
            }
        }
    }
}

void geiger_core_get_data(geiger_data_t *out)
{
    /* Delegate entirely to statistics – all state lives there. */
    statistics_get_snapshot(out);
}
