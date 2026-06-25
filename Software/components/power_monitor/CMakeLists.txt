idf_component_register(
    SRCS         "power_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config
    # driver (GPIO) added in Step 9
)
