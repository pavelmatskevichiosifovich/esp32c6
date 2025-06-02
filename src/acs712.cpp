#include "acs712.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "voltage.h"
#include "freertos/FreeRTOS.h"

namespace acs712 {
    const char *TAG = "acs712";
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    adc_channel_t adc_channel = ADC_CHANNEL_5;
    const float sensitivity = 0.066;
    const float v_ref = 3.3;
    float zero_current_voltage = v_ref / 2;
    const float default_voltage = 12.8;
    bool initialized = false;

    void init() {
        if (initialized) return;
        adc_oneshot_unit_handle_t voltage_handle = voltage::getAdcHandle();  // Из [11]
        if (voltage_handle != nullptr) {
            adc_handle = voltage_handle;
        } else {
            adc_oneshot_unit_init_cfg_t unit_config = {
                .unit_id = ADC_UNIT_1,
                .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
                .ulp_mode = ADC_ULP_MODE_DISABLE
            };
            esp_err_t err = adc_oneshot_new_unit(&unit_config, &adc_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ADC init failed: %d", err);
                vTaskDelay(100 / portTICK_PERIOD_MS);  // Дополнительная задержка
                return;
            }
        }
        adc_oneshot_chan_cfg_t config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12
        };
        esp_err_t err = adc_oneshot_config_channel(adc_handle, adc_channel, &config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC channel config failed: %d", err);
        }
        initialized = true;
        ESP_LOGI(TAG, "ACS712 initialized successfully");
    }

    float readCurrent() {
        if (!initialized) init();
        int raw_value;
        esp_err_t err = adc_oneshot_read(adc_handle, adc_channel, &raw_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC read failed: %d, returning 0", err);
            vTaskDelay(50 / portTICK_PERIOD_MS);  // Задержка для избежания блокировки
            return 0.0;
        }
        float voltage = ((float)raw_value / 4095.0) * v_ref;
        float current = (voltage - zero_current_voltage) / sensitivity;
        vTaskDelay(50 / portTICK_PERIOD_MS);
        return current;
    }

    float readPower() {
        float current = readCurrent();
        float volt = default_voltage;
        if (voltage::getAdcHandle() != nullptr) {
            volt = voltage::readVoltage(true);
        }
        float power = current * volt;
        vTaskDelay(50 / portTICK_PERIOD_MS);
        return power;
    }

    adc_oneshot_unit_handle_t getAdcHandle() {
        return adc_handle;
    }
}