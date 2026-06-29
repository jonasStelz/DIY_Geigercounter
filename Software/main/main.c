#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "app_events.h"
#include "geiger_core.h"
#include "button_handler.h"
#include "lcd_hd44780.h"
#include "ui_menu.h"
#include "buzzer.h"
#include "hv_control.h"
#include "geiger_config.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    /*
     * Early GPIO output initialisation – define safe states before any
     * component runs.  After reset all GPIOs default to input/high-Z;
     * output pins must be driven explicitly to avoid undefined hardware
     * states during the boot sequence.
     *
     *   GPIO_HV_ENABLE  (GPIO12) LOW  – ICM7555 oscillator off.
     *                                   Without this, the HV supply has an
     *                                   undefined state until hv_control_init()
     *                                   runs in Step 7.
     *
     *   GPIO_LCD_POWER  (GPIO21) LOW  – Q1 low-side MOSFET gate at 0 V.
     *                                   Prevents the MOSFET from partially
     *                                   conducting while the LCD is still
     *                                   unpowered during boot.
     *                                   lcd_init() drives it HIGH when ready.
     *
     *   GPIO_BUZZER     (GPIO11) LOW  – Piezo NPN transistor base at 0 V.
     *                                   Prevents spurious clicks before
     *                                   buzzer_init() runs in Step 8.
     */
    const gpio_config_t early_out_cfg = {
        .pin_bit_mask = (1ULL << GPIO_HV_ENABLE)
                      | (1ULL << GPIO_LCD_POWER)
                      | (1ULL << GPIO_BUZZER),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&early_out_cfg));
    gpio_set_level(GPIO_HV_ENABLE, 0);
    gpio_set_level(GPIO_LCD_POWER, 0);
    gpio_set_level(GPIO_BUZZER,    0);

    ESP_LOGI(TAG, "Geiger Counter starting");

    /* Central event loop – must be first. */
    ESP_ERROR_CHECK(app_events_init());

    /* Step 3: Geiger core (pulse counter + statistics). */
    ESP_ERROR_CHECK(geiger_core_init());
    xTaskCreate(geiger_core_task, "geiger_core",
                TASK_STACK_GEIGER_CORE, NULL, TASK_PRIO_GEIGER_CORE, NULL);

    /* Step 4: Button handler. */
    ESP_ERROR_CHECK(button_handler_init());
    xTaskCreate(button_handler_task, "btn_task",
                TASK_STACK_BUTTON, NULL, TASK_PRIO_BUTTON, NULL);

    /* Step 5: LCD driver. */
    ESP_ERROR_CHECK(lcd_init());

    /* Step 5: Buzzer. */
    ESP_ERROR_CHECK(buzzer_init());
    xTaskCreate(buzzer_task, "buzzer_task",
                TASK_STACK_BUZZER, NULL, TASK_PRIO_BUZZER, NULL);

    /* Step 6: Menu system. */
    ESP_ERROR_CHECK(ui_menu_init());
    xTaskCreate(ui_menu_task, "ui_task",
                TASK_STACK_UI, NULL, TASK_PRIO_UI, NULL);

    /* Step 7: High voltage control. */
    ESP_ERROR_CHECK(hv_control_init());
    xTaskCreate(hv_control_task, "hv_task",
                TASK_STACK_HV_CONTROL, NULL, TASK_PRIO_HV_CONTROL, NULL);

    /* Further initialisation added in Steps 7–10. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
