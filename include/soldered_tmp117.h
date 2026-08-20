/**
 * @file soldered_tmp117.h
 * @brief Public API for the soldered-tmp117 component
 *
 * ESP-IDF driver for the Soldered Temperature Sensor TMP117 Breakout over I2C.
 *
 * @author Soldered Electronics
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "tmp117_dfs.h"

/** Default I2C clock used for the TMP117, which is rated for up to 400 kHz */
#define TMP117_DEFAULT_SCL_SPEED_HZ (400 * 1000)

/** How long a single I2C transfer is allowed to take before it is given up on */
#define TMP117_I2C_TIMEOUT_MS 100

/**
 * @brief Callback invoked by tmp117_update() when a new conversion is available
 *
 * Runs in the context of whichever task called tmp117_update(), so it is free to
 * talk to the sensor over I2C.
 *
 * @param[in] arg The pointer handed to tmp117_set_data_ready_callback()
 */
typedef void (*tmp117_data_ready_cb_t)(void *arg);

/**
 * @brief Handle for one TMP117 sensor
 *
 * Create one per sensor. All fields are managed by the driver; treat the struct
 * as opaque and read state through the accessor functions.
 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev; /**< I2C device handle, created by tmp117_init() */
    uint8_t address;                 /**< I2C address the sensor answers on */

    tmp117_alert_t alert_type;           /**< Alert flag seen by the last tmp117_update() or tmp117_read_config() */
    tmp117_data_ready_cb_t data_ready_cb; /**< Callback for new data, or NULL */
    void *data_ready_arg;                /**< Argument passed to the data ready callback */
} tmp117_t;

/**
 * @brief Attach a TMP117 to an already initialized I2C bus
 *
 * Adds the sensor as a device on `bus`. The I2C bus itself must already exist,
 * created with i2c_new_master_bus(); this leaves the bus free to be shared with
 * other devices. Call tmp117_begin() afterwards to apply a working configuration.
 *
 * Uses ::TMP117_DEFAULT_SCL_SPEED_HZ; use tmp117_init_with_clock() to pick a
 * different I2C clock.
 *
 * @param[out] dev Handle to initialize
 * @param[in] bus I2C bus the breakout is wired to, previously created with
 *                i2c_new_master_bus()
 * @param[in] address I2C address of the sensor, ::TMP117_DEFAULT_ADDRESS on an
 *                    unmodified Soldered breakout
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL handle, or the error
 *         returned by i2c_master_bus_add_device()
 */
esp_err_t tmp117_init(tmp117_t *dev, i2c_master_bus_handle_t bus, uint8_t address);

/**
 * @brief Attach a TMP117 to an already initialized I2C bus at a given I2C clock
 *
 * Same as tmp117_init(), but lets you pick the I2C clock. The TMP117 is rated
 * for up to 400 kHz.
 *
 * @param[out] dev Handle to initialize
 * @param[in] bus I2C bus the breakout is wired to, previously created with
 *                i2c_new_master_bus()
 * @param[in] address I2C address of the sensor
 * @param[in] scl_speed_hz I2C clock in Hz
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL handle, or the error
 *         returned by i2c_master_bus_add_device()
 */
esp_err_t tmp117_init_with_clock(tmp117_t *dev, i2c_master_bus_handle_t bus, uint8_t address,
                                 uint32_t scl_speed_hz);

/**
 * @brief Detach the sensor from the I2C bus
 *
 * Does not delete the I2C bus itself, since the bus is owned by the caller.
 *
 * @param[in,out] dev Handle previously initialized with tmp117_init()
 *
 * @return ESP_OK on success, or the error returned by i2c_master_bus_rm_device()
 */
esp_err_t tmp117_deinit(tmp117_t *dev);

/**
 * @brief Check that the sensor is there and apply the default configuration
 *
 * Reads the device ID register and refuses to continue unless it reports 0x117,
 * so a failure here usually means miswiring or the wrong address rather than a
 * bad configuration. Then sets continuous conversion, a 125 ms conversion cycle,
 * averaging over 8 conversions, the ALERT pin as a data ready flag, and a zero
 * temperature offset.
 *
 * @param[in,out] dev Handle previously initialized with tmp117_init()
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if the device ID is wrong, or an
 *         I2C error
 */
esp_err_t tmp117_begin(tmp117_t *dev);

/**
 * @brief Perform a soft reset
 *
 * Reloads the configuration register, the limits and the offset from EEPROM,
 * which for a sensor whose EEPROM was never programmed means the factory
 * defaults. Takes about 2 ms, which this call waits out.
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_soft_reset(tmp117_t *dev);

/**
 * @brief Read the temperature
 *
 * Returns whatever is in the result register, which is the most recent finished
 * conversion, with the offset from tmp117_set_offset_temperature() already
 * applied by the sensor. Does not wait for a new conversion; use
 * tmp117_data_ready() or the ALERT pin for that.
 *
 * @param[in,out] dev Handle
 * @param[out] temperature Temperature in °C
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument, or an I2C error
 */
esp_err_t tmp117_get_temperature(tmp117_t *dev, float *temperature);

/**
 * @brief Set the conversion mode
 *
 * In ::TMP117_CMODE_ONESHOT the sensor runs one conversion and then shuts down,
 * so write the mode again for every reading.
 *
 * @param[in,out] dev Handle
 * @param[in] cmode Mode to switch to, a ::tmp117_cmode_t
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_conv_mode(tmp117_t *dev, tmp117_cmode_t cmode);

/**
 * @brief Set the conversion cycle time
 *
 * The averaging setting can stretch the cycle beyond this; see ::tmp117_convt_t
 * for the resulting cycle times.
 *
 * @param[in,out] dev Handle
 * @param[in] convtime Cycle time to use, a ::tmp117_convt_t
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_conv_time(tmp117_t *dev, tmp117_convt_t convtime);

/**
 * @brief Set how many conversions get averaged into one result
 *
 * @param[in,out] dev Handle
 * @param[in] ave Averaging mode, a ::tmp117_ave_t
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_averaging(tmp117_t *dev, tmp117_ave_t ave);

/**
 * @brief Choose what the ALERT pin reports
 *
 * ::TMP117_PMODE_DATA makes the pin pulse on every finished conversion, while
 * ::TMP117_PMODE_ALERT and ::TMP117_PMODE_THERMAL make it follow the limits set
 * with tmp117_set_alert_temperature(). The pin is left active low, so wire it to
 * a GPIO with a pull-up and trigger on the falling edge.
 *
 * @param[in,out] dev Handle
 * @param[in] mode What the pin should report, a ::tmp117_pmode_t
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_alert_mode(tmp117_t *dev, tmp117_pmode_t mode);

/**
 * @brief Set the temperatures the alert flags and the ALERT pin react to
 *
 * In alert mode the flags follow the result leaving the window, in therm mode
 * the high limit turns the pin on and the low limit turns it back off. Both
 * limits are clamped to the ±256 °C the registers can hold.
 *
 * @param[in,out] dev Handle
 * @param[in] low_temp Low limit in °C
 * @param[in] high_temp High limit in °C
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_alert_temperature(tmp117_t *dev, float low_temp, float high_temp);

/**
 * @brief Read the alert flag seen by the last configuration register read
 *
 * The flags live in the configuration register and the sensor clears them when
 * that register is read, so this reports what tmp117_update() or
 * tmp117_read_config() saw rather than issuing a read of its own. In therm mode
 * only the high alert flag is ever set.
 *
 * @param[in] dev Handle
 *
 * @return Last seen ::tmp117_alert_t
 */
tmp117_alert_t tmp117_get_alert_type(const tmp117_t *dev);

/**
 * @brief Set the offset the sensor adds to every result
 *
 * Meant for cancelling out a fixed error introduced by the system around the
 * sensor, such as heat from a nearby regulator. Clamped to the ±256 °C the
 * register can hold.
 *
 * @param[in,out] dev Handle
 * @param[in] offset Offset in °C
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_offset_temperature(tmp117_t *dev, float offset);

/**
 * @brief Read back the offset register
 *
 * @param[in,out] dev Handle
 * @param[out] offset Offset in °C
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument, or an I2C error
 */
esp_err_t tmp117_get_offset_temperature(tmp117_t *dev, float *offset);

/**
 * @brief Calibrate the sensor against a known temperature
 *
 * Reads the current result and sets the offset to the difference, so that the
 * next reading comes out as `target`. Only as good as the reference: hold the
 * sensor at a temperature you trust and let it settle first.
 *
 * @param[in,out] dev Handle
 * @param[in] target Temperature the sensor is actually sitting at, in °C
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_set_target_temperature(tmp117_t *dev, float target);

/**
 * @brief Read the configuration register and update the cached status
 *
 * Refreshes what tmp117_get_alert_type() reports. Reading this register clears
 * the alert and data ready flags in the sensor, so a flag not caught here is
 * gone.
 *
 * @param[in,out] dev Handle
 * @param[out] config Raw register value, or NULL if only the cached status is wanted
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_read_config(tmp117_t *dev, uint16_t *config);

/**
 * @brief Register a callback for finished conversions
 *
 * The callback is invoked from tmp117_update(), not from an interrupt, so it can
 * use I2C freely. Pass NULL to remove a previously registered callback.
 *
 * @param[in,out] dev Handle
 * @param[in] cb Function to call when new data is available, or NULL
 * @param[in] arg Passed to the callback untouched
 */
void tmp117_set_data_ready_callback(tmp117_t *dev, tmp117_data_ready_cb_t cb, void *arg);

/**
 * @brief Poll the status flags and dispatch the data ready callback
 *
 * Reads the configuration register, refreshes what tmp117_get_alert_type()
 * reports, and calls the callback registered with
 * tmp117_set_data_ready_callback() if a conversion finished since the last call.
 * Call it in a loop, no faster than the conversion cycle.
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_update(tmp117_t *dev);

/**
 * @brief Check whether a conversion finished since the last status read
 *
 * Reading the flag clears it, so a true result is reported exactly once per
 * conversion. Note that tmp117_update() and tmp117_read_config() clear it too.
 *
 * @param[in,out] dev Handle
 * @param[out] ready true if a new result is waiting in the temperature register
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument, or an I2C error
 */
esp_err_t tmp117_data_ready(tmp117_t *dev, bool *ready);

/**
 * @brief Read the device ID
 *
 * @param[in,out] dev Handle
 * @param[out] id Device ID, ::TMP117_DEVICE_ID_VALUE on a working TMP117
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument, or an I2C error
 */
esp_err_t tmp117_get_device_id(tmp117_t *dev, uint16_t *id);

/**
 * @brief Read the silicon revision
 *
 * @param[in,out] dev Handle
 * @param[out] rev Revision number out of bits [15:12] of the device ID register
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a NULL argument, or an I2C error
 */
esp_err_t tmp117_get_device_rev(tmp117_t *dev, uint16_t *rev);

/**
 * @brief Read one of the three general purpose EEPROM words
 *
 * Soldered ships these words holding the sensor's NIST traceable serial number.
 *
 * @param[in,out] dev Handle
 * @param[in] eeprom_nr Which word to read, 1 to 3
 * @param[out] data The word that was read
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a bad word number or NULL
 *         argument, or an I2C error
 */
esp_err_t tmp117_read_eeprom(tmp117_t *dev, uint8_t eeprom_nr, uint16_t *data);

/**
 * @brief Program one of the three general purpose EEPROM words
 *
 * Unlocks the EEPROM, writes the word, waits for the programming to finish and
 * locks it again. Overwrites the NIST traceable serial number Soldered programs
 * into these words, and the EEPROM is only rated for 1000 write cycles, so this
 * does not belong in a loop.
 *
 * @param[in,out] dev Handle
 * @param[in] eeprom_nr Which word to program, 1 to 3
 * @param[in] data Word to program
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a bad word number,
 *         ESP_ERR_TIMEOUT if the programming never finished, or an I2C error
 */
esp_err_t tmp117_write_eeprom(tmp117_t *dev, uint8_t eeprom_nr, uint16_t data);

/**
 * @brief Store the current settings in EEPROM so they survive a power cycle
 *
 * Programs the configuration register, both limits and the offset into EEPROM,
 * which is what the sensor loads on power-up and on tmp117_soft_reset(). The
 * plain setters only write the registers, so without this call the sensor comes
 * back up with its old settings.
 *
 * The EEPROM is only rated for 1000 write cycles - call this once after
 * configuring a board, not on every boot.
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the programming never finished,
 *         or an I2C error
 */
esp_err_t tmp117_save_settings(tmp117_t *dev);

/**
 * @brief Log the configuration register field by field through ESP_LOGI
 *
 * A debugging aid. Reads the register, which clears the alert and data ready
 * flags as a side effect.
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK on success, or an I2C error
 */
esp_err_t tmp117_log_config(tmp117_t *dev);

#ifdef __cplusplus
}
#endif
