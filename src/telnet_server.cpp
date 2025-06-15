#include "telnet_server.h"
#include "console.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <string>
#include <cstring>
#include "utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <functional>

namespace telnet_server {
    static const char* TAG = "telnet_server";
    const std::string ROOT_USER = "root";
    const std::string ROOT_PASS = "admin";
    const int PORT = 23;
    static int active_clients = 0;
    #define MAX_CLIENTS 4
    #define INACTIVITY_TIMEOUT_MS 300000

    #define IAC   255
    #define DONT  254
    #define DO    253
    #define WILL  251
    #define WONT  252
    #define SGA   3

    static bool stopFlag = false; // Флаг для остановки сервера
    static int server_sock = -1; // Сокет сервера для закрытия

    typedef struct {
        int sock;
        char ip[16];
    } ClientData;

    static bool sendIACCommand(int sock, uint8_t command, uint8_t option, const char* client_ip) {
        uint8_t iac_cmd[3] = { IAC, command, option };
        if (send(sock, iac_cmd, 3, 0) < 0) {
            ESP_LOGE(TAG, "Client %s: Failed to send IAC %d %d: %d", client_ip, command, option, errno);
            return false;
        }
        ESP_LOGD(TAG, "Client %s: Sent IAC %s %d", client_ip, 
                 command == DO ? "DO" : command == DONT ? "DONT" : command == WILL ? "WILL" : "WONT", option);
        return true;
    }

    static void cleanupClient(int sock, const char* client_ip) {
        ESP_LOGD(TAG, "Client %s: Closing socket", client_ip);
        close(sock);
        ESP_LOGI(TAG, "Client %s: Disconnected", client_ip);
        active_clients--;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
    }

    static std::string receiveLine(int sock, const char* client_ip) {
        char buffer[128];
        std::string line;
        uint32_t last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;

        int flags = fcntl(sock, F_GETFL, 0);
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
            ESP_LOGE(TAG, "Client %s: Failed to set non-blocking mode: %d", client_ip, errno);
            return "";
        }

        while (true) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now - last_activity >= INACTIVITY_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Client %s: Inactivity timeout in receiveLine", client_ip);
                return "";
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 }; // 50 ms
            int ret = select(sock + 1, &readfds, NULL, NULL, &tv);

            if (ret > 0) {
                int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (bytes > 0) {
                    last_activity = now; // Сброс таймера
                    buffer[bytes] = '\0';
                    ESP_LOGD(TAG, "Client %s: Received %d bytes", client_ip, bytes);
                    for (size_t j = 0; j < bytes; j++) {
                        if ((uint8_t)buffer[j] == IAC) {
                            if (j + 2 < bytes) {
                                uint8_t command = (uint8_t)buffer[j + 1];
                                uint8_t option = (uint8_t)buffer[j + 2];
                                ESP_LOGD(TAG, "Client %s: IAC command: %d, option: %d", client_ip, command, option);
                                if (command == DO || command == DONT || command == WILL || command == WONT) {
                                    if (command == DO || command == DONT) {
                                        sendIACCommand(sock, WONT, option, client_ip);
                                    } else {
                                        sendIACCommand(sock, DONT, option, client_ip);
                                    }
                                    j += 2;
                                    continue;
                                }
                            }
                            continue;
                        } else if (buffer[j] == '\n') {
                            ESP_LOGD(TAG, "Client %s: Line received: %s", client_ip, line.c_str());
                            return utils::trim(line); // Возвращаем обработанную строку
                        } else if (buffer[j] != '\r') {
                            line += buffer[j];
                        }
                    }
                } else if (bytes == 0) {
                    ESP_LOGD(TAG, "Client %s: Socket closed by client", client_ip);
                    return "";
                } else {
                    ESP_LOGE(TAG, "Client %s: Receive error, bytes=%d, errno=%d", client_ip, bytes, errno);
                    return "";
                }
            } else if (ret < 0) {
                ESP_LOGE(TAG, "Client %s: Select error: %d", client_ip, errno);
                return "";
            }

            vTaskDelay(5 / portTICK_PERIOD_MS);
            esp_task_wdt_reset();
        }
    }

    static void handleClient(void* pvParameters) {
        ClientData* clientData = (ClientData*)pvParameters;
        int sock = clientData->sock;
        char client_ip[16];
        strcpy(client_ip, clientData->ip);
        vPortFree(pvParameters);
        bool authenticated = false;

        active_clients++;
        ESP_LOGD(TAG, "Client %s: Active clients: %d", client_ip, active_clients);

        esp_task_wdt_add(NULL);

        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0) {
            ESP_LOGE(TAG, "Client %s: Invalid socket: %d", client_ip, errno);
            cleanupClient(sock, client_ip);
            return;
        }

        if (!sendIACCommand(sock, WILL, SGA, client_ip)) {
            ESP_LOGE(TAG, "Client %s: Failed to send initial IAC command", client_ip);
            cleanupClient(sock, client_ip);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Sending login prompt", client_ip);
        if (send(sock, "Login: ", 7, 0) < 0) {
            ESP_LOGE(TAG, "Client %s: Failed to send login prompt: %d", client_ip, errno);
            cleanupClient(sock, client_ip);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Waiting for username", client_ip);
        std::string username = receiveLine(sock, client_ip);
        ESP_LOGD(TAG, "Client %s: Received username: '%s' (len=%d)", client_ip, username.c_str(), username.length());
        if (username.empty()) {
            ESP_LOGE(TAG, "Client %s: Disconnected (no username or timeout): %d", client_ip, errno);
            cleanupClient(sock, client_ip);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Sending password prompt", client_ip);
        if (send(sock, "Password: ", 10, 0) < 0) {
            ESP_LOGE(TAG, "Client %s: Failed to send password prompt: %d", client_ip, errno);
            cleanupClient(sock, client_ip);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Waiting for password", client_ip);
        std::string password = receiveLine(sock, client_ip);
        ESP_LOGD(TAG, "Client %s: Received password: '%s' (len=%d)", client_ip, password.c_str(), password.length());
        if (password.empty()) {
            ESP_LOGE(TAG, "Client %s: Disconnected (no password or timeout): %d", client_ip, errno);
            cleanupClient(sock, client_ip);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Comparing username='%s' with ROOT_USER='%s', password='%s' with ROOT_PASS='%s'", 
                 client_ip, username.c_str(), ROOT_USER.c_str(), password.c_str(), ROOT_PASS.c_str());
        if (username == ROOT_USER && password == ROOT_PASS) {
            authenticated = true;
            ESP_LOGD(TAG, "Client %s: Sending authenticated message", client_ip);
            if (send(sock, "Authenticated.\r\n> ", 17, 0) < 0) {
                ESP_LOGE(TAG, "Client %s: Failed to send authenticated message: %d", client_ip, errno);
                cleanupClient(sock, client_ip);
                return;
            }
            ESP_LOGI(TAG, "Client %s: Authentication successful", client_ip);
        } else {
            ESP_LOGD(TAG, "Client %s: Sending auth failed message", client_ip);
            if (send(sock, "Authentication failed.\r\n", 26, 0) < 0) {
                ESP_LOGE(TAG, "Client %s: Failed to send auth failed message: %d", client_ip, errno);
            }
            ESP_LOGI(TAG, "Client %s: Disconnected (auth failed, username: '%s')", client_ip, username.c_str());
            cleanupClient(sock, client_ip);
            return;
        }

        if (authenticated) {
            auto sendResponse = [sock, client_ip](const char* response) {
                if (response && strlen(response) > 0) {
                    ESP_LOGD(TAG, "Client %s: Sending response: %s", client_ip, response);
                    int sent = send(sock, response, strlen(response), 0);
                    if (sent < 0) {
                        ESP_LOGE(TAG, "Client %s: Failed to send response: %d", client_ip, errno);
                        return false;
                    }
                    ESP_LOGD(TAG, "Client %s: Sent %d bytes of response", client_ip, sent);
                    if (send(sock, "\r\n", 2, 0) < 0) {
                        ESP_LOGE(TAG, "Client %s: Failed to send newline: %d", client_ip, errno);
                        return false;
                    }
                    ESP_LOGD(TAG, "Client %s: Sent newline", client_ip);
                }
                return true;
            };
            uint32_t last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS; // Таймер неактивности
            while (true) {
                ESP_LOGD(TAG, "Client %s: Waiting for command", client_ip);
                std::string commandStr = receiveLine(sock, client_ip);
                uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGD(TAG, "Client %s: Received command: '%s'", client_ip, commandStr.c_str());
                if (commandStr.empty()) {
                    // Проверяем, не вызван ли empty() из-за таймаута или ошибки
                    if (now - last_activity >= INACTIVITY_TIMEOUT_MS) {
                        ESP_LOGI(TAG, "Client %s: Disconnected due to inactivity timeout", client_ip);
                        cleanupClient(sock, client_ip);
                        return;
                    }
                    // Проверяем состояние сокета перед отправкой prompt
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                        ESP_LOGE(TAG, "Client %s: Socket error detected, errno=%d", client_ip, error);
                        cleanupClient(sock, client_ip);
                        return;
                    }
                    ESP_LOGD(TAG, "Client %s: Empty command received, sending prompt", client_ip);
                    int sent = send(sock, "> ", 2, 0);
                    if (sent < 0) {
                        ESP_LOGE(TAG, "Client %s: Failed to send prompt: %d", client_ip, errno);
                        cleanupClient(sock, client_ip);
                        return;
                    }
                    ESP_LOGD(TAG, "Client %s: Sent %d bytes of prompt", client_ip, sent);
                    last_activity = now; // Сбрасываем таймер при пустой команде
                    esp_task_wdt_reset();
                    continue;
                }
                std::string cmd = utils::trim(commandStr);
                ESP_LOGD(TAG, "Client %s: Trimmed command: '%s'", client_ip, cmd.c_str());
                last_activity = now; // Сбрасываем таймер при получении команды
                if (cmd == "exit") {
                    sendResponse("Exiting...");
                    ESP_LOGI(TAG, "Client %s: Exiting by command", client_ip);
                    cleanupClient(sock, client_ip);
                    return;
                } else {
                    ESP_LOGD(TAG, "Client %s: Processing command", client_ip);
                    console::processCommand(commandStr.c_str(), sendResponse);
                    ESP_LOGD(TAG, "Client %s: Command processed, sending prompt", client_ip);
                }
                int sent = send(sock, "> ", 2, 0);
                if (sent < 0) {
                    ESP_LOGE(TAG, "Client %s: Failed to send prompt: %d", client_ip, errno);
                    cleanupClient(sock, client_ip);
                    return;
                }
                ESP_LOGD(TAG, "Client %s: Sent %d bytes of prompt", client_ip, sent);
                esp_task_wdt_reset();
            }
        }
    }

    static void telnetServerTask(void* pvParameters) {
        int port = PORT;
        while (!stopFlag) {
            server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (server_sock < 0) {
                ESP_LOGE(TAG, "Socket creation failed: %d, retrying...", errno);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }

            int opt = 1;
            if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                ESP_LOGE(TAG, "Failed to set SO_REUSEADDR: %d", errno);
                close(server_sock);
                server_sock = -1;
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }

            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(port);
            if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
                ESP_LOGE(TAG, "Bind failed: %d, retrying...", errno);
                close(server_sock);
                server_sock = -1;
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            if (listen(server_sock, 5) != 0) {
                ESP_LOGE(TAG, "Listen failed: %d, retrying...", errno);
                close(server_sock);
                server_sock = -1;
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "Telnet server initialized on port %d", port);
            while (!stopFlag) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
                if (client_sock >= 0) {
                    char client_ip[16];
                    inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
                    if (active_clients >= MAX_CLIENTS) {
                        ESP_LOGW(TAG, "Client %s: Too many connections", client_ip);
                        send(client_sock, "Too many connections.\r\n", 23, 0);
                        close(client_sock);
                        continue;
                    }
                    ESP_LOGI(TAG, "Client %s: Connected", client_ip);
                    ClientData* client_data = (ClientData*)pvPortMalloc(sizeof(ClientData));
                    if (!client_data) {
                        ESP_LOGE(TAG, "Client %s: Failed to allocate client data", client_ip);
                        send(client_sock, "Server error.\r\n", 15, 0);
                        close(client_sock);
                        continue;
                    }
                    client_data->sock = client_sock;
                    strcpy(client_data->ip, client_ip);
                    xTaskCreate(handleClient, "handle_client_t", 12288, client_data, 5, NULL);
                } else {
                    ESP_LOGE(TAG, "Accept failed: %d, checking stop flag...", errno);
                    if (stopFlag) {
                        break;
                    }
                    close(server_sock);
                    server_sock = -1;
                    vTaskDelay(5000 / portTICK_PERIOD_MS);
                    break;
                }
            }
            if (server_sock >= 0) {
                close(server_sock);
                server_sock = -1;
            }
        }
        ESP_LOGI(TAG, "Telnet server task terminated");
        vTaskDelete(NULL);
    }

    void init() {
        ESP_LOGI(TAG, "Initializing Telnet server...");
        stopFlag = false; // Сбрасываем флаг при инициализации
        xTaskCreate(telnetServerTask, "telnet_server_task", 6144, NULL, 5, NULL);
    }

    void stop() {
        ESP_LOGI(TAG, "Stopping Telnet server...");
        stopFlag = true;
        if (server_sock >= 0) {
            shutdown(server_sock, SHUT_RDWR);
            close(server_sock);
            server_sock = -1;
        }
    }
}