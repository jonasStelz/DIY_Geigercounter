idf_component_register(
    SRCS         "button_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config
    # driver (GPIO interrupts) added in Step 4
)
