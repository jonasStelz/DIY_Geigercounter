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
    bool         alarm_inc;  /* true = increment, false = decrement */
} menu_state_t;

/* Alarm runtime state – written by event handlers, read by task + getters */
typedef struct {
    bool active;        /* value currently above threshold */
    bool acknowledged;  /* user pressed SW1 to suppress display/sound */
} alarm_rt_t;

static menu_state_t s_state = {
    .screen               = SCREEN_DASHBOARD,
    .prev_screen          = SCREEN_COUNT,   /* force clear on first draw   */
    .alarm_mode           = ALARM_MODE_OFF,
    .alarm_threshold_cpm  = 100,
    .alarm_threshold_usvh = 1.0f,
    .buzzer_enabled       = true,
    .just_woke            = false,
    .alarm_inc            = true,
};

static SemaphoreHandle_t   s_state_mutex    = NULL;
static SemaphoreHandle_t   s_wake_sem       = NULL;  /* binary: signals immediate redraw */
static TaskHandle_t        s_task_handle    = NULL;
static power_provider_fn_t s_power_provider = NULL;
static uint8_t             s_stats_tick     = 0;     /* counter for stats subview flip   */
static alarm_rt_t          s_alarm_rt       = {0};   /* alarm runtime state              */
static uint8_t             s_alarm_blink    = 0;     /* blink counter for alarm display  */

/* Forward declaration – defined below with the other screen renderers */
static void draw_alarm_warning(const geiger_data_t *d);

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
    /* Row 0: mode + current direction indicator (+ or -).
     * "Alarm:" (6) + mode_str (5) + "  [" (3) + char (1) + "]" (1) = 16 chars */
    write_line(0, "Alarm:%-5s  [%c]", mode_str, st->alarm_inc ? '+' : '-');

    if (st->alarm_mode == ALARM_MODE_USVH) {
        float v = st->alarm_threshold_usvh > 99.9f ? 99.9f : st->alarm_threshold_usvh;
        write_line(1, "Limit: %5.2fuSvh", v);
    } else if (st->alarm_mode == ALARM_MODE_CPM) {
        uint32_t v = st->alarm_threshold_cpm > 99999 ? 99999 : st->alarm_threshold_cpm;
        write_line(1, "Limit: %5" PRIu32 " cpm", v);
    } else {
        write_line(1, "");   /* OFF – no threshold shown */
    }
}

static void draw_audio_wifi(const menu_state_t *st)
{
    write_line(0, "Buzzer:  %-7s", st->buzzer_enabled ? "ON" : "OFF");
    write_line(1, "WiFi:  DISABLED");   /* placeholder – Step 10 */
}

/* ── Alarm warning screen ─────────────────────────────────────────────── */

static void draw_alarm_warning(const geiger_data_t *d)
{
    write_line(0, "!!!!  ALARM  !!!!");

    alarm_mode_t mode = s_state.alarm_mode;
    if (mode == ALARM_MODE_CPM) {
        uint32_t cpm = d->cpm > 99999 ? 99999 : d->cpm;
        write_line(1, ">> %5" PRIu32 " cpm    <<", cpm);
    } else if (mode == ALARM_MODE_USVH) {
        float usv = d->usvh_ema > 99.999f ? 99.999f : d->usvh_ema;
        write_line(1, ">> %6.3f uSv/h <<", usv);
    } else {
        write_line(1, "");
    }
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

/* ── Alarm event handlers ─────────────────────────────────────────────── */

static void on_alarm_trigger(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_alarm_rt.active = true;
    /* acknowledged stays as-is: user must re-ack for each new alarm event */
    xSemaphoreGive(s_state_mutex);
}

static void on_alarm_clear(void *arg, esp_event_base_t base,
                           int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_alarm_rt.active       = false;
    s_alarm_rt.acknowledged = false;  /* reset ack so next alarm fires fresh */
    s_alarm_blink           = 0;
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI("UI", "alarm cleared");
}

static void on_sw1_short(void *arg, esp_event_base_t base,
                         int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);

    /*
     * Alarm acknowledgement has ABSOLUTE priority – checked BEFORE just_woke.
     *
     * Bug history: just_woke was checked first.  When the LCD was off during
     * an alarm, the user's first SW1 press caused on_lcd_wake to set
     * just_woke=true.  Then on_sw1_short returned early on the just_woke guard
     * and the alarm check was never reached.  Every press navigated the menu
     * instead of acknowledging.
     *
     * Fix: check alarm first.  Consume the entire press (clear just_woke too)
     * so no navigation side-effect occurs alongside the acknowledgement.
     */
    if (s_alarm_rt.active && !s_alarm_rt.acknowledged) {
        s_alarm_rt.acknowledged = true;
        s_alarm_blink           = 0;
        s_state.just_woke       = false;
        ESP_LOGI(TAG, "alarm acknowledged");
        xSemaphoreGive(s_state_mutex);
        return;
    }

    if (s_state.just_woke) {
        s_state.just_woke = false;
        xSemaphoreGive(s_state_mutex);
        return;
    }

    /* SW1 short = next screen on all screens */
    s_state.prev_screen = s_state.screen;
    s_state.screen      = (screen_id_t)((s_state.screen + 1) % SCREEN_COUNT);
    s_state.alarm_inc   = true;   /* reset direction when leaving alarm screen */
    s_stats_tick        = 0;
    ESP_LOGI(TAG, "screen → %d", (int)s_state.screen);

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
                if (s_state.alarm_inc) {
                    if (s_state.alarm_threshold_cpm < ALARM_CPM_MAX)
                        s_state.alarm_threshold_cpm += ALARM_CPM_STEP;
                } else {
                    if (s_state.alarm_threshold_cpm > ALARM_CPM_STEP)
                        s_state.alarm_threshold_cpm -= ALARM_CPM_STEP;
                }
            } else if (s_state.alarm_mode == ALARM_MODE_USVH) {
                if (s_state.alarm_inc) {
                    if (s_state.alarm_threshold_usvh < ALARM_USVH_MAX - 0.001f)
                        s_state.alarm_threshold_usvh += ALARM_USVH_STEP;
                } else {
                    if (s_state.alarm_threshold_usvh > ALARM_USVH_STEP + 0.001f)
                        s_state.alarm_threshold_usvh -= ALARM_USVH_STEP;
                }
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

/*
 * SW1 long – toggle threshold adjustment direction on the alarm screen.
 * Switches between increment (+) and decrement (-).
 * SW2 short then adjusts in the selected direction.
 */
static void on_sw1_long(void *arg, esp_event_base_t base,
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
        s_state.alarm_inc = !s_state.alarm_inc;
        ESP_LOGI(TAG, "alarm dir → %s", s_state.alarm_inc ? "INC" : "DEC");
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

alarm_mode_t ui_menu_get_alarm_mode(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    alarm_mode_t m = s_state.alarm_mode;
    xSemaphoreGive(s_state_mutex);
    return m;
}

uint32_t ui_menu_get_alarm_threshold_cpm(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint32_t v = s_state.alarm_threshold_cpm;
    xSemaphoreGive(s_state_mutex);
    return v;
}

float ui_menu_get_alarm_threshold_usvh(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    float v = s_state.alarm_threshold_usvh;
    xSemaphoreGive(s_state_mutex);
    return v;
}

bool ui_menu_is_alarm_active(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool v = s_alarm_rt.active;
    xSemaphoreGive(s_state_mutex);
    return v;
}

bool ui_menu_is_alarm_acknowledged(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool v = s_alarm_rt.acknowledged;
    xSemaphoreGive(s_state_mutex);
    return v;
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
        APP_EVENTS, APP_EVENT_LCD_WAKE,         on_lcd_wake,      NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_SHORT_SW1, on_sw1_short,     NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_LONG_SW1,  on_sw1_long,      NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_SHORT_SW2, on_sw2_short,     NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_BUTTON_LONG_SW2,  on_sw2_long,      NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_ALARM_TRIGGER,    on_alarm_trigger, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        APP_EVENTS, APP_EVENT_ALARM_CLEAR,      on_alarm_clear,   NULL));

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

        /* Snapshot s_state AND s_alarm_rt under the same mutex.
         * On the dual-core ESP32-S3, ui_menu_task (Core 1) and the event
         * loop (Core 0) can run truly concurrently.  Reading s_alarm_rt
         * outside the mutex means Core 1 may never see the write from
         * on_sw1_short on Core 0 → acknowledgement appears to do nothing. */
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        local_state = s_state;
        alarm_rt_t local_alarm = s_alarm_rt;
        s_state.prev_screen = s_state.screen;
        xSemaphoreGive(s_state_mutex);

        lcd_lock();
        if (local_alarm.active && !local_alarm.acknowledged) {
            /* Blink 1 Hz: alternate alarm warning / normal screen */
            s_alarm_blink++;
            if ((s_alarm_blink / 2) & 1u) {
                draw_alarm_warning(&data);
            } else {
                redraw(&data, &local_state);
            }
        } else {
            s_alarm_blink = 0;
            redraw(&data, &local_state);
        }
        lcd_unlock();
    }
}
