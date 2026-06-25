#include "app_events.h"
#include "esp_event.h"

/* Single definition of the APP_EVENTS event base (declared in app_types.h). */
ESP_EVENT_DEFINE_BASE(APP_EVENTS);

esp_err_t app_events_init(void)
{
    return esp_event_loop_create_default();
}

esp_err_t app_events_post(app_event_id_t event_id)
{
    return esp_event_post(APP_EVENTS, (int32_t)event_id,
                          NULL, 0, pdMS_TO_TICKS(10));
}
