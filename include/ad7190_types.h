/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>


/* =========================================================================================================
    Operating Modes
*/

#define AD7190_MODE_CONT      0x00U   // Continuous conversion
#define AD7190_MODE_SINGLE    0x01U   // Single conversion
#define AD7190_MODE_IDLE      0x02U   // Idle mode
#define AD7190_MODE_PDOWN     0x03U   // Power-down mode
#define AD7190_MODE_IZC       0x04U   // Internal zero-scale calibration
#define AD7190_MODE_IFC       0x05U   // Internal full-scale calibration
#define AD7190_MODE_SZC       0x06U   // System zero-scale calibration
#define AD7190_MODE_ZFC       0x07U   // System full-scale calibration


/* =========================================================================================================
    Clock Sources
*/

#define AD7190_CLK_EXT_CRYS    0x00U   // External crystal between MCLK1 and MCLK2
#define AD7190_CLK_EXT_CLK     0x01U   // External clock applied to MCLK2
#define AD7190_CLK_INT         0x02U   // Internal 4.92 MHz clock
#define AD7190_CLK_INT_OUT     0x03U   // Internal 4.92 MHz clock available on MCLK2


/* =========================================================================================================
    PGA Gain
*/

#define AD7190_PGA_GAIN_1      0x00U
#define AD7190_PGA_GAIN_8      0x03U
#define AD7190_PGA_GAIN_16     0x04U
#define AD7190_PGA_GAIN_32     0x05U
#define AD7190_PGA_GAIN_64     0x06U
#define AD7190_PGA_GAIN_128    0x07U


/* =========================================================================================================
    Channel Selection
*/

// Differential inputs
#define AD7190_CH_AIN1_AIN2        (1U << 0)
#define AD7190_CH_AIN3_AIN4        (1U << 1)

// Internal
#define AD7190_CH_TEMP_SENSOR      (1U << 2)

// Shorted / internal test
#define AD7190_CH_AIN2_AIN2        (1U << 3)

// Pseudo-differential vs AINCOM
#define AD7190_CH_AIN1_AINCOM      (1U << 4)
#define AD7190_CH_AIN2_AINCOM      (1U << 5)
#define AD7190_CH_AIN3_AINCOM      (1U << 6)
#define AD7190_CH_AIN4_AINCOM      (1U << 7)


/* =========================================================================================================
    Status Register
*/

#define AD7190_STATUS_RDY_POS       7U
#define AD7190_STATUS_ERR_POS       6U
#define AD7190_STATUS_NOREF_POS     5U
#define AD7190_STATUS_PARITY_POS    4U
#define AD7190_STATUS_CHANNEL_POS   0U

#define AD7190_STATUS_CHANNEL_MASK  0x07U

#define AD7190_STATUS_CHANNEL_GET(x) \
    ((uint8_t)(((uint8_t)(x) >> AD7190_STATUS_CHANNEL_POS) & AD7190_STATUS_CHANNEL_MASK))


/* =========================================================================================================
    Public Configuration Types
*/

typedef struct {
    uint8_t mode;
    bool dat_sta;
    uint8_t clock;
    bool sinc3;
    bool enpar;
    bool single_cycle;
    bool rej60;
    uint16_t fs;
} ad7190_mode_cfg_t;


typedef struct {
    bool chop;
    bool refsel;
    uint16_t channel_mask;
    bool burn;
    bool refdet;
    bool buffer;
    bool unipolar;
    uint8_t gain;
} ad7190_config_cfg_t;


typedef struct {
    bool bpdsw;
    bool gp32en;
    bool gp10en;
    bool p3dat;
    bool p2dat;
    bool p1dat;
    bool p0dat;
} ad7190_gpocon_cfg_t;


typedef struct {
    uint32_t value;
} ad7190_offset_cfg_t;


typedef struct {
    uint32_t value;
} ad7190_fullscale_cfg_t;