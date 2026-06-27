#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "app_types.h"

/*
 * ui_menu  –  LCD screen manager and menu state machine.
 *
 * Screens (SW1 short = next, wraps):
 *   0  Dashboard     Live counts, CPM, µSv/h, power indicator
 *   1  Statistics    Session avg + max CPM and µSv/h
 *   2  Alarm         Mode (OFF/CPM/µSv/h) + threshold
 *   3  Audio & WiFi  Buzzer toggle, Wi-Fi status
 *
 * Power source injection:
 *   The UI does not import power_monitor directly.
 *   Register a callback that returns the current power_source_t.
 *   Called once per screen refresh to update the status indicator.
 *
 * Buzzer state:
 *   Toggled by SW2 on the Audio & Wi-Fi screen.
 *   Queried by the buzzer task (Step 8) via ui_menu_buzzer_enabled().
 */

/* Callback type: returns current power source (registered by power_monitor). */
typedef power_source_t (*power_provider_fn_t)(void);

/* Register the power source callback (call before ui_menu_task starts). */
void ui_menu_register_power_provider(power_provider_fn_t fn);

/* Returns true if the buzzer is currently enabled in the menu. */
bool ui_menu_buzzer_enabled(void);

/* Returns the FreeRTOS task handle (valid after ui_menu_task() starts).
 * Used by power_manager (Step 8) for vTaskSuspend() / vTaskResume(). */
TaskHandle_t ui_menu_get_task_handle(void);

/* Subscribe to APP_EVENTS and initialise menu state. */
esp_err_t ui_menu_init(void);

/* FreeRTOS task – 4 Hz LCD refresh. Priority: TASK_PRIO_UI. */
void ui_menu_task(void *arg);
