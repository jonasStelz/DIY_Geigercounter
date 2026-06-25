#pragma once

#include "esp_err.h"

/*
 * wifi_manager – Wi-Fi lifecycle with mandatory power dependency check.
 *
 * Power policy (strict):
 *   USB power present  → Wi-Fi allowed.
 *   Battery operation  → Wi-Fi FORBIDDEN. Radio must not be enabled.
 *
 * wifi_manager_init() and wifi_manager_task() must query
 * power_monitor_is_usb() before touching any Wi-Fi/radio API.
 *
 * Step 10 provides the architecture skeleton only.
 * Full Wi-Fi implementation is out of scope for the initial release.
 */

/* Placeholder: verify power state, register event handlers. */
esp_err_t wifi_manager_init(void);

/* Placeholder: FreeRTOS task – handles Wi-Fi events. Priority: TASK_PRIO_WIFI. */
void wifi_manager_task(void *arg);
