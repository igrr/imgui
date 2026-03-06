idf_component_register(
    SRCS "bsp_waveshare.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES "esp_driver_i2c" "esp_lcd"
)
