idf_component_register(
    SRCS         "ui_menu.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config lcd_hd44780 geiger_core
)
