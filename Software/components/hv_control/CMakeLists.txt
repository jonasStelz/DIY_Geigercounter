idf_component_register(
    SRCS         "hv_control.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES geiger_config driver esp_driver_gpio
    # esp_adc added back here when software regulation is re-enabled
)
