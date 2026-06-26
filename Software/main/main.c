#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_events.h"
#include "geiger_core.h"
#include "button_handler.h"
#include "lcd_hd44780.h"
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

    /* Step 4: Button handler. */
    ESP_ERROR_CHECK(button_handler_init());

    xTaskCreate(button_handler_task, "btn_task",
                TASK_STACK_BUTTON, NULL,
                TASK_PRIO_BUTTON, NULL);

    /* Step 5: LCD driver. */
    ESP_ERROR_CHECK(lcd_init());

    /* Further task creation added in Steps 5–10. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
