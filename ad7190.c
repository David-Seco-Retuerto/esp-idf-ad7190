/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

//============================
// INCLUDES begin
//============================
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "driver/gpio.h"

#include "ad7190.h"
#include "ad7190_driver.h"
//============================
// INCLUDES end
//============================

//============================
// DEFINES begin
//============================

#define ad7190_TAG "ad7190"

//============================
// DEFINES end
//============================

//============================
// Private Structs begin
//============================

typedef struct {
    ad7190_mode_cfg_t mode;
    ad7190_config_cfg_t config;
    ad7190_gpocon_cfg_t gpocon;

    bool mode_valid;
    bool config_valid;
    bool gpocon_valid;

    bool single_conversion_active;
    uint16_t single_expected_channels;
    uint16_t single_seen_channels;
} ad7190_user_state_t;

//============================
// Private Structs end
//============================

//============================
// PV begin
//============================

extern ad7190_driver_t ad7190;
static ad7190_user_state_t s_user = {0};

//============================
// PV end
//============================

//============================
// PRIVATE HELPERS begin
//============================

/** @brief Clears the software tracking state of a single-conversion sequence. */
static void ad7190_single_conversion_tracking_clear(void)
{
    s_user.single_conversion_active = false;
    s_user.single_expected_channels = 0U;
    s_user.single_seen_channels = 0U;
}


/** @brief Marks the current single-conversion sequence as complete.
 *
 * After one conversion has been performed on every enabled channel, the
 * AD7190 automatically enters power-down mode.
 */
static void ad7190_single_conversion_complete(void)
{
    ad7190_single_conversion_tracking_clear();

    if (s_user.mode_valid) {
        s_user.mode.mode = AD7190_MODE_PDOWN;
    }
}


/** @brief Checks whether calibration-register writes are currently allowed.
 *
 * The AD7190 offset and full-scale calibration registers may only be written
 * while the device is in idle or power-down mode.
 *
 * This helper validates the cached user-level operating-mode state before a
 * calibration coefficient is written. It does not perform any SPI transaction
 * or read back the hardware mode register.
 *
 * The check is intentionally conservative: a valid cached mode must exist and
 * the device must be tracked as AD7190_MODE_IDLE or AD7190_MODE_PDOWN.
 *
 * @note The check relies on the cached driver state. Calibration operations
 * started through ad7190_calibration_start() must be completed through
 * ad7190_calibration_is_complete() so that the cached mode is synchronized
 * with the hardware state.
 *
 * @return
 *      - ESP_OK if calibration-register writes are allowed
 *      - ESP_ERR_INVALID_STATE if no valid cached mode configuration exists
 *        or the cached operating mode is neither idle nor power-down
 */
static esp_err_t ad7190_cal_register_write_check(void)
{
    if (!s_user.mode_valid) {
        ESP_LOGE(ad7190_TAG, "Mode state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if ((s_user.mode.mode != AD7190_MODE_IDLE) &&
        (s_user.mode.mode != AD7190_MODE_PDOWN)) {
        ESP_LOGE(ad7190_TAG, "Calibration registers can only be written in idle or power-down mode");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}


/** @brief Checks whether an AD7190 operating mode is a calibration mode.
 *
 * @param mode AD7190 operating-mode code.
 *
 * @return True for IZC, IFC, SZC or ZFC; false otherwise.
 */
static bool ad7190_mode_is_calibration(uint8_t mode)
{
    return (mode == AD7190_MODE_IZC) ||
            (mode == AD7190_MODE_IFC) ||
            (mode == AD7190_MODE_SZC) ||
            (mode == AD7190_MODE_ZFC);
}


static esp_err_t ad7190_cal_channel_check(void)
{
    if (!s_user.config_valid) {
        ESP_LOGE(ad7190_TAG, "Config state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t channels = s_user.config.channel_mask;

    if ((channels == 0U) ||
        ((channels & (channels - 1U)) != 0U)) {

        ESP_LOGE(ad7190_TAG, "Calibration requires exactly one enabled channel");
        return ESP_ERR_INVALID_STATE;
    }

    if (channels == AD7190_CH_TEMP_SENSOR) {
        ESP_LOGE(ad7190_TAG, "Temperature sensor channel has no calibration registers");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

//============================
// PRIVATE HELPERS end
//============================

//============================
// INIT Functions begin
//============================

/** @brief Clears the cached user-level AD7190 configuration state.
 *
 * This internal helper resets the stored configuration copies and validity
 * flags used by the convenience setter functions.
 */
static void ad7190_user_state_reset(void)
{
    memset(&s_user, 0, sizeof(s_user));
}

esp_err_t ad7190_init(int clk_pin,
                    int cs_pin,
                    int miso_pin,
                    int mosi_pin,
                    const ad7190_mode_cfg_t *mode_cfg,
                    const ad7190_config_cfg_t *config_cfg,
                    const ad7190_gpocon_cfg_t *gpocon_cfg)
{
    if (ad7190.spi != NULL) {
        ESP_LOGE(ad7190_TAG, "AD7190 already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err;

    /* -------------------------------------------------------------------------
     * Validate mandatory initialization configurations
     * ------------------------------------------------------------------------- */
    if ((mode_cfg == NULL) || (config_cfg == NULL)) {
        ESP_LOGE(ad7190_TAG, "Mode and config initial settings must not be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* -------------------------------------------------------------------------
     * Reset cached user-level configuration state
     * ------------------------------------------------------------------------- */
    ad7190_user_state_reset();
    ESP_LOGI(ad7190_TAG, "User state reset to defaults");
    
    /* -------------------------------------------------------------------------
     * Initialize SPI interface
     * ------------------------------------------------------------------------- */
    err = ad7190_spi_init(clk_pin, cs_pin, miso_pin, mosi_pin);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "SPI init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(ad7190_TAG, "SPI initialized successfully");
    
    /* -------------------------------------------------------------------------
     * Reset AD7190 serial interface and internal registers
     * ------------------------------------------------------------------------- */
    err = ad7190_reset();
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "ADC reset failed: %s", esp_err_to_name(err));
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return err;
    }
    ESP_LOGI(ad7190_TAG, "ADC reset successful");


    /* -------------------------------------------------------------------------
     * Read and validate device ID
     *
     * Only the defined AD7190 identification bits are checked. The remaining
     * bits of the ID register are ignored through AD7190_ID_MASK.
     * ------------------------------------------------------------------------- */
    uint32_t reg = 0;

    err = ad7190_read_register(AD7190_ID_REG, &reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to read AD7190 ID register: %s", esp_err_to_name(err));
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return err;
    }

    uint8_t id = (uint8_t)(reg & 0xFFU);

    if ((id & AD7190_ID_MASK) != AD7190_ID_VALUE) {
        ESP_LOGE(ad7190_TAG, "Unexpected AD7190 ID: 0x%02X", id);
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(ad7190_TAG, "AD7190 detected, ID: 0x%02X", id);


    /* -------------------------------------------------------------------------
     * Apply initial MODE register configuration
     * ------------------------------------------------------------------------- */
    err = ad7190_mode_apply(mode_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Initial mode apply failed: %s", esp_err_to_name(err));
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return err;
    }
    ESP_LOGI(ad7190_TAG, "Initial mode configuration applied successfully");

    /* Read back MODE register to verify SPI communication/configuration */
    reg = 0;
    err = ad7190_read_register(AD7190_MODE_REG, &reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to read back MODE register: %s", esp_err_to_name(err));
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return err;
    }
    ESP_LOGI(ad7190_TAG, "AD7190 MODE REG after apply: 0x%06X", (unsigned int)reg);


    /* -------------------------------------------------------------------------
     * Apply initial CONFIGURATION register settings
     * ------------------------------------------------------------------------- */
    err = ad7190_config_apply(config_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Initial config apply failed: %s", esp_err_to_name(err));
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return err;
    }
    ESP_LOGI(ad7190_TAG, "Initial config configuration applied successfully");

    /* Read back CONFIGURATION register */
    reg = 0;
    err = ad7190_read_register(AD7190_CONFIG_REG, &reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to read back CONFIG register: %s", esp_err_to_name(err));
        esp_err_t cleanup_err = ad7190_spi_deinit();
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
        }
        ad7190_user_state_reset();
        return err;
    }
    ESP_LOGI(ad7190_TAG, "AD7190 CONFIG REG after apply: 0x%06X", (unsigned int)reg);


    /* -------------------------------------------------------------------------
     * Apply optional GPOCON configuration
     *
     * GPOCON configuration is optional during initialization. If no
     * configuration is provided, the register is left at its reset/default
     * state.
     * ------------------------------------------------------------------------- */
    if (gpocon_cfg != NULL) {
        err = ad7190_gpocon_apply(gpocon_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(ad7190_TAG, "Initial GPOCON apply failed: %s", esp_err_to_name(err));
            esp_err_t cleanup_err = ad7190_spi_deinit();
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
            }
            ad7190_user_state_reset();
            return err;
        }

        ESP_LOGI(ad7190_TAG, "Initial GPOCON configuration applied successfully");

        /* Read back GPOCON register */
        reg = 0;
        err = ad7190_read_register(AD7190_GPOCON_REG, &reg);
        if (err != ESP_OK) {
            ESP_LOGE(ad7190_TAG, "Failed to read back GPOCON register: %s", esp_err_to_name(err));
            esp_err_t cleanup_err = ad7190_spi_deinit();
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(ad7190_TAG, "SPI cleanup failed: %s", esp_err_to_name(cleanup_err));
            }
            ad7190_user_state_reset();
            return err;
        }

        ESP_LOGI(ad7190_TAG, "AD7190 GPOCON REG after apply: 0x%02X", (unsigned int)reg);
    }

    /* -------------------------------------------------------------------------
     * Initialization complete
     * ------------------------------------------------------------------------- */
    ESP_LOGI(ad7190_TAG, "Initialization successful");
    return ESP_OK;
}


esp_err_t ad7190_device_reset(void)
{
    if (ad7190.spi == NULL) {
        ESP_LOGE(ad7190_TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ad7190_reset();
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "ADC reset failed: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * The hardware registers have returned to their reset values.
     * Invalidate the cached high-level configuration accordingly.
     */
    ad7190_user_state_reset();

    ESP_LOGI(ad7190_TAG, "ADC reset successful");

    return ESP_OK;
}

esp_err_t ad7190_deinit(void)
{
    esp_err_t err = ad7190_spi_deinit();

    /*
     * Invalidate the high-level cached state regardless of cleanup errors.
     * After a deinitialization attempt, cached configuration must not be
     * considered usable.
     */
    ad7190_user_state_reset();

    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG,
                "AD7190 deinitialization failed: %s",
                esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

//============================
// INIT Functions end
//============================


//============================
// VALIDATION Functions begin
//============================

/** @brief Validates an AD7190 mode-register configuration.
 *
 * Checks that the mode, clock source and filter-select values fit within the
 * AD7190 mode-register bit fields. Also verifies that parity is not enabled
 * unless DAT_STA is enabled, since parity information is only available when
 * the status byte is appended to the data register.
 *
 * @param cfg Pointer to the mode-register configuration.
 *
 * @return
 *      - ESP_OK if the configuration is valid
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or any field is out of range
 */
static esp_err_t ad7190_mode_validate(const ad7190_mode_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(ad7190_TAG, "Mode config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->mode > 7u) {
        ESP_LOGE(ad7190_TAG, "Invalid mode value: %u", cfg->mode);
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->clock > 3u) {
        ESP_LOGE(ad7190_TAG, "Invalid clock value: %u", cfg->clock);
        return ESP_ERR_INVALID_ARG;
    }

    if ((cfg->fs == 0u) || (cfg->fs > 1023u)) {
        ESP_LOGE(ad7190_TAG, "Invalid FS value: %u", cfg->fs);
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->enpar && !cfg->dat_sta) {
        ESP_LOGE(ad7190_TAG, "ENPAR requires DAT_STA to be enabled");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}


/** @brief Checks whether an AD7190 PGA gain code is valid.
 *
 * The AD7190 only supports the gain codes corresponding to gains
 * 1, 8, 16, 32, 64 and 128. Reserved gain codes are rejected.
 *
 * @param gain AD7190 PGA gain code.
 *
 * @return
 *      - true if the gain code is valid
 *      - false otherwise
 */
static bool ad7190_gain_is_valid(uint8_t gain)
{
    switch (gain) {
        case AD7190_PGA_GAIN_1:
        case AD7190_PGA_GAIN_8:
        case AD7190_PGA_GAIN_16:
        case AD7190_PGA_GAIN_32:
        case AD7190_PGA_GAIN_64:
        case AD7190_PGA_GAIN_128:
            return true;

        default:
            return false;
    }
}


/** @brief Validates an AD7190 configuration-register structure.
 *
 * Checks that the selected channel mask only uses valid AD7190 channel bits,
 * that at least one channel is enabled, and that the selected PGA gain is
 * within the supported range.
 *
 * @param cfg Pointer to the configuration-register structure.
 *
 * @return
 *      - ESP_OK if the configuration is valid
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or any field is out of range
 */
static esp_err_t ad7190_config_validate(const ad7190_config_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(ad7190_TAG, "Configuration pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if ((cfg->channel_mask & ~0x00FFu) != 0u) {
        ESP_LOGE(ad7190_TAG, "Invalid channel mask: 0x%04X", cfg->channel_mask);
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->channel_mask == 0u) {
        ESP_LOGE(ad7190_TAG, "At least one channel must be enabled");
        return ESP_ERR_INVALID_ARG;
    }

    if (!ad7190_gain_is_valid(cfg->gain)) {
        ESP_LOGE(ad7190_TAG, "Invalid PGA gain code: %u", cfg->gain);
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->burn && !cfg->buffer) {
        ESP_LOGE(ad7190_TAG,
                "Burnout currents require the input buffer to be enabled");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->burn && cfg->chop) {
        ESP_LOGE(ad7190_TAG,
                "Burnout currents require chop to be disabled");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/** @brief Validates an AD7190 GPOCON-register configuration.
 *
 * Currently this function only checks that the configuration pointer is valid.
 * The individual GPOCON fields are expected to be boolean or already limited
 * by their corresponding configuration types.
 *
 * @param cfg Pointer to the GPOCON-register configuration.
 *
 * @return
 *      - ESP_OK if the configuration is valid
 *      - ESP_ERR_INVALID_ARG if cfg is NULL
 */
static esp_err_t ad7190_gpocon_validate(const ad7190_gpocon_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(ad7190_TAG, "GPOCON config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/** @brief Validates an AD7190 offset-register calibration value.
 *
 * Checks that the offset calibration coefficient fits in the 24-bit AD7190
 * offset register.
 *
 * @param cfg Pointer to the offset-register configuration.
 *
 * @return
 *      - ESP_OK if the configuration is valid
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or the value exceeds 24 bits
 */
static esp_err_t ad7190_offset_validate(const ad7190_offset_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(ad7190_TAG, "Offset config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->value > 0xFFFFFFu) {
        ESP_LOGE(ad7190_TAG, "Invalid offset value: 0x%08" PRIX32, cfg->value);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/** @brief Validates an AD7190 full-scale-register calibration value.
 *
 * Checks that the full-scale calibration coefficient fits in the 24-bit AD7190
 * full-scale register.
 *
 * @param cfg Pointer to the full-scale-register configuration.
 *
 * @return
 *      - ESP_OK if the configuration is valid
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or the value exceeds 24 bits
 */
static esp_err_t ad7190_fullscale_validate(const ad7190_fullscale_cfg_t *cfg)
{
    if (cfg == NULL) {
        ESP_LOGE(ad7190_TAG, "Fullscale config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->value > 0xFFFFFFu) {
        ESP_LOGE(ad7190_TAG, "Invalid fullscale value: 0x%08" PRIX32, cfg->value);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

//============================
// VALIDATE Functions end
//============================

//============================
// Configuration Apply Functions begin
//============================
esp_err_t ad7190_mode_apply(const ad7190_mode_cfg_t *cfg)
{
    esp_err_t err = ad7190_mode_validate(cfg);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * SINGLE and calibration modes are transient operations and require
     * dedicated lifecycle handling.
     */
    if (cfg->mode == AD7190_MODE_SINGLE) {
        ESP_LOGE(ad7190_TAG, "Use ad7190_start_single_conversion() for single-conversion mode");
        return ESP_ERR_INVALID_ARG;
    }

    if (ad7190_mode_is_calibration(cfg->mode)) {
        ESP_LOGE(ad7190_TAG,
                "Use ad7190_calibration_start() for calibration modes");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t reg = ad7190_mode_encode(cfg);

    err = ad7190_write_register(AD7190_MODE_REG, reg);

    if (err == ESP_OK) {
        ad7190.dat_sta_enabled = cfg->dat_sta;

        s_user.mode = *cfg;
        s_user.mode_valid = true;

        /*
         * Explicitly applying another persistent mode terminates any
         * outstanding single-conversion tracking.
         */
        ad7190_single_conversion_tracking_clear();

    } else {
        ESP_LOGE(ad7190_TAG, "Failed to apply mode configuration: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t ad7190_config_apply(const ad7190_config_cfg_t *cfg)
{
    esp_err_t err = ad7190_config_validate(cfg);
    if (err != ESP_OK) {
        return err;
    }

    if (s_user.single_conversion_active) {
        ESP_LOGE(ad7190_TAG, "Cannot change configuration during a single-conversion sequence");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_user.mode_valid && ad7190_mode_is_calibration(s_user.mode.mode)) {
        ESP_LOGE(ad7190_TAG, "Cannot change configuration during calibration");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t reg = ad7190_config_encode(cfg);

    err = ad7190_write_register(AD7190_CONFIG_REG, reg);

    if (err == ESP_OK) {
        s_user.config = *cfg;
        s_user.config_valid = true;

    } else {
        ESP_LOGE(ad7190_TAG, "Failed to apply config configuration: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t ad7190_gpocon_apply(const ad7190_gpocon_cfg_t *cfg)
{
    esp_err_t err = ad7190_gpocon_validate(cfg);
    if(err != ESP_OK){
        return err;
    }

    uint32_t reg = ad7190_gpocon_encode(cfg);
    err =  ad7190_write_register(AD7190_GPOCON_REG, reg);
    if (err == ESP_OK) {
        s_user.gpocon = *cfg;
        s_user.gpocon_valid = true;
    } else {
        ESP_LOGE(ad7190_TAG, "Failed to apply gpocon configuration: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t ad7190_offset_apply(const ad7190_offset_cfg_t *cfg)
{
    esp_err_t err = ad7190_offset_validate(cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = ad7190_cal_register_write_check();
    if (err != ESP_OK) {
        return err;
    }

    err = ad7190_cal_channel_check();
    if (err != ESP_OK) {
        return err;
    }

    uint32_t reg = ad7190_offset_encode(cfg);

    err = ad7190_write_register(AD7190_OFFSET_REG, reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to apply offset configuration: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t ad7190_fullscale_apply(const ad7190_fullscale_cfg_t *cfg)
{
    esp_err_t err = ad7190_fullscale_validate(cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = ad7190_cal_register_write_check();
    if (err != ESP_OK) {
        return err;
    }

    err = ad7190_cal_channel_check();
    if (err != ESP_OK) {
        return err;
    }

    uint32_t reg = ad7190_fullscale_encode(cfg);

    err = ad7190_write_register(AD7190_FULL_SCALE_REG, reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to apply fullscale configuration: %s", esp_err_to_name(err));
    }

    return err;
}
//============================
// Configuration Apply Functions end
//============================

// ===========================
// Internal User-State Helpers begin
// ===========================

esp_err_t ad7190_set_gain(uint8_t gain)
{
    if (!s_user.config_valid) {
        ESP_LOGE(ad7190_TAG, "Config state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ad7190_config_cfg_t cfg = s_user.config;
    cfg.gain = gain;

    return ad7190_config_apply(&cfg);
}

esp_err_t ad7190_set_single_channel(uint16_t channel_mask)
{
    if (!s_user.config_valid) {
        ESP_LOGE(ad7190_TAG, "Config state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if ((channel_mask == 0u) || ((channel_mask & ~0x00FFu) != 0u)) {
        ESP_LOGE(ad7190_TAG, "Invalid channel mask: 0x%04X", channel_mask);
        return ESP_ERR_INVALID_ARG;
    }

    if ((channel_mask & (channel_mask - 1u)) != 0u) {
        ESP_LOGE(ad7190_TAG, "Single-channel selection requires exactly one active bit");
        return ESP_ERR_INVALID_ARG;
    }

    ad7190_config_cfg_t cfg = s_user.config;
    cfg.channel_mask = channel_mask;

    return ad7190_config_apply(&cfg);
}

esp_err_t ad7190_set_filter(uint16_t fs)
{
    if (!s_user.mode_valid) {
        ESP_LOGE(ad7190_TAG, "Mode state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ad7190_mode_cfg_t cfg = s_user.mode;
    cfg.fs = fs;

    return ad7190_mode_apply(&cfg);
}

esp_err_t ad7190_enable_status_append(bool enable)
{
    if (!s_user.mode_valid) {
        ESP_LOGE(ad7190_TAG, "Mode state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ad7190_mode_cfg_t cfg = s_user.mode;
    cfg.dat_sta = enable;

    return ad7190_mode_apply(&cfg);
}

// ===========================
// Internal User-State Helpers end
// ===========================


//============================
// Status and Conversion Functions begin
//============================

esp_err_t ad7190_status_read(uint8_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t reg = 0;
    esp_err_t err = ad7190_read_register(AD7190_STATUS_REG, &reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to read status register: %s", esp_err_to_name(err));
        return err;
    }

    *status = (uint8_t)(reg & 0xFFu);
    return ESP_OK;
}

esp_err_t ad7190_is_ready(bool *ready)
{
    if (ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ad7190.spi == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ad7190.continuous_read_enabled) {
        *ready = (gpio_get_level(ad7190.drdy_pin) == 0);
        return ESP_OK;
    }

    uint8_t status = 0;
    esp_err_t err = ad7190_status_read(&status);
    if (err != ESP_OK) {
        return err;
    }

    *ready = ((status & (1U << AD7190_STATUS_RDY_POS)) == 0U);

    return ESP_OK;
}

esp_err_t ad7190_start_single_conversion(void)
{
    if (!s_user.mode_valid || !s_user.config_valid) {
        ESP_LOGE(ad7190_TAG, "Mode/config state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_user.single_conversion_active) {
        ESP_LOGW(ad7190_TAG, "Single-conversion sequence already active");
        return ESP_ERR_INVALID_STATE;
    }

    if (ad7190_mode_is_calibration(s_user.mode.mode)) {
        ESP_LOGE(ad7190_TAG, "Cannot start single conversion during calibration");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t channels = s_user.config.channel_mask;

    if (channels == 0U) {
        ESP_LOGE(ad7190_TAG, "Single conversion requires at least one enabled channel");
        return ESP_ERR_INVALID_STATE;
    }

    bool multi_channel =
        ((channels & (channels - 1U)) != 0U);

    /*
     * With several channels enabled, the status byte is required so the
     * driver can identify which channel produced each conversion.
     */
    if (multi_channel && !s_user.mode.dat_sta) {
        ESP_LOGE(ad7190_TAG, "Multi-channel single conversion requires DAT_STA");
        return ESP_ERR_INVALID_STATE;
    }

    ad7190_mode_cfg_t cfg = s_user.mode;
    cfg.mode = AD7190_MODE_SINGLE;

    uint32_t reg = ad7190_mode_encode(&cfg);

    esp_err_t err = ad7190_write_register(AD7190_MODE_REG, reg);
    if (err != ESP_OK) {
        return err;
    }

    s_user.mode = cfg;
    s_user.mode_valid = true;

    s_user.single_expected_channels = channels;
    s_user.single_seen_channels = 0U;
    s_user.single_conversion_active = true;

    return ESP_OK;

}

esp_err_t ad7190_conversion_read(uint32_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ad7190.dat_sta_enabled) {
        ESP_LOGE(ad7190_TAG, "DAT_STA enabled; use ad7190_conversion_read_ex()");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * During a single-conversion sequence, make sure a new conversion
     * is actually available before reading the data register.
     */
    if (s_user.single_conversion_active) {
        bool ready = false;

        esp_err_t err = ad7190_is_ready(&ready);
        if (err != ESP_OK) {
            return err;
        }

        if (!ready) {
            return ESP_ERR_NOT_FINISHED;
        }
    }

    esp_err_t err = ad7190_read_data(data);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * A tracked SINGLE sequence without DAT_STA can only contain one
     * enabled channel, so one successful read completes the sequence.
     */
    if (s_user.single_conversion_active) {
        ad7190_single_conversion_complete();
    }

    return ESP_OK;
}

esp_err_t ad7190_conversion_read_ex(uint32_t *data, uint8_t *status)
{
    if ((data == NULL) || (status == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ad7190.dat_sta_enabled) {
        ESP_LOGE(ad7190_TAG, "DAT_STA not enabled");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * During a single-conversion sequence, make sure a new conversion
     * is actually available before reading the data register.
     */
    if (s_user.single_conversion_active) {
        bool ready = false;

        esp_err_t err = ad7190_is_ready(&ready);
        if (err != ESP_OK) {
            return err;
        }

        if (!ready) {
            return ESP_ERR_NOT_FINISHED;
        }
    }

    uint32_t raw = 0U;

    esp_err_t err = ad7190_read_data(&raw);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NOT_FINISHED) {
            ESP_LOGE(ad7190_TAG, "Failed to read conversion with status: %s", esp_err_to_name(err));
        }

        return err;
    }

    *data = (raw >> 8) & 0x00FFFFFFU;
    *status = (uint8_t)(raw & 0xFFU);

    if (s_user.single_conversion_active) {

        uint8_t channel =
            AD7190_STATUS_CHANNEL_GET(*status);

        uint16_t channel_bit =
            (uint16_t)(1U << channel);

        /*
         * The received channel must belong to the channel mask that was
         * active when the SINGLE sequence was started.
         */
        if ((s_user.single_expected_channels & channel_bit) == 0U) {
            ESP_LOGE(ad7190_TAG, "Unexpected channel %u during single-conversion sequence", channel);
            return ESP_ERR_INVALID_RESPONSE;
        }

        /*
         * Record the channel actually received. Using a bit mask avoids
         * counting the same channel more than once.
         */
        s_user.single_seen_channels |= channel_bit;

        /*
         * The sequence is considered successfully consumed only after a
         * conversion has been received from every channel that was enabled
         * when the sequence started.
         */
        if (s_user.single_seen_channels ==
            s_user.single_expected_channels) {

            ad7190_single_conversion_complete();
        }
    }

    return ESP_OK;
}


//============================
// Status and Conversion Functions end
//============================

//============================
// Continuous Read Mode Functions begin
//============================

esp_err_t ad7190_continuous_read_start(void)
{
    if (!s_user.mode_valid) {
        ESP_LOGE(ad7190_TAG, "Mode state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_user.mode.mode != AD7190_MODE_CONT) {
        ESP_LOGE(ad7190_TAG,
                "Continuous read requires continuous conversion mode");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ad7190_start_contread();

    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG,
                "Failed to start continuous read: %s",
                esp_err_to_name(err));
    }

    return err;
}

esp_err_t ad7190_continuous_read_stop(void)
{
    esp_err_t err = ad7190_stop_contread();

    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG,
                "Failed to request continuous-read stop: %s",
                esp_err_to_name(err));
    }

    return err;
}

//============================
// Continuous Read Mode Functions end
//============================

//============================
// Calibration Functions begin
//============================
esp_err_t ad7190_calibration_start(uint8_t calibration_mode)
{
    /*
     * Only the four AD7190 calibration operating modes are accepted.
     */
    if (!ad7190_mode_is_calibration(calibration_mode)) {
        ESP_LOGE(ad7190_TAG, "Invalid calibration mode: %u", calibration_mode);
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Calibration uses the current MODE and CONFIG register settings.
     */
    if (!s_user.mode_valid || !s_user.config_valid) {
        ESP_LOGE(ad7190_TAG, "Mode/config state not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Do not start calibration while another transient operation is active.
     */
    if (s_user.single_conversion_active) {
        ESP_LOGE(ad7190_TAG, "Cannot start calibration during a single-conversion sequence");
        return ESP_ERR_INVALID_STATE;
    }

    if (ad7190_mode_is_calibration(s_user.mode.mode)) {
        ESP_LOGE(ad7190_TAG, "Calibration already active");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Internal full-scale calibration requires FS to be divisible by 16.
     */
    if ((calibration_mode == AD7190_MODE_IFC) &&
        ((s_user.mode.fs % 16U) != 0U)) {

        ESP_LOGE(ad7190_TAG, "Internal full-scale calibration requires FS divisible by 16");
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Calibration requires exactly one selected channel.
     */
    esp_err_t err = ad7190_cal_channel_check();
    if (err != ESP_OK) {
        return err;
    }
    /*
     * Preserve the current MODE-register settings and change only the
     * operating mode to the requested calibration mode.
     */
    ad7190_mode_cfg_t cfg = s_user.mode;
    cfg.mode = calibration_mode;

    uint32_t reg = ad7190_mode_encode(&cfg);

    err = ad7190_write_register(AD7190_MODE_REG, reg);
    if (err != ESP_OK) {
        ESP_LOGE(ad7190_TAG, "Failed to start calibration: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Keep the cached operating mode in the calibration state until
     * ad7190_calibration_is_complete() observes RDY low.
     *
     * The AD7190 itself automatically enters IDLE when calibration finishes.
     */
    s_user.mode.mode = calibration_mode;

    return ESP_OK;
}

esp_err_t ad7190_calibration_is_complete(bool *complete)
{
    if (complete == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_user.mode_valid || !ad7190_mode_is_calibration(s_user.mode.mode)) {
        ESP_LOGE(ad7190_TAG, "No calibration is currently active");
        return ESP_ERR_INVALID_STATE;
    }

    bool ready = false;

    esp_err_t err = ad7190_is_ready(&ready);
    if (err != ESP_OK) {
        return err;
    }

    if (!ready) {
        *complete = false;
        return ESP_OK;
    }

    s_user.mode.mode = AD7190_MODE_IDLE;

    *complete = true;

    return ESP_OK;
}


//============================
// Calibration Functions end
//============================