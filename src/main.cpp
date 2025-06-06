#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <driver/uart.h>
#include "nvs_flash.h"
#include "flash.h"
#include "lo.h"
#include "wifi_ap.h"
#include "wifi_sta.h"
#include "voltage.h"
#include "acs712.h"
#include "l298n.h"
#include "console.h"
#include "telnet_server.h"

extern "C" void app_main(void) {
    static const char *TAG = "main";
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Starting application...");
    const char* idf_version = esp_get_idf_version();
    ESP_LOGI(TAG, "ESP-IDF version: %s", idf_version);
    
    flash::init();
    lo::init();
    wifi_ap::init();
    wifi_sta::init();
    console::init();
    telnet_server::init();
    voltage::init();
    // acs712::init();
    l298n::init();
    l298n::startCycleTask();

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        float v3v3 = voltage::readVoltage(false);  // Чтение для 3.3V
        ESP_LOGI(TAG, "Voltage_3v3: %.2f V", v3v3);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        float v_r1_r2 = voltage::readVoltage(true);  // Чтение для R1/R2
        ESP_LOGI(TAG, "Voltage_r1_r2: %.2f V", v_r1_r2);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // float current = acs712::readCurrent();
        // if (current > 0) {
        //     ESP_LOGI(TAG, "Discharge: %.2f A", current);
        // } else if (current < 0) {
        //     ESP_LOGI(TAG, "Charge: %.2f A", current);
        // }
        
        // vTaskDelay(1000 / portTICK_PERIOD_MS);
        // float power = acs712::readPower();
        // if (power > 0) {
        //     ESP_LOGI(TAG, "Power (discharge): %.2f W", power);
        // } else if (power < 0) {
        //     ESP_LOGI(TAG, "Power (charge): %.2f W", power);
        // vTaskDelay(1000 / portTICK_PERIOD_MS);
        //   }
        }
}
