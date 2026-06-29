#pragma once

/*
 * geiger_config.h – Project-wide hardware constants.
 *
 * Rules:
 *   - Plain integer / float literals ONLY.
 *   - No #include directives (dependency-free by design).
 *   - No ESP-IDF enum types (use raw values with comments).
 *   - No magic numbers anywhere else in the firmware.
 */

/* ============================================================
 * GPIO Assignments
 * ============================================================ */
#define GPIO_GEIGER_PULSE       1   /* Pulse input  – active LOW, falling edge, open-collector */
#define GPIO_BUZZER             11  /* Passive piezo via NPN transistor – HIGH = active          */
#define GPIO_HV_ENABLE          12  /* ICM7555 oscillator enable         – HIGH = HV ON          */
#define GPIO_LCD_POWER          21  /* LCD ground-path MOSFET (low-side) – HIGH = LCD on         */
#define GPIO_SW1_MENU           9   /* Menu button   – active HIGH, external pull-down            */
#define GPIO_SW2_SELECT         10  /* Select button – active HIGH, external pull-down            */
#define GPIO_I2C_SCL            13
#define GPIO_I2C_SDA            14
#define GPIO_HV_ADC             2   /* ADC1_CH1: HV feedback voltage                            */
#define GPIO_USB_STATUS         3   /* USB power present – HIGH = USB connected                 */

/* ============================================================
 * I2C / LCD (HD44780 + PCF8574 backpack)
 * ============================================================ */
#define I2C_MASTER_BUS_NUM      0           /* I2C_NUM_0        */
#define I2C_MASTER_FREQ_HZ      100000
#define LCD_I2C_ADDR            0x27
#define LCD_BACKLIGHT_BIT       0x08
#define LCD_ENABLE_BIT          0x04
#define LCD_RW_BIT              0x02
#define LCD_RS_BIT              0x01
#define LCD_COLS                16
#define LCD_ROWS                2
#define LCD_POWER_STABILIZE_MS  50          /* Wait after GPIO_LCD_POWER → HIGH before I2C access */

/* ============================================================
 * High Voltage Control (ICM7555 / SBM-20)
 * ============================================================
 * Resistor network: R33 = R34 = R35 = 1 MΩ,  R36 = 18 kΩ
 * Vadc × HV_ADC_DIVIDER_RATIO = Vhigh
 * (Using 100 would be wrong – large measurement error.)
 * ============================================================ */
#define HV_ADC_DIVIDER_RATIO    167.7f
#define HV_TARGET_VOLTAGE_V     400.0f
#define HV_HYSTERESIS_V         5.0f
#define HV_ADC_CHANNEL_NUM      1           /* ADC1_CHANNEL_1 (GPIO2) */
#define HV_REGULATION_PERIOD_MS 100         /* Regulation loop period – 10 Hz */

/* ============================================================
 * Buzzer / LEDC PWM
 * ============================================================ */
#define BUZZER_FREQ_HZ          750
#define BUZZER_DUTY_RES_BITS    13          /* LEDC_TIMER_13_BIT */
#define BUZZER_DUTY_50_PCT      4096        /* (1 << 13) / 2     */
#define BUZZER_CLICK_MS         10
#define BUZZER_LEDC_TIMER_NUM   0           /* LEDC_TIMER_0      */
#define BUZZER_LEDC_CHANNEL_NUM 0           /* LEDC_CHANNEL_0    */

/* ============================================================
 * PCNT Pulse Counter
 * ============================================================ */
#define PCNT_HIGH_LIMIT         32767
#define PCNT_LOW_LIMIT          (-1)    /* Must be < 0; we never count negative */
#define PCNT_GLITCH_FILTER_NS   1000

/* ============================================================
 * Geiger Tube Calibration
 * ============================================================ */
#define GEIGER_TUBE_CPM_PER_USVH    22.0f  /* SBM-20 nominal – change only this for a different tube */

/* ============================================================
 * CPM / Dose Rate
 * ============================================================ */
#define CPM_RING_BUFFER_SIZE    60          /* seconds – rolling window length */
#define GEIGER_CORE_TICK_MS     1000        /* geiger_core_task period         */
#define DOSE_EMA_ALPHA          0.1f        /* EMA smoothing (0 = slow, 1 = instant) */

/* ============================================================
 * LCD Timeout
 * ============================================================ */
#define LCD_TIMEOUT_S           30

/* ============================================================
 * Button Timing
 * ============================================================ */
#define BUTTON_DEBOUNCE_MS      50
#define BUTTON_LONG_PRESS_MS    400

/* ============================================================
 * NVS Persistence
 * ============================================================ */
#define NVS_NAMESPACE               "geiger"
#define NVS_SYNC_INTERVAL_COUNTS    100
#define NVS_SYNC_INTERVAL_S         300

/* ============================================================
 * FreeRTOS Task Priorities  (higher number = higher priority)
 * ============================================================ */
#define TASK_PRIO_GEIGER_CORE   6
#define TASK_PRIO_BUTTON        5
#define TASK_PRIO_UI            4
#define TASK_PRIO_HV_CONTROL    4
#define TASK_PRIO_BUZZER        3
#define TASK_PRIO_WIFI          2

/* ============================================================
 * FreeRTOS Task Stack Sizes (bytes)
 * ============================================================ */
#define TASK_STACK_GEIGER_CORE  4096
#define TASK_STACK_BUTTON       3072
#define TASK_STACK_UI           4096
#define TASK_STACK_HV_CONTROL   2048
#define TASK_STACK_BUZZER       2048
#define TASK_STACK_WIFI         4096
