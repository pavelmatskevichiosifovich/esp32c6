#include "nvs_flash.h"
#include "esp_log.h"
#include "flash.h"

namespace flash {
    void init() {
        static const char *TAG = "flash";
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            esp_err_t erase_ret = nvs_flash_erase();
            if (erase_ret == ESP_OK) {
                ret = nvs_flash_init();
            }
        }
        ESP_LOGI(TAG, "Flash initialized");
    }
}