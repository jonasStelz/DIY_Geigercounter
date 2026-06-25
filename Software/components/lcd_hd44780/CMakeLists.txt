idf_component_register(
    SRCS         "lcd_hd44780.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES geiger_config
    # driver (GPIO, I2C) added in Step 5
)
