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

    static const char* forwarders[] = {
        "127.0.0.1",
        "194.158.196.245",
        "86.57.255.149",
        "134.17.1.0",
        "134.17.1.1",
        "192.168.100.1",
        "192.168.100.2",
        "192.168.2.1",
        "192.168.0.1",
        "10.0.0.1",
        "172.16.0.1",
        nullptr
    };
    static const int resolver_query_timeout_ms = 2000; // Увеличен таймаут
    static const int min_delay_ms = 200; // Увеличена задержка

    static bool is_valid_ip(const char* ip) {
        struct in_addr addr;
        int result = inet_pton(AF_INET, ip, &addr);
        if (result != 1) {
            ESP_LOGW(TAG, "Invalid IP: %s", ip);
        }
        return result == 1;
    }

    static bool is_loopback_response(const struct sockaddr_in* addr) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
        return strcmp(ip_str, "127.0.0.1") == 0;
    }

    static void dns_task(void* arg) {
        struct sockaddr_in server_addr, client_addr, forward_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[512];
        int forward_sock_fd = -1;
        TickType_t last_request_time = 0;

        sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock_fd < 0) {
            ESP_LOGE(TAG, "Failed to create socket");
            return;
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(53);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGE(TAG, "Failed to bind socket");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        forward_sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (forward_sock_fd < 0) {
            ESP_LOGE(TAG, "Failed to create forward socket");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        struct timeval timeout;
        timeout.tv_sec = resolver_query_timeout_ms / 1000;
        timeout.tv_usec = (resolver_query_timeout_ms % 1000) * 1000;
        setsockopt(forward_sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        ESP_LOGI(TAG, "DNS server started on 0.0.0.0:53");

        while (true) {
            int len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);
            if (len < 12) {
                ESP_LOGE(TAG, "Invalid DNS packet (len=%d)", len);
                continue;
            }

            // Игнорируем запросы от 127.0.0.1, чтобы избежать петли
            if (is_loopback_response(&client_addr)) {
                ESP_LOGW(TAG, "Ignoring request from 127.0.0.1 to prevent loop");
                continue;
            }

            int qname_len = 0;
            while (buffer[12 + qname_len] != 0 && qname_len < len - 12) qname_len++;
            qname_len++;
            if (12 + qname_len + 4 > len) {
                ESP_LOGE(TAG, "Invalid QNAME length");
                continue;
            }
            uint16_t qtype = (buffer[12 + qname_len] << 8) | buffer[12 + qname_len + 1];

            bool response_sent = false;
            for (int i = 0; forwarders[i] != nullptr && !response_sent; i++) {
                if (!is_valid_ip(forwarders[i])) continue;

                TickType_t current_time = xTaskGetTickCount();
                TickType_t elapsed_ms = (current_time - last_request_time) * portTICK_PERIOD_MS;
                if (elapsed_ms < min_delay_ms) {
                    vTaskDelay((min_delay_ms - elapsed_ms) / portTICK_PERIOD_MS);
                }
                last_request_time = xTaskGetTickCount();

                ESP_LOGI(TAG, "Trying forwarder %s", forwarders[i]);

                memset(&forward_addr, 0, sizeof(forward_addr));
                forward_addr.sin_family = AF_INET;
                forward_addr.sin_port = htons(53);
                if (inet_pton(AF_INET, forwarders[i], &forward_addr.sin_addr) != 1) {
                    ESP_LOGE(TAG, "Failed to parse IP: %s", forwarders[i]);
                    continue;
                }

                if (sendto(forward_sock_fd, buffer, len, 0, (struct sockaddr*)&forward_addr, sizeof(forward_addr)) < 0) {
                    ESP_LOGE(TAG, "Failed to forward to %s", forwarders[i]);
                    continue;
                }

                struct sockaddr_in recv_addr;
                socklen_t recv_len = sizeof(recv_addr);
                int recv_len_data = recvfrom(forward_sock_fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&recv_addr, &recv_len);
                if (recv_len_data > 0) {
                    // Игнорируем ответы от 127.0.0.1
                    if (is_loopback_response(&recv_addr)) {
                        ESP_LOGW(TAG, "Ignoring response from 127.0.0.1");
                        continue;
                    }
                    sendto(sock_fd, buffer, recv_len_data, 0, (struct sockaddr*)&client_addr, client_len);
                    ESP_LOGI(TAG, "Forwarded response from %s to %s", forwarders[i], inet_ntoa(client_addr.sin_addr));
                    response_sent = true;
                } else {
                    ESP_LOGW(TAG, "No response from %s", forwarders[i]);
                }
            }

            if (!response_sent && qtype == 1) {
                ESP_LOGI(TAG, "Using local fallback response for %s", inet_ntoa(client_addr.sin_addr));

                buffer[2] = 0x81;
                buffer[3] = 0x80;
                buffer[5] = 0x01;
                buffer[7] = 0x01;

                memmove(buffer + 12 + qname_len + 4, buffer + 12, qname_len + 4);

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
                buffer[pos++] = 0x3c;
                buffer[pos++] = 0x00;
                buffer[pos++] = 0x04;
                buffer[pos++] = 192;
                buffer[pos++] = 168;
                buffer[pos++] = 6;
                buffer[pos++] = 1;

                sendto(sock_fd, buffer, pos, 0, (struct sockaddr*)&client_addr, client_len);
                ESP_LOGI(TAG, "Sent local DNS response (A: 192.168.6.1) to %s", inet_ntoa(client_addr.sin_addr));
            } else if (!response_sent) {
                buffer[2] = 0x81;
                buffer[3] = 0x83;
                buffer[5] = 0x00;
                buffer[7] = 0x00;
                sendto(sock_fd, buffer, 12, 0, (struct sockaddr*)&client_addr, client_len);
                ESP_LOGI(TAG, "Sent NXDOMAIN to %s", inet_ntoa(client_addr.sin_addr));
            }
        }

        close(forward_sock_fd);
        close(sock_fd);
    }

    void init() {
        if (dns_task_handle) {
            ESP_LOGW(TAG, "DNS server already running");
            return;
        }

        xTaskCreate(dns_task, "dns_server", 4096, nullptr, 5, &dns_task_handle);
    }

    void stop() {
        if (sock_fd >= 0) {
            close(sock_fd);
            sock_fd = -1;
        }
        if (dns_task_handle) {
            vTaskDelete(dns_task_handle);
            dns_task_handle = nullptr;
        }
        ESP_LOGI(TAG, "DNS server stopped");
    }
}