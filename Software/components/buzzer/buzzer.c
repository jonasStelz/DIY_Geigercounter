#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buzzer.h"

/* Implementation – Step 3 (click) / Step 8 (PM lock) */

esp_err_t buzzer_init(void)
{
    return ESP_OK;
}

void buzzer_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}

void buzzer_set_enabled(bool enabled) { (void)enabled; }
bool buzzer_is_enabled(void)          { return true; }
