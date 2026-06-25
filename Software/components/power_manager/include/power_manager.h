#pragma once

#include "esp_err.h"

/*
 * power_manager – ESP-IDF power management configuration and PM lock helpers.
 *
 * Configures:
 *   - Tickless Idle (FreeRTOS)
 *   - Dynamic Frequency Scaling (DFS)
 *   - Automatic Light Sleep
 *
 * Provides a single NO_LIGHT_SLEEP lock used by the buzzer component
 * to prevent clock gating during PWM output.
 *
 * All other components that need to prevent sleep acquire this lock
 * through this API rather than managing their own esp_pm handles.
 */

/* Apply pm_config and create the shared NO_LIGHT_SLEEP lock handle. */
esp_err_t power_manager_init(void);

/* Acquire / release the NO_LIGHT_SLEEP lock (reference-counted). */
void power_manager_acquire_no_sleep(void);
void power_manager_release_no_sleep(void);
