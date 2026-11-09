#include "button.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "nvs.h"
#include "state.h"

static const char *TAG = "button";
static bool requested_mute;
static portMUX_TYPE mute_lock=portMUX_INITIALIZER_UNLOCKED;
/* Une commande distante peut uniquement couper le micro. */
void button_request_mute(void)
{
    portENTER_CRITICAL(&mute_lock);
    requested_mute = true;
    portEXIT_CRITICAL(&mute_lock);
}

#define BUTTON_GPIO GPIO_NUM_0

/* Le micro coupé le reste après un redémarrage : la vie privée ne dépend pas du courant. */
static void save_muted(bool muted)
{
    nvs_handle_t nvs;
    if (nvs_open("satellite", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u8(nvs, "muted", muted);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool load_muted(void)
{
    nvs_handle_t nvs;
    uint8_t muted = 0;
    if (nvs_open("satellite", NVS_READONLY, &nvs) != ESP_OK) return false;
    nvs_get_u8(nvs, "muted", &muted);
    nvs_close(nvs);
    return muted;
}

static void button_task(void *arg)
{
    int pressed_for = 0;
    for (;;) {
        portENTER_CRITICAL(&mute_lock);
        const bool requested = requested_mute;
        requested_mute = false;
        portEXIT_CRITICAL(&mute_lock);
        if (requested && !satellite.muted) {
            satellite.muted = true;
            save_muted(satellite.muted);
            link_send_mic(satellite.muted);
        }
        const bool down = gpio_get_level(BUTTON_GPIO) == 0;
        if (down) {
            /* Tenu 5 s : réinitialisation, l'anneau clignote en rouge puis le satellite repart
             * du portail de configuration. */
            if (++pressed_for == 250) {
                ESP_LOGW(TAG, "Bouton tenu 5 s : réinitialisation");
                satellite.reset_at_us = esp_timer_get_time() + 1500000;
                config_forget();
                vTaskDelay(pdMS_TO_TICKS(1500));
                esp_restart();
            }
        } else if (pressed_for >= 50) { /* relâché entre 1 et 5 s : sans effet */
            pressed_for = 0;
        } else if (pressed_for >= 2 && satellite.ringing) { /* appui bref pendant une alarme */
            satellite.ringing = false;
            link_send_ring_stop();
            ESP_LOGI(TAG, "Sonnerie arrêtée");
            pressed_for = 0;
        } else if (pressed_for >= 2 && satellite.session_end &&
                   satellite.conversation != CONVERSATION_IDLE) {
            /* appui bref pendant la conversation : le micro se tait tout de suite, puis Cornelius
             * ferme la conversation sans attendre le silence */
            satellite.end_pressed_at_us = esp_timer_get_time();
            link_send_session_end();
            ESP_LOGI(TAG, "Fin de conversation demandée");
            pressed_for = 0;
        } else if (pressed_for >= 2) { /* appui bref (anti-rebond 40 ms) : micro coupé ou rouvert */
            satellite.muted = !satellite.muted;
            save_muted(satellite.muted);
            link_send_mic(satellite.muted);
            ESP_LOGI(TAG, "Micro %s", satellite.muted ? "coupé" : "rouvert");
            pressed_for = 0;
        } else {
            pressed_for = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void button_start(void)
{
    satellite.muted = load_muted();
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&config);
    xTaskCreate(button_task, "button", 3072, NULL, 4, NULL);
}
