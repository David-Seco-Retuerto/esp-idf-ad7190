idf_component_register(
    SRCS
        "ad7190.c"
        "ad7190_driver.c"

    INCLUDE_DIRS
        "include"

    PRIV_INCLUDE_DIRS
        "private_include"

    REQUIRES
        esp_common

    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_gpio
)