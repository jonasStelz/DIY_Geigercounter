#pragma once

#include "esp_err.h"
#include "app_types.h"

/*
 * power_monitor – USB power presence detection via GPIO3.
 *
 * GPIO3: HIGH = USB connected, LOW = battery only.
 *
 * On state change posts:
 *   APP_EVENT_USB_CONNECTED    (USB plugged in)
 *   APP_EVENT_USB_DISCONNECTED (USB removed → Wi-Fi must be disabled)
 *
 * The Wi-Fi manager must query power_monitor_is_usb() before enabling
 * any radio hardware. Battery operation → Wi-Fi forbidden.
 */

/* Configure GPIO3 and detect initial power state. */
esp_err_t power_monitor_init(void);

/* FreeRTOS task – polls GPIO3 for state changes. */
void power_monitor_task(void *arg);

/* Query functions (safe to call from any context). */
power_source_t power_monitor_get_source(void);
bool           power_monitor_is_usb(void);
