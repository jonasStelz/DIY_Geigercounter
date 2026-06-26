/*
 * button_handler.c  –  Debounced GPIO interrupt handler for SW1 and SW2.
 *
 * Architecture:
 *   ISR (ANYEDGE on GPIO9/GPIO10)
 *       → lightweight struct pushed to queue (no processing in ISR)
 *           → button_handler_task() reads queue, runs state machine
 *
 * State machine per button:
 *   IDLE    : waiting for press
 *   PRESSED : rising edge seen, recording press time
 *
 * On rising edge (button pressed, active HIGH):
 *   1. Debounce check – edge within BUTTON_DEBOUNCE_MS of last edge is dropped.
 *   2. Post APP_EVENT_LCD_WAKE  (always; LCD component ignores it when already on).
 *   3. Enter PRESSED state and record press timestamp.
 *
 * On falling edge (button released, active HIGH):
 *   1. Debounce check.
 *   2. Compute held duration.
 *   3. If held >= BUTTON_LONG_PRESS_MS  → post APP_EVENT_BUTTON_LONG_SW1/SW2
 *      Else                             → post APP_EVENT_BUTTON_SHORT_SW1/SW2
 *   4. Return to IDLE state.
 *
 * Dependencies:
 *   REQUIRES:      app_types, esp_event
 *   PRIV_REQUIRES: geiger_config, driver, esp_driver_gpio
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"

#include "app_types.h"
#include "geiger_config.h"
#include "button_handler.h"

static const char *TAG = "BTN";

/* -----------------------------------------------------------------------
 * ISR event record
 * Small struct – pushed to queue directly from ISR.
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t   gpio_num;   /* GPIO_SW1_MENU or GPIO_SW2_SELECT            */
    uint8_t    level;      /* gpio_get_level() result: 1=pressed, 0=released */
    TickType_t tick;       /* xTaskGetTickCountFromISR() at event time     */
} btn_isr_evt_t;

static QueueHandle_t s_btn_queue = NULL;

/* -----------------------------------------------------------------------
 * Per-button runtime state (used inside task only)
 * ----------------------------------------------------------------------- */
typedef struct {
    bool       pressed;         /* Currently in PRESSED state               */
    bool       edge_seen;       /* True once any edge has been processed     */
    TickType_t press_tick;      /* Tick count when button went HIGH (pressed)    */
    TickType_t last_edge_tick;  /* Tick count of last processed edge (debounce) */
} btn_state_t;

/* -----------------------------------------------------------------------
 * ISR handler
 * Attached to both SW1 and SW2.  arg is the gpio_num cast to void*.
 * ----------------------------------------------------------------------- */
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;

    btn_isr_evt_t evt = {
        .gpio_num = gpio_num,
        .level    = (uint8_t)gpio_get_level((gpio_num_t)gpio_num),
        .tick     = xTaskGetTickCountFromISR(),
    };

    BaseType_t higher_prio_woken = pdFALSE;
    xQueueSendFromISR(s_btn_queue, &evt, &higher_prio_woken);

    /* Yield if a higher-priority task was unblocked by the queue send. */
    if (higher_prio_woken) {
        portYIELD_FROM_ISR();
    }
}

/* -----------------------------------------------------------------------
 * button_handler_init
 * ----------------------------------------------------------------------- */
esp_err_t button_handler_init(void)
{
    /* Queue depth: 10 is ample for burst presses during fast navigation. */
    s_btn_queue = xQueueCreate(10, sizeof(btn_isr_evt_t));
    if (!s_btn_queue) {
        ESP_LOGE(TAG, "Queue creation failed");
        return ESP_ERR_NO_MEM;
    }

    /*
     * Install GPIO ISR service.
     * Tolerate ESP_ERR_INVALID_STATE – another component may have already
     * installed it (e.g., pulse_counter in a future integration path).
     */
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(ret));
        return ret;
    }

    /*
     * Configure SW1 (GPIO_SW1_MENU) and SW2 (GPIO_SW2_SELECT).
     *
     * Both buttons:
     *   - Active HIGH (pressed = GPIO reads 1)
     *   - External pull-down on PCB → internal pull-down NOT enabled
     *   - ANYEDGE interrupt so we can measure press duration in software
     */
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_SW1_MENU) | (1ULL << GPIO_SW2_SELECT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(GPIO_SW1_MENU,   btn_isr_handler,
                               (void *)(uintptr_t)GPIO_SW1_MENU);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "isr_handler_add SW1: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(GPIO_SW2_SELECT, btn_isr_handler,
                               (void *)(uintptr_t)GPIO_SW2_SELECT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "isr_handler_add SW2: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "initialized  SW1=GPIO%d  SW2=GPIO%d  debounce=%d ms  long=%d ms",
             GPIO_SW1_MENU, GPIO_SW2_SELECT,
             BUTTON_DEBOUNCE_MS, BUTTON_LONG_PRESS_MS);
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * button_handler_task
 * ----------------------------------------------------------------------- */
void button_handler_task(void *arg)
{
    (void)arg;

    const TickType_t debounce_ticks   = pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS);
    const TickType_t long_press_ticks = pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS);

    /* Two slots: index 0 = SW1, index 1 = SW2 */
    btn_state_t state[2] = {0};

    btn_isr_evt_t evt;

    while (1) {
        if (xQueueReceive(s_btn_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* ---- Map GPIO to button index and event IDs ---- */
        uint8_t idx;
        app_event_id_t short_ev, long_ev;

        if (evt.gpio_num == (uint32_t)GPIO_SW1_MENU) {
            idx      = 0;
            short_ev = APP_EVENT_BUTTON_SHORT_SW1;
            long_ev  = APP_EVENT_BUTTON_LONG_SW1;
        } else if (evt.gpio_num == (uint32_t)GPIO_SW2_SELECT) {
            idx      = 1;
            short_ev = APP_EVENT_BUTTON_SHORT_SW2;
            long_ev  = APP_EVENT_BUTTON_LONG_SW2;
        } else {
            continue;  /* Spurious – ignore */
        }

        btn_state_t *s = &state[idx];

        /* ---- Debounce ----
         * Skip edges that arrive within BUTTON_DEBOUNCE_MS of the last one.
         * edge_seen guard ensures the very first edge is never debounced away,
         * even if the system boot tick count is close to 0. */
        if (s->edge_seen) {
            TickType_t since_last = evt.tick - s->last_edge_tick;
            if (since_last < debounce_ticks) {
                ESP_LOGD(TAG, "GPIO%"PRIu32" debounced (%"PRIu32" ms)",
                         evt.gpio_num,
                         (uint32_t)(since_last * portTICK_PERIOD_MS));
                continue;
            }
        }
        s->edge_seen      = true;
        s->last_edge_tick = evt.tick;

        /* ---- State machine ---- */
        if (evt.level == 1) {
            /* ===== RISING EDGE: button pressed (active HIGH) ===== */
            s->pressed    = true;
            s->press_tick = evt.tick;

            /*
             * Notify the UI / LCD that a button was touched.
             * APP_EVENT_LCD_WAKE is fired on every press – if the LCD is
             * already on, the handler treats it as a no-op.
             */
            esp_event_post(APP_EVENTS, (int32_t)APP_EVENT_LCD_WAKE,
                           NULL, 0, pdMS_TO_TICKS(10));

            ESP_LOGD(TAG, "GPIO%"PRIu32" PRESS", evt.gpio_num);
        } else {
            /* ===== FALLING EDGE: button released (active HIGH) ===== */
            if (s->pressed) {
                s->pressed = false;

                TickType_t held_ticks = evt.tick - s->press_tick;
                uint32_t   held_ms    = (uint32_t)(held_ticks * portTICK_PERIOD_MS);

                if (held_ticks >= long_press_ticks) {
                    ESP_LOGI(TAG, "GPIO%"PRIu32" LONG press (%"PRIu32" ms)",
                             evt.gpio_num, held_ms);
                    esp_event_post(APP_EVENTS, (int32_t)long_ev,
                                   NULL, 0, pdMS_TO_TICKS(10));
                } else {
                    ESP_LOGI(TAG, "GPIO%"PRIu32" SHORT press (%"PRIu32" ms)",
                             evt.gpio_num, held_ms);
                    esp_event_post(APP_EVENTS, (int32_t)short_ev,
                                   NULL, 0, pdMS_TO_TICKS(10));
                }
            }
        }
    }
}
