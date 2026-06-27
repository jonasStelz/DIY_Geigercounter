#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * lcd_hd44780 – HD44780 driver via PCF8574 I2C backpack.
 *
 * Two independent power-saving layers:
 *
 *   Layer 1 – Hard Power (GPIO21 → Q1 low-side MOSFET)
 *     GPIO21 HIGH → LCD ground path connected → LCD fully operational.
 *     GPIO21 LOW  → LCD completely unpowered  → zero quiescent current.
 *     Note: Q1 switches the GROUND, not VCC.
 *
 *   Layer 2 – Soft Backlight (PCF8574 bit 3)
 *     Controls only the backlight LED.
 *     LCD logic and display remain active.
 *
 * Thread safety: internal mutex – all public calls are safe from any task.
 *
 * LCD timeout flow (30 s no interaction):
 *   lcd_power_off() → stop updates → i2c_driver_delete() → state = OFF
 *   On next button event:
 *   lcd_power_on() → wait LCD_POWER_STABILIZE_MS → lcd_reinit()
 */

/* First-time initialisation (GPIO + I2C + HD44780 init sequence). */
esp_err_t lcd_init(void);

/*
 * Re-run the HD44780 initialisation sequence after a power cycle.
 * Also reinstalls the I2C driver. Call after lcd_power_on() + delay.
 */
esp_err_t lcd_reinit(void);

/* Hard power control via GPIO21. */
void lcd_power_on(void);
void lcd_power_off(void);

/* Returns true if the LCD is currently powered and operational. */
bool lcd_is_on(void);

/* Soft backlight control via PCF8574 bit 3. */
void lcd_backlight_on(void);
void lcd_backlight_off(void);

/* Display primitives. */
void lcd_clear(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_print(const char *str);
void lcd_print_char(char c);

/* Mutex – acquire before any sequence of calls that must be atomic. */
void lcd_lock(void);
void lcd_unlock(void);
