#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace dns_server {
    static const char* TAG = "dns_server";
    static int sock_fd = -1;
    static TaskHandle_t dns_task_handle = nullptr;

    // ===== КОНФИГУРАЦИЯ =====
    static const char* server_ip = "0.0.0.0";     // IP для прослушивания
    static const uint16_t server_port = 53;        // DNS порт
    static const int query_timeout = 1000;         // Таймаут запроса (мс)
    static const int min_query_delay = 100;        // Минимальная задержка (мс)
    static const uint8_t response_ip[4] = {192, 168, 6, 1}; // IP для ответа

    static const char* forwarders[] = {
        "194.158.196.245",
        "86.57.255.149",
        "134.17.1.0",
        "134.17.1.1",
        "192.168.100.1",
        "192.168.100.2",
        "192.168.6.1",
        "192.168.2.1",
        "192.168.0.1",
        "10.0.0.1",
        "172.16.0.1",
        "127.0.0.1",
        nullptr
    };

    // ===== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ =====
    static bool is_valid_ip(const char* ip) {
        struct in_addr addr;
        return inet_pton(AF_INET, ip, &addr) == 1;
    }

    static bool is_loopback(const struct sockaddr_in* addr) {
        return (ntohl(addr->sin_addr.s_addr) >> 24) == 127; // Проверка 127.x.x.x
    }

    // ===== ОСНОВНАЯ ФУНКЦИЯ DNS СЕРВЕРА =====
    static void dns_task(void* arg) {
        struct sockaddr_in server_addr, client_addr, forward_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[512];
        int forward_sock_fd = -1;

        // Создание и настройка сокета
        sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock_fd < 0) {
            ESP_LOGE(TAG, "Socket error");
            return;
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        server_addr.sin_addr.s_addr = inet_addr(server_ip);

        if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGE(TAG, "Bind error");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        forward_sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (forward_sock_fd < 0) {
            ESP_LOGE(TAG, "Forward socket error");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Установка таймаута
        struct timeval timeout = {
            .tv_sec = query_timeout / 1000,
            .tv_usec = (query_timeout % 1000) * 1000
        };
        setsockopt(forward_sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        ESP_LOGI(TAG, "DNS server started on %s:%d", server_ip, server_port);

        while (true) {
            int len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, 
                             (struct sockaddr*)&client_addr, &client_len);
            if (len < 12) continue;

            // Пропуск loopback-запросов
            if (is_loopback(&client_addr)) {
                ESP_LOGW(TAG, "Loopback ignored");
                continue;
            }

            // Анализ DNS запроса
            int qname_len = 0;
            while (buffer[12 + qname_len] != 0 && qname_len < len - 12) qname_len++;
            if (12 + qname_len + 4 > len) continue;
            
            uint16_t qtype = (buffer[12 + qname_len] << 8) | buffer[12 + qname_len + 1];
            bool response_sent = false;

            // Обработка форвардеров
            for (int i = 0; forwarders[i] && !response_sent; i++) {
                if (!is_valid_ip(forwarders[i])) continue;

                memset(&forward_addr, 0, sizeof(forward_addr));
                forward_addr.sin_family = AF_INET;
                forward_addr.sin_port = htons(53);
                inet_pton(AF_INET, forwarders[i], &forward_addr.sin_addr);

                if (sendto(forward_sock_fd, buffer, len, 0, 
                          (struct sockaddr*)&forward_addr, sizeof(forward_addr)) < 0) continue;

                struct sockaddr_in recv_addr;
                socklen_t recv_len = sizeof(recv_addr);
                int recv_len_data = recvfrom(forward_sock_fd, buffer, sizeof(buffer), 0,
                                           (struct sockaddr*)&recv_addr, &recv_len);
                
                if (recv_len_data > 0 && !is_loopback(&recv_addr)) {
                    sendto(sock_fd, buffer, recv_len_data, 0,
                          (struct sockaddr*)&client_addr, client_len);
                    response_sent = true;
                }
            }

            // Формирование локального ответа
            if (!response_sent && qtype == 1) {
                buffer[2] = 0x81;  // DNS flags
                buffer[3] = 0x80;
                buffer[5] = 0x01;  // 1 answer
                buffer[7] = 0x01;

                int pos = 12 + qname_len + 4;
                buffer[pos++] = 0xc0;
                buffer[pos++] = 0x0c;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x01;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x01;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x3c; // TTL = 60
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x04;
                buffer[pos++] = response_ip[0];
                buffer[pos++] = response_ip[1];
                buffer[pos++] = response_ip[2];
                buffer[pos++] = response_ip[3];

                sendto(sock_fd, buffer, pos, 0, 
                      (struct sockaddr*)&client_addr, client_len);
            }
        }

        close(forward_sock_fd);
        close(sock_fd);
    }

    void init() {
        if (dns_task_handle) return;
        xTaskCreate(dns_task, "dns_server", 4096, nullptr, 5, &dns_task_handle);
    }

    void stop() {
        if (sock_fd >= 0) close(sock_fd);
        if (dns_task_handle) vTaskDelete(dns_task_handle);
        sock_fd = -1;
        dns_task_handle = nullptr;
    }
}