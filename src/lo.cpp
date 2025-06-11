#include "lo.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "esp_mac.h"

namespace lo {
    static const char *TAG = "lo";

    struct Config {
        const char* ip_addr = "127.0.0.1";
        const char* netmask = "255.0.0.0";
        const char* gateway = "0.0.0.0";
    };

    static struct netif lo_netif = {};

    static err_t lo_init(struct netif *netif) {
        netif->name[0] = 'l';
        netif->name[1] = 'o';
        netif->flags |= NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
        netif->mtu = 1500;
        netif->output = nullptr; // Нет исходящего трафика
        netif->output_ip6 = nullptr;
        return ERR_OK;
    }

    void init() {
        Config config;

        // Проверка, не инициализирован ли интерфейс
        if (netif_is_up(&lo_netif)) {
            ESP_LOGW(TAG, "Loopback interface already initialized");
            return;
        }

        // Настройка IP-адресов
        ip_addr_t ipaddr, netmask, gw;
        if (!ipaddr_aton(config.ip_addr, &ipaddr)) {
            ESP_LOGE(TAG, "Invalid IP address: %s", config.ip_addr);
            return;
        }
        if (!ipaddr_aton(config.netmask, &netmask)) {
            ESP_LOGE(TAG, "Invalid netmask: %s", config.netmask);
            return;
        }
        if (!ipaddr_aton(config.gateway, &gw)) {
            ESP_LOGE(TAG, "Invalid gateway: %s", config.gateway);
            return;
        }

        // Добавление loopback-интерфейса
        if (netif_add(&lo_netif, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gw), nullptr, lo_init, nullptr) == nullptr) {
            ESP_LOGE(TAG, "Failed to add loopback interface");
            return;
        }

        // Активация интерфейса
        netif_set_up(&lo_netif);
        if (!netif_is_up(&lo_netif)) {
            ESP_LOGE(TAG, "Failed to bring up loopback interface");
            return;
        }

        ESP_LOGI(TAG, "Loopback interface initialized: IP=%s, Netmask=%s, Gateway=%s",
                 config.ip_addr, config.netmask, config.gateway);
    }
}