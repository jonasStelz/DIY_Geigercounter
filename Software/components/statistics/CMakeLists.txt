idf_component_register(
    SRCS         "statistics.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config
    # nvs_flash added in Step 9
)
