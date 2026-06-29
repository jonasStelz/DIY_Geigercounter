idf_component_register(
    SRCS
        "main.c"
        "app_events.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_types
        geiger_config
        esp_event
        geiger_core
        button_handler
        lcd_hd44780
        buzzer
        ui_menu
        hv_control
        driver
)
