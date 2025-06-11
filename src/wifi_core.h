#ifndef WIFI_CORE_H
#define WIFI_CORE_H

#include <cstdint>
#include "esp_err.h"

namespace wifi_core {
    esp_err_t init();
    uint8_t get_current_channel();
    esp_err_t set_current_channel(uint8_t channel);
    esp_err_t set_ap_channel(uint8_t channel);
}

#endif