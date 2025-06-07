#include "console.h"
#include "utils.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstring>
#include <esp_sleep.h>
#include <string>
#include "f660.h"

namespace console {
    const uart_port_t UART_PORT = UART_NUM_0;
    const int UART_BAUD_RATE = 115200;
    static const char* TAG = "console";

    static void uartTask(void *pvParameter) {
        char buffer[128];
        while (1) {
            int len = uart_read_bytes(UART_PORT, buffer, sizeof(buffer), 100 / portTICK_PERIOD_MS);
            if (len > 0) {
                buffer[len] = '\0';
                std::string response = processCommand(buffer);
                if (response == "Exiting...\n") {
                    vTaskDelete(NULL);
                } else {
                    uart_write_bytes(UART_PORT, response.c_str(), response.length());
                }
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    void init() {
        uart_config_t uart_config;
        memset(&uart_config, 0, sizeof(uart_config));
        uart_config.baud_rate = UART_BAUD_RATE;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_driver_install(UART_PORT, 256, 0, 0, NULL, 0);
        xTaskCreate(&uartTask, "uartTask", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "Console initialized");
    }

    std::string processCommand(const char* command) {
        std::string cmd = utils::trim(command);
        if (cmd.empty()) {
            return "Unknown command.";
        }
        if (cmd == "exit") {
            return "Exiting...\n";
        }
        if (cmd == "help") {
            return "Available commands: help, exit, poweroff, reboot, f660, f660stop";
        }
        if (cmd == "poweroff") {
            esp_deep_sleep_start();
            return "";
        }
        if (cmd == "reboot") {
            esp_restart();
            return "";
        }
        if (cmd == "f660") {
            f660::start();
            return "";
        }
        if (cmd == "f660stop") {
            f660::stop();
            return "";
        }
        return "Unknown command.";
    }
}