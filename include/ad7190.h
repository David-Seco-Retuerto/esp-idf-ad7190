/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"

#include "ad7190_types.h"
#include "ad7190_regs.h"

// ============================
// CHANNEL SELECTION (CONFIG REG)
// ============================

// Differential inputs
#define AD7190_CH_AIN1_AIN2        (1u << 0)   // CH0
#define AD7190_CH_AIN3_AIN4        (1u << 1)   // CH1

// Internal
#define AD7190_CH_TEMP_SENSOR      (1u << 2)   // CH2

// Shorted / internal test
#define AD7190_CH_AIN2_AIN2        (1u << 3)   // CH3

// Pseudo-differential vs AINCOM
#define AD7190_CH_AIN1_AINCOM      (1u << 4)   // CH4
#define AD7190_CH_AIN2_AINCOM      (1u << 5)   // CH5
#define AD7190_CH_AIN3_AINCOM      (1u << 6)   // CH6
#define AD7190_CH_AIN4_AINCOM      (1u << 7)   // CH7



/* =========================================================================================================
    INIT
*/


/** @brief Initializes the AD7190 and applies its initial configuration.
 *
 * This function initializes the SPI interface, resets the AD7190, verifies
 * the device ID, and applies the provided MODE and CONFIG register settings.
 *
 * GPOCON configuration is optional. If gpocon_cfg is NULL, the GPOCON
 * register is left at its reset/default value.
 *
 * On successful initialization, the applied mode, configuration and optional
 * GPOCON settings are cached by the high-level driver.
 *
 * @param clk_pin    GPIO used for SPI SCLK.
 * @param cs_pin     GPIO used for chip select.
 * @param miso_pin   GPIO used for MISO/DOUT-RDY.
 * @param mosi_pin   GPIO used for MOSI/DIN.
 * @param mode_cfg   Initial MODE-register configuration. Must not be NULL.
 * @param config_cfg Initial CONFIG-register configuration. Must not be NULL.
 * @param gpocon_cfg Optional initial GPOCON-register configuration, or NULL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if mandatory configuration or GPIO arguments
 *        are invalid
 *      - ESP_ERR_INVALID_STATE if the driver is already initialized
 *      - ESP_ERR_NOT_FOUND if the connected device does not report the
 *        expected AD7190 ID
 *      - Other ESP-IDF error codes returned during SPI setup or register access
 */
esp_err_t ad7190_init(int clk_pin,
                    int cs_pin,
                    int miso_pin,
                    int mosi_pin,
                    const ad7190_mode_cfg_t *mode_cfg,
                    const ad7190_config_cfg_t *config_cfg,
                    const ad7190_gpocon_cfg_t *gpocon_cfg);



/** @brief Deinitializes the AD7190 driver.
 *
 * This function releases the underlying SPI interface and clears the cached
 * high-level configuration state.
 *
 * The cached state is invalidated even if an error occurs while releasing
 * the low-level SPI resources, so it must not be considered usable after
 * this function has been called.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF or GPIO error codes returned during cleanup
 */
esp_err_t ad7190_deinit(void);


/** @brief Resets the AD7190 and invalidates the cached configuration state.
 *
 * This function performs an AD7190 serial-interface reset and clears the
 * high-level cached configuration maintained by the driver.
 *
 * After a successful reset, the AD7190 registers return to their reset/default
 * values. Any previously cached mode, configuration and GPOCON settings are
 * therefore marked as invalid.
 *
 * Configuration-dependent convenience functions cannot be used again until
 * the corresponding mode and configuration registers have been reapplied.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF SPI error codes returned by the low-level reset
 */
esp_err_t ad7190_device_reset(void);

/* =========================================================================================================
    Configuration Apply Functions
*/

/** @brief Applies an AD7190 mode-register configuration.
 *
 * This function validates the provided mode configuration, encodes it into the
 * AD7190 mode-register format, and writes it to the device.
 *
 * Normal mode-register writes are rejected while continuous-read mode is
 * active. Continuous-read mode must first be fully exited before applying a
 * new mode configuration.
 * 
 * Single-conversion and calibration modes are transient operations and cannot
 * be applied through this function. Use ad7190_start_single_conversion() or
 * ad7190_calibration_start() instead.
 * 
 * On success, the cached DAT_STA state and mode configuration are updated.
 *
 * @param cfg Pointer to the mode-register configuration to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if cfg is NULL, contains invalid fields, or
 *        requests single-conversion or calibration mode
 *       - ESP_ERR_INVALID_STATE if SPI is not initialized or continuous-read
 *        mode is still active
 *      - Other ESP-IDF error codes returned by the register write operation
 */
esp_err_t ad7190_mode_apply(const ad7190_mode_cfg_t *cfg);

/** @brief Applies an AD7190 configuration-register setup.
 *
 * This function validates the provided configuration-register structure,
 * encodes it into the AD7190 configuration-register format, and writes it to
 * the device.
 *
 * Normal configuration-register writes are rejected while continuous-read mode
 * is active. Continuous-read mode must first be fully exited before applying a
 * new configuration.
 *
 * Single-conversion and calibration modes are transient operations and cannot
 * be applied through this function. Use ad7190_start_single_conversion() or
 * ad7190_calibration_start() instead.
 * 
 * On success, a copy of the applied configuration is cached internally.
 *
 * @param cfg Pointer to the configuration-register setup to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if cfg is NULL, contains invalid fields, or
 *        requests single-conversion or calibration mode
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF error codes returned by the register write operation
 */
esp_err_t ad7190_config_apply(const ad7190_config_cfg_t *cfg);

/** @brief Applies an AD7190 GPOCON-register configuration.
 *
 * This function validates the provided GPIO-control configuration, encodes it
 * into the AD7190 GPOCON-register format, and writes it to the device.
 *
 * The GPOCON register controls the bridge power-down switch and the optional
 * digital output pins P3, P2, P1, and P0.
 *
 * A copy of the applied configuration is stored internally for later reference.
 *
 * @param cfg Pointer to the GPOCON-register configuration to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or contains invalid fields
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF error codes returned by the register write operation
 */
esp_err_t ad7190_gpocon_apply(const ad7190_gpocon_cfg_t *cfg);

/** @brief Applies an AD7190 offset calibration coefficient.
 *
 * This function validates the provided 24-bit offset calibration value and
 * writes it to the AD7190 offset register associated with the currently
 * selected calibratable channel.
 *
 * The offset register may only be written while the ADC is in idle or
 * power-down mode. The function therefore checks the cached operating-mode
 * state before performing the register write.
 *
 * Exactly one calibratable channel must be selected. The temperature sensor
 * is rejected because it has no associated calibration register pair.
 *
 * @note The function relies on the cached driver state. Calibration operations
 * started through ad7190_calibration_start() must be completed through
 * ad7190_calibration_is_complete() before writing calibration coefficients.
 *
 * @param cfg Pointer to the offset-register configuration to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or the value exceeds 24 bits
 *      - ESP_ERR_INVALID_STATE if no valid mode/configuration state is available,
 *        the ADC is not in idle or power-down mode, exactly one calibratable
 *        channel is not selected, SPI is not initialized, or continuous-read
 *        mode is active
 *      - Other ESP-IDF error codes returned by the underlying register write
 */
esp_err_t ad7190_offset_apply(const ad7190_offset_cfg_t *cfg);

/** @brief Applies an AD7190 full-scale calibration coefficient.
 *
 * This function validates the provided 24-bit full-scale calibration value and
 * writes it to the AD7190 full-scale register.
 *
 * The full-scale register may only be written while the ADC is in idle or
 * power-down mode. The function therefore checks the cached operating-mode
 * state before performing the register write.
 *
 * On success, a copy of the applied full-scale coefficient is stored internally
 * for later reference.
 *
 * @note After an ADC calibration cycle, the AD7190 automatically returns to
 * idle mode. However, the driver's cached mode state is not updated
 * automatically. If a calibration mode was previously started through
 * ad7190_mode_apply(), explicitly apply AD7190_MODE_IDLE before calling this
 * function so that the cached driver state remains synchronized with the
 * intended operating state.
 * 
 * @param cfg Pointer to the full-scale-register configuration to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if cfg is NULL or the value exceeds 24 bits
 *      - ESP_ERR_INVALID_STATE if no valid mode state is available, the ADC is
 *        not configured in idle or power-down mode, the SPI device is not
 *        initialized, or continuous-read mode is active
 *      - Other ESP-IDF error codes returned by the underlying register write
 */
esp_err_t ad7190_fullscale_apply(const ad7190_fullscale_cfg_t *cfg);



/* =========================================================================================================
    Convenience Configuration Setters
*/
/** @brief Updates the AD7190 PGA gain while preserving the current configuration.
 *
 * This function modifies only the gain field of the currently cached
 * configuration-register setup and reapplies the configuration register.
 *
 * A valid configuration must have been previously applied using
 * ad7190_config_apply() or a higher-level initialization function.
 *
 * @param gain AD7190 PGA gain setting.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if no valid configuration state is available
 *      - ESP_ERR_INVALID_ARG if the gain value is invalid
 *      - Other ESP-IDF error codes returned by the underlying register write
 */
esp_err_t ad7190_set_gain(uint8_t gain);

/** @brief Selects exactly one AD7190 input channel.
 *
 * This function updates the channel-selection field of the currently cached
 * configuration-register setup and reapplies the configuration register.
 *
 * The provided channel mask must contain exactly one active channel bit. For
 * multi-channel configurations, use ad7190_config_apply() directly.
 *
 * A valid configuration must have been previously applied using
 * ad7190_config_apply() or a higher-level initialization function.
 *
 * @param channel_mask Channel mask containing exactly one enabled channel bit.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if no valid configuration state is available
 *      - ESP_ERR_INVALID_ARG if the channel mask is zero, contains invalid bits,
 *        or enables more than one channel
 *      - Other ESP-IDF error codes returned by the underlying register write
 */
esp_err_t ad7190_set_single_channel(uint16_t channel_mask);

/** @brief Updates the AD7190 filter-select value.
 *
 * This function modifies only the FS field of the currently cached mode-register
 * configuration and reapplies the mode register.
 *
 * The FS value controls the output data rate and digital filter settling
 * behavior according to the AD7190 mode-register configuration.
 *
 * A valid mode configuration must have been previously applied using
 * ad7190_mode_apply() or a higher-level initialization function.
 *
 * @param fs Filter-select value. Valid range is 1 to 1023.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if no valid mode state is available
 *      - ESP_ERR_INVALID_ARG if fs is outside the valid range
 *      - Other ESP-IDF error codes returned by the underlying register write
 */
esp_err_t ad7190_set_filter(uint16_t fs);

/** @brief Enables or disables status-byte append mode.
 *
 * This function updates the DAT_STA bit in the currently cached mode-register
 * configuration and reapplies the mode register.
 *
 * When enabled, the AD7190 appends one status byte to each data-register read.
 * In this driver, data reads then return a 32-bit value where bits [31:8]
 * contain the 24-bit conversion result and bits [7:0] contain the status byte.
 *
 * A valid mode configuration must have been previously applied using
 * ad7190_mode_apply() or a higher-level initialization function.
 *
 * @param enable True to append the status byte to conversion data, false to
 * disable status append mode.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if no valid mode state is available
 *      - ESP_ERR_INVALID_ARG if the resulting mode configuration is invalid
 *      - Other ESP-IDF error codes returned by the underlying register write
 */
esp_err_t ad7190_enable_status_append(bool enable);


/* =========================================================================================================
    Status and Single Conversion
*/

/** @brief Reads the AD7190 status register.
 *
 * This function reads the AD7190 status register and returns its 8-bit value.
 * The status register contains the RDY bit, error flags, no-reference flag,
 * parity flag, and the active channel identification bits.
 *
 * @param status Pointer where the status-register value will be stored.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if status is NULL
 *      - ESP_ERR_INVALID_STATE if SPI is not initialized or continuous-read
 *        mode is active
 *      - Other ESP-IDF error codes returned by the underlying register read
 */
esp_err_t ad7190_status_read(uint8_t *status);

/** @brief Checks whether a new AD7190 conversion is ready.
 *
 * When continuous-read mode is disabled, readiness is determined from the RDY
 * bit in the AD7190 status register.
 *
 * When continuous-read mode is active, normal register accesses are unavailable,
 * so the physical DOUT/RDY GPIO level is used instead. DOUT/RDY is active low.
 *
 * @param ready Pointer where the readiness state is stored.
 *              True indicates that conversion data is available.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if ready is NULL
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF error codes returned by the status-register read
 */
esp_err_t ad7190_is_ready(bool *ready);


/** @brief Starts an AD7190 single-conversion sequence.
 *
 * In single-conversion mode, the AD7190 performs one conversion on each
 * enabled channel and automatically enters power-down mode after all selected
 * channels have been converted.
 *
 * When only one channel is enabled, DAT_STA is optional.
 *
 * When several channels are enabled, DAT_STA must be enabled before starting
 * the sequence. The appended status byte allows the driver to identify which
 * enabled channels have actually been read.
 *
 * The driver tracks the channel mask present when the sequence is started and
 * marks the cached operating mode as AD7190_MODE_PDOWN once a conversion has
 * been successfully read from every expected channel.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if mode/configuration state is unavailable,
 *        no channel is enabled, another single-conversion sequence is already
 *        active, or multiple channels are enabled without DAT_STA
 *      - Other ESP-IDF error codes returned by the underlying mode-register
 *        write
 */
esp_err_t ad7190_start_single_conversion(void);

/** @brief Reads one AD7190 conversion result without status byte.
 *
 * This function must be used only when DAT_STA is disabled.
 *
 * A tracked single-conversion sequence without DAT_STA can contain only one
 * enabled channel. During such a sequence, the function first verifies that a
 * conversion is ready and updates the cached state to power-down after the
 * conversion has been read successfully.
 *
 * @param data Pointer where the 24-bit conversion result will be stored.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if data is NULL
 *      - ESP_ERR_INVALID_STATE if DAT_STA is enabled or SPI is not initialized
 *      - ESP_ERR_NOT_FINISHED if the requested single or continuous-read
 *        conversion is not yet available
 *      - Other ESP-IDF error codes returned by the underlying data read
 */
esp_err_t ad7190_conversion_read(uint32_t *data);


/** @brief Reads one AD7190 conversion result together with its status byte.
 *
 * This function reads a conversion when DAT_STA is enabled and separates the
 * returned frame into the 24-bit conversion result and the appended status
 * byte.
 *
 * During a tracked single-conversion sequence, the CHD bits contained in the
 * status byte are used to record which enabled channels have actually been
 * received. The cached mode is updated to power-down only after all expected
 * channels have been read.
 *
 * @param data Pointer where the 24-bit conversion result will be stored.
 * @param status Pointer where the appended status byte will be stored.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if data or status is NULL
 *      - ESP_ERR_INVALID_STATE if DAT_STA is disabled or SPI is not initialized
 *      - ESP_ERR_INVALID_RESPONSE if the status byte reports a channel that was
 *        not enabled when the single-conversion sequence was started
 *      - ESP_ERR_NOT_FINISHED if the requested single or continuous-read
 *        conversion is not yet available
 *      - Other ESP-IDF error codes returned by the underlying data read
 */
esp_err_t ad7190_conversion_read_ex(uint32_t *data, uint8_t *status);


/* =========================================================================================================
    Continuous-Read Mode
*/

/** @brief Enables AD7190 continuous-read mode.
 *
 * Continuous-read mode removes the need to transmit a communications-register
 * command before every conversion read. Once enabled, each ready conversion can
 * be clocked directly from DOUT/RDY.
 *
 * The ADC must already be configured in continuous-conversion mode.
 *
 * @note Continuous-read mode currently requires manual chip-select control.
 *       When AD7190_USE_AUTO_CS is enabled, this operation is not supported.
 *
 * ...
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if no valid mode configuration exists, the ADC
 *        is not in continuous-conversion mode, SPI is not initialized, or
 *        continuous-read mode is already active
 *      - ESP_ERR_NOT_SUPPORTED if SPI-driven chip select is configured
 *      - Other ESP-IDF SPI error codes returned by the low-level driver
 */
esp_err_t ad7190_continuous_read_start(void);

/** @brief Requests termination of AD7190 continuous-read mode.
 *
 * This function is non-blocking. It records a request to leave continuous-read
 * mode but does not wait for the next conversion.
 *
 * The exit operation is completed during the next conversion read for which
 * DOUT/RDY is low. During that read, the driver transmits the AD7190 0x58
 * continuous-read disable command while simultaneously receiving the final
 * conversion frame.
 *
 * The final frame is returned normally by ad7190_conversion_read() or
 * ad7190_conversion_read_ex().
 *
 * Until that final read succeeds, continuous-read mode remains active and
 * normal register accesses remain blocked.
 *
 * @return
 *      - ESP_OK if the stop request was accepted
 *      - ESP_ERR_INVALID_STATE if continuous-read mode is not active, SPI is
 *        not initialized, or a stop request is already pending
 *      - ESP_ERR_NOT_SUPPORTED if SPI-driven chip select is configured
 *      - Other ESP-IDF SPI error codes returned by the low-level driver
*/
esp_err_t ad7190_continuous_read_stop(void);


/** @brief Starts an AD7190 calibration operation.
 *
 * Starts one of the four AD7190 calibration modes using the currently cached
 * MODE and CONFIG register settings.
 *
 * Exactly one calibratable input channel must be enabled. The temperature
 * sensor cannot be calibrated because it has no associated calibration
 * register pair.
 *
 * Calibration is asynchronous. RDY goes high when calibration starts and
 * returns low when calibration completes. The AD7190 then automatically enters
 * idle mode.
 *
 * Use ad7190_calibration_is_complete() to detect completion and synchronize the
 * cached operating-mode state.
 *
 * @param calibration_mode One of AD7190_MODE_IZC, AD7190_MODE_IFC,
 * AD7190_MODE_SZC or AD7190_MODE_ZFC.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if calibration_mode is not a calibration mode
 *      - ESP_ERR_INVALID_STATE if mode/config state is unavailable, exactly one
 *        calibratable channel is not selected, another transient operation is
 *        active, SPI is not initialized, or continuous-read mode is active
 *      - Other ESP-IDF error codes returned by the MODE-register write
 */
esp_err_t ad7190_calibration_start(uint8_t calibration_mode);


/** @brief Checks whether the active AD7190 calibration has completed.
 *
 * Calibration completion is detected from the AD7190 RDY indication.
 * While calibration is in progress, RDY remains high. When calibration
 * completes, RDY returns low and the AD7190 automatically enters idle mode.
 *
 * When completion is detected, the cached operating mode is updated to
 * AD7190_MODE_IDLE so that the software state remains synchronized with
 * the hardware state.
 *
 * @param complete Pointer where the completion state will be stored.
 *                 True indicates that calibration has completed.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if complete is NULL
 *      - ESP_ERR_INVALID_STATE if no calibration operation is currently active
 *      - Other ESP-IDF error codes returned while checking RDY
 */
esp_err_t ad7190_calibration_is_complete(bool *complete);