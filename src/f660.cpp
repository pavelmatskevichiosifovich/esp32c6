#include "f660.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace f660 {
    static const char* TAG = "f660";
    static volatile bool stopFlag = false;

    // Telnet опции
    #define IAC   255
    #define DONT  254
    #define DO    253
    #define WILL  251
    #define WONT  252
    #define SB    250
    #define SE    240
    #define ECHO  1
    #define SGA   3
    #define TERMINAL_TYPE 24
    #define NAWS  31

    // Конфигурация
    struct Config {
        std::string server_ip = "192.168.100.1";
        int port = 23;
        int timeout_ms = 15000;
        std::string username = "root";
        std::string password = "Zte521";
        std::vector<std::pair<std::string, std::string>> cycle_commands = {
            {"ip link set dev eth3 down\r", "#"},
            {"sleep 3\r", "#"},
            {"ip link set dev eth3 up\r", "#"},
            {"exit 0\r", ""}
        };
        std::vector<std::pair<std::string, std::string>> stop_commands = {
            {"ip link set dev eth3 up\r", "#"},
            {"exit 0\r", ""}
        };
    };

    static Config config;

    // Отправка IAC-команды
    static bool sendIACCommand(int sock, uint8_t command, uint8_t option) {
        uint8_t iac_cmd[3] = {IAC, command, option};
        if (send(sock, iac_cmd, 3, 0) < 0) {
            ESP_LOGI(TAG, "Failed to send IAC %d %d: %d", command, option, errno);
            return false;
        }
        ESP_LOGD(TAG, "Sent IAC %s %d", 
                 command == DO ? "DO" : command == DONT ? "DONT" : command == WILL ? "WILL" : "WONT", option);
        return true;
    }

    // Обработка подкоманд SB
    static bool handleSubnegotiation(int sock, const char* buffer, int len, int& i) {
        if (i + 2 >= len || (unsigned char)buffer[i] != SB) return false;
        uint8_t option = (unsigned char)buffer[i + 1];
        std::string sub_data;
        i += 2;
        while (i < len && (unsigned char)buffer[i] != IAC) {
            sub_data += buffer[i];
            i++;
        }
        if (i + 1 >= len || (unsigned char)buffer[i] != IAC || (unsigned char)buffer[i + 1] != SE) {
            ESP_LOGI(TAG, "Invalid SB for option %d", option);
            return false;
        }
        i += 2;

        if (option == NAWS && sub_data.length() >= 4) {
            uint16_t width = (unsigned char)sub_data[0] << 8 | (unsigned char)sub_data[1];
            uint16_t height = (unsigned char)sub_data[2] << 8 | (unsigned char)sub_data[3];
            ESP_LOGD(TAG, "Received NAWS: width=%d, height=%d", width, height);
        } else if (option == TERMINAL_TYPE && !sub_data.empty()) {
            ESP_LOGD(TAG, "Received TERMINAL-TYPE: %s", sub_data.c_str());
        }
        return true;
    }

    // Ожидание приглашения
    static bool waitForPrompt(int sock, const char* prompt, int timeout_ms) {
        char buffer[256];
        std::string received;
        int64_t start_time = esp_timer_get_time() / 1000;

        int flags = fcntl(sock, F_GETFL, 0);
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
            ESP_LOGI(TAG, "Failed to set non-blocking mode: %d", errno);
            return false;
        }

        while ((esp_timer_get_time() / 1000 - start_time) < timeout_ms) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
            int select_result = select(sock + 1, &read_fds, NULL, NULL, &tv);

            if (select_result > 0) {
                int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (len > 0) {
                    buffer[len] = '\0';
                    for (int i = 0; i < len; i++) {
                        if ((unsigned char)buffer[i] == IAC) {
                            if (i + 1 < len) {
                                uint8_t command = (unsigned char)buffer[i + 1];
                                if (command == SB) {
                                    if (!handleSubnegotiation(sock, buffer, len, i)) return false;
                                    continue;
                                }
                                if (i + 2 < len) {
                                    uint8_t option = (unsigned char)buffer[i + 2];
                                    ESP_LOGD(TAG, "Received IAC %s %d", 
                                             command == DO ? "DO" : command == DONT ? "DONT" : 
                                             command == WILL ? "WILL" : "WONT", option);
                                    if (command == DO) {
                                        if (option == NAWS || option == TERMINAL_TYPE) {
                                            if (!sendIACCommand(sock, WILL, option)) return false;
                                        } else {
                                            if (!sendIACCommand(sock, WONT, option)) return false;
                                        }
                                    } else if (command == DONT) {
                                        if (!sendIACCommand(sock, WONT, option)) return false;
                                    } else if (command == WILL) {
                                        if (option == ECHO || option == SGA) {
                                            if (!sendIACCommand(sock, DO, option)) return false;
                                        } else {
                                            if (!sendIACCommand(sock, DONT, option)) return false;
                                        }
                                    } else if (command == WONT) {
                                        if (!sendIACCommand(sock, DONT, option)) return false;
                                    }
                                    i += 2;
                                }
                            }
                        } else {
                            received += buffer[i];
                        }
                    }
                    ESP_LOGD(TAG, "Received: %s", received.c_str());
                    if (received.find(prompt) != std::string::npos) {
                        return true;
                    }
                } else if (len <= 0) {
                    if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        ESP_LOGI(TAG, "Receive error: %d", errno);
                    }
                    return false;
                }
            } else if (select_result < 0) {
                ESP_LOGI(TAG, "Select error: %d", errno);
                return false;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
            esp_task_wdt_reset();
        }
        ESP_LOGW(TAG, "Timeout waiting for prompt: %s", prompt);
        return false;
    }

    // Отправка команды
    static bool sendCommand(int sock, const char* cmd, const char* expected_prompt, int timeout_ms) {
        if (send(sock, cmd, strlen(cmd), 0) <= 0) {
            ESP_LOGI(TAG, "Failed to send command: %s, errno: %d", cmd, errno);
            return false;
        }
        ESP_LOGI(TAG, "Sent: %s", cmd);
        return waitForPrompt(sock, expected_prompt, timeout_ms);
    }

    // Выполнение последовательности
    static bool executeSequence(int sock, const std::vector<std::pair<std::string, std::string>>& commands) {
        if (!sendIACCommand(sock, DONT, ECHO) ||
            !sendIACCommand(sock, WILL, SGA) ||
            !sendIACCommand(sock, DONT, TERMINAL_TYPE) ||
            !sendIACCommand(sock, DONT, NAWS)) {
            return false;
        }

        if (!waitForPrompt(sock, "Login:", config.timeout_ms)) return false;
        if (!sendCommand(sock, (config.username + "\r").c_str(), "Password:", config.timeout_ms)) return false;
        if (!sendCommand(sock, (config.password + "\r").c_str(), "#", config.timeout_ms)) return false;

        for (const auto& cmd : commands) {
            if (!sendCommand(sock, cmd.first.c_str(), cmd.second.c_str(), config.timeout_ms)) {
                return false;
            }
        }
        return true;
    }

    static void telnetClientTask(void *pvParameters) {
        ESP_LOGW(TAG, "Starting Telnet cycle for IP %s", config.server_ip.c_str());
        esp_task_wdt_add(NULL);
        int retry_count = 0;
        const int MAX_RETRIES = 3;

        while (!stopFlag) {
            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock < 0) {
                ESP_LOGI(TAG, "Socket creation failed: %d", errno);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            int flags = fcntl(sock, F_GETFL, 0);
            if (flags < 0) {
                ESP_LOGI(TAG, "Invalid socket: %d", errno);
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
                ESP_LOGI(TAG, "Failed to set non-blocking mode: %d", errno);
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            struct timeval timeout = { .tv_sec = config.timeout_ms / 1000, .tv_usec = 0 };
            if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                ESP_LOGI(TAG, "Failed to set socket timeouts: %d", errno);
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            struct sockaddr_in server_addr = {
                .sin_len = sizeof(struct sockaddr_in),
                .sin_family = AF_INET,
                .sin_port = htons(config.port),
                .sin_addr = { .s_addr = 0 },
                .sin_zero = {0}
            };
            if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <= 0) {
                ESP_LOGI(TAG, "Invalid IP address: %s", config.server_ip.c_str());
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            int connect_result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
            if (connect_result < 0 && errno != EINPROGRESS) {
                ESP_LOGI(TAG, "Connection failed for IP %s: %d", config.server_ip.c_str(), errno);
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock, &write_fds);
            struct timeval connect_timeout = { .tv_sec = 5, .tv_usec = 0 };
            int select_result = select(sock + 1, NULL, &write_fds, NULL, &connect_timeout);
            if (select_result > 0) {
                int so_error;
                socklen_t len = sizeof(so_error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
                    ESP_LOGI(TAG, "Connection failed for IP %s: %d", config.server_ip.c_str(), so_error ? so_error : errno);
                    close(sock);
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    esp_task_wdt_reset();
                    continue;
                }
            } else {
                ESP_LOGI(TAG, "Connection timeout for IP %s", config.server_ip.c_str());
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            flags = fcntl(sock, F_GETFL, 0);
            if (fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
                ESP_LOGI(TAG, "Failed to reset blocking mode: %d", errno);
                close(sock);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                esp_task_wdt_reset();
                continue;
            }

            ESP_LOGI(TAG, "Connected to IP %s", config.server_ip.c_str());
            if (executeSequence(sock, config.cycle_commands)) {
                ESP_LOGI(TAG, "Cycle completed for IP %s", config.server_ip.c_str());
                retry_count = 0;
            } else {
                retry_count++;
                ESP_LOGI(TAG, "Command sequence failed for IP %s, retry %d/%d", 
                         config.server_ip.c_str(), retry_count, MAX_RETRIES);
                if (retry_count >= MAX_RETRIES) {
                    ESP_LOGI(TAG, "Max retries reached, pausing...");
                    vTaskDelay(5000 / portTICK_PERIOD_MS);
                    retry_count = 0;
                }
            }

            close(sock);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_task_wdt_reset();
        }

        int stop_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (stop_sock >= 0) {
            struct sockaddr_in server_addr = {
                .sin_len = sizeof(struct sockaddr_in),
                .sin_family = AF_INET,
                .sin_port = htons(config.port),
                .sin_addr = { .s_addr = 0 },
                .sin_zero = {0}
            };
            if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <= 0) {
                ESP_LOGI(TAG, "Invalid IP address: %s", config.server_ip.c_str());
                close(stop_sock);
            } else if (connect(stop_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
                if (executeSequence(stop_sock, config.stop_commands)) {
                    ESP_LOGI(TAG, "Stop sequence completed.");
                } else {
                    ESP_LOGI(TAG, "Stop sequence failed.");
                }
            } else {
                ESP_LOGI(TAG, "Stop connection failed: %d", errno);
            }
            close(stop_sock);
        } else {
            ESP_LOGI(TAG, "Stop socket creation failed: %d", errno);
        }

        stopFlag = false;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
    }

    void start() {
        stopFlag = false;
        xTaskCreate(telnetClientTask, "f660_task", 8192, NULL, 5, NULL);
        ESP_LOGI(TAG, "f660 task started.");
    }

    void stop() {
        stopFlag = true;
        ESP_LOGI(TAG, "f660 task stopped.");
    }
}