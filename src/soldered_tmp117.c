/**
 * @file soldered_tmp117.c
 * @brief Implementation for the soldered-tmp117 component
 *
 * Register level behaviour follows the TMP117 datasheet,
 * https://www.ti.com/lit/ds/symlink/tmp117.pdf
 *
 * @author Soldered Electronics
 */

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soldered_tmp117.h"

static const char *TAG = "TMP117";

/* A soft reset takes about 1.5 ms before the sensor answers again */
#define RESET_TIME_MS 2

/* One EEPROM word takes about 7 ms to program; give up well past that */
#define EEPROM_TIMEOUT_MS  50
#define EEPROM_POLL_STEP_MS 2

// *****************************************************************************
// Section: Small helpers

/**
 * @brief Block for at least the given number of milliseconds
 *
 * pdMS_TO_TICKS() rounds down, which turns short waits into no wait at all on a
 * coarse tick rate. Round up to one tick instead.
 */
static void tmp_delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

/**
 * @brief Convert a temperature in °C into a raw register value
 *
 * Clamps to the range the 16 bit registers can hold, so an out of range request
 * saturates instead of wrapping around into the opposite sign.
 *
 * @param[in] celsius Temperature in °C
 *
 * @return Two's complement register value
 */
static int16_t temp_to_raw(float celsius)
{
    if (celsius > TMP117_TEMP_MAX) {
        celsius = TMP117_TEMP_MAX;
    } else if (celsius < TMP117_TEMP_MIN) {
        celsius = TMP117_TEMP_MIN;
    }

    return (int16_t)lroundf(celsius / TMP117_RESOLUTION);
}

/**
 * @brief Read one 16 bit register
 *
 * The TMP117 sends registers most significant byte first, after being told which
 * register to point at.
 *
 * @param[in,out] dev Handle
 * @param[in] reg Register address
 * @param[out] data Value that was read
 *
 * @return ESP_OK on success, or the error returned by the I2C driver
 */
static esp_err_t read16(tmp117_t *dev, uint8_t reg, uint16_t *data)
{
    uint8_t buf[2];

    esp_err_t err = i2c_master_transmit_receive(dev->i2c_dev, &reg, 1, buf, sizeof(buf), TMP117_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not read register 0x%02X: %s", reg, esp_err_to_name(err));
        return err;
    }

    *data = ((uint16_t)buf[0] << 8) | buf[1];

    return ESP_OK;
}

/**
 * @brief Write one 16 bit register
 *
 * @param[in,out] dev Handle
 * @param[in] reg Register address
 * @param[in] data Value to write
 *
 * @return ESP_OK on success, or the error returned by the I2C driver
 */
static esp_err_t write16(tmp117_t *dev, uint8_t reg, uint16_t data)
{
    uint8_t buf[3] = {reg, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};

    esp_err_t err = i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), TMP117_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not write register 0x%02X: %s", reg, esp_err_to_name(err));
    }

    return err;
}

/**
 * @brief Read the configuration register, replace some of its bits and write it back
 *
 * Read-modify-write, so that setting one field leaves the others alone. The
 * status bits [15:12] are read only, and the value read back for them is
 * harmless to write.
 *
 * @param[in,out] dev Handle
 * @param[in] mask Bits to replace
 * @param[in] value New value for those bits, already shifted into place
 *
 * @return ESP_OK on success, or an I2C error
 */
static esp_err_t update_config(tmp117_t *dev, uint16_t mask, uint16_t value)
{
    uint16_t config;

    esp_err_t err = tmp117_read_config(dev, &config);
    if (err != ESP_OK) {
        return err;
    }

    config = (config & ~mask) | (value & mask);

    return write16(dev, TMP117_REG_CONFIGURATION, config);
}

/**
 * @brief Wait until the EEPROM has finished programming
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK once the EEPROM is idle, ESP_ERR_TIMEOUT if it stayed busy, or
 *         an I2C error
 */
static esp_err_t eeprom_wait_idle(tmp117_t *dev)
{
    uint16_t unlock_reg;

    for (uint32_t waited = 0; waited <= EEPROM_TIMEOUT_MS; waited += EEPROM_POLL_STEP_MS) {
        esp_err_t err = read16(dev, TMP117_REG_EEPROM_UNLOCK, &unlock_reg);
        if (err != ESP_OK) {
            return err;
        }

        if (!(unlock_reg & (1U << TMP117_EEPROM_BUSY_BIT))) {
            return ESP_OK;
        }

        tmp_delay_ms(EEPROM_POLL_STEP_MS);
    }

    ESP_LOGE(TAG, "EEPROM stayed busy for more than %d ms", EEPROM_TIMEOUT_MS);

    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Remove the EEPROM write protection and wait for the sensor to be ready
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the EEPROM never went idle, or
 *         an I2C error
 */
static esp_err_t eeprom_unlock(tmp117_t *dev)
{
    esp_err_t err = eeprom_wait_idle(dev);
    if (err != ESP_OK) {
        return err;
    }

    return write16(dev, TMP117_REG_EEPROM_UNLOCK, 1U << TMP117_EEPROM_UNLOCK_BIT);
}

/**
 * @brief Put the EEPROM write protection back, once programming has finished
 *
 * @param[in,out] dev Handle
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the EEPROM never went idle, or
 *         an I2C error
 */
static esp_err_t eeprom_lock(tmp117_t *dev)
{
    esp_err_t err = eeprom_wait_idle(dev);
    if (err != ESP_OK) {
        return err;
    }

    return write16(dev, TMP117_REG_EEPROM_UNLOCK, 0);
}

/**
 * @brief Map an EEPROM word number to its register address
 *
 * The three general purpose words are not contiguous, since the offset register
 * sits between the second and the third.
 *
 * @param[in] eeprom_nr Word number, 1 to 3
 * @param[out] reg Register address of that word
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on a bad word number
 */
static esp_err_t eeprom_reg(uint8_t eeprom_nr, uint8_t *reg)
{
    switch (eeprom_nr) {
    case 1:
        *reg = TMP117_REG_EEPROM1;
        break;
    case 2:
        *reg = TMP117_REG_EEPROM2;
        break;
    case 3:
        *reg = TMP117_REG_EEPROM3;
        break;
    default:
        ESP_LOGE(TAG, "EEPROM word number must be between 1 and 3, got %u", eeprom_nr);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Program one register into EEPROM by rewriting it while unlocked
 *
 * The sensor has no separate EEPROM address space: a write that lands while the
 * EEPROM is unlocked is what programs the matching EEPROM word.
 *
 * @param[in,out] dev Handle
 * @param[in] reg Register to program
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the programming never finished,
 *         or an I2C error
 */
static esp_err_t eeprom_program_reg(tmp117_t *dev, uint8_t reg)
{
    uint16_t value;

    esp_err_t err = read16(dev, reg, &value);
    if (err != ESP_OK) {
        return err;
    }

    err = write16(dev, reg, value);
    if (err != ESP_OK) {
        return err;
    }

    return eeprom_wait_idle(dev);
}

// *****************************************************************************
// Section: Lifecycle

esp_err_t tmp117_init(tmp117_t *dev, i2c_master_bus_handle_t bus, uint8_t address)
{
    return tmp117_init_with_clock(dev, bus, address, TMP117_DEFAULT_SCL_SPEED_HZ);
}

esp_err_t tmp117_init_with_clock(tmp117_t *dev, i2c_master_bus_handle_t bus, uint8_t address, uint32_t scl_speed_hz)
{
    if (dev == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));

    dev->address = address;
    dev->alert_type = TMP117_ALERT_NONE;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev->i2c_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not add the TMP117 to the I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "TMP117 attached to the I2C bus at address 0x%02X, %u Hz", address, (unsigned int)scl_speed_hz);

    return ESP_OK;
}

esp_err_t tmp117_deinit(tmp117_t *dev)
{
    esp_err_t err = ESP_OK;

    if (dev->i2c_dev) {
        err = i2c_master_bus_rm_device(dev->i2c_dev);
        dev->i2c_dev = NULL;
    }

    return err;
}

esp_err_t tmp117_begin(tmp117_t *dev)
{
    uint16_t id;

    esp_err_t err = tmp117_get_device_id(dev, &id);
    if (err != ESP_OK) {
        return err;
    }

    if (id != TMP117_DEVICE_ID_VALUE) {
        ESP_LOGE(TAG, "Device ID is 0x%03X, expected 0x%03X - check the wiring and the address", id,
                 TMP117_DEVICE_ID_VALUE);
        return ESP_ERR_NOT_FOUND;
    }

    /* A new result every 125 ms, averaged over 8 conversions, with the ALERT
     * pin flagging new data. */
    err = tmp117_set_conv_mode(dev, TMP117_CMODE_CONTINUOUS);
    if (err == ESP_OK) {
        err = tmp117_set_conv_time(dev, TMP117_CONVT_C125mS);
    }
    if (err == ESP_OK) {
        err = tmp117_set_averaging(dev, TMP117_AVE_8);
    }
    if (err == ESP_OK) {
        err = tmp117_set_alert_mode(dev, TMP117_PMODE_DATA);
    }
    if (err == ESP_OK) {
        err = tmp117_set_offset_temperature(dev, 0.0f);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TMP117 ready");
    }

    return err;
}

esp_err_t tmp117_soft_reset(tmp117_t *dev)
{
    /* Everything else in the register is about to be reloaded from EEPROM, so
     * there is nothing worth preserving here. */
    esp_err_t err = write16(dev, TMP117_REG_CONFIGURATION, 1U << TMP117_CONFIG_SOFT_RESET_BIT);
    if (err != ESP_OK) {
        return err;
    }

    tmp_delay_ms(RESET_TIME_MS);

    dev->alert_type = TMP117_ALERT_NONE;

    return ESP_OK;
}

// *****************************************************************************
// Section: Measurement

esp_err_t tmp117_get_temperature(tmp117_t *dev, float *temperature)
{
    uint16_t raw;

    if (temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = read16(dev, TMP117_REG_TEMPERATURE, &raw);
    if (err != ESP_OK) {
        return err;
    }

    *temperature = (int16_t)raw * TMP117_RESOLUTION;

    return ESP_OK;
}

// *****************************************************************************
// Section: Configuration

esp_err_t tmp117_set_conv_mode(tmp117_t *dev, tmp117_cmode_t cmode)
{
    return update_config(dev, TMP117_CONFIG_MOD_MASK << TMP117_CONFIG_MOD_SHIFT,
                         ((uint16_t)cmode & TMP117_CONFIG_MOD_MASK) << TMP117_CONFIG_MOD_SHIFT);
}

esp_err_t tmp117_set_conv_time(tmp117_t *dev, tmp117_convt_t convtime)
{
    return update_config(dev, TMP117_CONFIG_CONV_MASK << TMP117_CONFIG_CONV_SHIFT,
                         ((uint16_t)convtime & TMP117_CONFIG_CONV_MASK) << TMP117_CONFIG_CONV_SHIFT);
}

esp_err_t tmp117_set_averaging(tmp117_t *dev, tmp117_ave_t ave)
{
    return update_config(dev, TMP117_CONFIG_AVG_MASK << TMP117_CONFIG_AVG_SHIFT,
                         ((uint16_t)ave & TMP117_CONFIG_AVG_MASK) << TMP117_CONFIG_AVG_SHIFT);
}

esp_err_t tmp117_set_alert_mode(tmp117_t *dev, tmp117_pmode_t mode)
{
    const uint16_t mask =
        (1U << TMP117_CONFIG_THERM_BIT) | (1U << TMP117_CONFIG_POL_BIT) | (1U << TMP117_CONFIG_DR_ALERT_BIT);
    uint16_t value;

    switch (mode) {
    case TMP117_PMODE_THERMAL:
        value = 1U << TMP117_CONFIG_THERM_BIT; // Therm mode, pin follows the limits, active low
        break;
    case TMP117_PMODE_ALERT:
        value = 0; // Alert mode, pin follows the limits, active low
        break;
    case TMP117_PMODE_DATA:
        value = 1U << TMP117_CONFIG_DR_ALERT_BIT; // Pin flags a finished conversion instead
        break;
    default:
        ESP_LOGE(TAG, "Unknown alert pin mode %d", (int)mode);
        return ESP_ERR_INVALID_ARG;
    }

    return update_config(dev, mask, value);
}

esp_err_t tmp117_set_alert_temperature(tmp117_t *dev, float low_temp, float high_temp)
{
    esp_err_t err = write16(dev, TMP117_REG_TEMP_HIGH_LIMIT, (uint16_t)temp_to_raw(high_temp));
    if (err != ESP_OK) {
        return err;
    }

    return write16(dev, TMP117_REG_TEMP_LOW_LIMIT, (uint16_t)temp_to_raw(low_temp));
}

tmp117_alert_t tmp117_get_alert_type(const tmp117_t *dev)
{
    return dev->alert_type;
}

esp_err_t tmp117_set_offset_temperature(tmp117_t *dev, float offset)
{
    return write16(dev, TMP117_REG_TEMPERATURE_OFFSET, (uint16_t)temp_to_raw(offset));
}

esp_err_t tmp117_get_offset_temperature(tmp117_t *dev, float *offset)
{
    uint16_t raw;

    if (offset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = read16(dev, TMP117_REG_TEMPERATURE_OFFSET, &raw);
    if (err != ESP_OK) {
        return err;
    }

    *offset = (int16_t)raw * TMP117_RESOLUTION;

    return ESP_OK;
}

esp_err_t tmp117_set_target_temperature(tmp117_t *dev, float target)
{
    float actual;

    /* The result register already has the current offset folded in, so the new
     * offset has to be added on top of the old one. */
    esp_err_t err = tmp117_get_temperature(dev, &actual);
    if (err != ESP_OK) {
        return err;
    }

    float offset;
    err = tmp117_get_offset_temperature(dev, &offset);
    if (err != ESP_OK) {
        return err;
    }

    return tmp117_set_offset_temperature(dev, offset + (target - actual));
}

// *****************************************************************************
// Section: Status

esp_err_t tmp117_read_config(tmp117_t *dev, uint16_t *config)
{
    uint16_t reg_value;

    esp_err_t err = read16(dev, TMP117_REG_CONFIGURATION, &reg_value);
    if (err != ESP_OK) {
        return err;
    }

    if (reg_value & (1U << TMP117_CONFIG_HIGH_ALERT_BIT)) {
        dev->alert_type = TMP117_ALERT_HIGH;
    } else if (reg_value & (1U << TMP117_CONFIG_LOW_ALERT_BIT)) {
        dev->alert_type = TMP117_ALERT_LOW;
    } else {
        dev->alert_type = TMP117_ALERT_NONE;
    }

    if (config != NULL) {
        *config = reg_value;
    }

    return ESP_OK;
}

void tmp117_set_data_ready_callback(tmp117_t *dev, tmp117_data_ready_cb_t cb, void *arg)
{
    dev->data_ready_cb = cb;
    dev->data_ready_arg = arg;
}

esp_err_t tmp117_update(tmp117_t *dev)
{
    uint16_t config;

    esp_err_t err = tmp117_read_config(dev, &config);
    if (err != ESP_OK) {
        return err;
    }

    if ((config & (1U << TMP117_CONFIG_DATA_READY_BIT)) && dev->data_ready_cb != NULL) {
        dev->data_ready_cb(dev->data_ready_arg);
    }

    return ESP_OK;
}

esp_err_t tmp117_data_ready(tmp117_t *dev, bool *ready)
{
    uint16_t config;

    if (ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = tmp117_read_config(dev, &config);
    if (err != ESP_OK) {
        return err;
    }

    *ready = (config & (1U << TMP117_CONFIG_DATA_READY_BIT)) != 0;

    return ESP_OK;
}

esp_err_t tmp117_get_device_id(tmp117_t *dev, uint16_t *id)
{
    uint16_t raw;

    if (id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = read16(dev, TMP117_REG_DEVICE_ID, &raw);
    if (err != ESP_OK) {
        return err;
    }

    *id = raw & TMP117_DEVICE_ID_MASK;

    return ESP_OK;
}

esp_err_t tmp117_get_device_rev(tmp117_t *dev, uint16_t *rev)
{
    uint16_t raw;

    if (rev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = read16(dev, TMP117_REG_DEVICE_ID, &raw);
    if (err != ESP_OK) {
        return err;
    }

    *rev = (raw >> 12) & TMP117_DEVICE_REV_MASK;

    return ESP_OK;
}

esp_err_t tmp117_log_config(tmp117_t *dev)
{
    uint16_t reg_value;

    esp_err_t err = tmp117_read_config(dev, &reg_value);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Configuration register: 0x%04X", reg_value);
    ESP_LOGI(TAG, "  HIGH alert:  %u", (reg_value >> TMP117_CONFIG_HIGH_ALERT_BIT) & 0x1);
    ESP_LOGI(TAG, "  LOW alert:   %u", (reg_value >> TMP117_CONFIG_LOW_ALERT_BIT) & 0x1);
    ESP_LOGI(TAG, "  Data ready:  %u", (reg_value >> TMP117_CONFIG_DATA_READY_BIT) & 0x1);
    ESP_LOGI(TAG, "  EEPROM busy: %u", (reg_value >> TMP117_CONFIG_EEPROM_BUSY_BIT) & 0x1);
    ESP_LOGI(TAG, "  MOD[1:0]:    %u", (reg_value >> TMP117_CONFIG_MOD_SHIFT) & TMP117_CONFIG_MOD_MASK);
    ESP_LOGI(TAG, "  CONV[2:0]:   %u", (reg_value >> TMP117_CONFIG_CONV_SHIFT) & TMP117_CONFIG_CONV_MASK);
    ESP_LOGI(TAG, "  AVG[1:0]:    %u", (reg_value >> TMP117_CONFIG_AVG_SHIFT) & TMP117_CONFIG_AVG_MASK);
    ESP_LOGI(TAG, "  T/nA:        %u", (reg_value >> TMP117_CONFIG_THERM_BIT) & 0x1);
    ESP_LOGI(TAG, "  POL:         %u", (reg_value >> TMP117_CONFIG_POL_BIT) & 0x1);
    ESP_LOGI(TAG, "  DR/Alert:    %u", (reg_value >> TMP117_CONFIG_DR_ALERT_BIT) & 0x1);
    ESP_LOGI(TAG, "  Soft_Reset:  %u", (reg_value >> TMP117_CONFIG_SOFT_RESET_BIT) & 0x1);

    return ESP_OK;
}

// *****************************************************************************
// Section: EEPROM

esp_err_t tmp117_read_eeprom(tmp117_t *dev, uint8_t eeprom_nr, uint16_t *data)
{
    uint8_t reg;

    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = eeprom_reg(eeprom_nr, &reg);
    if (err != ESP_OK) {
        return err;
    }

    err = eeprom_wait_idle(dev);
    if (err != ESP_OK) {
        return err;
    }

    return read16(dev, reg, data);
}

esp_err_t tmp117_write_eeprom(tmp117_t *dev, uint8_t eeprom_nr, uint16_t data)
{
    uint8_t reg;

    esp_err_t err = eeprom_reg(eeprom_nr, &reg);
    if (err != ESP_OK) {
        return err;
    }

    err = eeprom_unlock(dev);
    if (err != ESP_OK) {
        return err;
    }

    err = write16(dev, reg, data);
    if (err == ESP_OK) {
        err = eeprom_wait_idle(dev);
    }

    /* Lock again even if the write failed, so the EEPROM is never left open */
    esp_err_t lock_err = eeprom_lock(dev);

    return (err != ESP_OK) ? err : lock_err;
}

esp_err_t tmp117_save_settings(tmp117_t *dev)
{
    /* Every register that has an EEPROM word behind it. Programming one is a
     * matter of writing it back while the EEPROM is unlocked. */
    static const uint8_t regs[] = {TMP117_REG_CONFIGURATION, TMP117_REG_TEMP_HIGH_LIMIT, TMP117_REG_TEMP_LOW_LIMIT,
                                   TMP117_REG_TEMPERATURE_OFFSET
                                  };

    esp_err_t err = eeprom_unlock(dev);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        err = eeprom_program_reg(dev, regs[i]);
        if (err != ESP_OK) {
            break;
        }
    }

    esp_err_t lock_err = eeprom_lock(dev);
    if (err != ESP_OK) {
        return err;
    }

    if (lock_err == ESP_OK) {
        ESP_LOGI(TAG, "Settings stored in EEPROM");
    }

    return lock_err;
}
