idf_component_register(
    SRCS         "geiger_core.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config pulse_counter statistics esp_event ui_menu
)
