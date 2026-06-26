idf_component_register(
    SRCS         "lcd_hd44780.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES geiger_config driver esp_driver_i2c esp_driver_gpio
)
