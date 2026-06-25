#pragma once

#include "esp_err.h"
#include "app_types.h"

/*
 * ui_menu – LCD screen manager and menu state machine.
 *
 * Screens (cycled with SW1 short press):
 *
 *   Screen 0 – Dashboard
 *     Line 0: "Count: <lifetime>  <cpm> cpm"
 *     Line 1: "<usvh_ema> µSv/h  [U] or [B]"
 *
 *   Screen 1 – Statistics
 *     Uptime, avg CPM, avg µSv/h, max CPM, max µSv/h.
 *
 *   Screen 2 – Alarm Settings
 *     Mode (OFF / CPM / µSv/h) + threshold.
 *     SW2: adjust threshold. SW2 long: change mode.
 *
 *   Screen 3 – Audio & Wi-Fi
 *     Buzzer on/off. Wi-Fi status.
 *     SW2: toggle buzzer.
 *
 * Power status injection:
 *   The UI does not import power_monitor directly.
 *   Instead, the caller registers a provider callback so the UI can
 *   query the current power source for the status indicator.
 */

/* Register a function that returns the current power source. */
typedef power_source_t (*power_provider_fn_t)(void);
void ui_menu_register_power_provider(power_provider_fn_t fn);

/* Initialise screen state and subscribe to APP_EVENTS. */
esp_err_t ui_menu_init(void);

/* FreeRTOS task – refreshes the LCD at ~4 Hz. Priority: TASK_PRIO_UI. */
void ui_menu_task(void *arg);
