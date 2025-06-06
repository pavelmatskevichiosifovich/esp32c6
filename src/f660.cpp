#include "f660.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string>

namespace f660 {
    static const char* TAG = "f660";
    static volatile bool stopFlag = false;

    static void telnetClientTask(void *pvParameters) {
        const char* server_ip = "192.168.100.1";
        const int port = 23;
        const int connect_timeout_sec = 5;
        ESP_LOGI(TAG, "f660: Starting cycle for IP %s", server_ip);
        while (!stopFlag) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                ESP_LOGE(TAG, "Socket failed for IP %s", server_ip);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
            struct timeval timeout;
            timeout.tv_sec = connect_timeout_sec;
            timeout.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
                ESP_LOGI(TAG, "Connected to IP %s", server_ip);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(sock, "root\r", strlen("root\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(sock, "Zte521\r", strlen("Zte521\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(sock, "ip link set dev eth3 down\r", strlen("ip link set dev eth3 down\r"), 0);
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                send(sock, "ip link set dev eth3 up\r", strlen("ip link set dev eth3 up\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(sock, "exit 0\r", strlen("exit 0\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "Disconnected IP: %s", server_ip);
                close(sock);
            } else {
                ESP_LOGE(TAG, "Connection failed for IP %s", server_ip);
                close(sock);
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        // Выполнение действий в stop
        int stop_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (stop_sock >= 0) {
            struct sockaddr_in stop_addr;
            stop_addr.sin_family = AF_INET;
            stop_addr.sin_port = htons(port);
            inet_pton(AF_INET, server_ip, &stop_addr.sin_addr);
            if (connect(stop_sock, (struct sockaddr*)&stop_addr, sizeof(stop_addr)) == 0) {
                send(stop_sock, "root\r", strlen("root\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(stop_sock, "Zte521\r", strlen("Zte521\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(stop_sock, "ip link set dev eth3 up\r", strlen("ip link set dev eth3 up\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                send(stop_sock, "exit 0\r", strlen("exit 0\r"), 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                close(stop_sock);
                ESP_LOGI(TAG, "Stop executed.");
            } else {
                ESP_LOGE(TAG, "Failed stop.");
                close(stop_sock);
            }
        }
        stopFlag = false;
        vTaskDelete(NULL);
    }

    void start() {
        stopFlag = false;
        xTaskCreate(telnetClientTask, "f660_task", 4096, NULL, 5, NULL);
    }

    void stop() {
        stopFlag = true;
    }
}