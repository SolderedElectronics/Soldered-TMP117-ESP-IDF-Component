/**
 * @file main.c
 * @brief Example for reading the temperature every 500 ms
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

static const char *TAG = "SIMPLE_READING";

// Change these to match how your breakout is wired
#define PIN_NUM_SDA GPIO_NUM_4
#define PIN_NUM_SCL GPIO_NUM_5

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

    // The breakout answers on 0x49 unless its jumpers have been changed
    ESP_ERROR_CHECK(tmp117_init(&tmp, bus, TMP117_DEFAULT_ADDRESS));

    /* The configuration tmp117_begin() leaves behind is:
     *    Conversion mode = CONTINUOUS  ---> keeps converting forever
     *    Conversion time = C125mS      -|
     *    Averaging mode  = AVE8        -|-> new data every 125 ms
     *    Alert mode      = DATA        ---> ALERT pin flags that new data is available
     */
    ESP_ERROR_CHECK(tmp117_begin(&tmp));

    while (1) {
        float temperature;

        // The sensor keeps the newest finished conversion in its result
        // register, so this can be read whenever the application feels like it
        ESP_ERROR_CHECK(tmp117_get_temperature(&tmp, &temperature));
        ESP_LOGI(TAG, "Temperature: %.4f C", temperature);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
