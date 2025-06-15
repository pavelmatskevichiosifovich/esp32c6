#include "http_api_server.h"
#include "voltage.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <string.h>

namespace http_api_server {
    static const char* TAG = "http_api_server";
    static httpd_handle_t server = NULL;

    // Конфигурация сервера
    static const char* SERVER_IP = "0.0.0.0"; // Слушать все интерфейсы
    static const uint16_t SERVER_PORT = 80;   // Порт по умолчанию

    // Обработчик GET-запроса для /api/voltage_3v3
    static esp_err_t voltage_3v3_get_handler(httpd_req_t *req) {
        ESP_LOGD(TAG, "Handling GET /api/voltage_3v3");

        // Читаем напряжение без делителя (false)
        float voltage = voltage::readVoltage(false);

        // Формируем ответ как строку (только цифры, без "V")
        char response[16];
        snprintf(response, sizeof(response), "%.2f", voltage);

        // Отправляем ответ
        esp_err_t ret = httpd_resp_send(req, response, strlen(response));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send response: %d", ret);
        }
        return ret;
    }

    // Обработчик GET-запроса для /api/voltage_r1_r2
    static esp_err_t voltage_r1_r2_get_handler(httpd_req_t *req) {
        ESP_LOGD(TAG, "Handling GET /api/voltage_r1_r2");

        // Читаем напряжение с использованием делителя (true)
        float voltage = voltage::readVoltage(true);

        // Формируем ответ как строку (только цифры, без "V")
        char response[16];
        snprintf(response, sizeof(response), "%.2f", voltage);

        // Отправляем ответ
        esp_err_t ret = httpd_resp_send(req, response, strlen(response));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send response: %d", ret);
        }
        return ret;
    }

    // Регистрация обработчиков URI
    static void register_handlers(httpd_handle_t server) {
        httpd_uri_t voltage_3v3_uri = {
            .uri       = "/api/voltage_3v3",
            .method    = HTTP_GET,
            .handler   = voltage_3v3_get_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t voltage_r1_r2_uri = {
            .uri       = "/api/voltage_r1_r2",
            .method    = HTTP_GET,
            .handler   = voltage_r1_r2_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &voltage_3v3_uri);
        httpd_register_uri_handler(server, &voltage_r1_r2_uri);
        ESP_LOGI(TAG, "Registered URI handlers for /api/voltage_3v3 and /api/voltage_r1_r2");
    }

    void init() {
        ESP_LOGI(TAG, "Initializing HTTP API server on %s:%d...", SERVER_IP, SERVER_PORT);

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = SERVER_PORT;
        config.lru_purge_enable = true;

        // Запускаем сервер
        if (httpd_start(&server, &config) == ESP_OK) {
            ESP_LOGI(TAG, "HTTP server started on %s:%d", SERVER_IP, SERVER_PORT);
            register_handlers(server);
        } else {
            ESP_LOGE(TAG, "Failed to start HTTP server");
        }
    }

    void stop() {
        ESP_LOGI(TAG, "Stopping HTTP API server...");
        if (server) {
            httpd_stop(server);
            server = NULL;
            ESP_LOGI(TAG, "HTTP server stopped");
        }
    }
}