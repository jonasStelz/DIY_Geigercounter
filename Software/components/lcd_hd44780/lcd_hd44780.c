#include "lcd_hd44780.h"

/* Implementation – Step 5 */

esp_err_t lcd_init(void)   { return ESP_OK; }
esp_err_t lcd_reinit(void) { return ESP_OK; }
void lcd_power_on(void)    {}
void lcd_power_off(void)   {}
void lcd_backlight_on(void) {}
void lcd_backlight_off(void){}
void lcd_clear(void)       {}
void lcd_set_cursor(uint8_t col, uint8_t row) { (void)col; (void)row; }
void lcd_print(const char *str) { (void)str; }
void lcd_print_char(char c) { (void)c; }
void lcd_lock(void)        {}
void lcd_unlock(void)      {}
