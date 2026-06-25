idf_component_register(
    SRCS         "power_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES geiger_config
    # esp_pm added in Step 8
)
