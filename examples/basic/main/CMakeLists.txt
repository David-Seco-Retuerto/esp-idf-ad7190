idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        ad7190
        esp_driver_gpio
)
