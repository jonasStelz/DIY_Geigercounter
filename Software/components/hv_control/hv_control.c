#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hv_control.h"

/* Implementation – Step 7 */

esp_err_t hv_control_init(void)
{
    return ESP_OK;
}

void hv_control_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}

void hv_control_enable(void)  {}
void hv_control_disable(void) {}

float hv_control_get_voltage(void)
{
    return 0.0f;
}
