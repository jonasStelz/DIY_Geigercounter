#pragma once

#include "esp_err.h"

/*
 * buzzer – LEDC PWM driver for the passive piezo element.
 *
 * The Geiger core never touches LEDC hardware directly.
 * Event-driven flow:
 *
 *   geiger_core_task  →  APP_EVENT_PULSE  →  buzzer_task  →  LEDC click
 *
 * Light Sleep compatibility:
 *   Before enabling LEDC output:  acquire ESP_PM_NO_LIGHT_SLEEP lock.
 *   After click completes:        release lock.
 *   (Prevents LEDC clock gating during sound generation.)
 *
 * Buzzer can be globally muted via buzzer_set_enabled().
 */

/* Initialise LEDC timer and channel. GPIO11 output defaults to LOW. */
esp_err_t buzzer_init(void);

/* FreeRTOS task – waits for APP_EVENT_PULSE. Priority: TASK_PRIO_BUZZER. */
void buzzer_task(void *arg);

/* Enable / disable click output (persisted preference, toggled via UI). */
void buzzer_set_enabled(bool enabled);
bool buzzer_is_enabled(void);
