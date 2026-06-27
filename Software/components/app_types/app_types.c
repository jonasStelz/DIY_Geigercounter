#include "app_types.h"
#include "esp_event.h"

/*
 * app_types.c – Shared application types component.
 *
 * app_events_post() lives here so any component can post APP_EVENTS
 * without depending on main/. The APP_EVENTS base is defined once in
 * main/app_events.c; ESP_EVENT_DECLARE_BASE in app_types.h makes the
 * symbol visible everywhere.
 */

esp_err_t app_events_post(app_event_id_t event_id)
{
    return esp_event_post(APP_EVENTS, (int32_t)event_id,
                          NULL, 0, pdMS_TO_TICKS(10));
}
