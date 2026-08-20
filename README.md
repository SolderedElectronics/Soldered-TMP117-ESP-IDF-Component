# Soldered Temperature Sensor TMP117 Breakout Component

| ![Temperature Sensor TMP117 Breakout](https://soldered.com/cdn/shop/files/333175_featured-photo_04c294_c1fdd92d-4591-4127-97d7-7d64fb97efeb.png) |
| :---------------------------------------------------------------------------------------------------------: |
|                     [Temperature Sensor TMP117 Breakout](https://solde.red/333175)                          |

ESP-IDF driver for the Soldered Temperature Sensor TMP117 Breakout, built around the Texas Instruments TMP117 high-precision digital temperature sensor and driven over I2C. Measures from -55 °C to +150 °C with a resolution of 7.8125 m°C and an accuracy of ±0.1 °C between -20 °C and +50 °C, with no calibration needed. Part of the [Qwiic ecosystem](https://soldered.com/collections/qwiic-ecosystem).

### Repository Contents

- **/src** - source files (.c)
- **/include** - header files (.h)
  - `soldered_tmp117.h` - the public API
  - `tmp117_dfs.h` - register map, bit definitions and enumerations
- **/examples** - examples for using the library
  - `simple_reading` - read the temperature every 500 ms
  - `custom_configuration` - pick a conversion time and averaging mode, and get a callback for every result
  - `alert_pin` - react to a temperature limit through the ALERT pin
  - `calibration` - read the NIST serial number and correct a reading against a known temperature
- **_other_** - idf_component.yml manifest file for ESP Component Registry

### Usage

The I2C bus belongs to your application, not to the driver, so that other devices can share it:

```c
i2c_master_bus_handle_t bus;
i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = GPIO_NUM_21,
    .scl_io_num = GPIO_NUM_22,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .flags.enable_internal_pullup = true,
};
ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

tmp117_t tmp;
ESP_ERROR_CHECK(tmp117_init(&tmp, bus, TMP117_DEFAULT_ADDRESS));
ESP_ERROR_CHECK(tmp117_begin(&tmp));

float temperature;
ESP_ERROR_CHECK(tmp117_get_temperature(&tmp, &temperature));
```

`tmp117_begin()` checks that the device ID reads back as 0x117 before applying the defaults, so a failure there means miswiring or the wrong address rather than a bad configuration. The breakout answers on `TMP117_DEFAULT_ADDRESS` (0x49); the onboard jumpers move it to 0x48, 0x4A or 0x4B.

**Conversion cycle:** `tmp117_set_conv_time()` and `tmp117_set_averaging()` together decide how often a new result appears, from 15.5 ms with no averaging up to 16 s. More averaging means less noise but a longer cycle; the table in `tmp117_dfs.h` has the resulting cycle times. `tmp117_set_conv_mode()` picks between converting continuously, one conversion at a time, and shutting down at about 250 nA.

**New data:** the sensor holds the newest finished conversion in its result register, so `tmp117_get_temperature()` can be called at any time. To follow the conversion cycle instead, either poll `tmp117_data_ready()`, or register a callback and poll `tmp117_update()`:

```c
tmp117_set_data_ready_callback(&tmp, new_temperature, &tmp);

while (1) {
    tmp117_update(&tmp);            // calls new_temperature() when a conversion finished
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

**Alert pin:** `tmp117_set_alert_temperature()` sets the two limits, and `tmp117_set_alert_mode()` chooses what the ALERT pin reports - a finished conversion (`TMP117_PMODE_DATA`, the default), the result leaving the window between the limits (`TMP117_PMODE_ALERT`), or a thermostat-style hysteresis on the high limit (`TMP117_PMODE_THERMAL`). The pin is active low and open drain, so wire it to a GPIO with a pull-up and trigger on the falling edge, as `alert_pin` does. Which limit was crossed lives in the two alert flags, which the sensor clears when the status register is read: `tmp117_read_config()` reads it and `tmp117_get_alert_type()` reports what it saw.

**Offset and calibration:** `tmp117_set_offset_temperature()` adds a fixed correction to every result, and `tmp117_set_target_temperature()` works that correction out for you from a temperature you trust.

**EEPROM:** the configuration register, both limits and the offset each have an EEPROM word behind them, and that is what the sensor loads on power-up and after `tmp117_soft_reset()`. The setters only write registers, so settings are lost on a power cycle unless `tmp117_save_settings()` is called. The three general purpose words, read with `tmp117_read_eeprom()`, hold the sensor's NIST traceable serial number. The EEPROM is rated for 1000 write cycles, so programming it belongs in a one-off provisioning step, not in a loop.

### Original source

This is a port of the [Soldered Temperature Sensor TMP117 Arduino library](https://github.com/SolderedElectronics/Soldered-Temperature-Sensor-TMP117-Arduino-Library), whose register level code comes from the [TMP117 Arduino library](https://github.com/NilsMinor/TMP117-Arduino) by Nils Minor. Thank you, Nils Minor.

### Hardware design

You can find hardware design for this board in _Temperature Sensor TMP117 Breakout_ hardware repository.

### Documentation

Access library documentation [here](https://docs.soldered.com/).

### About Soldered

<img src="https://raw.githubusercontent.com/SolderedElectronics/Soldered-Generic-Arduino-Library/dev/extras/Soldered-logo-color.png" alt="soldered-logo" width="500"/>

At Soldered, we design and manufacture a wide selection of electronic products to help you turn your ideas into acts and bring you one step closer to your final project. Our products are intented for makers and crafted in-house by our experienced team in Osijek, Croatia. We believe that sharing is a crucial element for improvement and innovation, and we work hard to stay connected with all our makers regardless of their skill or experience level. Therefore, all our products are open-source. Finally, we always have your back. If you face any problem concerning either your shopping experience or your electronics project, our team will help you deal with it, offering efficient customer service and cost-free technical support anytime. Some of those might be useful for you:

- [Web Store](https://www.soldered.com/shop)
- [Tutorials & Projects](https://soldered.com/learn)
- [Documentation](https://docs.soldered.com)

### Open-source license

Soldered invests vast amounts of time into hardware & software for these products, which are all open-source. Please support future development by buying one of our products.

Check license details in the LICENSE file. Long story short, use these open-source files for any purpose you want to, as long as you apply the same open-source licence to it and disclose the original source. No warranty - all designs in this repository are distributed in the hope that they will be useful, but without any warranty. They are provided "AS IS", therefore without warranty of any kind, either expressed or implied. The entire quality and performance of what you do with the contents of this repository are your responsibility. In no event, Soldered (TAVU) will be liable for your damages, losses, including any general, special, incidental or consequential damage arising out of the use or inability to use the contents of this repository.

## Have fun!

And thank you from your fellow makers at Soldered Electronics.
