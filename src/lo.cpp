#include "lo.h"
#include "esp_log.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "esp_mac.h"

namespace lo {
    void init() {
        static const char *TAG = "lo";
        static struct netif lo_netif;
        ip4_addr_t lo_ipaddr, lo_netmask, lo_gw;
        
        IP4_ADDR(&lo_ipaddr, 127, 0, 0, 1);
        IP4_ADDR(&lo_netmask, 255, 0, 0, 0);
        IP4_ADDR(&lo_gw, 0, 0, 0, 0);
        
        if (netif_add(&lo_netif, &lo_ipaddr, &lo_netmask, &lo_gw, NULL, nullptr, netif_input) == NULL) {
            ESP_LOGE(TAG, "Failed to add loopback interface");
            return;
        }
        
        netif_set_up(&lo_netif);
        ESP_LOGI(TAG, "Loopback interface initialized (127.0.0.1)");
    }
}