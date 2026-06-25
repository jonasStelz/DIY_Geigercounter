idf_component_register(
    SRCS         "hv_control.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES geiger_config
    # driver (GPIO, ADC) added in Step 7
)
