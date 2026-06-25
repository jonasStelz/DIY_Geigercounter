#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_event.h"

/*
 * app_types.h – Shared application types and event declarations.
 *
 * All components that post or handle application events include this header.
 * The event base symbol APP_EVENTS is *declared* here and *defined* once
 * in main/app_events.c via ESP_EVENT_DEFINE_BASE(APP_EVENTS).
 */

/* ============================================================
 * Application Event System
 * ============================================================ */
ESP_EVENT_DECLARE_BASE(APP_EVENTS);

typedef enum {
    APP_EVENT_PULSE = 0,            /* Geiger pulse detected (from geiger_core)    */
    APP_EVENT_BUTTON_SHORT_SW1,     /* SW1 short press                             */
    APP_EVENT_BUTTON_SHORT_SW2,     /* SW2 short press                             */
    APP_EVENT_BUTTON_LONG_SW1,      /* SW1 long press                              */
    APP_EVENT_BUTTON_LONG_SW2,      /* SW2 long press                              */
    APP_EVENT_LCD_WAKE,             /* Any button → LCD wakeup                     */
    APP_EVENT_LCD_SLEEP,            /* Timeout → LCD power-off                     */
    APP_EVENT_ALARM_TRIGGER,        /* Radiation alarm threshold crossed            */
    APP_EVENT_USB_CONNECTED,        /* USB power detected                          */
    APP_EVENT_USB_DISCONNECTED,     /* USB power lost                              */
    APP_EVENT_WIFI_CONNECTED,
    APP_EVENT_WIFI_DISCONNECTED,
} app_event_id_t;

/* ============================================================
 * Power Source
 * ============================================================ */
typedef enum {
    POWER_SOURCE_BATTERY = 0,
    POWER_SOURCE_USB,
} power_source_t;

/* ============================================================
 * Alarm Mode
 * ============================================================ */
typedef enum {
    ALARM_MODE_OFF = 0,
    ALARM_MODE_CPM,
    ALARM_MODE_USVH,
} alarm_mode_t;

/* ============================================================
 * Geiger Measurement Snapshot
 * (passed between components; never duplicated as separate structs)
 * ============================================================ */
typedef struct {
    uint32_t cpm;               /* Rolling 60-second CPM                           */
    float    usvh_raw;          /* Raw dose rate µSv/h (no filtering)               */
    float    usvh_ema;          /* EMA-filtered dose rate µSv/h (displayed)         */
    uint64_t lifetime_counts;   /* Total counts since first boot (NVS-backed)       */
    uint32_t max_cpm;           /* Historical maximum CPM                           */
    float    max_usvh;          /* Historical maximum µSv/h                         */
    uint32_t uptime_s;          /* Seconds since boot                               */
    float    avg_cpm;           /* Session average CPM                              */
    float    avg_usvh;          /* Session average µSv/h                            */
} geiger_data_t;

/* ============================================================
 * Application State
 * ============================================================ */
typedef enum {
    APP_STATE_INIT = 0,
    APP_STATE_RUNNING,
    APP_STATE_LCD_OFF,
    APP_STATE_ALARM,
} app_state_t;
