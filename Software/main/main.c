#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_events.h"
#include "geiger_core.h"
#include "geiger_config.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Geiger Counter starting");

    /* Central event loop – must be first. */
    ESP_ERROR_CHECK(app_events_init());

    /* Step 3: Geiger core (pulse counter + statistics). */
    ESP_ERROR_CHECK(geiger_core_init());

    xTaskCreate(geiger_core_task, "geiger_core",
                TASK_STACK_GEIGER_CORE, NULL,
                TASK_PRIO_GEIGER_CORE, NULL);

    /* Further task creation added in Steps 4–10. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
