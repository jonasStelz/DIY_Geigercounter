#pragma once

#include "esp_err.h"

/*
 * button_handler – Debounced GPIO interrupt handler for SW1 and SW2.
 *
 * Events posted to the default event loop (APP_EVENTS):
 *   APP_EVENT_BUTTON_SHORT_SW1 / SW2  – released within BUTTON_LONG_PRESS_MS
 *   APP_EVENT_BUTTON_LONG_SW1  / SW2  – held for ≥ BUTTON_LONG_PRESS_MS
 *   APP_EVENT_LCD_WAKE               – any press when LCD is off
 *
 * Both GPIOs are active LOW with external pull-ups (no internal pull).
 */

/* Configure GPIO interrupts and create the debounce queue. */
esp_err_t button_handler_init(void);

/* FreeRTOS task – processes the interrupt queue. Priority: TASK_PRIO_BUTTON. */
void button_handler_task(void *arg);
