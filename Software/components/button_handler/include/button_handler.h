#pragma once

#include "esp_err.h"

/*
 * button_handler  –  Debounced GPIO interrupt handler for SW1 and SW2.
 *
 * Events posted to the default event loop (APP_EVENTS):
 *
 *   APP_EVENT_LCD_WAKE           – any button press (fired on falling edge,
 *                                   before short/long classification)
 *   APP_EVENT_BUTTON_SHORT_SW1  – SW1 released within BUTTON_LONG_PRESS_MS
 *   APP_EVENT_BUTTON_SHORT_SW2  – SW2 released within BUTTON_LONG_PRESS_MS
 *   APP_EVENT_BUTTON_LONG_SW1   – SW1 held for >= BUTTON_LONG_PRESS_MS
 *   APP_EVENT_BUTTON_LONG_SW2   – SW2 held for >= BUTTON_LONG_PRESS_MS
 *
 * Both GPIOs are active LOW with external pull-ups (no internal pull).
 * Debounce window and long-press threshold are defined in geiger_config.h.
 */

/*
 * Configure GPIO interrupts and create the internal event queue.
 * Must be called before starting button_handler_task().
 * Installs the GPIO ISR service if not already installed.
 */
esp_err_t button_handler_init(void);

/*
 * FreeRTOS task entry point.
 * Processes the ISR queue and posts application events.
 * Recommended priority: TASK_PRIO_BUTTON (defined in geiger_config.h).
 * Recommended stack:    TASK_STACK_BUTTON (defined in geiger_config.h).
 */
void button_handler_task(void *arg);
