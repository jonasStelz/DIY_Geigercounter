#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_manager.h"

/* Architecture skeleton – Step 10 */

esp_err_t wifi_manager_init(void)
{
    /* TODO Step 10: query power_monitor_is_usb() before any radio init. */
    return ESP_OK;
}

void wifi_manager_task(void *arg)
{
    (void)arg;
    /* TODO Step 10: event-driven Wi-Fi management, USB-only. */
    vTaskDelete(NULL);
}
