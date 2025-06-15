#include "f660.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace f660 {
    static const char* TAG = "f660";
    static volatile bool stopFlag = false;

    // Telnet options
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

    // Configuration
    struct Config {
        std::string server_ip = "192.168.100.1";
        int port = 23;
        int timeout_ms = 15000;           // Socket operation timeout
        int reconnect_delay_ms = 1000;    // Delay between reconnection attempts
        int max_attempts_before_pause = 10; // Attempts before 60s pause
        int pause_after_attempts_ms = 60000; // 60s pause after 10 attempts
        std::string username = "root";
        std::string password = "Zte521";
        std::vector<std::pair<std::string, std::string>> cycle_commands = {
            {"reboot\r\n", "#"},
            {"sleep 3\r\n", "#"},
            {"true\r\n", "#"},
            {"exit 0\r\n", ""}
        };
        std::vector<std::pair<std::string, std::string>> stop_commands = {
            {"ip link set dev eth3 up\r\n", "#"},
            {"exit 0\r\n", ""}
        };
    };

    static Config config;

    // Send IAC command
    static bool sendIACCommand(int sock, uint8_t command, uint8_t option) {
        uint8_t iac_cmd[3] = {IAC, command, option};
        if (send(sock, iac_cmd, 3, 0) < 0) {
            ESP_LOGE(TAG, "Failed to send IAC command %d %d, errno: %d", command, option, errno);
            return false;
        }
        return true;
    }

    // Handle SB subnegotiation
    static bool handleSubnegotiation(int sock, const char* buffer, int len, int& i) {
        if (i + 2 >= len || (unsigned char)buffer[i] != SB) {
            ESP_LOGE(TAG, "Invalid SB subnegotiation at index %d", i);
            return false;
        }
        uint8_t option = (unsigned char)buffer[i + 1];
        std::string sub_data;
        i += 2;
        while (i < len && (unsigned char)buffer[i] != IAC) {
            sub_data += buffer[i];
            i++;
        }
        if (i + 1 >= len || (unsigned char)buffer[i] != IAC || (unsigned char)buffer[i + 1] != SE) {
            ESP_LOGE(TAG, "Invalid SB end for option %d", option);
            return false;
        }
        i += 2;
        return true;
    }

    // Wait for prompt
    static bool waitForPrompt(int sock, const char* prompt, int timeout_ms) {
        char buffer[256];
        std::string received;
        int64_t start_time = esp_timer_get_time() / 1000;

        int flags = fcntl(sock, F_GETFL, 0);
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
            ESP_LOGE(TAG, "Failed to set non-blocking mode, errno: %d", errno);
            return false;
        }

        while ((esp_timer_get_time() / 1000 - start_time) < timeout_ms) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
            int select_result = select(sock + 1, &read_fds, NULL, NULL, &tv);

            if (select_result < 0) {
                ESP_LOGE(TAG, "Select error, errno: %d", errno);
                return false;
            }

            if (select_result > 0) {
                int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (len > 0) {
                    buffer[len] = '\0';
                    for (int i = 0; i < len; i++) {
                        if ((unsigned char)buffer[i] == IAC) {
                            if (i + 1 < len && (unsigned char)buffer[i + 1] == SB) {
                                if (!handleSubnegotiation(sock, buffer, len, i)) return false;
                                continue;
                            }
                            if (i + 2 < len) {
                                uint8_t command = (unsigned char)buffer[i + 1];
                                uint8_t option = (unsigned char)buffer[i + 2];
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
                        ESP_LOGE(TAG, "Receive error, errno: %d", errno);
                        return false;
                    }
                    if (len == 0) {
                        ESP_LOGE(TAG, "Connection closed by server");
                        return false;
                    }
                }
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        ESP_LOGW(TAG, "Timeout waiting for prompt: %s", prompt);
        return false;
    }

    // Send command
    static bool sendCommand(int sock, const char* cmd, const char* expected_prompt, int timeout_ms) {
        ssize_t sent = send(sock, cmd, strlen(cmd), 0);
        if (sent <= 0) {
            ESP_LOGE(TAG, "Failed to send command: %s, errno: %d", cmd, errno);
            return false;
        }
        return waitForPrompt(sock, expected_prompt, timeout_ms);
    }

    // Execute command sequence
    static bool executeSequence(int sock, const std::vector<std::pair<std::string, std::string>>& commands) {
        if (!sendIACCommand(sock, DONT, ECHO) ||
            !sendIACCommand(sock, WILL, SGA) ||
            !sendIACCommand(sock, DONT, TERMINAL_TYPE) ||
            !sendIACCommand(sock, DONT, NAWS)) {
            ESP_LOGE(TAG, "Failed to send initial IAC commands");
            return false;
        }

        if (!waitForPrompt(sock, "Login:", config.timeout_ms)) {
            ESP_LOGE(TAG, "Failed to get Login prompt");
            return false;
        }
        if (!sendCommand(sock, (config.username + "\r").c_str(), "Password:", config.timeout_ms)) {
            ESP_LOGE(TAG, "Failed to send username");
            return false;
        }
        if (!sendCommand(sock, (config.password + "\r").c_str(), "#", config.timeout_ms)) {
            ESP_LOGE(TAG, "Failed to send password");
            return false;
        }

        for (const auto& cmd : commands) {
            if (!sendCommand(sock, cmd.first.c_str(), cmd.second.c_str(), config.timeout_ms)) {
                ESP_LOGE(TAG, "Failed to execute command: %s", cmd.first.c_str());
                return false;
            }
        }
        return true;
    }

    static void telnetClientTask(void *pv) {
    ESP_LOGI(TAG, "Starting Telnet task for IP %s:%d", config.server_ip.c_str(), config.port);
    int attempt_count = 0;

    while (!stopFlag) {
        int sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_fd < 0) {
            ESP_LOGE(TAG, "Socket creation failed, errno: %d", errno);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
            continue;
        }

        struct timeval timeout = { .tv_sec = config.timeout_ms / 1000, .tv_usec = (config.timeout_ms % 1000) * 1000 };
        if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
            setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            ESP_LOGE(TAG, "Failed to set socket timeouts, errno: %d", errno);
            close(sock_fd);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
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
            ESP_LOGE(TAG, "Invalid IP address: %s", config.server_ip.c_str());
            close(sock_fd);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
            continue;
        }

        int flags = fcntl(sock_fd, F_GETFL, 0);
        if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            ESP_LOGE(TAG, "Failed to set non-blocking mode, errno: %d", errno);
            close(sock_fd);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
            continue;
        }

        int connect_result = connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (connect_result < 0 && errno != EINPROGRESS) {
            ESP_LOGE(TAG, "Connection failed for IP %s:%d, errno: %d", config.server_ip.c_str(), config.port, errno);
            close(sock_fd);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
            continue;
        }

        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock_fd, &write_fds);
        struct timeval connect_timeout = { .tv_sec = config.timeout_ms / 1000, .tv_usec = 0 };
        int select_result = select(sock_fd + 1, NULL, &write_fds, NULL, &connect_timeout);
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (select_result <= 0 || (select_result > 0 && (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0))) {
            ESP_LOGE(TAG, "Connection %s for IP %s:%d, errno: %d", select_result == 0 ? "timeout" : "failed",
                     config.server_ip.c_str(), config.port, so_error ? so_error : errno);
            close(sock_fd);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
            continue;
        }

        if (fcntl(sock_fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            ESP_LOGE(TAG, "Failed to reset blocking mode, errno: %d", errno);
            close(sock_fd);
            vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
            continue;
        }

        ESP_LOGI(TAG, "Connected to IP %s:%d", config.server_ip.c_str(), config.port);
        attempt_count = 0; // Reset attempts on successful connection

        bool sequence_success = executeSequence(sock_fd, config.cycle_commands);
        close(sock_fd);
        if (sequence_success) {
            ESP_LOGI(TAG, "Cycle completed for IP %s", config.server_ip.c_str());
            attempt_count = 0;
        } else {
            ESP_LOGE(TAG, "Command sequence failed for IP %s", config.server_ip.c_str());
            attempt_count++;
            if (attempt_count >= config.max_attempts_before_pause) {
                ESP_LOGW(TAG, "Reached %d attempts, pausing for %d ms", attempt_count, config.pause_after_attempts_ms);
                vTaskDelay(config.pause_after_attempts_ms / portTICK_PERIOD_MS);
                attempt_count = 0;
            }
        }
        vTaskDelay(config.reconnect_delay_ms / portTICK_PERIOD_MS);
    }

        // Execute stop sequence
        int stop_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (stop_sock < 0) {
            ESP_LOGE(TAG, "Stop socket creation failed, errno: %d", errno);
        } else {
            struct sockaddr_in server_addr = {
                .sin_len = sizeof(struct sockaddr_in),
                .sin_family = AF_INET,
                .sin_port = htons(config.port),
                .sin_addr = { .s_addr = 0 },
                .sin_zero = {0}
            };
            if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <= 0) {
                ESP_LOGE(TAG, "Invalid IP address for stop: %s", config.server_ip.c_str());
            } else {
                struct timeval timeout = { .tv_sec = config.timeout_ms / 1000, .tv_usec = (config.timeout_ms % 1000) * 1000 };
                if (setsockopt(stop_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
                    setsockopt(stop_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                    ESP_LOGE(TAG, "Failed to set stop socket timeouts, errno: %d", errno);
                } else {
                    int flags = fcntl(stop_sock, F_GETFL, 0);
                    if (fcntl(stop_sock, F_SETFL, flags | O_NONBLOCK) < 0) {
                        ESP_LOGE(TAG, "Failed to set stop non-blocking mode, errno: %d", errno);
                    } else {
                        int connect_result = connect(stop_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
                        if (connect_result < 0 && errno != EINPROGRESS) {
                            ESP_LOGE(TAG, "Stop connection failed, errno: %d", errno);
                        } else {
                            fd_set write_fds;
                            FD_ZERO(&write_fds);
                            FD_SET(stop_sock, &write_fds);
                            struct timeval connect_timeout = { .tv_sec = config.timeout_ms / 1000, .tv_usec = 0 };
                            int select_result = select(stop_sock + 1, NULL, &write_fds, NULL, &connect_timeout);
                            int so_error = 0;
                            socklen_t len = sizeof(so_error);
                            if (select_result <= 0 || (select_result > 0 && (getsockopt(stop_sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0))) {
                                ESP_LOGE(TAG, "Stop connection %s, errno: %d", select_result == 0 ? "timeout" : "failed", so_error ? so_error : errno);
                            } else if (fcntl(stop_sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
                                ESP_LOGE(TAG, "Failed to reset stop blocking mode, errno: %d", errno);
                            } else {
                                if (executeSequence(stop_sock, config.stop_commands)) {
                                    ESP_LOGI(TAG, "Stop sequence completed");
                                } else {
                                    ESP_LOGE(TAG, "Stop sequence failed");
                                }
                            }
                        }
                    }
                }
            }
            close(stop_sock);
        }

        stopFlag = false;
        vTaskDelete(NULL);
    }

    void start() {
        stopFlag = false;
        xTaskCreate(telnetClientTask, "f660_task", 8192, NULL, 5, NULL);
        ESP_LOGI(TAG, "f660 task started");
    }

    void stop() {
        stopFlag = true;
        ESP_LOGI(TAG, "f660 task stopping");
    }
}