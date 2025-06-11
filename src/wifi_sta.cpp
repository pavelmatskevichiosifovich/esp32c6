#include "wifi_sta.h"
#include "wifi_core.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <inttypes.h>

namespace wifi_sta {
    static const char *TAG = "wifi_sta";

    struct ConfigSTA {
        const char* ssid = "ZTE-be628a";
        const char* password = "";
        const char* static_ip = "192.168.100.6";
        const char* netmask = "255.255.255.0";
        const char* gateway = "192.168.100.1";
        const char* dns1 = "192.168.100.1";
        const char* dns2 = "192.168.100.2";
        uint32_t reconnect_delay_ms = 60000;
    };

    static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        ConfigSTA* config = static_cast<ConfigSTA*>(arg);
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "STA started, connecting to SSID '%s'", config->ssid);
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "Disconnected from SSID '%s', reason: %d, waiting %" PRIu32 " ms before reconnecting...", 
                     config->ssid, event->reason, config->reconnect_delay_ms);
            vTaskDelay(config->reconnect_delay_ms / portTICK_PERIOD_MS);
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
            ESP_LOGI(TAG, "STA connected to SSID '%s', channel: %d", config->ssid, event->channel);
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ESP_LOGI(TAG, "STA got IP");
        }
    }

    void init() {
        static ConfigSTA config;

        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!sta_netif) {
            ESP_LOGE(TAG, "Failed to get STA netif handle");
            return;
        }

        esp_err_t ret = esp_netif_dhcpc_stop(sta_netif);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to stop DHCP client: %d", ret);
            return;
        }

        esp_netif_ip_info_t ip_info;
        ip_addr_t ip_addr_lwip, gw_addr_lwip, netmask_addr_lwip;

        ipaddr_aton(config.static_ip, &ip_addr_lwip);
        ipaddr_aton(config.gateway, &gw_addr_lwip);
        ipaddr_aton(config.netmask, &netmask_addr_lwip);

        ip_info.ip.addr = ip_addr_lwip.u_addr.ip4.addr;
        ip_info.gw.addr = gw_addr_lwip.u_addr.ip4.addr;
        ip_info.netmask.addr = netmask_addr_lwip.u_addr.ip4.addr;

        ret = esp_netif_set_ip_info(sta_netif, &ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set IP info: %d", ret);
            return;
        }

        ip_addr_t dns1_ip, dns2_ip;
        ipaddr_aton(config.dns1, &dns1_ip);
        dns_setserver(0, &dns1_ip);
        ipaddr_aton(config.dns2, &dns2_ip);
        dns_setserver(1, &dns2_ip);

        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, config.ssid);
        strcpy((char*)wifi_config.sta.password, config.password);
        wifi_config.sta.channel = 0; // Автоматический выбор канала
        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi config: %d", ret);
            return;
        }

        ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register WIFI event handler: %d", ret);
            return;
        }

        ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register IP event handler: %d", ret);
            return;
        }

        ESP_LOGI(TAG, "STA configured: SSID '%s'", config.ssid);
        esp_wifi_connect();
    }
}