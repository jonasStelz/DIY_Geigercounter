#pragma once

#include "app_types.h"
#include "esp_err.h"

/*
 * app_events.h – Application event loop helpers.
 *
 * Components that need to post events include app_types.h directly and call
 * esp_event_post(APP_EVENTS, event_id, ...) themselves.
 * These helpers are provided for main() and simple no-data posts.
 */

/* Initialize the default ESP event loop. Call once in app_main(). */
esp_err_t app_events_init(void);

/* Post an application event with no associated data payload. */
esp_err_t app_events_post(app_event_id_t event_id);
