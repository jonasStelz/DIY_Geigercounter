#pragma once

#include "esp_err.h"

/*
 * buzzer  –  Passive piezo click driver via LEDC PWM.
 *
 * Triggered by APP_EVENT_PULSE events from the event loop.
 * Mute state is read from ui_menu_buzzer_enabled().
 *
 * LEDC parameters (all in geiger_config.h):
 *   GPIO11, 750 Hz, 13-bit resolution, 50 % duty, 10 ms click.
 *
 * PM lock (Step 8):
 *   ESP_PM_NO_LIGHT_SLEEP is acquired before each click and released
 *   after to prevent the LEDC clock from being gated during playback.
 *   The lock calls are present but commented out until Step 8 activates
 *   power management.
 */

/* Register APP_EVENT_PULSE handler, configure LEDC, create click queue. */
esp_err_t buzzer_init(void);

/* FreeRTOS task – blocks on internal queue, plays one click per entry.
 * Priority: TASK_PRIO_BUZZER.  Stack: TASK_STACK_BUZZER. */
void buzzer_task(void *arg);
