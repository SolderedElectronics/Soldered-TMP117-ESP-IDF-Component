/**
 * @file main.c
 * @brief Example for configuring the conversion cycle and getting a callback for each result
 *
 * Shows how to pick a conversion time and an averaging mode, and how to have the
 * driver hand every finished conversion to a callback instead of polling the
 * temperature on a timer of your own.
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

static const char *TAG = "CUSTOM_CONFIG";

// Change these to match how your breakout is wired
#define PIN_NUM_SDA GPIO_NUM_21
#define PIN_NUM_SCL GPIO_NUM_22

// Which of the setups below to run, 1 to 4
#define SETUP_NR 3

/**
 * @brief Called by tmp117_update() every time a conversion finishes
 *
 * Runs in the context of app_main, not in an interrupt, so it is free to talk to
 * the sensor over I2C.
 */
static void new_temperature(void *arg)
{
    tmp117_t *tmp = (tmp117_t *)arg;
    float temperature;

    if (tmp117_get_temperature(tmp, &temperature) == ESP_OK) {
        ESP_LOGI(TAG, "Temperature: %.4f C", temperature);
    }
}

void app_main(void)
{
    tmp117_t tmp;
    i2c_master_bus_handle_t bus;

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

    // Keep converting forever; TMP117_CMODE_ONESHOT and TMP117_CMODE_SHUTDOWN
    // are the low power alternatives
    ESP_ERROR_CHECK(tmp117_set_conv_mode(&tmp, TMP117_CMODE_CONTINUOUS));

    // The conversion cycle is the longer of the conversion time and the time the
    // averaging takes, so the two settings have to be picked together
    switch (SETUP_NR) {
    case 1: // 15.5 ms per result, noisiest
        ESP_ERROR_CHECK(tmp117_set_conv_time(&tmp, TMP117_CONVT_C15mS5));
        ESP_ERROR_CHECK(tmp117_set_averaging(&tmp, TMP117_AVE_NOAVE));
        break;
    case 2: // 125 ms per result
        ESP_ERROR_CHECK(tmp117_set_conv_time(&tmp, TMP117_CONVT_C125mS));
        ESP_ERROR_CHECK(tmp117_set_averaging(&tmp, TMP117_AVE_8));
        break;
    case 3: // 500 ms per result, since averaging 32 conversions takes that long
        ESP_ERROR_CHECK(tmp117_set_conv_time(&tmp, TMP117_CONVT_C125mS));
        ESP_ERROR_CHECK(tmp117_set_averaging(&tmp, TMP117_AVE_32));
        break;
    case 4: // 4 s per result, quietest
        ESP_ERROR_CHECK(tmp117_set_conv_time(&tmp, TMP117_CONVT_C4S));
        ESP_ERROR_CHECK(tmp117_set_averaging(&tmp, TMP117_AVE_64));
        break;
    default:
        ESP_LOGE(TAG, "SETUP_NR must be between 1 and 4");
        break;
    }

    // Print what the sensor ended up configured as
    ESP_ERROR_CHECK(tmp117_log_config(&tmp));

    // Hand the sensor handle to the callback so it can read the temperature
    tmp117_set_data_ready_callback(&tmp, new_temperature, &tmp);

    while (1) {
        // Reads the status flags and calls new_temperature() if a conversion
        // finished since the last call. Polling faster than the conversion cycle
        // just means most calls do nothing.
        ESP_ERROR_CHECK(tmp117_update(&tmp));

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
