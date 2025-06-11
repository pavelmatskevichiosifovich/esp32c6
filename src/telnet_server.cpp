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
    #define INACTIVITY_TIMEOUT_MS 60000 // 60 секунд

    #define IAC   255
    #define DONT  254
    #define DO    253
    #define WILL  251
    #define WONT  252
    #define SGA   3

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
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 }; // 50 ms
            int ret = select(sock + 1, &readfds, NULL, NULL, &tv);

            if (ret > 0) {
                int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (bytes > 0) {
                    last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS; // Сброс таймера
                    buffer[bytes] = '\0';
                    std::string hex;
                    for (int i = 0; i < bytes; i++) {
                        char hex_buf[4];
                        snprintf(hex_buf, sizeof(hex_buf), "%02x ", (unsigned char)buffer[i]);
                    }
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
                            return utils::trim(line);
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

            // Проверка таймаута неактивности
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now - last_activity >= INACTIVITY_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Client %s: Inactivity timeout, disconnecting", client_ip);
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
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }

        if (!sendIACCommand(sock, WILL, SGA, client_ip)) {
            ESP_LOGE(TAG, "Client %s: Failed to send initial IAC command", client_ip);
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Sending login prompt", client_ip);
        if (send(sock, "Login: ", 7, 0) < 0) {
            ESP_LOGE(TAG, "Client %s: Failed to send login prompt: %d", client_ip, errno);
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Waiting for username", client_ip);
        std::string username = receiveLine(sock, client_ip);
        ESP_LOGD(TAG, "Client %s: Received username: '%s' (len=%d)", client_ip, username.c_str(), username.length());
        if (username.empty()) {
            ESP_LOGE(TAG, "Client %s: Disconnected (no username): %d", client_ip, errno);
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Sending password prompt", client_ip);
        if (send(sock, "Password: ", 10, 0) < 0) {
            ESP_LOGE(TAG, "Client %s: Failed to send password prompt: %d", client_ip, errno);
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Waiting for password", client_ip);
        std::string password = receiveLine(sock, client_ip);
        ESP_LOGD(TAG, "Client %s: Received password: '%s' (len=%d)", client_ip, password.c_str(), password.length());
        if (password.empty()) {
            ESP_LOGE(TAG, "Client %s: Disconnected (no password): %d", client_ip, errno);
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGD(TAG, "Client %s: Comparing username='%s' with ROOT_USER='%s', password='%s' with ROOT_PASS='%s'", 
                 client_ip, username.c_str(), ROOT_USER.c_str(), password.c_str(), ROOT_PASS.c_str());
        if (username == ROOT_USER && password == ROOT_PASS) {
            authenticated = true;
            ESP_LOGD(TAG, "Client %s: Sending authenticated message", client_ip);
            if (send(sock, "Authenticated.\r\n> ", 17, 0) < 0) {
                ESP_LOGE(TAG, "Client %s: Failed to send authenticated message: %d", client_ip, errno);
                close(sock);
                active_clients--;
                esp_task_wdt_delete(NULL);
                vTaskDelete(NULL);
                return;
            }
            ESP_LOGI(TAG, "Client %s: Authentication successful", client_ip);
        } else {
            ESP_LOGD(TAG, "Client %s: Sending auth failed message", client_ip);
            if (send(sock, "Authentication failed.\r\n", 26, 0) < 0) {
                ESP_LOGE(TAG, "Client %s: Failed to send auth failed message: %d", client_ip, errno);
            }
            ESP_LOGI(TAG, "Client %s: Disconnected (auth failed, username: '%s')", client_ip, username.c_str());
            close(sock);
            active_clients--;
            esp_task_wdt_delete(NULL);
            vTaskDelete(NULL);
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
            while (true) {
                ESP_LOGD(TAG, "Client %s: Waiting for command", client_ip);
                std::string commandStr = receiveLine(sock, client_ip);
                ESP_LOGD(TAG, "Client %s: Received command: '%s'", client_ip, commandStr.c_str());
                if (commandStr.empty()) {
                    if (errno != 0) {
                        break; // Ошибка уже залогирована в receiveLine
                    }
                    ESP_LOGD(TAG, "Client %s: Empty command, sending new prompt", client_ip);
                    int sent = send(sock, "> ", 2, 0);
                    if (sent < 0) {
                        ESP_LOGE(TAG, "Client %s: Failed to send prompt: %d", client_ip, errno);
                        break;
                    }
                    ESP_LOGD(TAG, "Client %s: Sent %d bytes of prompt", client_ip, sent);
                    esp_task_wdt_reset();
                    continue;
                }
                std::string cmd = utils::trim(commandStr);
                ESP_LOGD(TAG, "Client %s: Trimmed command: '%s'", client_ip, cmd.c_str());
                if (cmd == "exit") {
                    sendResponse("Exiting...");
                    ESP_LOGI(TAG, "Client %s: Exiting by command", client_ip);
                    break;
                }
                ESP_LOGD(TAG, "Client %s: Processing command", client_ip);
                console::processCommand(commandStr.c_str(), sendResponse);
                ESP_LOGD(TAG, "Client %s: Command processed, sending prompt", client_ip);
                int sent = send(sock, "> ", 2, 0);
                if (sent < 0) {
                    ESP_LOGE(TAG, "Client %s: Failed to send prompt: %d", client_ip, errno);
                    break;
                }
                ESP_LOGD(TAG, "Client %s: Sent %d bytes of prompt", client_ip, sent);
                esp_task_wdt_reset();
            }
        }
        ESP_LOGD(TAG, "Client %s: Closing socket", client_ip);
        close(sock);
        ESP_LOGI(TAG, "Client %s: Disconnected", client_ip);
        active_clients--;
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
    }

    static void telnetServerTask(void* pvParameters) {
        int port = PORT;
        while (true) {
            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock < 0) {
                ESP_LOGE(TAG, "Socket creation failed: %d, retrying...", errno);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }

            int opt = 1;
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                ESP_LOGE(TAG, "Failed to set SO_REUSEADDR: %d", errno);
                close(sock);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }

            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(port);
            if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
                ESP_LOGE(TAG, "Bind failed: %d, retrying...", errno);
                close(sock);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            if (listen(sock, 5) != 0) {
                ESP_LOGE(TAG, "Listen failed: %d, retrying...", errno);
                close(sock);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "Telnet server initialized on port %d", port);
            while (true) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_len);
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
                    ClientData* clientData = (ClientData*)pvPortMalloc(sizeof(ClientData));
                    if (!clientData) {
                        ESP_LOGE(TAG, "Client %s: Failed to allocate client data", client_ip);
                        send(client_sock, "Server error.\r\n", 15, 0);
                        close(client_sock);
                        continue;
                    }
                    clientData->sock = client_sock;
                    strcpy(clientData->ip, client_ip);
                    xTaskCreate(handleClient, "handle_client_t", 12288, clientData, 5, NULL);
                } else {
                    ESP_LOGE(TAG, "Accept failed: %d, restarting server...", errno);
                    close(sock);
                    vTaskDelay(5000 / portTICK_PERIOD_MS);
                    break;
                }
            }
        }
        vTaskDelete(NULL);
    }

    void init() {
        ESP_LOGI(TAG, "Initializing Telnet server...");
        xTaskCreate(telnetServerTask, "telnet_server_task", 6144, NULL, 5, NULL);
    }
}