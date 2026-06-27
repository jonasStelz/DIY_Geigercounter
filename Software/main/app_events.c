#include "app_events.h"
#include "esp_event.h"

/* Single definition of the APP_EVENTS event base (declared in app_types.h). */
ESP_EVENT_DEFINE_BASE(APP_EVENTS);

esp_err_t app_events_init(void)
{
    return esp_event_loop_create_default();
}
