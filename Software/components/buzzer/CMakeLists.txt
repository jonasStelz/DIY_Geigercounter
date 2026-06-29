idf_component_register(
    SRCS         "buzzer.c"
    INCLUDE_DIRS "include"
    REQUIRES     app_types esp_event ui_menu
    PRIV_REQUIRES geiger_config driver esp_driver_ledc
    # esp_pm added in Step 8 when PM lock is activated
)
