/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

//============================
// INCLUDES begin
//============================
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include "ad7190_driver.h"
//============================
// INCLUDES end
//============================

//============================
// DEFINES begin
//============================

#define ad7190_TAG "ad7190_driver"
#define AD_SPI SPI3_HOST

#ifndef AD7190_USE_AUTO_CS
    #define AD7190_MANUAL_CS
#endif

// #define DEBUG

ad7190_driver_t ad7190;


static const uint8_t ad7190_reg_sizes[] = {
    [AD7190_COMM_REG] = 1,   // Same register code as STATUS_REG when reading
    [AD7190_MODE_REG] = 3,
    [AD7190_CONFIG_REG] = 3,
    [AD7190_DATA_REG] = 3,
    [AD7190_ID_REG] = 1,
    [AD7190_GPOCON_REG] = 1,
    [AD7190_OFFSET_REG] = 3,
    [AD7190_FULL_SCALE_REG] = 3,
};

//============================
// PRIVATE HELPERS begin
//============================

static bool ad7190_register_is_writable(uint8_t regCode)
{
    switch (regCode) {
        case AD7190_MODE_REG:
        case AD7190_CONFIG_REG:
        case AD7190_GPOCON_REG:
        case AD7190_OFFSET_REG:
        case AD7190_FULL_SCALE_REG:
            return true;

        default:
            return false;
    }
}


static esp_err_t ad7190_validate_spi_pins(int clk_pin,
                                            int cs_pin,
                                            int miso_pin,
                                            int mosi_pin)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(clk_pin)) {
        ESP_LOGE(ad7190_TAG, "Invalid SCLK GPIO: %d", clk_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (!GPIO_IS_VALID_OUTPUT_GPIO(cs_pin)) {
        ESP_LOGE(ad7190_TAG, "Invalid CS GPIO: %d", cs_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (!GPIO_IS_VALID_GPIO(miso_pin)) {
        ESP_LOGE(ad7190_TAG, "Invalid MISO/DOUT-RDY GPIO: %d", miso_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (!GPIO_IS_VALID_OUTPUT_GPIO(mosi_pin)) {
        ESP_LOGE(ad7190_TAG, "Invalid MOSI/DIN GPIO: %d", mosi_pin);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
//============================
// PRIVATE HELPERS end
//============================

//============================
// INIT Functions begin
//============================

esp_err_t ad7190_reset(void)
{
    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    // This function writes 40 consecutive HIGH bits to the ADC, causing it to reset
    
    // Reset buffer
    uint8_t buffer[5] ={
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    } ;

    // Declaration of the base transaction //
    spi_transaction_t tbase = {
        .length = 40,
        .tx_buffer = buffer,
        .flags = SPI_TRANS_VARIABLE_CMD,
    };

    // Extended transaction with no CMD //
    spi_transaction_ext_t t_ext ={
      .base = tbase,                // Base of the extended is the normal transaction
      .command_bits = 0,            // No CMD
      .address_bits = 0,            // No Address
      .dummy_bits = 0,              // No extra dummy bits
    };

    // Poll transaction
    esp_err_t err = spi_device_polling_transmit(ad7190.spi, (spi_transaction_t *)&t_ext);

    if (err != ESP_OK){return err;}

    // Set flags to default
    ad7190.continuous_read_enabled = false;
    ad7190.dat_sta_enabled = false;
    ad7190.flags.disable_cont_read_pending = false;

    // Requires at least 500 us without register access.
    esp_rom_delay_us(1000);
    return err;
}

esp_err_t ad7190_spi_init(int clk_pin, int cs_pin, int miso_pin, int mosi_pin)
{
    if (ad7190.spi != NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    // This function configures and initializes the SPI bus //
    esp_err_t ret = ad7190_validate_spi_pins( clk_pin, cs_pin, miso_pin, mosi_pin);
    if (ret != ESP_OK) {
        return ret;
    }

    #ifdef DEBUG
    ESP_LOGI(ad7190_TAG, "Initializing AD 7190");
    #endif
    // Config SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = miso_pin,
        .mosi_io_num = mosi_pin,
        .sclk_io_num = clk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 80,                  // max transfer size in bytes
    }; 
    // CS Manual //
    #ifdef AD7190_MANUAL_CS

        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.pin_bit_mask = 1ULL << cs_pin;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 0;

        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(ad7190_TAG, "Failed to configure CS GPIO: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = gpio_set_level(cs_pin, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(ad7190_TAG, "Failed to set CS high: %s", esp_err_to_name(ret));
            return ret;
        }

        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = 1 * 1000 * 1000,
            .mode = 3,
            .spics_io_num = -1,
            .queue_size = 7,
            .command_bits = 8,
        };

        ad7190.flags.manual_cs = true;
        ad7190.flags.cs_keep_active = false;

    #else

        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = 1 * 1000 * 1000,
            .mode = 3,
            .spics_io_num = cs_pin,
            .queue_size = 7,
            .command_bits = 8,
        };

        ad7190.flags.manual_cs = false;
        ad7190.flags.cs_keep_active = false;

    #endif
    #ifdef DEBUG
    ESP_LOGI(ad7190_TAG, "Initializing SPI bus");
    #endif

    //Initialize the SPI bus
    ret = spi_bus_initialize(AD_SPI, &buscfg, SPI_DMA_DISABLED); // SPI_DMA_CH_AUTO);
    if(ret != ESP_OK){
        return ret;
    }

    #ifdef DEBUG
    ESP_LOGI(ad7190_TAG, "SPI bus initialized, adding device to bus");
    #endif

    //Attach the ADC to the SPI bus
    ret = spi_bus_add_device(AD_SPI, &devcfg, &ad7190.spi);
    if(ret != ESP_OK){
        spi_bus_free(AD_SPI);
        return ret;
    }

    #ifdef DEBUG
    ESP_LOGI(ad7190_TAG, "Device added to SPI bus");
    #endif

    #ifdef AD7190_MANUAL_CS
        ret = gpio_set_level(cs_pin, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(ad7190_TAG, "Failed to assert CS low: %s", esp_err_to_name(ret));

            spi_bus_remove_device(ad7190.spi);
            ad7190.spi = NULL;
            spi_bus_free(AD_SPI);

            return ret;
        }
        ad7190.cs_pin = (gpio_num_t)cs_pin;
    #endif

    ad7190.drdy_pin = (gpio_num_t)miso_pin;

    // Enable DOUT/RDY as GPIO input for continuous-read readiness checks.
    ret = gpio_input_enable(ad7190.drdy_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to enable DOUT/RDY GPIO input");

        #ifdef AD7190_MANUAL_CS
        (void)gpio_set_level(cs_pin, 1);
        #endif

        spi_bus_remove_device(ad7190.spi);
        ad7190.spi = NULL;
        spi_bus_free(AD_SPI);

        return ret;
    }

    return ret;
}

esp_err_t ad7190_spi_deinit(void)
{
    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t first_err = ESP_OK;

#ifdef AD7190_MANUAL_CS
    if (ad7190.spi != NULL) {
        esp_err_t err = gpio_set_level(ad7190.cs_pin, 1);
        if (err != ESP_OK) {
            ESP_LOGE(ad7190_TAG, "Failed to release CS: %s", esp_err_to_name(err));
            first_err = err;
        }
    }
#endif

    if (ad7190.spi != NULL) {
        esp_err_t err = spi_bus_remove_device(ad7190.spi);

        if (err != ESP_OK) {
            if (first_err == ESP_OK) {
                first_err = err;
            }
        } else {
            ad7190.spi = NULL;
        }
    }

    esp_err_t err = spi_bus_free(AD_SPI);

    if ((err != ESP_OK) &&
        (err != ESP_ERR_INVALID_STATE) &&
        (first_err == ESP_OK)) {
        first_err = err;
    }

    ad7190.continuous_read_enabled = false;
    ad7190.dat_sta_enabled = false;
    ad7190.flags.disable_cont_read_pending = false;
    ad7190.flags.manual_cs = false;
    ad7190.flags.cs_keep_active = false;

    return first_err;
}
//============================
// INIT Functions end
//============================

//============================
// Register Access Functions begin
//============================

esp_err_t ad7190_read_register(uint8_t regCode, uint32_t *data_out){
    if (data_out == NULL) {
        ESP_LOGE(ad7190_TAG, "Invalid data output pointer");
        return ESP_ERR_INVALID_ARG;
    }
    if(regCode > AD7190_FULL_SCALE_REG) {
        ESP_LOGE(ad7190_TAG, "Invalid register code 0x%02X", regCode);
        return ESP_ERR_INVALID_ARG;
    }
    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (ad7190.continuous_read_enabled) {
        ESP_LOGW(ad7190_TAG, "Cannot read register 0x%02X while continuous read is enabled", regCode);
        return ESP_ERR_INVALID_STATE;
    }

    // Prep communication register for a normal register read
    uint8_t comm_reg = 0
        | AD7190_COMM_RW_SELECT(AD7190_COMM_RW_READ)
        | AD7190_COMM_RS_SELECT(regCode)
        | AD7190_COMM_CREAD_SELECT(AD7190_COMM_CREAD_OFF);

    // With DAT_STA enabled, data register becomes 1 byte longer
    uint8_t reglen;
    if ((regCode == AD7190_DATA_REG) && (ad7190.dat_sta_enabled)) {
        reglen = ad7190_reg_sizes[regCode] + 1;
    } else {
        reglen = ad7190_reg_sizes[regCode];
    }

    // Dummy TX buffer to keep DIN low and avoid accidental reset
    uint8_t dummy_tx[4] = {0x00, 0x00, 0x00, 0x00};

    // RX buffer
    uint8_t rx_data[4] = {0x00, 0x00, 0x00, 0x00};

    spi_transaction_t t = {
        .cmd = comm_reg,
        .length = 8 * reglen,   // length is in bits
        .rx_buffer = rx_data,
        .tx_buffer = dummy_tx,
    };

#ifndef AD7190_MANUAL_CS
    if (ad7190.flags.cs_keep_active) {
        t.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
#endif

    esp_err_t err = spi_device_polling_transmit(ad7190.spi, &t);
    if (err != ESP_OK) {
        return err;
    }

    if (reglen == 1) {
        *data_out = (uint32_t)rx_data[0];
    } else if (reglen == 3) {
        *data_out = ((uint32_t)rx_data[0] << 16) |
                    ((uint32_t)rx_data[1] << 8)  |
                    ((uint32_t)rx_data[2]);
    } else if (reglen == 4) {
        *data_out = ((uint32_t)rx_data[0] << 24) |
                    ((uint32_t)rx_data[1] << 16) |
                    ((uint32_t)rx_data[2] << 8)  |
                    ((uint32_t)rx_data[3]);
    } else {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}


esp_err_t ad7190_write_register(uint8_t regCode, uint32_t value)
{
    if (regCode > AD7190_FULL_SCALE_REG) {
        ESP_LOGE(ad7190_TAG, "Invalid register code 0x%02X", regCode);
        return ESP_ERR_INVALID_ARG;
    }
    if (!ad7190_register_is_writable(regCode)) {
        ESP_LOGE(ad7190_TAG, "Register 0x%02X is not writable", regCode);
        return ESP_ERR_INVALID_ARG;
    }
    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (ad7190.continuous_read_enabled) {
        ESP_LOGW(ad7190_TAG, "Cannot write register 0x%02X while continuous read is enabled, disable it with ad7190_continuous_read_stop()", regCode);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t comm_reg = 0
        | AD7190_COMM_RW_SELECT(AD7190_COMM_RW_WRITE)
        | AD7190_COMM_RS_SELECT(regCode)
        | AD7190_COMM_CREAD_SELECT(AD7190_COMM_CREAD_OFF);

    uint8_t reglen = ad7190_reg_sizes[regCode];

    uint8_t regVal[4] = {0};

    if (reglen == 3) {
        regVal[0] = (uint8_t)((value >> 16) & 0xFFU);
        regVal[1] = (uint8_t)((value >> 8)  & 0xFFU);
        regVal[2] = (uint8_t)(value & 0xFFU);
    } else if (reglen == 1) {
        regVal[0] = (uint8_t)(value & 0xFFU);
    } else {
        return ESP_ERR_INVALID_SIZE;
    }

    spi_transaction_t t = {
        .cmd = comm_reg,
        .length = 8 * reglen,
        .tx_buffer = regVal,
    };

#ifndef AD7190_MANUAL_CS
    if (ad7190.flags.cs_keep_active) {
        t.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
#endif

    return spi_device_polling_transmit(ad7190.spi, &t);
}

//============================
// Register Access Functions end
//============================


//============================
// Continuous-Read Mode Functions begin
//============================

esp_err_t ad7190_start_contread(void){

    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ad7190.flags.manual_cs) {
        ESP_LOGE(ad7190_TAG, "Continuous-read mode is not supported with SPI-driven CS");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (ad7190.continuous_read_enabled) {
        ESP_LOGE(ad7190_TAG, "Continuous read already enabled");
        return ESP_ERR_INVALID_STATE;
    }

    // With DAT_STA enabled, data register becomes 1 byte longer
    uint8_t reglen;
    if (ad7190.dat_sta_enabled) {
        reglen = 4;
    } else {
        reglen = 3;
    }

    uint8_t start_instruction[5] = {AD7190_CMD_CONT_READ_ENABLE, 0x00, 0x00, 0x00, 0x00};
    /*
    * Send the continuous-read enable command followed by enough
    * zero clocks to complete any data frame that may already be
    * pending when continuous-read mode is entered.
    *
    * Any pending conversion is intentionally flushed here; normal
    * acquisition starts with the next conversion.
    */
    spi_transaction_t tbase = {
        .length = 8 * (1+reglen),
        .tx_buffer = start_instruction,
        .flags = SPI_TRANS_VARIABLE_CMD,
    };

    // Extended transaction with no CMD //
    spi_transaction_ext_t t_ext ={
      .base = tbase,        // Base of the extended is the normal transaction
      .command_bits = 0,    // No CMD
      .address_bits = 0,    // No Address
      .dummy_bits = 0,      // No extra dummy bits
    };
    // If not in Manual mode --> Keep CS active //
    #ifndef AD7190_MANUAL_CS
        if (ad7190.flags.cs_keep_active){
        t_ext.base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
    #endif
    
    // Poll transmition using extended transmission previously defined//
    esp_err_t err = spi_device_polling_transmit(ad7190.spi, (spi_transaction_t *)&t_ext);
    if (err == ESP_OK) {
        ad7190.continuous_read_enabled = true;
        ad7190.flags.disable_cont_read_pending = false;
    }
    
    return err;
}

static esp_err_t ad7190_stop_contread_now(uint32_t *data_out)
{
    if (ad7190.spi == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ad7190.flags.manual_cs) {
        ESP_LOGE(ad7190_TAG, "Continuous-read mode is not supported with SPI-driven CS");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!ad7190.continuous_read_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * The AD7190 requires the continuous-read disable instruction
     * to be transmitted while RDY is low.
     */
    if (gpio_get_level(ad7190.drdy_pin) != 0) {
        return ESP_ERR_NOT_FINISHED;
    }

    uint8_t reglen = ad7190.dat_sta_enabled ? 4 : 3;

    /*
     * The 0x58 instruction is transmitted while the current data
     * frame is shifted out. One additional zero byte is intentionally
     * clocked as a conservative measure to ensure the transaction
     * completes cleanly.
     */
    uint8_t stop_instruction[5] = {
        AD7190_CMD_CONT_READ_DISABLE,
        0x00, 0x00, 0x00, 0x00
    };

    uint8_t rx_data[5] = {0};

    spi_transaction_t tbase = {
        .length = 8 * (1 + reglen),
        .tx_buffer = stop_instruction,
        .rx_buffer = rx_data,
        .flags = SPI_TRANS_VARIABLE_CMD,
    };

    spi_transaction_ext_t t_ext = {
        .base = tbase,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };

#ifndef AD7190_MANUAL_CS
    if (ad7190.flags.cs_keep_active) {
        t_ext.base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
#endif

    esp_err_t err = spi_device_polling_transmit(
        ad7190.spi,
        (spi_transaction_t *)&t_ext
    );

    if (err != ESP_OK) {
        return err;
    }

    if (reglen == 3) {
        *data_out =
            ((uint32_t)rx_data[0] << 16) |
            ((uint32_t)rx_data[1] << 8) |
            ((uint32_t)rx_data[2]);

    } else {
        *data_out =
            ((uint32_t)rx_data[0] << 24) |
            ((uint32_t)rx_data[1] << 16) |
            ((uint32_t)rx_data[2] << 8) |
            ((uint32_t)rx_data[3]);
    }

    ad7190.continuous_read_enabled = false;
    ad7190.flags.disable_cont_read_pending = false;

    return ESP_OK;
}

esp_err_t ad7190_stop_contread(void)
{
    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ad7190.flags.manual_cs) {
        ESP_LOGE(ad7190_TAG, "Continuous-read mode is not supported with SPI-driven CS");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!ad7190.continuous_read_enabled) {
        ESP_LOGW(ad7190_TAG, "Continuous read is not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (ad7190.flags.disable_cont_read_pending) {
        ESP_LOGW(ad7190_TAG, "Continuous read stop already requested");
        return ESP_ERR_INVALID_STATE;
    }

    
    ad7190.flags.disable_cont_read_pending = true;

    return ESP_OK;
}

esp_err_t ad7190_data_contread(uint32_t *data){
    // This function reads data directly from DOUT by clocking it out
    if (data == NULL) {
        ESP_LOGE(ad7190_TAG, "Invalid data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ad7190.flags.manual_cs) {
        ESP_LOGE(ad7190_TAG, "Continuous-read mode is not supported with SPI-driven CS");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!ad7190.continuous_read_enabled) {
        ESP_LOGW(ad7190_TAG, "Continuous read is not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (ad7190.flags.disable_cont_read_pending) {
        ESP_LOGW(ad7190_TAG, "Continuous read stop already requested");
        return ESP_ERR_INVALID_STATE;
    }

    if (gpio_get_level(ad7190.drdy_pin) != 0) {
        return ESP_ERR_NOT_FINISHED;
    }

    uint8_t dummy_tx[4] = {0x00, 0x00, 0x00, 0x00};

    // With DAT_STA enabled, data register becomes 1 byte longer
    uint8_t reglen = ad7190.dat_sta_enabled ? 4 : 3;

    spi_transaction_t tbase = {
        .length = reglen * 8,
        .tx_buffer = dummy_tx,
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_VARIABLE_CMD,
    };

    spi_transaction_ext_t t_ext = {
        .base = tbase,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };

#ifndef AD7190_MANUAL_CS
    if (ad7190.flags.cs_keep_active) {
        t_ext.base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
#endif

    esp_err_t err = spi_device_polling_transmit(ad7190.spi, (spi_transaction_t *)&t_ext);
    if (err != ESP_OK) {
        return err;
    }

    if (reglen == 3) {
        *data = ((uint32_t)t_ext.base.rx_data[0] << 16) |
                ((uint32_t)t_ext.base.rx_data[1] << 8)  |
                ((uint32_t)t_ext.base.rx_data[2]);
    } else {
        *data = ((uint32_t)t_ext.base.rx_data[0] << 24) |
                ((uint32_t)t_ext.base.rx_data[1] << 16) |
                ((uint32_t)t_ext.base.rx_data[2] << 8)  |
                ((uint32_t)t_ext.base.rx_data[3]);
    }

    return ESP_OK;
}

//============================
// Continuous-Read Mode Functions end
//============================


//============================
// Data Acquisition Functions begin
//============================

esp_err_t ad7190_read_data(uint32_t *data){
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ad7190.spi == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t sample = 0;
    esp_err_t err = ESP_OK;

    if (!ad7190.continuous_read_enabled) {
        // Normally read data register
        err = ad7190_read_register(AD7190_DATA_REG, &sample);

    } else if (ad7190.flags.disable_cont_read_pending) {
        // Send stop continuous read and read sample
        err = ad7190_stop_contread_now(&sample);

    } else {
        // Read in continuous read mode
        err = ad7190_data_contread(&sample);
    }

    if (err == ESP_OK) {
        *data = sample;
    }

    return err;
}

//============================
// Data Acquisition Functions end
//============================

//============================
// REGISTER ENCODING Functions begin
//============================

uint32_t ad7190_mode_encode(const ad7190_mode_cfg_t *cfg)
{
    uint32_t value =
        AD7190_MODE_MODE_SELECT(cfg->mode) |            // MR23–MR21: Operating mode (3 bits)
        AD7190_MODE_DAT_STA_SELECT(cfg->dat_sta) |      // MR20: Append status to data register
        AD7190_MODE_CLOCK_SELECT(cfg->clock) |          // MR19–MR18: Clock source (2 bits)
        AD7190_MODE_SINC3_SELECT(cfg->sinc3) |          // MR15: SINC3 filter enable
        AD7190_MODE_ENPAR_SELECT(cfg->enpar) |          // MR13: Enable parity
        AD7190_MODE_SCYCLE_SELECT(cfg->single_cycle) |  // MR11: Single cycle settling
        AD7190_MODE_REJ60_SELECT(cfg->rej60) |          // MR10: 60 Hz rejection
        AD7190_MODE_FILTER_SELECT(cfg->fs);             // MR9–MR0: Filter select (10 bits)

    return value;
}

uint32_t ad7190_config_encode(const ad7190_config_cfg_t *cfg)
{
    uint32_t value =
        AD7190_CONF_CHOP_SELECT(cfg->chop) |             // CON23: Chop enable (offset drift reduction)
        AD7190_CONF_REFSEL_SELECT(cfg->refsel) |         // CON20: Reference select
        AD7190_CONF_CHANNEL_SELECT(cfg->channel_mask) |  // CON15–CON8: Channel select bits (mask)
        AD7190_CONF_BURN_SELECT(cfg->burn) |             // CON7: Burnout current enable
        AD7190_CONF_REFDET_SELECT(cfg->refdet) |         // CON6: Reference detect enable
        AD7190_CONF_BUF_SELECT(cfg->buffer) |            // CON4: Input buffer enable
        AD7190_CONF_POLSEL_SELECT(cfg->unipolar) |       // CON3: Unipolar/Bipolar selection
        AD7190_CONF_PGA_SELECT(cfg->gain);               // CON2–CON0: PGA gain (3 bits)

    return value;
}

uint32_t ad7190_gpocon_encode(const ad7190_gpocon_cfg_t *cfg)
{
    uint32_t value =
        AD7190_GPOCON_BPDSW_SELECT(cfg->bpdsw) |        // GP6: Bridge power-down switch control
        AD7190_GPOCON_GP32EN_SELECT(cfg->gp32en) |      // GP5: Enable P3/P2 as digital outputs
        AD7190_GPOCON_GP10EN_SELECT(cfg->gp10en) |      // GP4: Enable P1/P0 as digital outputs
        AD7190_GPOCON_P3DAT_SELECT(cfg->p3dat) |        // GP3: Output value on P3
        AD7190_GPOCON_P2DAT_SELECT(cfg->p2dat) |        // GP2: Output value on P2
        AD7190_GPOCON_P1DAT_SELECT(cfg->p1dat) |        // GP1: Output value on P1
        AD7190_GPOCON_P0DAT_SELECT(cfg->p0dat);         // GP0: Output value on P0

    // GP7 must always be 0 → implicitly satisfied

    return value;
}

uint32_t ad7190_offset_encode(const ad7190_offset_cfg_t *cfg)
{
    uint32_t value = AD7190_OFFSET_SELECT(cfg->value);  // 24-bit offset calibration coefficient
    return value;
}

uint32_t ad7190_fullscale_encode(const ad7190_fullscale_cfg_t *cfg)
{
    uint32_t value = AD7190_FULLSCALE_SELECT(cfg->value);  // 24-bit full-scale calibration coefficient
    return value;
}


//============================
// REGISTER ENCODING Functions end
//============================