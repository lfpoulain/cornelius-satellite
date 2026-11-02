#include "discovery.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mdns.h"

static const char *TAG = "discovery";
static char name[40];

void discovery_start(void)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGE(TAG, "Répondeur mDNS indisponible");
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "cornelius-satellite-%02x%02x%02x", mac[3], mac[4], mac[5]);
    mdns_hostname_set(name);
    ESP_LOGI(TAG, "Nom sur le réseau : %s.local", name);
}

static const char *txt_value(const mdns_result_t *result, const char *key)
{
    for (size_t i = 0; i < result->txt_count; i++)
        if (strcmp(result->txt[i].key, key) == 0) return result->txt[i].value;
    return NULL;
}

const char *discovery_device_name(void)
{
    return name;
}

/* Bits de tête communs à deux adresses IPv4 : 32 pour la même, 24 pour un même /24. */
static int common_prefix(esp_ip4_addr_t a, esp_ip4_addr_t b)
{
    /* Adresses en ordre réseau ; l'ESP32 est petit-boutiste. */
    const uint32_t diff = __builtin_bswap32(a.addr ^ b.addr);
    return diff ? __builtin_clz(diff) : 32;
}

bool discovery_find(char *uri, size_t size)
{
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_cornelius", "_tcp", 3000, 4, &results) != ESP_OK || !results) {
        ESP_LOGW(TAG, "Aucun Cornelius trouvé sur le réseau");
        return false;
    }
    /* Clé retenue à l'association : un autre Cornelius du réseau (une autre machine du foyer)
     * annonce une autre empreinte et serait refusé ; on le passe. Un Cornelius sans empreinte
     * dans son annonce reste essayé. */
    const char *wanted = config_get()->server_key;
    /* L'ordinateur annonce toutes ses adresses (Ethernet, Wi-Fi, machines virtuelles Hyper-V
     * ou Docker), parfois sur des réponses séparées. On prend la plus proche de celle du
     * satellite (le plus long préfixe commun) : le même sous-réseau d'abord, sinon le réseau de
     * la maison (10.0.0.10 pour un satellite en 10.0.1.241) plutôt qu'un réseau virtuel. */
    esp_netif_ip_info_t own = {0};
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &own);
    esp_ip4_addr_t best = {0};
    int best_score = -1;
    const mdns_result_t *chosen = NULL;
    for (const mdns_result_t *result = results; result; result = result->next) {
        const char *key = txt_value(result, "key");
        if (wanted[0] && key && strcasecmp(key, wanted) != 0) {
            ESP_LOGI(TAG, "Autre Cornelius ignoré : %s",
                     result->instance_name ? result->instance_name : "?");
            continue;
        }
        if (result->port == 0) continue;
        bool any = false;
        for (const mdns_ip_addr_t *addr = result->addr; addr; addr = addr->next) {
            if (addr->addr.type != ESP_IPADDR_TYPE_V4) continue;
            any = true;
            const esp_ip4_addr_t candidate = addr->addr.u_addr.ip4;
            const int score = common_prefix(candidate, own.ip);
            if (score <= best_score) continue;
            best = candidate;
            best_score = score;
            chosen = result;
        }
        /* Adresse absente de la réponse : on la demande par le nom de l'ordinateur. */
        if (!any && result->hostname) {
            esp_ip4_addr_t resolved;
            if (mdns_query_a(result->hostname, 2000, &resolved) == ESP_OK &&
                common_prefix(resolved, own.ip) > best_score) {
                best = resolved;
                best_score = common_prefix(resolved, own.ip);
                chosen = result;
            }
        }
    }
    if (chosen) {
        const char *path = txt_value(chosen, "path");
        snprintf(uri, size, "wss://" IPSTR ":%u%s", IP2STR(&best), chosen->port,
                 path && path[0] == '/' ? path : "/satellite");
        ESP_LOGI(TAG, "Cornelius trouvé : %s (%s)", uri,
                 chosen->instance_name ? chosen->instance_name : "?");
    }
    mdns_query_results_free(results);
    return chosen != NULL;
}
