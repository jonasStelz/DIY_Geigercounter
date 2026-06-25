#pragma once

#include "esp_err.h"

/*
 * hv_control – Hysteresis regulator for the ICM7555-based HV generator.
 *
 * GPIO12 controls the oscillator:
 *   HIGH → HV ON
 *   LOW  → HV OFF  (default at startup – oscillator stays off until init)
 *
 * ADC reads the feedback divider (GPIO2 / ADC1_CH1).
 * Conversion: Vhigh = Vadc × HV_ADC_DIVIDER_RATIO  (= 167.7)
 *
 * Regulation logic:
 *   Vhigh < (target − hysteresis) → enable oscillator
 *   Vhigh > (target + hysteresis) → disable oscillator
 */

/* Initialise GPIO12 LOW (oscillator disabled), configure ADC. */
esp_err_t hv_control_init(void);

void  hv_control_task(void *arg);

/* Direct control (used during startup / shutdown sequences). */
void  hv_control_enable(void);
void  hv_control_disable(void);

/* Latest measured high voltage in volts. */
float hv_control_get_voltage(void);
