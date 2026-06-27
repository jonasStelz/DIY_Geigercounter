/*
 * ui_menu.c  –  LCD screen manager and menu state machine.
 *
 * Fixes applied vs. original Step-6 version:
 *   - memset(&data, 0) before statistics_get_snapshot() → no uninit display
 *   - Binary semaphore s_wake_sem: LCD wake triggers immediate redraw
 *     instead of waiting up to 250 ms for next tick
 *   - s_task_handle stored for external suspend/resume (Step 8)
 *   - Statistics screen rotates uptime view every 2 s (8 × 250 ms ticks)
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_event.h"

#include "app_types.h"
#include "geiger_config.h"
#include "lcd_hd44780.h"
#include "statistics.h"
#include "ui_menu.h"

static const char *TAG = "UI";

/* ── Screen IDs ───────────────────────────────────────────────────────── */

typedef enum {
    SCREEN_DASHBOARD  = 0,
    SCREEN_STATISTICS = 1,
    SCREEN_ALARM      = 2,
    SCREEN_AUDIO_WIFI = 3,
    SCREEN_COUNT      = 4,
} screen_id_t;

#define ALARM_CPM_STEP    10u
#define ALARM_CPM_MAX     99990u
#define ALARM_USVH_STEP   0.1f
#define ALARM_USVH_MAX    99.9f

/* Statistics sub-view flips every N refreshes (250 ms each → 2 s) */
#define STATS_FLIP_TICKS  8u

/* ── Menu state ───────────────────────────────────────────────────────── */

typedef struct {
    screen_id_t  screen;
    screen_id_t  prev_screen;       /* detect screen change for full redraw */
    alarm_mode_t alarm_mode;
    uint32_t     alarm_threshold_cpm;
    float        alarm_threshold_usvh;
    bool         buzzer_enabled;
    bool         just_woke;
} menu_state_t;

static menu_state_t s_state = {
    .screen               = SCREEN_DASHBOARD,
    .prev_screen          = SCREEN_COUNT,   /* force clear on first draw   */
    .alarm_mode           = ALARM_MODE_OFF,
    .alarm_threshold_cpm  = 100,
    .alarm_threshold_usvh = 1.0f,
    .buzzer_enabled       = true,
    .just_woke            = false,
};

static SemaphoreHandle_t   s_state_mutex    = NULL;
static SemaphoreHandle_t   s_wake_sem       = NULL;  /* binary: signals immediate redraw */
static TaskHandle_t        s_task_handle    = NULL;
static power_provider_fn_t s_power_provider = NULL;
static uint8_t             s_stats_tick     = 0;     /* counter for stats subview flip   */

/* ── LCD line formatter ───────────────────────────────────────────────── */

static void write_line(uint8_t row, const char *fmt, ...)
{
    char buf[LCD_COLS + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = strlen(buf);
    while (len < LCD_COLS) buf[len++] = ' ';
    buf[LCD_COLS] = '\0';

    lcd_set_cursor(0, row);
    lcd_print(buf);
}

/* ── Screen renderers ─────────────────────────────────────────────────── */

static void draw_dashboard(const geiger_data_t *d)
{
    char power_ch = 'B';
    if (s_power_provider && s_power_provider() == POWER_SOURCE_USB) {
        power_ch = 'U';
    }

    uint64_t cnt = d->lifetime_counts > 99999999ULL
                   ? 99999999ULL : d->lifetime_counts;
    uint32_t cpm = d->cpm > 9999 ? 9999 : d->cpm;
    float    usv = d->usvh_ema > 9.999f ? 9.999f : d->usvh_ema;

    /* Row 0: "12345678 9999cpm"  (8-digit count + 4-digit CPM) */
    write_line(0, "%8" PRIu64 " %4" PRIu32 "cpm", cnt, cpm);

    /* Row 1: "9.999 uSv/h  [U]"  (EMA dose + power indicator) */
    write_line(1, "%.3f uSv/h  [%c]", usv, power_ch);
}

static void draw_statistics(const geiger_data_t *d)
{
    uint32_t avg_cpm  = (uint32_t)d->avg_cpm  > 999 ? 999 : (uint32_t)d->avg_cpm;
    uint32_t max_cpm  = d->max_cpm             > 999 ? 999 : d->max_cpm;
    float    avg_usvh = d->avg_usvh > 9.99f ? 9.99f : d->avg_usvh;
    float    max_usvh = d->max_usvh > 9.99f ? 9.99f : d->max_usvh;

    /* Flip between two sub-views every STATS_FLIP_TICKS refreshes */
    bool show_uptime = (s_stats_tick / STATS_FLIP_TICKS) & 1u;
    s_stats_tick++;

    if (!show_uptime) {
        /* Sub-view A: avg + max dose/CPM */
        write_line(0, "Avg:%3" PRIu32 "cpm %4.2fu", avg_cpm, avg_usvh);
        write_line(1, "Max:%3" PRIu32 "cpm %4.2fu", max_cpm, max_usvh);
    } else {
        /* Sub-view B: uptime + max CPM */
        uint32_t h = d->uptime_s / 3600;
        uint32_t m = (d->uptime_s % 3600) / 60;
        write_line(0, "Up: %4" PRIu32 "h %02" PRIu32 "m", h, m);
        write_line(1, "Max:%3" PRIu32 "cpm %4.2fu", max_cpm, max_usvh);
    }
}

static void draw_alarm(const menu_state_t *st)
{
    const char *mode_str;
    switch (st->alarm_mode) {
        case ALARM_MODE_OFF:  mode_str = "OFF  ";  break;
        case ALARM_MODE_CPM:  mode_str = "CPM  ";  break;
        case ALARM_MODE_USVH: mode_str = "uSv/h";  break;
        default:              mode_str = "???  ";  break;
    }
    write_line(0, "Alarm:  %s", mode_str);

    if (st->alarm_mode == ALARM_MODE_USVH) {
        float v = st->alarm_threshold_usvh > 99.9f ? 99.9f : st->alarm_threshold_usvh;
        write_line(1, "Limit: %5.2fuSvh", v);
    } else {
        uint32_t v = st->alarm_threshold_cpm > 99999 ? 99999 : st->alarm_threshold_cpm;
        write_line(1, "Limit: %5" PRIu32 " cpm", v);
    }
}

static void draw_audio_wifi(const menu_state_t *st)
{
    write_line(0, "Buzzer:  %-7s", st->buzzer_enabled ? "ON" : "OFF");
    write_line(1, "WiFi:  DISABLED");   /* placeholder – Step 10 */
}

/* ── Dispatch ─────────────────────────────────────────────────────────── */

static void redraw(const geiger_data_t *d, const menu_state_t *st)
{
    /* Clear display on screen change to remove ghost content */
    if (st->screen != st->prev_screen) {
        lcd_clear();
    }

    switch (st->screen) {
        case SCREEN_DASHBOARD:   draw_dashboard(d);    break;
        case SCREEN_STATISTICS:  draw_statistics(d);   break;
        case SCREEN_ALARM:       draw_alarm(st);       break;
        case SCREEN_AUDIO_WIFI:  draw_audio_wifi(st);  break;
        default:                                       break;
    }
}

/* ── Event handlers ───────────────────────────────────────────────────── */

static void on_lcd_wake(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (!lcd_is_on()) {
        lcd_lock();
        lcd_power_on();
        lcd_unlock();
        s_state.just_woke   = true;
        s_state.prev_screen = SCREEN_COUNT;   /* force clear after power-on */
        /* Signal immediate redraw – don't wait for the next 250 ms tick */
        xSemaphoreGive(s_wake_sem);
        ESP_LOGI(TAG, "LCD woken by button");
    }
    xSemaphoreGive(s_state_mutex);
}

static void on_sw1_short(void *arg, esp_event_base_t base,
                         int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (s_state.just_woke) {
        s_state.just_woke = false;
    } else {
        s_state.prev_screen = s_state.screen;
        s_state.screen = (screen_id_t)((s_state.screen + 1) % SCREEN_COUNT);
        s_stats_tick = 0;   /* reset sub-view on screen entry */
        ESP_LOGI(TAG, "screen → %d", (int)s_state.screen);
    }
    xSemaphoreGive(s_state_mutex);
}

static void on_sw2_short(void *arg, esp_event_base_t base,
                         int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);

    if (s_state.just_woke) {
        s_state.just_woke = false;
        xSemaphoreGive(s_state_mutex);
        return;
    }

    switch (s_state.screen) {
        case SCREEN_ALARM:
            if (s_state.alarm_mode == ALARM_MODE_CPM) {
                s_state.alarm_threshold_cpm += ALARM_CPM_STEP;
                if (s_state.alarm_threshold_cpm > ALARM_CPM_MAX)
                    s_state.alarm_threshold_cpm = ALARM_CPM_STEP;
            } else if (s_state.alarm_mode == ALARM_MODE_USVH) {
                s_state.alarm_threshold_usvh += ALARM_USVH_STEP;
                if (s_state.alarm_threshold_usvh > ALARM_USVH_MAX)
                    s_state.alarm_threshold_usvh = ALARM_USVH_STEP;
            }
            break;
        case SCREEN_AUDIO_WIFI:
            s_state.buzzer_enabled = !s_state.buzzer_enabled;
            ESP_LOGI(TAG, "buzzer %s", s_state.buzzer_enabled ? "ON" : "OFF");
            break;
        default:
            break;
    }

    xSemaphoreGive(s_state_mutex);
}

static void on_sw2_long(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);

    if (s_state.just_woke) {
        s_state.just_woke = false;
        xSemaphoreGive(s_state_mutex);
        return;
    }

    if (s_state.screen == SCREEN_ALARM) {
        switch (s_state.alarm_mode) {
            case ALARM_MODE_OFF:  s_state.alarm_mode = ALARM_MODE_CPM;  break;
            case ALARM_MODE_CPM:  s_state.alarm_mode = ALARM_MODE_USVH; break;
            case ALARM_MODE_USVH: s_state.alarm_mode = ALARM_MODE_OFF;  break;
            default:              s_state.alarm_mode = ALARM_MODE_OFF;  break;
        }
        ESP_LOGI(TAG, "alarm mode → %d", (int)s_state.alarm_mode);
    }

    xSemaphoreGive(s_state_mutex);
}

/* ── Public API ───────────────────────────────────────────────────────── */

void ui_menu_register_power_provider(power_provider_fn_t fn)
{
    s_power_provider = fn;
}

bool ui_menu_buzzer_enabled(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool en = s_state.buzzer_enabled;
    xSemaphoreGive(s_state_mutex);
    return en;
}

TaskHandle_t ui_menu_get_task_handle(void)
{
    return s_task_handle;
}

esp_err_t ui_menu_init(void)
{
    s_state_mutex = xSemaphoreCreateMutex();
    if (!s_state_mutex) return ESP_ERR_NO_MEM;

    /* Binary semaphore: given by on_lcd_wake to trigger immediate redraw */
    s_wake_sem = xSemaphoreCreateBinary();
    if (!s_wake_sem) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_LCD_WAKE,         on_lcd_wake,  NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_SHORT_SW1, on_sw1_short, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_SHORT_SW2, on_sw2_short, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_LONG_SW2,  on_sw2_long,  NULL));

    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

void ui_menu_task(void *arg)
{
    (void)arg;

    /* Store handle so power_manager (Step 8) can suspend/resume this task */
    s_task_handle = xTaskGetCurrentTaskHandle();

    geiger_data_t data;
    menu_state_t  local_state;

    while (1) {
        /*
         * Block for up to 250 ms (4 Hz normal refresh).
         * on_lcd_wake gives s_wake_sem to trigger an immediate redraw
         * after the LCD is powered on – no waiting for the next tick.
         */
        xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(250));

        if (!lcd_is_on()) continue;

        /* Fix #2: zero-initialise before snapshot so stale data never
         * reaches the display if statistics_get_snapshot() returns early */
        memset(&data, 0, sizeof(data));
        statistics_get_snapshot(&data);

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        local_state = s_state;
        /* Update prev_screen after copying so next iteration detects change */
        s_state.prev_screen = s_state.screen;
        xSemaphoreGive(s_state_mutex);

        lcd_lock();
        redraw(&data, &local_state);
        lcd_unlock();
    }
}
