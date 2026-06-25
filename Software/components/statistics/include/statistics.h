#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "app_types.h"

/*
 * statistics – Rolling CPM buffer, dose history, and NVS persistence.
 *
 * Rolling CPM:
 *   60-element ring buffer; one bucket per second.
 *   CPM = sum(last 60 buckets).
 *   No extrapolation for short measurements.
 *
 * Dose rate:
 *   usvh_raw = cpm / GEIGER_TUBE_CPM_PER_USVH
 *   usvh_ema updated with DOSE_EMA_ALPHA each tick.
 *
 * Persistence:
 *   RTC_DATA_ATTR for survival across Light Sleep.
 *   NVS for survival across power loss:
 *     - Every NVS_SYNC_INTERVAL_COUNTS pulses
 *     - Every NVS_SYNC_INTERVAL_S seconds
 *     - On explicit statistics_sync_nvs() call (shutdown)
 */

/* Initialise ring buffer; load lifetime_counts / max values from NVS. */
esp_err_t statistics_init(void);

/*
 * Record a new delta of pulse counts from one GEIGER_CORE_TICK_MS interval.
 * Updates ring buffer, lifetime counter, EMA, and max tracking.
 * Called by geiger_core_task once per second.
 */
void statistics_record_counts(uint32_t delta);

/* Fill *out with a consistent snapshot (briefly locks internal mutex). */
void statistics_get_snapshot(geiger_data_t *out);

/* Force an NVS write of lifetime_counts, max_cpm, max_usvh. */
esp_err_t statistics_sync_nvs(void);
