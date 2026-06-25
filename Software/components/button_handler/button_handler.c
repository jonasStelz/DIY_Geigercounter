#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_handler.h"

/* Implementation – Step 4 */

esp_err_t button_handler_init(void)
{
    return ESP_OK;
}

void button_handler_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}
