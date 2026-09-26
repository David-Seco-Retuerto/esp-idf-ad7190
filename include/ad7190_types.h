/*
 * AD7190 ESP-IDF Driver
 * Copyright (c) 2026 David Seco Retuerto
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>


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