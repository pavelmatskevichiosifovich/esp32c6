#include "wifi_sta.h"
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
        const char* ssid = "ZTE-be628a";         // SSID для STA
        const char* password = "";               // Пароль для STA
        const char* static_ip = "192.168.100.6"; // Статический IP
        const char* netmask = "255.255.255.0";   // Маска подсети
        const char* gateway = "192.168.100.1";   // Шлюз
        const char* dns1 = "192.168.100.1";      // Первый DNS
        const char* dns2 = "192.168.100.2";      // Второй DNS
        uint32_t reconnect_delay_ms = 60000;     // Задержка переподключения
    };

    static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        ConfigSTA* config = static_cast<ConfigSTA*>(arg);
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "Waiting %" PRIu32 " ms before reconnecting...", config->reconnect_delay_ms);
            vTaskDelay(config->reconnect_delay_ms / portTICK_PERIOD_MS);
            esp_wifi_connect();
        }
    }

    void init() {
        static ConfigSTA config;  // Сделали статической, чтобы не терять данные
        
        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_sta();
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        
        esp_wifi_set_ps(WIFI_PS_NONE);  // Power save отключен
        
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, &config);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, &config);
        
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_dhcpc_stop(sta_netif);
        
        esp_netif_ip_info_t ip_info;
        ip_addr_t ip_addr_lwip, gw_addr_lwip, netmask_addr_lwip;
        
        ipaddr_aton(config.static_ip, &ip_addr_lwip);
        ipaddr_aton(config.gateway, &gw_addr_lwip);
        ipaddr_aton(config.netmask, &netmask_addr_lwip);
        
        ip_info.ip.addr = ip_addr_lwip.u_addr.ip4.addr;
        ip_info.gw.addr = gw_addr_lwip.u_addr.ip4.addr;
        ip_info.netmask.addr = netmask_addr_lwip.u_addr.ip4.addr;
        
        esp_netif_set_ip_info(sta_netif, &ip_info);
        
        ip_addr_t dns1_ip, dns2_ip;
        ipaddr_aton(config.dns1, &dns1_ip);
        dns_setserver(0, &dns1_ip);
        ipaddr_aton(config.dns2, &dns2_ip);
        dns_setserver(1, &dns2_ip);
        
        wifi_country_t country = {.cc = "BY", .schan = 1, .nchan = 13, .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_MANUAL};
        esp_wifi_set_country(&country);
        
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, config.ssid);
        strcpy((char*)wifi_config.sta.password, config.password);
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();
    }
}
