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
#include <functional>
#include "f660.h"
#include "voltage.h"
#include "l298n.h"
#include "dns_server.h"
#include "telnet_server.h"
#include "http_api_server.h"

namespace console {
    const uart_port_t UART_PORT = UART_NUM_0;
    const int UART_BAUD_RATE = 921600;
    static const char* TAG = "console";
    const std::string ROOT_USER = "root";
    const std::string ROOT_PASS = "admin";
    static bool authenticated = false;

    static std::string readLine() {
        std::string line;
        char buffer[128];
        while (1) {
            int len = uart_read_bytes(UART_PORT, buffer, sizeof(buffer) - 1, 100 / portTICK_PERIOD_MS);
            if (len > 0) {
                buffer[len] = '\0';
                for (int i = 0; i < len; i++) {
                    if (buffer[i] == '\n') {
                        return line;
                    } else if (buffer[i] != '\r') {
                        line += buffer[i];
                    }
                }
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    static void uartTask(void *pvParameter) {
        char buffer[128];
        auto sendResponse = [](const char* response) {
            if (response && strlen(response) > 0) {
                uart_write_bytes(UART_PORT, response, strlen(response));
                uart_write_bytes(UART_PORT, "\n", 1);
            }
        };

        while (1) {
            if (!authenticated) {
                static bool waiting_password = false;
                if (!waiting_password) {
                    sendResponse("Login: ");
                    std::string username = readLine();
                    if (username == ROOT_USER) {
                        waiting_password = true;
                        sendResponse("Password: ");
                    } else {
                        sendResponse("Authentication failed.");
                    }
                } else {
                    std::string password = readLine();
                    if (password == ROOT_PASS) {
                        authenticated = true;
                        sendResponse("Authenticated.");
                        uart_write_bytes(UART_PORT, "> ", 2);
                    } else {
                        waiting_password = false;
                        sendResponse("Authentication failed.");
                        sendResponse("Login: ");
                    }
                }
                continue;
            }

            int len = uart_read_bytes(UART_PORT, buffer, sizeof(buffer) - 1, 100 / portTICK_PERIOD_MS);
            if (len > 0) {
                buffer[len] = '\0';
                std::string cmd = utils::trim(buffer);
                if (cmd == "exit") {
                    sendResponse("Exiting...");
                    authenticated = false;
                    sendResponse("Login: ");
                    continue;
                }
                processCommand(buffer, sendResponse);
                uart_write_bytes(UART_PORT, "> ", 2);
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
        ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 256, 256, 0, NULL, 0));
        xTaskCreate(&uartTask, "uartTask", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "Console initialized");
    }

    void processCommand(const char* command, std::function<void(const char*)> sendResponse) {
        std::string cmd = utils::trim(command);
        if (cmd == "help") {
            sendResponse("Available commands: help, exit, poweroff, reboot, f660, f660_stop, voltage_3v3, voltage_r1_r2, l298, l298_stop, dns_server_init, dns_server_stop, telnet_server_init, telnet_server_stop, http_api_server_init, http_api_server_stop");
            return;
        }
        if (cmd == "poweroff") {
            sendResponse("Entering deep sleep...");
            esp_deep_sleep_start();
            return;
        }
        if (cmd == "reboot") {
            sendResponse("Rebooting...");
            esp_restart();
            return;
        }
        if (cmd == "f660") {
            f660::start();
            sendResponse("f660 started.");
            return;
        }
        if (cmd == "f660_stop") {
            f660::stop();
            sendResponse("f660 stopped.");
            return;
        }
        if (cmd == "voltage_3v3") {
            float voltage = voltage::readVoltage(false);
            char response[32];
            snprintf(response, sizeof(response), "Voltage: %.2f V", voltage);
            sendResponse(response);
            return;
        }
        if (cmd == "voltage_r1_r2") {
            float voltage = voltage::readVoltage(true);
            char response[32];
            snprintf(response, sizeof(response), "Voltage: %.2f V", voltage);
            sendResponse(response);
            return;
        }
        if (cmd == "l298") {
            sendResponse("l298 started.");
            l298n::startCycleTask();
            return;
        }
        if (cmd == "l298_stop") {
            l298n::stopCycleTask();
            sendResponse("l298 stopped.");
            return;
        }
        if (cmd == "dns_server_init") {
            dns_server::init();
            sendResponse("DNS server started.");
            return;
        }
        if (cmd == "dns_server_stop") {
            dns_server::stop();
            sendResponse("DNS server stopped.");
            return;
        }
        if (cmd == "telnet_server_init") {
            telnet_server::init();
            sendResponse("Telnet server started.");
            return;
        }
        if (cmd == "telnet_server_stop") {
            telnet_server::stop();
            sendResponse("Telnet server stopped.");
            return;
        }
        if (cmd == "http_api_server_init") {
            http_api_server::init();
            sendResponse("HTTP API server started.");
            return;
        }
        if (cmd == "http_api_server_stop") {
            http_api_server::stop();
            sendResponse("HTTP API server stopped.");
            return;
        }
        sendResponse("Unknown command.");
    }
}