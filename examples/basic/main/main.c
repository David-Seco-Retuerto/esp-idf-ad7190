/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "ad7190.h"


#define AD7190_SCLK_PIN    GPIO_NUM_12
#define AD7190_CS_PIN      GPIO_NUM_10
#define AD7190_MISO_PIN    GPIO_NUM_13
#define AD7190_MOSI_PIN    GPIO_NUM_11

static const char *TAG = "ad7190_example";


void app_main(void)
{
    /*
     * Example configuration:
     *  - Internal AD7190 clock
     *  - Differential AIN1/AIN2 input
     *  - Bipolar operation
     *  - Input buffer enabled
     *  - PGA gain = 128
     *  - DAT_STA disabled
     *
     * Adjust the GPIOs and analog configuration for your hardware.
     */
    ad7190_mode_cfg_t mode_cfg = {
        .mode = AD7190_MODE_IDLE,
        .dat_sta = false,
        .clock = AD7190_CLK_INT,
        .sinc3 = false,
        .enpar = false,
        .single_cycle = false,
        .rej60 = false,
        .fs = 96,
    };

    ad7190_config_cfg_t config_cfg = {
        .chop = false,
        .refsel = false,
        .channel_mask = AD7190_CH_AIN1_AIN2,
        .burn = false,
        .refdet = false,
        .buffer = true,
        .unipolar = false,
        .gain = AD7190_PGA_GAIN_128,
    };

    ESP_ERROR_CHECK(ad7190_init(
        AD7190_SCLK_PIN,
        AD7190_CS_PIN,
        AD7190_MISO_PIN,
        AD7190_MOSI_PIN,
        &mode_cfg,
        &config_cfg,
        NULL
    ));

    ESP_LOGI(TAG, "AD7190 initialized");

    while (1) {
        ESP_ERROR_CHECK(ad7190_start_single_conversion());

        bool ready = false;

        while (!ready) {
            ESP_ERROR_CHECK(ad7190_is_ready(&ready));

            if (!ready) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

        uint32_t data = 0;
        ESP_ERROR_CHECK(ad7190_conversion_read(&data));

        ESP_LOGI(TAG, "Raw conversion: 0x%06" PRIX32, data);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
