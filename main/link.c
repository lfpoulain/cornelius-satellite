#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "link.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "cJSON.h"
#include "config.h"
#include "discovery.h"
#include "echo.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "sdkconfig.h"
#include "state.h"

static const char *TAG = "link";

/*
 * Cornelius signe lui-même son certificat et le recrée quand les adresses de l'ordinateur
 * changent ; sa clé, elle, ne change pas. Le satellite ne fait donc confiance qu'à cette clé :
 * l'empreinte SHA-256 de sa clé publique (SubjectPublicKeyInfo), affichée dans
 * Administration → Satellites audio.
 */
static uint8_t pinned_key[32];
static bool pin_valid;
/* Satellite neuf, sans jeton : il demande à être associé et retient la clé de ce premier
 * Cornelius (confiance au premier contact), qu'il épinglera ensuite. */
static bool pairing_mode;
static uint8_t candidate_key[32];
static bool candidate_seen;
/* mbedTLS exige une liste d'autorités, même vide : c'est la vérification ci-dessous qui juge. */
static mbedtls_x509_crt no_authority;

static bool parse_pin(const char *text, uint8_t out[32])
{
    size_t count = 0;
    for (const char *p = text; *p && count < 32;) {
        if (*p == ':' || *p == ' ') {
            p++;
            continue;
        }
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return false;
        char pair[3] = {p[0], p[1], 0};
        out[count++] = (uint8_t)strtoul(pair, NULL, 16);
        p += 2;
    }
    return count == 32;
}

static int verify_key(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    /* Seule la clé du serveur compte : ni autorité, ni nom, ni dates (l'horloge du satellite
     * n'est pas réglée au démarrage). */
    if (depth != 0) {
        *flags = 0;
        return 0;
    }
    uint8_t digest[32];
    mbedtls_sha256(crt->pk_raw.p, crt->pk_raw.len, digest, 0);
    if (pairing_mode) {
        memcpy(candidate_key, digest, sizeof(digest));
        candidate_seen = true;
        *flags = 0;
        return 0;
    }
    if (pin_valid && memcmp(digest, pinned_key, sizeof(digest)) == 0) {
        *flags = 0;
    } else {
        ESP_LOGE(TAG, "Clé de serveur inconnue : ce n'est pas le Cornelius configuré");
        *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
    }
    return 0;
}

static esp_err_t attach_key_pin(void *conf)
{
    mbedtls_ssl_config *ssl = conf;
    mbedtls_x509_crt_init(&no_authority);
    mbedtls_ssl_conf_ca_chain(ssl, &no_authority, NULL);
    mbedtls_ssl_conf_authmode(ssl, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_verify(ssl, verify_key, NULL);
    return ESP_OK;
}

static esp_websocket_client_handle_t client;

/* Trames du micro en attente d'envoi : 12 au plus dans la file (≈ 1 s), 16 emplacements pour
 * qu'une trame en cours d'envoi ne soit jamais réécrite. Au-delà de 8 en attente (640 ms de
 * retard), les plus anciennes sont abandonnées : le son doit rester en temps réel. */
#define FRAME_BYTES (LINK_FRAME_SAMPLES * sizeof(int16_t))
#define FRAME_SLOTS 16
#define FRAME_QUEUE 12
#define FRAME_BACKLOG_MAX 8
static int16_t (*frame_pool)[LINK_FRAME_SAMPLES];
static QueueHandle_t frames;
static uint8_t next_slot;
static void sender_task(void *arg);
static char pending_log[200];
static volatile bool log_pending;
static void start_client(const char *uri);
static char headers[320];
/* Message texte reçu en plusieurs morceaux. */
static char text[1024];
static uint8_t message_opcode;

static void send_json(cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    if (body && esp_websocket_client_is_connected(client))
        esp_websocket_client_send_text(client, body, strlen(body), pdMS_TO_TICKS(1000));
    cJSON_free(body);
    cJSON_Delete(json);
}

static void send_hello(void)
{
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "type", "hello");
    cJSON_AddNumberToObject(hello, "protocol", SATELLITE_PROTOCOL);
    cJSON_AddStringToObject(hello, "firmware", esp_app_get_description()->version);
    cJSON_AddBoolToObject(hello, "aec", echo_active());
    send_json(hello);
    if (satellite.muted) link_send_mic(true);
}

static void send_pair(void)
{
    cJSON *pair = cJSON_CreateObject();
    cJSON_AddStringToObject(pair, "type", "pair");
    cJSON_AddNumberToObject(pair, "protocol", SATELLITE_PROTOCOL);
    cJSON_AddStringToObject(pair, "device", discovery_device_name());
    cJSON_AddStringToObject(pair, "firmware", esp_app_get_description()->version);
    send_json(pair);
}

static void set_conversation(conversation_t next)
{
    if (satellite.conversation == next) return;
    satellite.conversation = next;
    /* Fin de conversation : plus rien à jouer de la précédente. */
    if (next != CONVERSATION_READY) audio_play_flush();
}

static void handle_text(const char *body, size_t length)
{
    cJSON *message = cJSON_ParseWithLength(body, length);
    if (!message) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(message, "type"));
    if (!type) {
        cJSON_Delete(message);
        return;
    }
    if (strcmp(type, "waiting") == 0) {
        satellite.pairing = true;
        ESP_LOGI(TAG, "En attente d'association dans l'administration de Cornelius (%s)",
                 discovery_device_name());
    } else if (strcmp(type, "identify") == 0) {
        satellite.identify_until_us = esp_timer_get_time() + 4000000;
    } else if (strcmp(type, "paired") == 0) {
        const char *token = cJSON_GetStringValue(cJSON_GetObjectItem(message, "token"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(message, "name"));
        if (token && candidate_seen) {
            char key[100] = {0};
            for (int i = 0; i < 32; i++)
                snprintf(key + strlen(key), sizeof(key) - strlen(key), i ? ":%02X" : "%02X",
                         candidate_key[i]);
            config_set("key", key);
            config_set("token", token);
            ESP_LOGI(TAG, "Associé à Cornelius : « %s ». Redémarrage.", name ? name : "?");
            cJSON_Delete(message);
            esp_restart();
        }
    } else if (strcmp(type, "welcome") == 0) {
        const cJSON *volume = cJSON_GetObjectItem(message, "volume");
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(message, "name"));
        if (cJSON_IsNumber(volume)) board_set_volume(volume->valueint);
        ESP_LOGI(TAG, "Accueilli par Cornelius : « %s »", name ? name : "?");
        satellite.online = true;
    } else if (strcmp(type, "listen") == 0) {
        satellite.listen = cJSON_IsTrue(cJSON_GetObjectItem(message, "enabled"));
        ESP_LOGI(TAG, "Micro %s", satellite.listen ? "demandé" : "au repos");
    } else if (strcmp(type, "wake") == 0) {
        satellite.wake_at_us = esp_timer_get_time();
        ESP_LOGI(TAG, "« Cornelius » entendu");
    } else if (strcmp(type, "busy") == 0) {
        satellite.busy_at_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Occupé : la conversation ne s'ouvre pas ici");
    } else if (strcmp(type, "session") == 0) {
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(message, "state"));
        if (state && strcmp(state, "connecting") == 0) set_conversation(CONVERSATION_CONNECTING);
        else if (state && strcmp(state, "ready") == 0) set_conversation(CONVERSATION_READY);
        else if (state && strcmp(state, "closed") == 0) set_conversation(CONVERSATION_IDLE);
        ESP_LOGI(TAG, "Conversation : %s", state ? state : "?");
    } else if (strcmp(type, "volume") == 0) {
        const cJSON *volume = cJSON_GetObjectItem(message, "volume");
        if (cJSON_IsNumber(volume)) board_set_volume(volume->valueint);
    } else if (strcmp(type, "settings") == 0) {
        const cJSON *volume = cJSON_GetObjectItem(message, "volume");
        const cJSON *led = cJSON_GetObjectItem(message, "led");
        const cJSON *brightness = cJSON_GetObjectItem(led, "brightness");
        config_apply_settings(cJSON_GetStringValue(cJSON_GetObjectItem(led, "color")),
                              cJSON_IsNumber(brightness) ? brightness->valueint : 6,
                              cJSON_IsTrue(cJSON_GetObjectItem(led, "nightLight")),
                              cJSON_IsNumber(volume) ? volume->valueint : 70,
                              cJSON_IsTrue(cJSON_GetObjectItem(message, "attentive")));
        ESP_LOGI(TAG, "Réglages reçus de Cornelius");
    }
    cJSON_Delete(message);
}

static void offline(void)
{
    satellite.online = false;
    satellite.listen = false;
    satellite.pairing = false;
    set_conversation(CONVERSATION_IDLE);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    const esp_websocket_event_data_t *data = event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket ouvert");
        if (pairing_mode) send_pair();
        else send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(TAG, "WebSocket fermé");
        offline();
        break;
    case WEBSOCKET_EVENT_DATA: {
        /* Un morceau peut continuer le message précédent (opcode 0) ou un grand message
         * découpé par le tampon de réception (payload_offset > 0). */
        if (data->op_code == 0x1 || data->op_code == 0x2) message_opcode = data->op_code;
        else if (data->op_code != 0x0) break;
        if (message_opcode == 0x2) {
            audio_play_push((const uint8_t *)data->data_ptr, data->data_len);
        } else if (message_opcode == 0x1) {
            const size_t offset = data->payload_offset;
            if (offset + data->data_len >= sizeof(text)) break;
            memcpy(text + offset, data->data_ptr, data->data_len);
            if (offset + data->data_len >= (size_t)data->payload_len)
                handle_text(text, offset + data->data_len);
        }
        break;
    }
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "Erreur WebSocket");
        break;
    default:
        break;
    }
}

/* Adresse fixe si elle est réglée, sinon Cornelius trouvé par mDNS (nouvel essai toutes les
 * 5 s tant qu'aucun ne répond). */
static void resolve_server(char *uri, size_t size)
{
    const satellite_config_t *config = config_get();
    if (config->server_uri[0]) {
        strlcpy(uri, config->server_uri, size);
        return;
    }
    while (!discovery_find(uri, size)) vTaskDelay(pdMS_TO_TICKS(5000));
}

/*
 * Démarrage de la liaison, puis surveillance : injoignable depuis 30 s (ordinateur qui a changé
 * d'adresse, Wi-Fi du PC coupé…), Cornelius est recherché par mDNS ; s'il a une autre adresse,
 * la liaison y repart. Vaut aussi pour une adresse fixe devenue fausse.
 */
static void link_task(void *arg)
{
    while (!satellite.wifi) vTaskDelay(pdMS_TO_TICKS(200));
    static char uri[128];
    resolve_server(uri, sizeof(uri));
    pairing_mode = config_needs_pairing();
    if (pairing_mode) strlcat(uri, "/pair", sizeof(uri));
    start_client(uri);
    int offline_s = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        offline_s = satellite.online ? 0 : offline_s + 1;
        if (offline_s < 30 || !satellite.wifi) continue;
        offline_s = 0;
        char found[128];
        if (!discovery_find(found, sizeof(found))) continue;
        if (pairing_mode) strlcat(found, "/pair", sizeof(found));
        if (strcmp(found, uri) == 0) continue;
        ESP_LOGI(TAG, "Cornelius a changé d'adresse : %s", found);
        strlcpy(uri, found, sizeof(uri));
        esp_websocket_client_stop(client);
        esp_websocket_client_set_uri(client, uri);
        esp_websocket_client_start(client);
    }
}

void link_start(void)
{
    /* En PSRAM : la mémoire interne revient à l'annulation d'écho et au Wi-Fi. */
    frame_pool = heap_caps_malloc(FRAME_SLOTS * FRAME_BYTES, MALLOC_CAP_SPIRAM);
    frames = xQueueCreate(FRAME_QUEUE, sizeof(uint8_t));
    if (!frame_pool || !frames) ESP_LOGE(TAG, "Pas de mémoire pour la file du micro");
    xTaskCreatePinnedToCore(sender_task, "sender", 4096, NULL, 6, NULL, 0);
    xTaskCreate(link_task, "link", 6144, NULL, 5, NULL);
}

static void start_client(const char *uri)
{
    const satellite_config_t *settings = config_get();
    if (!pairing_mode)
        snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", settings->token);
    pin_valid = parse_pin(settings->server_key, pinned_key);
    const bool secure = strncmp(uri, "wss:", 4) == 0;
    if (secure && !pin_valid && !pairing_mode)
        ESP_LOGE(TAG, "Empreinte de la clé de Cornelius absente ou invalide : connexion refusée");
    esp_websocket_client_config_t config = {
        .uri = uri,
        .headers = pairing_mode ? NULL : headers,
        .crt_bundle_attach = secure ? attach_key_pin : NULL,
        /* La clé épinglée identifie Cornelius ; le nom ou l'adresse du certificat n'ajoute rien
         * (il change avec les adresses de l'ordinateur). */
        .skip_cert_common_name_check = true,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 10000,
        .buffer_size = 8192,
        .task_stack = 6144,
        .task_prio = 6,
        .ping_interval_sec = 10,
    };
    ESP_LOGI(TAG, "Connexion à %s", uri);
    client = esp_websocket_client_init(&config);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, NULL);
    esp_websocket_client_start(client);
}

void link_send_frame(const int16_t *frame)
{
    if (!satellite.online || !satellite.listen || satellite.muted || satellite.updating || !frames || !frame_pool)
        return;
    /* La tâche du micro ne doit jamais attendre le Wi-Fi : la trame est copiée dans un
     * emplacement libre et confiée à la tâche d'envoi. File pleine : la plus ancienne part. */
    const uint8_t slot = next_slot;
    next_slot = (next_slot + 1) % FRAME_SLOTS;
    memcpy(frame_pool[slot], frame, FRAME_BYTES);
    if (xQueueSend(frames, &slot, 0) != pdTRUE) {
        uint8_t oldest;
        xQueueReceive(frames, &oldest, 0);
        xQueueSend(frames, &slot, 0);
    }
}

/*
 * Envoi du micro, à part : TCP rattrape un paquet perdu au bout d'1, 2 puis 4 s (mesuré). Une
 * écriture qui dépasse son délai ferme la connexion WebSocket ; on lui laisse donc 10 s, et l'on
 * rattrape le retard en abandonnant les trames les plus anciennes.
 */
static void sender_task(void *arg)
{
    uint8_t slot;
    unsigned dropped = 0;
    for (;;) {
        if (xQueueReceive(frames, &slot, portMAX_DELAY) != pdTRUE) continue;
        if (uxQueueMessagesWaiting(frames) > FRAME_BACKLOG_MAX) {
            dropped++;
            continue;
        }
        if (dropped) {
            ESP_LOGW(TAG, "Micro en retard : %u trames abandonnées", dropped);
            dropped = 0;
        }
        if (!satellite.online || !esp_websocket_client_is_connected(client)) continue;
        if (log_pending) {
            cJSON *log = cJSON_CreateObject();
            cJSON_AddStringToObject(log, "type", "log");
            cJSON_AddStringToObject(log, "message", pending_log);
            send_json(log);
            log_pending = false;
        }
        const int64_t start = esp_timer_get_time();
        const int sent = esp_websocket_client_send_bin(client, (const char *)frame_pool[slot],
                                                       FRAME_BYTES, pdMS_TO_TICKS(10000));
        const int64_t took_ms = (esp_timer_get_time() - start) / 1000;
        if (sent < 0) ESP_LOGW(TAG, "Envoi du micro échoué après %lld ms", took_ms);
        else if (took_ms > 250) ESP_LOGW(TAG, "Envoi du micro lent : %lld ms", took_ms);
    }
}

void link_send_mic(bool muted)
{
    if (!client || !esp_websocket_client_is_connected(client)) return;
    cJSON *mic = cJSON_CreateObject();
    cJSON_AddStringToObject(mic, "type", "mic");
    cJSON_AddBoolToObject(mic, "muted", muted);
    send_json(mic);
}

void link_send_log(const char *message)
{
    /* Appelée par la tâche du micro, qui ne doit jamais attendre le Wi-Fi : le message part
     * avec la prochaine trame, par la tâche d'envoi. Un message en attente n'est pas remplacé. */
    if (log_pending) return;
    strlcpy(pending_log, message, sizeof(pending_log));
    log_pending = true;
}
