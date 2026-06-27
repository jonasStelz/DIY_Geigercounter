/*
 * lcd_hd44780.c  –  HD44780 LCD driver via PCF8574 I2C backpack.
 *
 * Power layers
 * ────────────
 *   Layer 1 – Hard power (GPIO21 → Q1 low-side MOSFET)
 *     GPIO21 HIGH  →  LCD GND path connected  →  display fully operational
 *     GPIO21 LOW   →  LCD GND path open       →  display completely unpowered
 *     Note: Q1 switches the GROUND rail, not VCC.
 *
 *   Layer 2 – Soft backlight (PCF8574 bit 3 = LCD_BACKLIGHT_BIT)
 *     Controls only the backlight LED; LCD logic stays active.
 *
 * PCF8574 bit mapping (all writes go to its single output port)
 * ─────────────────────────────────────────────────────────────
 *   Bit 7–4  DB7–DB4   HD44780 data (4-bit mode, high nibble first)
 *   Bit 3    BL        Backlight  (LCD_BACKLIGHT_BIT)
 *   Bit 2    EN        Enable     (LCD_ENABLE_BIT)
 *   Bit 1    RW        Read/Write (LCD_RW_BIT)    – always 0 (write)
 *   Bit 0    RS        Register   (LCD_RS_BIT)    – 0 = command, 1 = data
 *
 * HD44780 init sequence (hardware-verified in baseline.c)
 * ────────────────────────────────────────────────────────
 *   0x33 → 0x32 → 0x28 → 0x0C → 0x06 → 0x01
 *
 * I2C driver
 * ──────────
 *   Uses the ESP-IDF 5.x / 6.x new master API (driver/i2c_master.h).
 *   The bus and device handles are created in lcd_init() / lcd_reinit()
 *   and torn down in lcd_power_off() so power-gating is clean.
 *
 * Thread safety
 * ─────────────
 *   An internal mutex serialises all display operations.
 *   lcd_lock() / lcd_unlock() expose it for multi-call atomic sequences.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "geiger_config.h"
#include "lcd_hd44780.h"

static const char *TAG = "LCD";

/* ── Internal state ───────────────────────────────────────────────────── */

static SemaphoreHandle_t        s_mutex   = NULL;
static i2c_master_bus_handle_t  s_bus     = NULL;
static i2c_master_dev_handle_t  s_dev     = NULL;
static bool                     s_powered = false;
static bool                     s_bl      = true;   /* current backlight state */

/* ── Low-level PCF8574 write ──────────────────────────────────────────── */

static esp_err_t pcf8574_write(uint8_t val)
{
    return i2c_master_transmit(s_dev, &val, 1, 10 /* ms */);
}

/* ── HD44780 4-bit nibble + byte helpers ──────────────────────────────── */

/* ── HD44780 4-bit byte helper ────────────────────────────────────────── */

/**
 * @brief Sends a full byte as two nibbles (high nibble first) in a single,
 * atomic I2C transaction. This ensures precise timing for the 
 * HD44780's Enable (EN) pulse edges.
 */
static void lcd_send_byte(uint8_t byte, uint8_t rs)
{
    uint8_t bl = s_bl ? LCD_BACKLIGHT_BIT : 0;
    
    uint8_t high = byte & 0xF0;
    uint8_t low  = (byte << 4) & 0xF0;

    /* Pack the 4-step sequence into a single array:
     * 1. High nibble with EN high
     * 2. High nibble with EN low  (latches high nibble)
     * 3. Low nibble with EN high
     * 4. Low nibble with EN low   (latches low nibble)
     */
    uint8_t data[4];
    data[0] = high | rs | bl | LCD_ENABLE_BIT;
    data[1] = high | rs | bl;
    data[2] = low  | rs | bl | LCD_ENABLE_BIT;
    data[3] = low  | rs | bl;

    /* Transmit all 4 bytes in one fast, burst I2C transfer */
    i2c_master_transmit(s_dev, data, sizeof(data), 10 /* ms timeout */);

    /* Allow execution time for the HD44780 to process the command */
    //vTaskDelay(pdMS_TO_TICKS(10));  //not necessary
}

static inline void lcd_cmd(uint8_t cmd) { lcd_send_byte(cmd, 0); }
static inline void lcd_dat(uint8_t  c)  { lcd_send_byte(c,   LCD_RS_BIT); }


/* ── HD44780 initialisation sequence ─────────────────────────────────── */

/* Verified working sequence from baseline.c – do not modify.
 * Note: VDD rise-time delay (50 ms) must be completed by the caller
 * before invoking this function (lcd_init waits once; lcd_reinit inherits
 * the wait from lcd_power_on). */
static void hd44780_init_sequence(void)
{
    lcd_cmd(0x33);                  /* Init – forces 8-bit mode twice      */
    lcd_cmd(0x32);                  /* Switch to 4-bit mode                */
    lcd_cmd(0x28);                  /* 2 lines, 5×8 font                   */
    lcd_cmd(0x0C);                  /* Display ON, cursor OFF, blink OFF   */
    lcd_cmd(0x06);                  /* Entry mode: increment, no shift     */
    lcd_cmd(0x01);                  /* Clear display                       */
    vTaskDelay(pdMS_TO_TICKS(10));  /* Clear command takes up to 1.52 ms  */
}

/* ── I2C bus/device lifecycle ─────────────────────────────────────────── */

static esp_err_t i2c_bus_create(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = (i2c_port_num_t)I2C_MASTER_BUS_NUM,
        .sda_io_num        = GPIO_I2C_SDA,
        .scl_io_num        = GPIO_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = false, /* External pull-ups on PCB */
        },
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = LCD_I2C_ADDR,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    return ret;
}

static void i2c_bus_destroy(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * PUBLIC API
 * ═══════════════════════════════════════════════════════════════════════ */

esp_err_t lcd_init(void)
{
    /* Mutex */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /*
     * GPIO21 – hard power control.
     * Q1 is a low-side MOSFET: HIGH = GND path closed = LCD operational.
     * Drive HIGH before touching I2C so the PCF8574 is already powered.
     */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = 1ULL << GPIO_LCD_POWER,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&gpio_cfg);
    if (ret != ESP_OK) return ret;

    gpio_set_level(GPIO_LCD_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(LCD_POWER_STABILIZE_MS));

    ret = i2c_bus_create();
    if (ret != ESP_OK) return ret;

    s_bl = true;
    vTaskDelay(pdMS_TO_TICKS(LCD_POWER_STABILIZE_MS)); /* VDD rise time (cold boot) */
    hd44780_init_sequence();
    s_powered = true;

    ESP_LOGI(TAG, "initialized (I2C addr=0x%02X, SCL=%d, SDA=%d)",
             LCD_I2C_ADDR, GPIO_I2C_SCL, GPIO_I2C_SDA);
    return ESP_OK;
}

/*
 * lcd_reinit – re-run HD44780 init after a power cycle.
 * Caller must have already called lcd_power_on() and waited
 * LCD_POWER_STABILIZE_MS before invoking this.
 */
esp_err_t lcd_reinit(void)
{
    esp_err_t ret = i2c_bus_create();
    if (ret != ESP_OK) return ret;

    s_bl = true;
    hd44780_init_sequence();
    s_powered = true;

    ESP_LOGI(TAG, "reinitialized after power cycle");
    return ESP_OK;
}

/* Hard power on: GPIO21 HIGH → stabilise → I2C + HD44780 reinit. */
void lcd_power_on(void)
{
    if (s_powered) return;

    gpio_set_level(GPIO_LCD_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(LCD_POWER_STABILIZE_MS));

    if (lcd_reinit() != ESP_OK) {
        ESP_LOGE(TAG, "power_on: reinit failed");
    }
}

/*
 * Hard power off:
 *   1. Kill backlight (PCF8574 all LOW) before the GND path opens.
 *   2. Tear down I2C to prevent bus contention after power cut.
 *   3. Pull GPIO21 LOW → GND path open → display unpowered.
 */
void lcd_power_off(void)
{
    if (!s_powered) return;

    /* Extinguish backlight gracefully */
    uint8_t all_low = 0x00;
    i2c_master_transmit(s_dev, &all_low, 1, 10);
    vTaskDelay(pdMS_TO_TICKS(10));

    i2c_bus_destroy();

    gpio_set_level(GPIO_LCD_POWER, 0);

    s_powered = false;
    s_bl      = false;

    ESP_LOGI(TAG, "power OFF");
}

/* ── Backlight ────────────────────────────────────────────────────────── */

void lcd_backlight_on(void)
{
    if (!s_powered) return;
    s_bl = true;
    pcf8574_write(LCD_BACKLIGHT_BIT);   /* No-op nibble, just updates BL bit */
}

void lcd_backlight_off(void)
{
    if (!s_powered) return;
    s_bl = false;
    pcf8574_write(0x00);
}

/* ── Display primitives ───────────────────────────────────────────────── */

void lcd_clear(void)
{
    if (!s_powered) return;
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void lcd_set_cursor(uint8_t col, uint8_t row)
{
    if (!s_powered) return;

    /* HD44780 DDRAM row offsets for a 2-line, 16-col display */
    static const uint8_t row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };

    if (row >= LCD_ROWS) row = LCD_ROWS - 1;
    if (col >= LCD_COLS) col = LCD_COLS - 1;

    lcd_cmd(0x80 | (col + row_offsets[row]));
}

void lcd_print(const char *str)
{
    if (!s_powered || !str) return;
    while (*str) {
        lcd_dat((uint8_t)*str++);
    }
}

void lcd_print_char(char c)
{
    if (!s_powered) return;
    lcd_dat((uint8_t)c);
}

/* ── Power state query ────────────────────────────────────────────────── */

bool lcd_is_on(void)
{
    return s_powered;
}

/* ── Mutex ────────────────────────────────────────────────────────────── */

void lcd_lock(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "lcd_lock timeout");
    }
}

void lcd_unlock(void)
{
    xSemaphoreGive(s_mutex);
}
