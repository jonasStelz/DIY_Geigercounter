idf_component_register(
    SRCS         "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config power_monitor
)
