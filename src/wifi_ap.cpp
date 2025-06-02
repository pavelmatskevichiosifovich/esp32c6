#include "wifi_ap.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"

namespace wifi_ap {
    struct Config {
        const char* ssid = "esp32c6";
        uint8_t channel = 11;
        const char* ip_addr = "192.168.6.1";
        const char* netmask = "255.255.255.0";
        const char* gateway = "192.168.6.1";
    };

    static const char *TAG = "wifi_ap";

    void init() {
        Config config;
        
        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_ap();
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        
        esp_wifi_set_ps(WIFI_PS_NONE);  // Отключаем power save
        
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.ap.ssid, config.ssid);
        wifi_config.ap.ssid_len = strlen(config.ssid);
        wifi_config.ap.channel = config.channel;
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        wifi_config.ap.max_connection = 4;
        
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_ip_info_t ipInfo;
        ip_addr_t ip_addr;
        ip_addr_t gw_addr;
        ip_addr_t netmask_addr;
        
        ipaddr_aton(config.ip_addr, &ip_addr);  // Убрана проверка, как раньше
        ipaddr_aton(config.gateway, &gw_addr);  // Убрана проверка
        ipaddr_aton(config.netmask, &netmask_addr);  // Убрана проверка
        
        ipInfo.ip.addr = ip_addr.u_addr.ip4.addr;
        ipInfo.gw.addr = gw_addr.u_addr.ip4.addr;
        ipInfo.netmask.addr = netmask_addr.u_addr.ip4.addr;
        
        esp_netif_dhcps_stop(netif);
        esp_netif_set_ip_info(netif, &ipInfo);
        esp_netif_dhcps_start(netif);
        
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        esp_wifi_start();
        
        ESP_LOGI(TAG, "AP created: SSID '%s'", config.ssid);
    }
}