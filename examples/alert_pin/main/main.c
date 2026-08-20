/**
 * @file main.c
 * @brief Example for reacting to a temperature limit through the ALERT pin
 *
 * Sets a low and a high limit, points the ALERT pin at them, and lets a GPIO
 * interrupt wake the application when either one is crossed.
 *
 * Product used is www.solde.red/333175
 *
 * @author Soldered Electronics
 */

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soldered_tmp117.h"

static const char *TAG = "ALERT_PIN";

// Change these to match how your breakout is wired
#define PIN_NUM_SDA   GPIO_NUM_21
#define PIN_NUM_SCL   GPIO_NUM_22
#define PIN_NUM_ALERT GPIO_NUM_26

#define LOW_TEMPERATURE_ALERT  20.0f // Low alert at 20 C
#define HIGH_TEMPERATURE_ALERT 28.0f // High alert at 28 C

// Signals from the interrupt handler that the ALERT pin changed
static SemaphoreHandle_t alert_ready;

/**
 * @brief Interrupt handler for the breakout's ALERT pin
 *
 * Must stay as short as possible. I2C cannot be used from an interrupt, so this
 * only wakes app_main, which then reads out which limit was crossed.
 */
static void IRAM_ATTR alert_isr(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    xSemaphoreGiveFromISR(alert_ready, &higher_priority_task_woken);

    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

void app_main(void)
{
    tmp117_t tmp;
    i2c_master_bus_handle_t bus;

    alert_ready = xSemaphoreCreateBinary();

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

    // A new result every 15.5 ms, so the sensor reacts to a limit quickly
    ESP_ERROR_CHECK(tmp117_set_conv_time(&tmp, TMP117_CONVT_C15mS5));
    ESP_ERROR_CHECK(tmp117_set_averaging(&tmp, TMP117_AVE_NOAVE));

    // Point the pin at the limits instead of at the data ready flag.
    // TMP117_PMODE_THERMAL is the other option: there the high limit turns the
    // pin on and the low limit turns it back off, as a thermostat would.
    ESP_ERROR_CHECK(tmp117_set_alert_mode(&tmp, TMP117_PMODE_ALERT));
    ESP_ERROR_CHECK(tmp117_set_alert_temperature(&tmp, LOW_TEMPERATURE_ALERT, HIGH_TEMPERATURE_ALERT));

    // The breakout's ALERT pin is open drain and idles high, so it needs a
    // pull-up and fires on the falling edge
    const uint64_t alert_pin_mask = 1ULL << PIN_NUM_ALERT;
    gpio_config_t alert_cfg = {
        .pin_bit_mask = alert_pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&alert_cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_NUM_ALERT, alert_isr, NULL));

    while (1) {
        if (xSemaphoreTake(alert_ready, portMAX_DELAY) == pdTRUE) {
            float temperature;

            // Reading the status register is what tells the two alert flags
            // apart, and it clears them in the sensor as it goes
            ESP_ERROR_CHECK(tmp117_read_config(&tmp, NULL));
            ESP_ERROR_CHECK(tmp117_get_temperature(&tmp, &temperature));

            switch (tmp117_get_alert_type(&tmp)) {
            case TMP117_ALERT_HIGH:
                ESP_LOGW(TAG, "High temperature alert: %.4f C", temperature);
                break;
            case TMP117_ALERT_LOW:
                ESP_LOGW(TAG, "Low temperature alert: %.4f C", temperature);
                break;
            case TMP117_ALERT_NONE:
                // The result came back inside the window between the interrupt
                // firing and this read
                ESP_LOGI(TAG, "Back within limits: %.4f C", temperature);
                break;
            }
        }
    }
}
