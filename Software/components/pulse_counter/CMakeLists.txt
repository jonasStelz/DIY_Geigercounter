idf_component_register(
    SRCS         "pulse_counter.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES geiger_config driver esp_driver_pcnt
)
