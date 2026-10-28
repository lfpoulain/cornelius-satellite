/* Local authenticated OTA push. No credential is sent over the network.
 * ota_key.h is generated locally, excluded from version control. */
#include "ota.h"
#include "ota_key.h"
#include "state.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char nonce[65];
static int64_t nonce_until;
static const char *TAG = "ota";

static void hex(const unsigned char *data, size_t len, char *out)
{
    const char *digits = "0123456789abcdef";
    for (size_t i=0; i<len; i++) { out[2*i]=digits[data[i]>>4]; out[2*i+1]=digits[data[i]&15]; }
    out[len*2]=0;
}

static void rotate_nonce(void)
{
    unsigned char bytes[32];
    esp_fill_random(bytes, sizeof(bytes));
    hex(bytes, sizeof(bytes), nonce);
    nonce_until=esp_timer_get_time()+60000000;
}

static esp_err_t status(httpd_req_t *req)
{
    rotate_nonce();
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    esp_ota_img_states_t ota_state=ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(esp_ota_get_running_partition(),&ota_state);
    char body[512];
    snprintf(body,sizeof(body),
        "{\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"version\":\"%s\","
        "\"partition\":\"%s\",\"nonce\":\"%s\",\"ota_state\":%d,\"online\":%s,\"idle\":%s}",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],esp_app_get_description()->version,
        esp_ota_get_running_partition()->label,nonce,(int)ota_state,satellite.online?"true":"false",
        satellite.conversation==CONVERSATION_IDLE && !satellite.speaking?"true":"false");
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,body);
}

static esp_err_t reject(httpd_req_t *req, httpd_err_code_t code, const char *message)
{
    /* The body may still be unread. Close instead of parsing firmware as another request. */
    httpd_resp_send_err(req,code,message);
    return ESP_FAIL;
}

static esp_err_t upload(httpd_req_t *req)
{
    char hash[65], signature[65], expected[65], message[160];
    if (req->content_len<=0 || esp_timer_get_time()>nonce_until ||
        httpd_req_get_hdr_value_str(req,"X-Image-SHA256",hash,sizeof(hash))!=ESP_OK || strlen(hash)!=64 ||
        httpd_req_get_hdr_value_str(req,"X-OTA-Signature",signature,sizeof(signature))!=ESP_OK || strlen(signature)!=64)
        return reject(req,HTTPD_403_FORBIDDEN,"Invalid authorization");
    for (int i=0;i<64;i++) if (!((hash[i]>='0'&&hash[i]<='9')||(hash[i]>='a'&&hash[i]<='f')))
        return reject(req,HTTPD_403_FORBIDDEN,"Invalid digest");
    snprintf(message,sizeof(message),"%s:%s:%u",nonce,hash,(unsigned)req->content_len);
    unsigned char digest[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),OTA_KEY,sizeof(OTA_KEY),
                       (const unsigned char*)message,strlen(message),digest)!=0)
        return reject(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Authentication unavailable");
    hex(digest,32,expected);
    unsigned char difference=0;
    for (int i=0;i<64;i++) difference|=expected[i]^signature[i];
    if (difference) return reject(req,HTTPD_403_FORBIDDEN,"Invalid authorization");
    nonce_until=0; /* One use, including failed uploads. */
    if (!satellite.online || satellite.updating || satellite.speaking || satellite.conversation!=CONVERSATION_IDLE) {
        httpd_resp_set_status(req,"409 Conflict"); httpd_resp_sendstr(req,"Satellite busy or offline"); return ESP_FAIL;
    }
    const esp_partition_t *partition=esp_ota_get_next_update_partition(NULL);
    if (!partition || req->content_len>partition->size)
        return reject(req,HTTPD_400_BAD_REQUEST,"Image exceeds slot");
    unsigned char *buffer=malloc(4096);
    if (!buffer) return reject(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    esp_ota_handle_t handle=0;
    satellite.updating=true;
    esp_err_t err=esp_ota_begin(partition,req->content_len,&handle);
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
    if (err==ESP_OK && mbedtls_sha256_starts(&sha,0)!=0) err=ESP_FAIL;
    size_t remaining=req->content_len;
    const int64_t deadline=esp_timer_get_time()+180000000;
    while (err==ESP_OK && remaining && esp_timer_get_time()<deadline) {
        int n=httpd_req_recv(req,(char*)buffer,remaining>4096?4096:remaining);
        if (n<=0) { err=ESP_FAIL; break; }
        if (mbedtls_sha256_update(&sha,buffer,n)!=0) { err=ESP_FAIL; break; }
        err=esp_ota_write(handle,buffer,n); remaining-=n;
    }
    if (remaining) err=ESP_FAIL;
    if (err==ESP_OK && mbedtls_sha256_finish(&sha,digest)!=0) err=ESP_FAIL;
    hex(digest,32,expected);
    if (err==ESP_OK && strcmp(expected,hash)) err=ESP_FAIL;
    mbedtls_sha256_free(&sha); free(buffer);
    if (err!=ESP_OK) {
        if (handle) esp_ota_abort(handle);
    } else {
        err=esp_ota_end(handle); /* Validate ESP image and target chip. */
        if (err==ESP_OK) err=esp_ota_set_boot_partition(partition);
    }
    if (err!=ESP_OK) {
        satellite.updating=false;
        ESP_LOGW(TAG,"Update rejected: %s",esp_err_to_name(err));
        return reject(req,HTTPD_400_BAD_REQUEST,"Update rejected; current firmware retained");
    }
    ESP_LOGI(TAG,"Image verified, rebooting into %s",partition->label);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_sendstr(req,"{\"accepted\":true,\"rebooting\":true}");
    /* The audio/TLS workload may leave no contiguous stack for another task. */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static void ota_task(void *arg)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running=esp_ota_get_running_partition();
    bool pending=esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.server_port=8081; config.ctrl_port=32769; config.stack_size=6144;
    config.max_open_sockets=2; config.recv_wait_timeout=10; config.lru_purge_enable=true;
    httpd_handle_t server=NULL;
    esp_err_t server_error=httpd_start(&server,&config);
    if (server_error==ESP_OK) {
        httpd_uri_t get={.uri="/ota/status",.method=HTTP_GET,.handler=status};
        httpd_uri_t post={.uri="/ota/firmware",.method=HTTP_POST,.handler=upload};
        httpd_register_uri_handler(server,&get); httpd_register_uri_handler(server,&post);
        ESP_LOGI(TAG,"Authenticated OTA available on port 8081");
    } else ESP_LOGE(TAG,"OTA HTTP startup failed: %s",esp_err_to_name(server_error));
    if (pending) {
        /* Prove Wi-Fi and the authenticated Cornelius link before accepting a new image. */
        int stable=0;
        for (int i=0;i<90;i++) {
            stable=server && satellite.wifi && satellite.online?stable+1:0;
            if (stable>=10) {
                ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
                ESP_LOGI(TAG,"OTA self-test passed: Cornelius connected");
                vTaskDelete(NULL);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        ESP_LOGE(TAG,"OTA self-test failed, restoring previous firmware");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    vTaskDelete(NULL);
}

void satellite_ota_start(void)
{
    if (xTaskCreate(ota_task,"ota",4096,NULL,3,NULL)!=pdPASS)
        ESP_LOGE(TAG,"Cannot allocate OTA task");
}
