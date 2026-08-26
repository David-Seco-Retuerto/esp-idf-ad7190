/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

// https://www.analog.com/media/en/technical-documentation/data-sheets/AD7190.pdf 


/* =========================================================================================================
    COMMUNICATIONS REGISTER 
*/

/* CR7  Must be 0 */
#define AD7190_COMM_WEN_POS           7   // Must always be written as 0

/* CR6      Read Write bit */
#define AD7190_COMM_RW_POS    6
#define AD7190_COMM_RW_SELECT(x)     (((uint8_t)(x) & 0x1U) << AD7190_COMM_RW_POS)
#define AD7190_COMM_RW_WRITE  0x0U
#define AD7190_COMM_RW_READ   0x1U

/* CR3-5    Register select */
#define AD7190_COMM_RS_POS     3
#define AD7190_COMM_RS_SELECT(x)      (((uint8_t)(x) & 0x7U) << AD7190_COMM_RS_POS)

    /* Registers */
    #define AD7190_COMM_REG        0x00        // When writing is the communication
    #define AD7190_STATUS_REG      0x00        // When reading is the status
    #define AD7190_MODE_REG        0x01
    #define AD7190_CONFIG_REG      0x02
    #define AD7190_DATA_REG        0x03
    #define AD7190_ID_REG          0x04        
    #define AD7190_GPOCON_REG      0x05
    #define AD7190_OFFSET_REG      0x06
    #define AD7190_FULL_SCALE_REG  0x07

/* CR2      Continuous Read */
#define AD7190_COMM_CREAD_POS   2
#define AD7190_COMM_CREAD_SELECT(x)    (((uint8_t)(x) & 0x1U) << AD7190_COMM_CREAD_POS)

#define AD7190_COMM_CREAD_OFF   0x0
#define AD7190_COMM_CREAD_ON    0x1


/* =========================================================================================================
    STATUS REGISTER (8)
*/

#define AD7190_STATUS_RDY_POS      7   // Indicates Conversion complete
#define AD7190_STATUS_ERR_POS      6   // Indicates error in conversion, data is '0' or saturated '0xFF...'
#define AD7190_STATUS_NOREF_POS    5   // Indicates the external reference is below threshold
#define AD7190_STATUS_PARITY_POS   4   // Indicates parity within the Data register if DAT_STA is set to '1'
#define AD7190_STATUS_CHANNEL_POS  0   // Indicates last converted channel


/* =========================================================================================================
    MODE REGISTER (24)
*/

/* MR21-23  Operating MODES */
#define AD7190_MODE_MODE_POS        21  // (3) Selects the AD7190 operation mode
#define AD7190_MODE_MODE_SELECT(x)  (((uint32_t)(x) & 0x7U) << AD7190_MODE_MODE_POS)
#define AD7190_MODE_MODE_GET(x)     ((uint8_t)(((uint32_t)(x) >> AD7190_MODE_MODE_POS) & 0x7U))

    /* MODES */
    #define AD7190_MODE_CONT   0x00        // Continuous conversion
    #define AD7190_MODE_SINGLE 0x01        // Single conversion
    #define AD7190_MODE_IDLE   0x02        // IDLE mode
    #define AD7190_MODE_PDOWN  0x03        // Power Down
    #define AD7190_MODE_IZC    0x04        // Internal Zero-Scale Calibration
    #define AD7190_MODE_IFC    0x05        // Internal Full-Scale Calibration
    #define AD7190_MODE_SZC    0x06        // System Zero-Scale Calibration
    #define AD7190_MODE_ZFC    0x07        // System Full-Scale Calibration

/* MR20     DAT_STA: Transmit status register alongside data register */
#define AD7190_MODE_DAT_STA_POS    20  // 
#define AD7190_MODE_DAT_STA_SELECT(x)  (((uint32_t)(x) & 0x1U) << AD7190_MODE_DAT_STA_POS)

/* MR18-19  CLOCK: Select Clock Source */
#define AD7190_MODE_CLOCK_POS      18  
#define AD7190_MODE_CLOCK_SELECT(x)    (((uint32_t)(x) & 0x3U) << AD7190_MODE_CLOCK_POS)

#define AD7190_CLK_EXT_CRYS    0x00    // External crystal, from MCLK1 to MCLK2
#define AD7190_CLK_EXT_CLK     0x01    // External clock, applied to MCLK2
#define AD7190_CLK_INT         0x02    // Internal 4.92MHz
#define AD7190_CLK_INT_OUT     0x03    // Internal 4.92MHz, available on MCLK2 

/* MR15     SINC3 Filter: Use sinc3 filter -> Lower settling time when chop disabled */
#define AD7190_MODE_SINC3_POS      15 
#define AD7190_MODE_SINC3_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_MODE_SINC3_POS)

/* MR13     ENPAR: Enables parity check, result within parity bit in status reg*/
#define AD7190_MODE_ENPAR_POS      13 
#define AD7190_MODE_ENPAR_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_MODE_ENPAR_POS)

/* MR11     SINGLE cycle settling: AD7190 Settles in one conversion cycle -> Zero latency */
#define AD7190_MODE_SCYCLE_POS     11  
#define AD7190_MODE_SCYCLE_SELECT(x)   (((uint32_t)(x) & 0x1U) << AD7190_MODE_SCYCLE_POS)

/* MR10     REJ60: Enable Notch filter at 60Hz when first of the sinc filter is at 50Hz */
#define AD7190_MODE_REJ60_POS      10  
#define AD7190_MODE_REJ60_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_MODE_REJ60_POS)

/* MR0-9    Filter Select: (9) Filter select bits Output rate is dependent on filter */
#define AD7190_MODE_FS_POS         0   
#define AD7190_MODE_FILTER_SELECT(x)   (((uint32_t)(x) & 0x03FFU) << AD7190_MODE_FS_POS)

/* =========================================================================================================
    CONFIGURATION REGISTER (24)
*/ 

/* CON23    CHOP: If set, Offset and offset drift are continuously minimized. Increases settling time */
#define AD7190_CONF_CHOP_POS       23 
#define AD7190_CONF_CHOP_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_CONF_CHOP_POS)

/* CON20    REFSEL: Reference select bit | 0: REFIN1(+)-REFIN2(-)   1: P1/REFIN2(+) - P0/REFIN2(-). */
#define AD7190_CONF_REFSEL_POS     20  
#define AD7190_CONF_REFSEL_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_CONF_REFSEL_POS)

/* CON8-15  CHANNEL Selection*/
#define AD7190_CONF_CHANNEL_POS    8   
#define AD7190_CONF_CHANNEL_SELECT(x)   (((uint32_t)(x) & 0xFFU) << AD7190_CONF_CHANNEL_POS)

#define AD7190_STATUS_CHANNEL_MASK  0x07U

#define AD7190_STATUS_CHANNEL_GET(x) \
    ((uint8_t)(((uint8_t)(x) >> AD7190_STATUS_CHANNEL_POS) & AD7190_STATUS_CHANNEL_MASK))

/* CON7     BURN: 500 nA sources are enabled (REQUIRES CHOP OFF & Buffer active) */
#define AD7190_CONF_BURN_POS       7   
#define AD7190_CONF_BURN_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_CONF_BURN_POS)

/* CON6     REFDET: Enables Reference Detect -> NOREF bit set when REF < 0.6V */
#define AD7190_CONF_REFDET_POS     6   
#define AD7190_CONF_REFDET_SELECT(x)    (((uint32_t)(x) & 0x1U) << AD7190_CONF_REFDET_POS)

/* CON4     BUF: Enables input buffering (REQUIRES VOLTAGES OVER 250mV) */
#define AD7190_CONF_BUF_POS        4   
#define AD7190_CONF_BUF_SELECT(x)      (((uint32_t)(x) & 0x1U) << AD7190_CONF_BUF_POS)

/* CON3     POLSEL: Unipolar/Bipolar operation, SET-> Unipolar operation */
#define AD7190_CONF_POLSEL_POS     3   
#define AD7190_CONF_POLSEL_SELECT(x)   (((uint32_t)(x) & 0x1U) << AD7190_CONF_POLSEL_POS)

/* CON0-2   PGA: (3) Gain select */
#define AD7190_CONF_PGA_POS        0   
#define AD7190_CONF_PGA_SELECT(x) (((uint32_t)(x) & 0x7U) << AD7190_CONF_PGA_POS)
    /* Gains */
    #define AD7190_PGA_GAIN_1      0x00
    #define AD7190_PGA_GAIN_8      0x03
    #define AD7190_PGA_GAIN_16     0x04
    #define AD7190_PGA_GAIN_32     0x05
    #define AD7190_PGA_GAIN_64     0x06
    #define AD7190_PGA_GAIN_128    0x07

/* =========================================================================================================
    ID REGISTER (8)
*/ 
#define AD7190_ID_7190_8   0x17U
#define AD7190_ID_7190_4E  0x07U
#define AD7190_ID_7190_4D  0x04U

#define AD7190_ID_MASK      0x0FU
#define AD7190_ID_VALUE     0x04U


/* =========================================================================================================
    GPOCON REGISTER (8)
*/

/* GP7 - Must be 0 */
#define AD7190_GPOCON_GP7_POS           7   // Must always be written as 0

/* GP6 - BPDSW: Bridge Power-Down Switch Control */
#define AD7190_GPOCON_BPDSW_POS         6
#define AD7190_GPOCON_BPDSW_SELECT(x)   (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_BPDSW_POS)

/* GP5 - GP32EN: Enable P3 and P2 as digital outputs */
#define AD7190_GPOCON_GP32EN_POS        5
#define AD7190_GPOCON_GP32EN_SELECT(x)  (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_GP32EN_POS)

/* GP4 - GP10EN: Enable P1 and P0 as digital outputs */
#define AD7190_GPOCON_GP10EN_POS        4
#define AD7190_GPOCON_GP10EN_SELECT(x)  (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_GP10EN_POS)

/* GP3 - P3DAT: Output value for P3 pin */
#define AD7190_GPOCON_P3DAT_POS         3
#define AD7190_GPOCON_P3DAT_SELECT(x)   (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_P3DAT_POS)

/* GP2 - P2DAT: Output value for P2 pin */
#define AD7190_GPOCON_P2DAT_POS         2
#define AD7190_GPOCON_P2DAT_SELECT(x)   (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_P2DAT_POS)

/* GP1 - P1DAT: Output value for P1 pin */
#define AD7190_GPOCON_P1DAT_POS         1
#define AD7190_GPOCON_P1DAT_SELECT(x)   (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_P1DAT_POS)

/* GP0 - P0DAT: Output value for P0 pin */
#define AD7190_GPOCON_P0DAT_POS         0
#define AD7190_GPOCON_P0DAT_SELECT(x)   (((uint8_t)(x) & 0x1U) << AD7190_GPOCON_P0DAT_POS)

/* =========================================================================================================
    OFFSET REGISTER (24)
*/

/*
 * Holds the offset calibration coefficient.
 * Default (power-on reset): 0x800000.
 *
 * The AD7190 implements four calibration register pairs.
 * The currently selected input channel determines which
 * offset/full-scale register pair is accessed.
 */

#define AD7190_OFFSET_DEFAULT    0x800000UL   // Default offset value (mid-scale)

/* Mask for full 24-bit register */
#define AD7190_OFFSET_MASK       0xFFFFFFUL

/* Helper macro (optional, por consistencia con SELECT) */
#define AD7190_OFFSET_SELECT(x)  ((uint32_t)(x) & AD7190_OFFSET_MASK)

/* =========================================================================================================
    FULL-SCALE REGISTER (24)
*/

/*
 * Holds the full-scale calibration coefficient.
 *
 * The AD7190 implements four calibration register pairs.
 * The currently selected input channel determines which
 * offset/full-scale register pair is accessed.
 *
 * The ADC must be in power-down or idle mode before writing.
 */

#define AD7190_FULLSCALE_MASK        0xFFFFFFUL

/* Helper macro */
#define AD7190_FULLSCALE_SELECT(x)   ((uint32_t)(x) & AD7190_FULLSCALE_MASK)


/* =========================================================================================================
    Predefined commands for continuous read mode
*/

#define AD7190_CMD_CONT_READ_ENABLE \
    ((uint8_t)(AD7190_COMM_RW_SELECT(AD7190_COMM_RW_READ) | \
                AD7190_COMM_RS_SELECT(AD7190_DATA_REG) | \
                AD7190_COMM_CREAD_SELECT(AD7190_COMM_CREAD_ON)))

#define AD7190_CMD_CONT_READ_DISABLE \
    ((uint8_t)(AD7190_COMM_RW_SELECT(AD7190_COMM_RW_READ) | \
                AD7190_COMM_RS_SELECT(AD7190_DATA_REG) | \
                AD7190_COMM_CREAD_SELECT(AD7190_COMM_CREAD_OFF)))