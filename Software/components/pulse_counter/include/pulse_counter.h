#pragma once

#include <stdint.h>
#include "esp_err.h"

/*
 * pulse_counter – Hardware PCNT pulse counter for the Geiger tube.
 *
 * Uses ESP32-S3 PCNT peripheral (esp_driver_pcnt in ESP-IDF 5.x+).
 * Counts falling edges on GPIO_GEIGER_PULSE with overflow handling
 * to provide a full 32-bit accumulator.
 *
 * The CPU may enter Light Sleep while counting continues in hardware.
 */

/* Initialise PCNT unit, watchpoints, and overflow ISR. */
esp_err_t pulse_counter_init(void);

/*
 * Read the 32-bit accumulated pulse count since the last call and
 * atomically reset the accumulator to zero.
 * Called once per second by geiger_core_task.
 */
uint32_t pulse_counter_read_and_reset(void);
