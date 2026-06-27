#pragma once

#include "app_types.h"
#include "esp_err.h"

/* Initialize the default ESP event loop. Call once in app_main(). */
esp_err_t app_events_init(void);
