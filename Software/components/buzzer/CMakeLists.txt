idf_component_register(
    SRCS         "buzzer.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types
    PRIV_REQUIRES geiger_config
    # driver (LEDC), esp_pm added in Steps 3/8
)
