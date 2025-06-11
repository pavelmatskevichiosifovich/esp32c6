#include "wifi_core.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

namespace wifi_core {
    static const char *TAG = "wifi_core";
    static uint8_t current_channel = 11;
    static bool is_initialized = false;

    esp_err_t init() {
        if (is_initialized) {
            ESP_LOGW(TAG, "Wi-Fi core already initialized, skipping");
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Starting Wi-Fi core initialization");

        // Инициализация сетевого интерфейса
        esp_err_t ret = esp_netif_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init netif: %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "Netif initialized");

        // Создание событийного цикла
        ret = esp_event_loop_create_default();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create event loop: %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "Event loop created");

        // Инициализация Wi-Fi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init Wi-Fi: %d", ret);
            return ret;
        }

        ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %d", ret);
            return ret;
        }
        ESP_LOGI(TAG, "Wi-Fi initialized in AP+STA mode");

        // Создание интерфейсов AP и STA
        esp_netif_create_default_wifi_ap();
        esp_netif_create_default_wifi_sta();
        ESP_LOGI(TAG, "AP and STA interfaces created");

        // Запуск Wi-Fi
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Wi-Fi: %d", ret);
            return ret;
        }

        // Установка канала после запуска Wi-Fi
        if (current_channel != 0) {
            ret = esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set channel %d: %d", current_channel, ret);
                return ret;
            }
            ESP_LOGI(TAG, "Wi-Fi channel set to %d", current_channel);
        } else {
            ESP_LOGE(TAG, "No channel set, initialization may fail");
            return ESP_ERR_INVALID_STATE;
        }

        is_initialized = true;
        ESP_LOGI(TAG, "Wi-Fi core initialized");
        return ESP_OK;
    }

    uint8_t get_current_channel() {
        return current_channel;
    }

    esp_err_t set_current_channel(uint8_t channel) {
        if (channel == 0 || channel > 13) {
            ESP_LOGE(TAG, "Invalid channel %d", channel);
            return ESP_ERR_INVALID_ARG;
        }
        current_channel = channel;
        ESP_LOGI(TAG, "Set channel to %d", channel);
        esp_err_t ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set channel %d: %d", channel, ret);
            return ret;
        }
        return ESP_OK;
    }

    esp_err_t set_ap_channel(uint8_t channel) {
        if (channel == 0 || channel > 13) {
            ESP_LOGE(TAG, "Invalid AP channel %d", channel);
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "AP channel set to %d", channel);
        return set_current_channel(channel);
    }
}