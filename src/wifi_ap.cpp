#include "wifi_ap.h"
#include "wifi_core.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace wifi_ap {
    struct Config {
        const char* ssid = "esp32c6";
        const char* password = "";
        const char* ip_addr = "192.168.6.1";
        const char* netmask = "255.255.255.0";
        const char* gateway = "192.168.6.1";
        uint8_t max_connection = 4;
        uint32_t dhcp_lease_time = 1440;
    };

    static const char *TAG = "wifi_ap";

    static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        if (event_base == WIFI_EVENT) {
            if (event_id == WIFI_EVENT_AP_STACONNECTED) {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
                ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x joined, AID=%d",
                         event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
            } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
                ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x left, AID=%d, reason=%d",
                         event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid, event->reason);
            } else if (event_id == WIFI_EVENT_AP_PROBEREQRECVED) {
                wifi_event_ap_probe_req_rx_t* event = (wifi_event_ap_probe_req_rx_t*)event_data;
                ESP_LOGI(TAG, "Probe request from %02x:%02x:%02x:%02x:%02x:%02x, RSSI=%d",
                         event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->rssi);
            }
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*)event_data;
            ESP_LOGI(TAG, "Assigned IP " IPSTR " to station", IP2STR(&event->ip));
        }
    }

    void init() {
        Config config;

        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!netif) {
            ESP_LOGE(TAG, "Failed to get AP netif handle");
            return;
        }

        esp_err_t ret = esp_netif_dhcps_stop(netif);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to stop DHCP server: %d", ret);
            return;
        }

        esp_netif_ip_info_t ip_info = {};
        ip_info.ip.addr = ipaddr_addr(config.ip_addr);
        ip_info.netmask.addr = ipaddr_addr(config.netmask);
        ip_info.gw.addr = ipaddr_addr(config.gateway);

        ret = esp_netif_set_ip_info(netif, &ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set IP info: %d", ret);
            return;
        }

        uint32_t lease_time = config.dhcp_lease_time;
        ret = esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &lease_time, sizeof(lease_time));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set DHCP lease time: %d", ret);
            return;
        }
        ESP_LOGI(TAG, "DHCP lease time set to %lu minutes", lease_time);

        ret = esp_netif_dhcps_start(netif);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start DHCP server: %d", ret);
            return;
        }

        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.ap.ssid, config.ssid);
        wifi_config.ap.ssid_len = strlen(config.ssid);
        wifi_config.ap.channel = wifi_core::get_current_channel();
        wifi_config.ap.max_connection = config.max_connection;
        wifi_config.ap.beacon_interval = 100;
        wifi_config.ap.ssid_hidden = false;

        if (strlen(config.password) == 0) {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
            ESP_LOGI(TAG, "Wi-Fi configured as open (no password)");
        } else {
            wifi_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
            strcpy((char*)wifi_config.ap.password, config.password);
            wifi_config.ap.pmf_cfg.capable = true;
            wifi_config.ap.pmf_cfg.required = false;
            wifi_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
            ESP_LOGI(TAG, "Wi-Fi configured with WPA2/WPA3, password: %s", config.password);
        }

        ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure Wi-Fi: %d", ret);
            return;
        }

        ret = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi protocol: %d", ret);
            return;
        }

        ret = esp_wifi_config_80211_tx_rate(WIFI_IF_AP, WIFI_PHY_RATE_MCS0_SGI);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi tx rate: %d", ret);
            return;
        }

        ret = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi bandwidth: %d", ret);
            return;
        }

        ret = esp_wifi_set_max_tx_power(78);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set max tx power: %d", ret);
            return;
        }

        ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &event_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register AP connected event handler: %d", ret);
            return;
        }

        ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &event_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register AP disconnected event handler: %d", ret);
            return;
        }

        ret = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_PROBEREQRECVED, &event_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register AP probe request event handler: %d", ret);
            return;
        }

        ret = esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register IP assigned event handler: %d", ret);
            return;
        }

        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && sta_netif) {
            ret = esp_netif_napt_enable(netif);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to enable NAPT: %d", ret);
                return;
            }
            ESP_LOGI(TAG, "Enabled NAPT");
        } else {
            ESP_LOGE(TAG, "Failed to get AP or STA netif handles");
            return;
        }

        ESP_LOGI(TAG, "AP configured: SSID=%s, channel=%d, clients=%d",
                 config.ssid, wifi_config.ap.channel, config.max_connection);
    }
}