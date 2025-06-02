#include "telnet_server.h"
#include "console.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <string>
#include <cstring>
#include "utils.h"
#include <cctype>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace telnet_server {
    static const char* TAG = "telnet_server";
    const std::string ROOT_USER = "root";
    const std::string ROOT_PASS = "admin";
    const int PORT = 23;
    const int AUTH_TIMEOUT_SEC = 60;

    typedef struct {
        int sock;
        char ip[16];
    } ClientData;

    static std::string cleanString(const std::string& input) {
        std::string cleaned;
        for (char c : input) {
            if (std::isalnum(c) || c == ' ') {
                if (std::isalpha(c)) {
                    cleaned += std::tolower(c);
                } else {
                    cleaned += c;
                }
            }
        }
        return utils::trim(cleaned);  // Исправлено: utils::trim
    }

    static std::string receiveLine(int sock) {
        char buffer[128];
        std::string line;
        fd_set readfds;
        struct timeval tv = {AUTH_TIMEOUT_SEC, 0};
        while (1) {
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            int ret = select(sock + 1, &readfds, NULL, NULL, &tv);
            if (ret > 0) {
                int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (len > 0) {
                    buffer[len] = '\0';
                    for (int i = 0; i < len; i++) {
                        if (buffer[i] == '\n') {
                            return line;
                        } else if (buffer[i] != '\r') {
                            line += buffer[i];
                        }
                    }
                } else if (len == 0) {
                    return "";
                }
            } else if (ret == 0) {
                return "";
            } else {
                return "";
            }
        }
    }

    static void handleClient(void *pvParameters) {
        ClientData *clientData = (ClientData*)pvParameters;
        int client_sock = clientData->sock;
        char client_ip[16];
        strcpy(client_ip, clientData->ip);
        vPortFree(pvParameters);
        bool authenticated = false;
        
        send(client_sock, "Login: ", 7, 0);
        std::string username = receiveLine(client_sock);
        std::string cleaned_username = cleanString(username);
        ESP_LOGI(TAG, "Cleaned username: '%s'", cleaned_username.c_str());
        send(client_sock, "Password: ", 10, 0);
        std::string password = receiveLine(client_sock);
        std::string cleaned_password = cleanString(password);
        ESP_LOGI(TAG, "Cleaned password: '%s'", cleaned_password.c_str());
        
        if (cleaned_username == ROOT_USER && cleaned_password == ROOT_PASS) {
            authenticated = true;
            send(client_sock, "Authenticated.\n> ", 16, 0);
        } else {
            send(client_sock, "Authentication failed.\n", 25, 0);
            close(client_sock);
            ESP_LOGI(TAG, "Client disconnected from IP: %s (auth failed)", client_ip);
            vTaskDelete(NULL);
            return;
        }
        
        if (authenticated) {
            while (1) {
                std::string commandStr = receiveLine(client_sock);
                if (!commandStr.empty()) {
                    std::string cleaned_command = cleanString(commandStr);
                    std::string response = processCommand(cleaned_command.c_str());
                    send(client_sock, response.c_str(), response.length(), 0);
                    if (response != "Exiting...\n") {
                        send(client_sock, "\n> ", 3, 0);
                    }
                    if (response == "Exiting...\n") {
                        break;
                    }
                } else {
                    send(client_sock, "> ", 2, 0);
                }
            }
        }
        close(client_sock);
        ESP_LOGI(TAG, "Client disconnected from IP: %s", client_ip);
        vTaskDelete(NULL);
    }

    static void telnetServerTask(void *pvParameters) {
        int port = PORT;
        while (1) {
            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (sock < 0) {
                ESP_LOGE(TAG, "Socket creation failed, retrying...");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(port);
            if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
                ESP_LOGE(TAG, "Bind failed, retrying...");
                close(sock);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            if (listen(sock, 5) != 0) {
                ESP_LOGE(TAG, "Listen failed, retrying...");
                close(sock);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "Telnet server initialized on port %d", port);
            while (1) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_len);
                if (client_sock >= 0) {
                    char client_ip[16];
                    inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
                    ESP_LOGI(TAG, "Client connected from IP: %s", client_ip);
                    ClientData *clientData = (ClientData*)pvPortMalloc(sizeof(ClientData));
                    clientData->sock = client_sock;
                    strcpy(clientData->ip, client_ip);
                    xTaskCreate(handleClient, "handle_client_task", 4096, clientData, 5, NULL);
                } else {
                    ESP_LOGE(TAG, "Accept failed, possible network issue, restarting server...");
                    close(sock);
                    vTaskDelay(5000 / portTICK_PERIOD_MS);
                    break;
                }
            }
        }
        vTaskDelete(NULL);
    }

    std::string processCommand(const char* command) {
        return console::processCommand(command);
    }

    void init() {
        xTaskCreate(telnetServerTask, "telnet_server_task", 4096, NULL, 5, NULL);
    }
}