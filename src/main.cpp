#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <driver/uart.h>
#include "nvs_flash.h"
#include <esp_idf_version.h>
#include "flash.h"
#include "lo.h"
#include "wifi_core.h"
#include "wifi_ap.h"
#include "wifi_sta.h"
#include "voltage.h"
#include "acs712.h"
#include "l298n.h"
#include "console.h"
#include "telnet_server.h"
#include "dns_server.h"
#include "http_api_server.h"

extern "C" void app_main(void) {
    static const char *TAG = "main";
    vTaskDelay(5000 / portTICK_PERIOD_MS); // Задержка для стабилизации
    ESP_LOGI(TAG, "Starting application...");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());

    // 1. Инициализация flash (NVS)
    flash::init();

    // 2. Инициализация UART и консоли
    console::init();

    // 3. Инициализация базовых компонентов
    lo::init();

    // 4. Инициализация Wi-Fi
    wifi_core::init();
    wifi_ap::init();
    wifi_sta::init();

    // 5. Инициализация периферии
    voltage::init();
    l298n::init();
    // acs712::init();

    // 6. Инициализация сетевых сервисов
    dns_server::init();
    telnet_server::init();
    http_api_server::init();

    // Основной цикл
    while (1) {
        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}