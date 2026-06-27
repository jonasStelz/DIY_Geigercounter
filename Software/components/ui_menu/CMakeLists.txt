idf_component_register(
    SRCS         "ui_menu.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types esp_event
    PRIV_REQUIRES geiger_config lcd_hd44780 statistics
)
