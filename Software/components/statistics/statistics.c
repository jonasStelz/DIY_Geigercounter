/*
 * statistics.c  –  Rolling CPM buffer, dose rate, EMA, lifetime tracking.
 *
 * Called by geiger_core_task once per second with the pulse delta.
 * Read by ui_menu_task (any frequency) via statistics_get_snapshot().
 *
 * Concurrency:
 *   A FreeRTOS mutex serialises the single writer (geiger_core_task, prio 6)
 *   and any readers (ui_menu_task, prio 4). Priority inheritance prevents
 *   inversion.
 *
 * Persistence:
 *   s_lifetime_counts, s_max_cpm, s_max_usvh are RTC_DATA_ATTR:
 *     → survive Light Sleep automatically.
 *     → reset to 0 on cold boot (NVS restore added in Step 9).
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "statistics.h"
#include "geiger_config.h"

static const char *TAG = "STATISTICS";

/* ── RTC-backed variables (survive Light Sleep) ──────────────────────────── */
RTC_DATA_ATTR static uint64_t s_lifetime_counts = 0;
RTC_DATA_ATTR static uint32_t s_max_cpm         = 0;
RTC_DATA_ATTR static float    s_max_usvh        = 0.0f;

/* ── Session state (reset on every boot) ─────────────────────────────────── */
static uint32_t s_ring_buf[CPM_RING_BUFFER_SIZE]; /* one bucket per second  */
static uint8_t  s_ring_idx  = 0;                   /* next write position    */
static uint32_t s_ring_sum  = 0;                   /* running sum of buckets */

static float    s_usvh_ema  = 0.0f;
static uint32_t s_uptime_s  = 0;
static uint64_t s_session_count_sum = 0;           /* for avg CPM            */

/* NVS sync counters (thresholds from geiger_config.h, Step 9) */
static uint32_t s_counts_since_sync  = 0;
static uint32_t s_seconds_since_sync = 0;

static SemaphoreHandle_t s_mutex = NULL;

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t statistics_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_ring_buf, 0, sizeof(s_ring_buf));
    s_ring_idx          = 0;
    s_ring_sum          = 0;
    s_usvh_ema          = 0.0f;
    s_uptime_s          = 0;
    s_session_count_sum = 0;
    s_counts_since_sync = 0;
    s_seconds_since_sync= 0;

    /* NVS load of s_lifetime_counts, s_max_cpm, s_max_usvh – Step 9. */
    ESP_LOGI(TAG, "ready (lifetime=%llu, max_cpm=%lu)",
             (unsigned long long)s_lifetime_counts, (unsigned long)s_max_cpm);
    return ESP_OK;
}

void statistics_record_counts(uint32_t delta)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "record_counts: mutex timeout");
        return;
    }

    /* ── Ring buffer update ─────────────────────────────────────────────── */
    s_ring_sum -= s_ring_buf[s_ring_idx];   /* evict oldest bucket          */
    s_ring_buf[s_ring_idx] = delta;         /* insert new bucket            */
    s_ring_sum += delta;
    s_ring_idx = (uint8_t)((s_ring_idx + 1u) % CPM_RING_BUFFER_SIZE);

    /* ── Counters ───────────────────────────────────────────────────────── */
    s_lifetime_counts   += delta;
    s_session_count_sum += delta;
    s_uptime_s++;

    /* ── Dose rate (raw + EMA) ───────────────────────────────────────────── */
    float usvh_raw = (float)s_ring_sum / GEIGER_TUBE_CPM_PER_USVH;
    s_usvh_ema = DOSE_EMA_ALPHA * usvh_raw
               + (1.0f - DOSE_EMA_ALPHA) * s_usvh_ema;

    /* ── Historical maxima ───────────────────────────────────────────────── */
    if (s_ring_sum  > s_max_cpm)  s_max_cpm  = s_ring_sum;
    if (s_usvh_ema  > s_max_usvh) s_max_usvh = s_usvh_ema;

    /* ── NVS sync trigger (write implemented in Step 9) ─────────────────── */
    s_counts_since_sync  += delta;
    s_seconds_since_sync++;
    if (s_counts_since_sync  >= NVS_SYNC_INTERVAL_COUNTS ||
        s_seconds_since_sync >= NVS_SYNC_INTERVAL_S) {
        s_counts_since_sync  = 0;
        s_seconds_since_sync = 0;
        /* statistics_sync_nvs() called here in Step 9 */
    }

    xSemaphoreGive(s_mutex);
}

void statistics_get_snapshot(geiger_data_t *out)
{
    if (!out) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "get_snapshot: mutex timeout");
        return;
    }

    out->cpm             = s_ring_sum;
    out->usvh_raw        = (float)s_ring_sum / GEIGER_TUBE_CPM_PER_USVH;
    out->usvh_ema        = s_usvh_ema;
    out->lifetime_counts = s_lifetime_counts;
    out->max_cpm         = s_max_cpm;
    out->max_usvh        = s_max_usvh;
    out->uptime_s        = s_uptime_s;

    /* avg_cpm = total counts / elapsed seconds × 60 */
    out->avg_cpm  = (s_uptime_s > 0)
                    ? ((float)s_session_count_sum * 60.0f / (float)s_uptime_s)
                    : 0.0f;
    out->avg_usvh = out->avg_cpm / GEIGER_TUBE_CPM_PER_USVH;

    xSemaphoreGive(s_mutex);
}

esp_err_t statistics_sync_nvs(void)
{
    /* NVS write of s_lifetime_counts, s_max_cpm, s_max_usvh – Step 9. */
    return ESP_OK;
}
