idf_component_register(
    SRCS         "button_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config driver esp_driver_gpio
)
