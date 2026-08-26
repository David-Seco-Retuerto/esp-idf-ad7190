/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "ad7190_types.h"
#include "ad7190_regs.h"

/* =========================================================================================================
    Structs
*/
typedef struct {
    bool disable_cont_read_pending;     // flag disable continuous read
    bool manual_cs;
    bool cs_keep_active;
} ad7190_flags_t;

typedef struct {
    spi_device_handle_t spi;
    bool continuous_read_enabled;   // Current state
    gpio_num_t drdy_pin;
    gpio_num_t cs_pin;

    bool dat_sta_enabled;           // Current dat_sta_enabled
    ad7190_flags_t flags;
} ad7190_driver_t;


/* =========================================================================================================
    Init functions
*/
/** @brief Initializes the SPI interface used to communicate with the AD7190.
 *
 * This function configures the selected SPI bus pins, initializes the SPI bus,
 * and attaches the AD7190 as an SPI device. The resulting SPI device handle is
 * stored internally by the driver and used by the remaining AD7190 functions.
 *
 * In manual chip-select mode, the CS pin is configured as a GPIO output and the
 * SPI driver does not control CS automatically. In automatic chip-select mode,
 * the CS pin is handled directly by the ESP-IDF SPI driver.
 *
 * @param clk_pin  GPIO number used as SPI clock/SCLK.
 * @param cs_pin   GPIO number used as chip select.
 * @param miso_pin GPIO number used as MISO/DOUT/RDY.
 * @param mosi_pin GPIO number used as MOSI/DIN.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if one of the GPIO numbers is invalid
 *      - ESP_ERR_INVALID_STATE if the SPI bus or device is already initialized
 *      - Other ESP-IDF error codes returned by spi_bus_initialize() or
 *        spi_bus_add_device()
 */
esp_err_t ad7190_spi_init(int clk_pin, int cs_pin, int miso_pin, int mosi_pin);


/** @brief Deinitializes the SPI interface used by the AD7190.
 *
 * This function removes the AD7190 device from the SPI bus, releases the
 * SPI bus, and clears the internal driver state.
 *
 * In manual chip-select mode, CS is driven high before the SPI device is
 * removed.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF or GPIO error codes returned during cleanup
 */
esp_err_t ad7190_spi_deinit(void);


/** @warning Calling this low-level reset function directly invalidates any
 * high-level cached configuration maintained by ad7190.h. Application code
 * should normally reinitialize or reapply the configuration after a reset. 
 * 
 * @brief Resets the AD7190 by sending at least 40 consecutive logic 1 bits.
 *
 * The AD7190 can be reset by writing a sequence of 40 consecutive HIGH bits
 * on DIN while the SPI interface is active. This function sends five bytes
 * with value 0xFF over SPI, resets the internal driver state flags, and waits
 * at least 500 us before allowing further register access.
 *
 * After this function returns successfully, the driver assumes that continuous
 * read mode is disabled and that DAT_STA is disabled, matching the AD7190
 * default communication state after reset.
 *
 * @note This function requires the AD7190 SPI device handle to be already
 * initialized.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - Other ESP-IDF SPI error codes returned by the reset transaction
 */
esp_err_t ad7190_reset(void);

/* =========================================================================================================
    Register Access
*/ 
/** @brief Reads a register from the AD7190.
 *
 * This function sends a normal AD7190 register-read command through the
 * communication register and then reads the number of bytes associated with
 * the selected register.
 *
 * The communication register is sent using the SPI command phase. During the
 * read phase, zeroes are transmitted on DIN to keep the line low and avoid an
 * accidental serial-interface reset caused by 40 consecutive logic 1 bits.
 *
 * If the data register is read while DAT_STA is enabled, one additional status
 * byte is read together with the conversion data.
 *
 * @param regCode AD7190 register code to read.
 * @param data_out Pointer where the register value will be stored.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if data_out is NULL or regCode is invalid
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized or continuous-read mode is active
 *      - ESP_ERR_INVALID_SIZE if the selected register size is unsupported
 *      - Other ESP-IDF SPI error codes returned by spi_device_polling_transmit()
 */
esp_err_t ad7190_read_register(uint8_t regCode, uint32_t *data_out);

/**
 * @warning Direct register writes may bypass the high-level cached
 * configuration state maintained by ad7190.h. Application code should
 * normally use the corresponding high-level configuration functions.
 * 
 * @brief Writes a value to an AD7190 register.
 *
 * This function sends a normal AD7190 register-write command through the
 * communication register and then writes the number of bytes associated with
 * the selected register.
 *
 * The communication register is sent using the SPI command phase. Therefore,
 * the SPI device must have been configured with command_bits = 8 during
 * initialization.
 *
 * Register values are transmitted MSB first, as required by the AD7190.
 *
 * @note This is a low-level register-access function. The caller is responsible
 * for satisfying register-specific operating-mode requirements. In particular,
 * the AD7190 must be in idle or power-down mode before writing the offset or
 * full-scale calibration registers.
 * 
 * @param regCode AD7190 register code to write.
 * @param value Value to write to the selected register. Only the least
 * significant bits corresponding to the register size are used.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if regCode is invalid
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized or continuous-read mode is active
 *      - ESP_ERR_INVALID_SIZE if the selected register size is unsupported
 *      - Other ESP-IDF SPI error codes returned by spi_device_polling_transmit()
 */
esp_err_t ad7190_write_register(uint8_t regCode, uint32_t value);

/* =========================================================================================================
    Data Acquisition
*/
/** @brief Reads one AD7190 conversion frame.
 *
 * The actual SPI transaction depends on the current serial-interface state:
 *
 * - With continuous-read mode disabled, the data register is accessed through
 *   a normal communications-register transaction.
 * - With continuous-read mode enabled, the frame is clocked directly from
 *   DOUT/RDY.
 * - If a continuous-read stop is pending, the current frame is read while the
 *   0x58 command is transmitted, completing the exit from continuous-read mode.
 *
 * If DAT_STA is enabled, the returned 32-bit value contains the 24-bit
 * conversion result in bits [31:8] and the status byte in bits [7:0].
 * Otherwise, only the 24-bit conversion result is returned.
 *
 * In continuous-read mode, the function returns ESP_ERR_NOT_FINISHED if
 * DOUT/RDY indicates that no new conversion is available.
 *
 * @param data Pointer where the received conversion frame is stored.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if data is NULL
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized
 *      - ESP_ERR_NOT_FINISHED if no continuous-read conversion is ready
 *      - Other ESP-IDF SPI error codes returned by the underlying transaction
 */
esp_err_t ad7190_read_data(uint32_t *data);

/* =========================================================================================================
 *  Continuous-Read Mode
 *
 *  Low-level continuous-read helpers used by the higher-level AD7190 API.
 *  Application code should normally use the functions exposed in ad7190.h
 *  instead of calling these directly.
 */

/** @brief Enables AD7190 continuous-read mode.
 *
 * This function transmits the AD7190 continuous-read enable command (0x5C).
 * Additional zero-valued clock cycles are intentionally generated after the
 * command to complete any conversion frame that may already be pending when
 * continuous-read mode is entered.
 *
 * Consequently, a conversion that was already pending when this function was
 * called may be discarded. Normal acquisition begins with the following
 * conversion.
 *
 * While continuous-read mode is active, normal register accesses are rejected.
 *
 * @note Continuous-read mode currently requires manual chip-select control.
 *       SPI-driven chip select is not supported for this operation.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized or
 *        continuous-read mode is already active
 *      - ESP_ERR_NOT_SUPPORTED if SPI-driven chip select is configured
 *      - Other ESP-IDF SPI error codes returned by the SPI transaction
 */
esp_err_t ad7190_start_contread(void);


/** @brief Requests termination of AD7190 continuous-read mode.
 *
 * This function does not immediately transmit the continuous-read disable
 * command. Instead, it marks the stop operation as pending.
 *
 * The physical exit from continuous-read mode is completed by the next call to
 * ad7190_read_data() for which DOUT/RDY is low. That read transmits the 0x58
 * command while simultaneously receiving the final conversion frame.
 *
 * Until that final read succeeds, continuous-read mode remains active and
 * normal register accesses remain unavailable.
 *
 * The final conversion is returned through the normal data-read API; it is not
 * returned by this function.
 *
 * @note Continuous-read mode currently requires manual chip-select control.
 *       SPI-driven chip select is not supported for this operation.
 *
 * @return
 *      - ESP_OK if the stop request was accepted
 *      - ESP_ERR_INVALID_STATE if the SPI device is not initialized,
 *        continuous-read mode is not active, or a stop request is already pending
 *      - ESP_ERR_NOT_SUPPORTED if SPI-driven chip select is configured
 */
esp_err_t ad7190_stop_contread(void);

/** @brief Reads one frame while AD7190 continuous-read mode is active.
 *
 * This function directly clocks one conversion frame from DOUT/RDY without
 * transmitting a communications-register command.
 *
 * It may only be used while continuous-read mode is active and no stop request
 * is pending. DOUT/RDY must be low before the transaction begins.
 *
 * Application code should normally use ad7190_conversion_read() or
 * ad7190_conversion_read_ex() instead of calling this function directly.
 *
 * @note Continuous-read mode currently requires manual chip-select control.
 *       SPI-driven chip select is not supported for this operation.
 *
 * @param data Pointer where the received frame is stored.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if data is NULL
 *      - ESP_ERR_INVALID_STATE if SPI is not initialized, continuous-read mode
 *        is not active, or a stop operation is pending
 *      - ESP_ERR_NOT_SUPPORTED if SPI-driven chip select is configured
 *      - ESP_ERR_NOT_FINISHED if no new conversion is ready
 *      - Other ESP-IDF SPI error codes returned by the SPI transaction
 */
esp_err_t ad7190_data_contread(uint32_t *data);



/* =========================================================================================================
    Register Encoders
*/

/** @brief Encodes an AD7190 mode-register configuration structure.
 *
 * This function converts a user-level mode configuration structure into the
 * 24-bit value expected by the AD7190 mode register.
 *
 * The encoded value includes the operating mode, DAT_STA option, clock source,
 * filter configuration, parity option, single-cycle settling option, and
 * 50/60 Hz rejection settings.
 *
 * @pre cfg must not be NULL.
 *
 * @param cfg Pointer to the mode-register configuration structure.
 *
 * @return Encoded 24-bit AD7190 mode-register value.
 */
uint32_t ad7190_mode_encode(const ad7190_mode_cfg_t *cfg);

/** @brief Encodes an AD7190 configuration-register structure.
 *
 * This function converts a user-level configuration structure into the 24-bit
 * value expected by the AD7190 configuration register.
 *
 * The encoded value includes the chop setting, reference selection, enabled
 * input channels, burnout current setting, reference-detect option, input
 * buffer setting, polarity mode, and PGA gain.
 *
 * @pre cfg must not be NULL.
 *
 * @param cfg Pointer to the configuration-register structure.
 *
 * @return Encoded 24-bit AD7190 configuration-register value.
 */
uint32_t ad7190_config_encode(const ad7190_config_cfg_t *cfg);

/** @brief Encodes an AD7190 GPOCON-register configuration structure.
 *
 * This function converts a user-level GPIO-control configuration structure into
 * the value expected by the AD7190 GPOCON register.
 *
 * The encoded value configures the bridge power-down switch, enables or
 * disables the P3/P2 and P1/P0 digital output pairs, and sets the output value
 * of pins P3, P2, P1, and P0.
 *
 * Bit GP7 is reserved and is kept cleared by the encoding macros.
 *
 * @pre cfg must not be NULL.
 *
 * @param cfg Pointer to the GPOCON-register configuration structure.
 *
 * @return Encoded AD7190 GPOCON-register value.
 */
uint32_t ad7190_gpocon_encode(const ad7190_gpocon_cfg_t *cfg);

/** @brief Encodes an AD7190 offset-register calibration value.
 *
 * This function converts a user-level offset calibration structure into the
 * 24-bit value expected by the AD7190 offset register.
 *
 * The offset register stores the ADC offset calibration coefficient.
 *
 * @pre cfg must not be NULL.
 *
 * @param cfg Pointer to the offset-register configuration structure.
 *
 * @return Encoded 24-bit AD7190 offset-register value.
 */
uint32_t ad7190_offset_encode(const ad7190_offset_cfg_t *cfg);

/** @brief Encodes an AD7190 full-scale-register calibration value.
 *
 * This function converts a user-level full-scale calibration structure into the
 * 24-bit value expected by the AD7190 full-scale register.
 *
 * The full-scale register stores the ADC gain calibration coefficient.
 *
 * @pre cfg must not be NULL.
 *
 * @param cfg Pointer to the full-scale-register configuration structure.
 *
 * @return Encoded 24-bit AD7190 full-scale-register value.
 */
uint32_t ad7190_fullscale_encode(const ad7190_fullscale_cfg_t *cfg);

