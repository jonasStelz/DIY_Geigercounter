#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "power_monitor.h"

/* Implementation – Step 9 */

esp_err_t power_monitor_init(void)
{
    return ESP_OK;
}

void power_monitor_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}

power_source_t power_monitor_get_source(void)
{
    return POWER_SOURCE_BATTERY;
}

bool power_monitor_is_usb(void)
{
    return false;
}
