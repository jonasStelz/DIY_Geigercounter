#pragma once

#include "esp_err.h"
#include "app_types.h"

/*
 * geiger_core – 1-second measurement tick, CPM ring buffer, dose calculation.
 *
 * Responsibilities:
 *   - Calls pulse_counter_read_and_reset() every GEIGER_CORE_TICK_MS.
 *   - Feeds pulse deltas into statistics (ring buffer, lifetime counter).
 *   - Computes rolling CPM and EMA-filtered µSv/h.
 *   - Posts APP_EVENT_PULSE to the event loop on each non-zero tick.
 *
 * Priority: TASK_PRIO_GEIGER_CORE (highest application task).
 */

esp_err_t geiger_core_init(void);
void      geiger_core_task(void *arg);

/* Thread-safe snapshot of latest measurement data. */
void geiger_core_get_data(geiger_data_t *out);
