# AD7190 ESP-IDF Driver

ESP-IDF component for the Analog Devices **AD7190** low-noise, low-drift, 24-bit sigma-delta ADC.

The driver provides a validated high-level API for initialization, configuration, data acquisition, calibration, and continuous-read operation, while keeping the low-level SPI interface private to the component.

## Features

- AD7190 initialization and device-ID validation
- Serial-interface reset using 40 consecutive logic `1` bits
- Validated MODE, CONFIG, GPOCON, OFFSET, and FULL-SCALE configuration
- Cached high-level configuration state
- PGA gain and burnout-current validation
- Single-channel and multi-channel single-conversion sequences
- Optional status-byte append through `DAT_STA`
- Internal and system zero-scale/full-scale calibration
- Continuous conversion and continuous-read operation
- Non-blocking continuous-read shutdown
- Manual or SPI-driven chip select for normal operations
- ESP-IDF public/private component interface

## Component Structure

```text
ad7190/
├── CMakeLists.txt
├── ad7190.c
├── ad7190_driver.c
│
├── include/
│   ├── ad7190.h
│   ├── ad7190_types.h
│   └── ad7190_regs.h
│
└── private_include/
    └── ad7190_driver.h
```

Application code should include only:

```c
#include "ad7190.h"
```

`ad7190_driver.h` is private to the component and is not intended for application use.

## Installation

Place the driver inside the `components` directory of an ESP-IDF project:

```text
your_project/
├── main/
└── components/
    └── ad7190/
```

Then declare the dependency from the component using the driver:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ad7190
)
```

The AD7190 component uses:

```cmake
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
```

## Initialization

Define the initial MODE and CONFIG settings:

```c
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
```

Initialize the device:

```c
ESP_ERROR_CHECK(ad7190_init(
    AD7190_CLK_PIN,
    AD7190_CS_PIN,
    AD7190_MISO_PIN,
    AD7190_MOSI_PIN,
    &mode_cfg,
    &config_cfg,
    NULL
));
```

`mode_cfg` and `config_cfg` are mandatory.

The GPOCON configuration is optional and may be passed as `NULL`.

Initialization performs:

```text
SPI initialization
        ↓
AD7190 reset
        ↓
Device-ID validation
        ↓
MODE configuration
        ↓
CONFIG configuration
        ↓
Optional GPOCON configuration
```

Use:

```c
ESP_ERROR_CHECK(ad7190_deinit());
```

to release the component resources and invalidate the cached high-level state.

## Configuration

The public configuration structures are:

```c
ad7190_mode_cfg_t
ad7190_config_cfg_t
ad7190_gpocon_cfg_t
ad7190_offset_cfg_t
ad7190_fullscale_cfg_t
```

Configuration is applied through:

```c
ad7190_mode_apply(...);
ad7190_config_apply(...);
ad7190_gpocon_apply(...);
ad7190_offset_apply(...);
ad7190_fullscale_apply(...);
```

The driver follows the general configuration flow:

```text
validate -> encode -> write -> update cached state
```

Transient operating modes use dedicated functions:

```c
ad7190_start_single_conversion();
ad7190_calibration_start(...);
```

and should not be started through `ad7190_mode_apply()`.

### Convenience Setters

Common fields can be modified without rebuilding the complete configuration structure:

```c
ad7190_set_gain(...);
ad7190_set_single_channel(...);
ad7190_set_filter(...);
ad7190_enable_status_append(...);
```

These functions modify the cached configuration and reapply the corresponding register.

## Status and Readiness

Read the status register with:

```c
uint8_t status = 0;
ESP_ERROR_CHECK(ad7190_status_read(&status));
```

Check whether conversion data is ready with:

```c
bool ready = false;
ESP_ERROR_CHECK(ad7190_is_ready(&ready));
```

The AD7190 `RDY` indication is active-low.

During normal operation, readiness is obtained from the status register. During continuous-read mode, normal register access is unavailable and the driver uses the physical `DOUT/RDY` GPIO instead.

## Single Conversion

Start a single-conversion sequence with:

```c
ESP_ERROR_CHECK(ad7190_start_single_conversion());
```

The conversion is asynchronous.

### Single Channel

With one enabled channel and `DAT_STA` disabled:

```c
bool ready = false;

do {
    ESP_ERROR_CHECK(ad7190_is_ready(&ready));
    vTaskDelay(pdMS_TO_TICKS(10));
} while (!ready);

uint32_t data = 0;
ESP_ERROR_CHECK(ad7190_conversion_read(&data));
```

After the conversion is consumed, the AD7190 enters power-down mode and the cached mode is updated accordingly.

### Multiple Channels

When several channels are enabled, the AD7190 performs one conversion on each enabled channel before entering power-down mode.

`DAT_STA` must be enabled so each returned conversion can be associated with its source channel.

```c
uint32_t data = 0;
uint8_t status = 0;

ESP_ERROR_CHECK(ad7190_conversion_read_ex(&data, &status));

uint8_t channel = AD7190_STATUS_CHANNEL_GET(status);
```

The driver tracks which enabled channels have been received and completes the sequence after all expected channels have been consumed.

## Reading Conversion Data

### Without `DAT_STA`

```c
uint32_t data = 0;
ESP_ERROR_CHECK(ad7190_conversion_read(&data));
```

The returned value contains the 24-bit conversion result.

If `DAT_STA` is enabled, this function returns `ESP_ERR_INVALID_STATE`.

### With `DAT_STA`

```c
uint32_t data = 0;
uint8_t status = 0;

ESP_ERROR_CHECK(ad7190_conversion_read_ex(&data, &status));
```

The driver separates the raw frame into:

```text
data   -> 24-bit conversion result
status -> appended AD7190 status byte
```

## Continuous Conversion and Continuous Read

The AD7190 distinguishes between **continuous conversion** and **continuous read**.

Continuous conversion is an ADC operating mode selected through the MODE register:

```c
.mode = AD7190_MODE_CONT
```

Continuous read is a serial-interface mode that removes the need to transmit a communications-register command before every conversion read.

Continuous-read mode can only be entered when the ADC is already configured in continuous-conversion mode.

### Start Continuous Read

```c
ESP_ERROR_CHECK(ad7190_continuous_read_start());
```

A pending conversion may be intentionally flushed while entering continuous-read mode. Normal acquisition then continues from the following conversion.

Samples are read using the normal conversion API:

```c
uint32_t data = 0;

esp_err_t err = ad7190_conversion_read(&data);

if (err == ESP_ERR_NOT_FINISHED) {
    // No new conversion is available yet.
}
```

With `DAT_STA` enabled:

```c
uint32_t data = 0;
uint8_t status = 0;

esp_err_t err = ad7190_conversion_read_ex(&data, &status);
```

### Stop Continuous Read

Continuous-read shutdown is non-blocking:

```c
ESP_ERROR_CHECK(ad7190_continuous_read_stop());
```

The physical exit is completed during the next ready conversion read. That read also returns the final conversion frame.

Until that final conversion has been read successfully, continuous-read mode remains active and normal register accesses remain blocked.

### Chip-Select Limitation

Normal register access, single conversions, continuous conversion, and calibration support SPI-driven chip select.

Continuous-read mode currently requires **manual chip-select control**.

Manual chip select is used by default. If `AD7190_USE_AUTO_CS` is enabled when building the component, continuous-read operations return `ESP_ERR_NOT_SUPPORTED`.

## Calibration

The driver supports the four AD7190 calibration operating modes:

```c
AD7190_MODE_IZC   // Internal zero-scale calibration
AD7190_MODE_IFC   // Internal full-scale calibration
AD7190_MODE_SZC   // System zero-scale calibration
AD7190_MODE_ZFC   // System full-scale calibration
```

Start a calibration with:

```c
ESP_ERROR_CHECK(
    ad7190_calibration_start(AD7190_MODE_IZC)
);
```

Calibration is asynchronous:

```c
bool complete = false;

do {
    ESP_ERROR_CHECK(
        ad7190_calibration_is_complete(&complete)
    );

    vTaskDelay(pdMS_TO_TICKS(10));

} while (!complete);
```

When calibration completes, the AD7190 automatically enters idle mode and the driver synchronizes its cached operating mode.

Calibration requires exactly one calibratable input channel to be enabled.

The temperature sensor cannot be calibrated because it does not have an associated calibration-register pair.

Internal full-scale calibration additionally requires the configured `FS` value to be divisible by 16.

Offset and full-scale calibration registers may only be written while the ADC is in idle or power-down mode.

## Reset

The high-level reset API is:

```c
ESP_ERROR_CHECK(ad7190_device_reset());
```

The reset sequence sends at least 40 consecutive logic `1` bits and waits before allowing further register access.

A successful reset invalidates the cached MODE, CONFIG, and GPOCON state. Configuration must therefore be reapplied before using configuration-dependent helper functions.

## Deinitialization

Release the driver with:

```c
ESP_ERROR_CHECK(ad7190_deinit());
```

This releases the underlying SPI resources and invalidates the cached high-level configuration state.

## Error Handling

All public functions return `esp_err_t`.

Common return values include:

```text
ESP_OK                   Operation successful
ESP_ERR_INVALID_ARG      Invalid pointer or configuration value
ESP_ERR_INVALID_STATE    Operation incompatible with the current state
ESP_ERR_NOT_FINISHED     Conversion is not ready yet
ESP_ERR_NOT_SUPPORTED    Operation unsupported by the selected configuration
ESP_ERR_NOT_FOUND        Expected AD7190 device ID was not detected
ESP_ERR_INVALID_RESPONSE Unexpected channel/status information was received
```

Underlying ESP-IDF SPI and GPIO errors may also be propagated.

## Current Limitations

- The driver currently manages a single AD7190 instance.
- The SPI host is currently configured internally as `SPI3_HOST`.
- The driver does not currently provide internal locking.
- Concurrent access from multiple tasks must therefore be serialized by the application if required.
- `DAT_STA` changes a conversion frame from 3 bytes to 4 bytes.
- `ENPAR` requires `DAT_STA` in the current high-level validation.
- Normal register accesses are blocked while continuous-read mode is active.
- Continuous-read mode currently requires manual chip select.
- Continuous-read timing should be validated on the target hardware before relying on it at high output data rates.

## Basic Example

```c
#include "ad7190.h"

void ad7190_example(void)
{
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
        AD7190_CLK_PIN,
        AD7190_CS_PIN,
        AD7190_MISO_PIN,
        AD7190_MOSI_PIN,
        &mode_cfg,
        &config_cfg,
        NULL
    ));

    ESP_ERROR_CHECK(ad7190_start_single_conversion());

    bool ready = false;

    do {
        ESP_ERROR_CHECK(ad7190_is_ready(&ready));
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (!ready);

    uint32_t data = 0;

    ESP_ERROR_CHECK(ad7190_conversion_read(&data));

    ESP_ERROR_CHECK(ad7190_deinit());
}
```

## Reference

Analog Devices, **AD7190: 4.8 kHz Ultralow Noise 24-Bit Sigma-Delta ADC with PGA**

https://www.analog.com/media/en/technical-documentation/data-sheets/AD7190.pdf
