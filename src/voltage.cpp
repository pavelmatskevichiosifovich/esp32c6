#include "voltage.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

namespace voltage {
    const char *TAG = "voltage";
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    float scaleFactor = 1.0;  // Фактор масштабирования
    const float calibrationFactor_3v3 = 1.255;  // Коэффициент калибровки для 3.3V
    const float calibrationFactor_r1_r2 = 1.262;  // Коэффициент калибровки для делителя
    adc_channel_t adc_channel = ADC_CHANNEL_4;  // Канал ADC
    float R1 = 30000.0;  // Значение резистора R1 
    float R2 = 7500.0;   // Значение резистора R2

    void init() {
        scaleFactor = (R1 + R2) / R2;  // По умолчанию используем делитель
        ESP_LOGI(TAG, "Initializing ADC with R1=%.2f, R2=%.2f...", R1, R2);
        gpio_num_t gpio_pin = static_cast<gpio_num_t>(adc_channel);
        gpio_set_direction(gpio_pin, GPIO_MODE_INPUT);
        adc_oneshot_unit_init_cfg_t unit_config = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE
        };
        adc_oneshot_new_unit(&unit_config, &adc_handle);
        adc_oneshot_chan_cfg_t config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12
        };
        adc_oneshot_config_channel(adc_handle, adc_channel, &config);
        ESP_LOGI(TAG, "ADC initialized successfully.");
    }

    float readVoltage(bool useDivider) {
        int raw_value;
        esp_err_t err = adc_oneshot_read(adc_handle, adc_channel, &raw_value);
        if (err != ESP_OK) {
            return 0.0;  // Возврат 0 при ошибке
        }
        if (useDivider) {
            return ((float)raw_value / 4095.0) * 3.3 * scaleFactor * calibrationFactor_r1_r2;
        } else {
            return ((float)raw_value / 4095.0) * 3.3 * calibrationFactor_3v3;
        }
    }

    adc_oneshot_unit_handle_t getAdcHandle() {
        return adc_handle;
    }
}