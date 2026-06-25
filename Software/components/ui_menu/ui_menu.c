#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ui_menu.h"

/* Implementation – Step 6 */

static power_provider_fn_t s_power_provider = NULL;

void ui_menu_register_power_provider(power_provider_fn_t fn)
{
    s_power_provider = fn;
}

esp_err_t ui_menu_init(void)
{
    return ESP_OK;
}

void ui_menu_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}
