/**
 * @file main.c
 * @brief Example for reading the sensor's serial number and calibrating it against a known temperature
 *
 * Reads the NIST traceable serial number out of the general purpose EEPROM, then
 * shows both ways of correcting a reading: setting the offset directly, and
 * letting the driver work the offset out from a temperature you trust.
 *
 * Product used is www.solde.red/333175
 *
 * @author Soldered Electronics
 */

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soldered_tmp117.h"

static const char *TAG = "CALIBRATION";

// Change these to match how your breakout is wired
#define PIN_NUM_SDA GPIO_NUM_21
#define PIN_NUM_SCL GPIO_NUM_22

// The temperature the sensor is actually sitting at, from a reference you trust
#define REFERENCE_TEMPERATURE 25.0f

void app_main(void)
{
    tmp117_t tmp;
    i2c_master_bus_handle_t bus;
    uint16_t id, rev, eeprom[3];
    float temperature, offset;

    // The I2C bus belongs to the application, not to the driver, so that other
    // devices can share it. Create it first, then hand it to the driver.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_NUM_SDA,
        .scl_io_num = PIN_NUM_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    ESP_ERROR_CHECK(tmp117_init(&tmp, bus, TMP117_DEFAULT_ADDRESS));
    ESP_ERROR_CHECK(tmp117_begin(&tmp));

    ESP_ERROR_CHECK(tmp117_get_device_id(&tmp, &id));
    ESP_ERROR_CHECK(tmp117_get_device_rev(&tmp, &rev));
    ESP_LOGI(TAG, "Device ID 0x%03X, revision %u", id, rev);

    // Soldered programs the sensor's NIST traceable serial number into the three
    // general purpose EEPROM words
    for (uint8_t i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(tmp117_read_eeprom(&tmp, i + 1, &eeprom[i]));
    }
    ESP_LOGI(TAG, "NIST serial number: %04X%04X%04X", eeprom[0], eeprom[1], eeprom[2]);

    // Start from a clean slate, then let the sensor settle into the new
    // conversion cycle before reading it
    ESP_ERROR_CHECK(tmp117_set_offset_temperature(&tmp, 0.0f));
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_ERROR_CHECK(tmp117_get_temperature(&tmp, &temperature));
    ESP_LOGI(TAG, "Uncorrected temperature: %.4f C", temperature);

    // Either add a correction you already know, in °C...
    ESP_ERROR_CHECK(tmp117_set_offset_temperature(&tmp, -0.25f));
    ESP_ERROR_CHECK(tmp117_get_offset_temperature(&tmp, &offset));
    ESP_LOGI(TAG, "Offset set to %.4f C", offset);

    // ...or hand the driver the temperature the sensor is really sitting at and
    // let it work the offset out from the current reading
    ESP_ERROR_CHECK(tmp117_set_target_temperature(&tmp, REFERENCE_TEMPERATURE));
    ESP_ERROR_CHECK(tmp117_get_offset_temperature(&tmp, &offset));
    ESP_LOGI(TAG, "Calibrated to %.4f C, offset is now %.4f C", REFERENCE_TEMPERATURE, offset);

    // The setters only write registers, so the sensor would come back up with
    // its old settings. Uncomment to store the configuration, the limits and the
    // offset in EEPROM - the EEPROM is only rated for 1000 write cycles, so this
    // belongs in a one-off provisioning step rather than in every boot.
    // ESP_ERROR_CHECK(tmp117_save_settings(&tmp));

    while (1) {
        ESP_ERROR_CHECK(tmp117_get_temperature(&tmp, &temperature));
        ESP_LOGI(TAG, "Corrected temperature: %.4f C", temperature);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
