#include "leds.h"

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "state.h"

static const char *TAG = "leds";

#define LED_GPIO 38
#define LED_COUNT CONFIG_SATELLITE_LED_COUNT
/* Alertes (Wi-Fi, hors ligne, occupé, micro coupé) : couleur et intensité fixes, toujours vues. */
#define LED_MAX 60
/* Couleur principale à la luminosité 10 : un satellite de chambre n'éclaire pas la pièce. */
#define LED_FULL 90

static led_strip_handle_t strip;

typedef struct {
    uint8_t r, g, b;
} color_t;

static void fill(color_t color, float level)
{
    for (int i = 0; i < LED_COUNT; i++)
        led_strip_set_pixel(strip, i, color.r * level, color.g * level, color.b * level);
}

/* Un point qui tourne, avec une traîne : attente (Wi-Fi, ouverture). */
static void spin(color_t color, float seconds)
{
    const float head = fmodf(seconds * 8.0f, LED_COUNT);
    for (int i = 0; i < LED_COUNT; i++) {
        float distance = fmodf(head - i + LED_COUNT, LED_COUNT);
        float level = distance < 3 ? 1.0f - distance / 3.0f : 0;
        led_strip_set_pixel(strip, i, color.r * level, color.g * level, color.b * level);
    }
}

static float pulse(float seconds, float period)
{
    return 0.25f + 0.75f * (0.5f + 0.5f * sinf(seconds * 2.0f * (float)M_PI / period));
}

static void render(float seconds, int64_t now_us)
{
    static const color_t blue = {0, 20, LED_MAX};
    static const color_t orange = {LED_MAX, 25, 0};
    static const color_t red = {LED_MAX, 0, 0};
    static const color_t amber = {LED_MAX, 40, 0};
    /* Couleur principale choisie dans l'administration, à la luminosité choisie. */
    const float scale = satellite.led_brightness * LED_FULL / (10.0f * 255.0f);
    const color_t accent = {satellite.led_r * scale, satellite.led_g * scale,
                            satellite.led_b * scale};

    static const color_t white = {LED_MAX, LED_MAX, LED_MAX};
    if (now_us < satellite.reset_at_us) return fill(red, ((now_us / 100000) % 2) ? 0.9f : 0);
    if (now_us < satellite.identify_until_us)
        return fill(white, ((now_us / 250000) % 2) ? 0.9f : 0);
    /* Portail de configuration ouvert : bleu qui respire. */
    if (satellite.setup && !satellite.wifi) return fill(blue, pulse(seconds, 3.0f));
    if (satellite.updating) return fill(orange, pulse(seconds, 0.5f));
    if (satellite.muted) return fill(red, 0.35f);
    if (!satellite.wifi) return spin(blue, seconds);
    /* En attente d'association : blanc qui respire. */
    if (satellite.pairing) return fill(white, pulse(seconds, 3.0f) * 0.5f);
    if (!satellite.online) return fill(orange, pulse(seconds, 2.0f) * 0.6f);
    if (now_us - satellite.busy_at_us < 1200000)
        return fill(amber, ((now_us - satellite.busy_at_us) / 200000) % 2 ? 0 : 0.8f);
    if (satellite.conversation == CONVERSATION_CONNECTING) return spin(accent, seconds);
    if (satellite.conversation == CONVERSATION_READY)
        return fill(accent, satellite.speaking ? pulse(seconds, 0.6f) : 0.3f);
    if (now_us - satellite.wake_at_us < 400000) return fill(accent, 1.0f);
    /* Veille : éteint, ou veilleuse, une lueur faible de la couleur principale. */
    fill(accent, satellite.night_light ? 0.08f : 0);
}

static void leds_task(void *arg)
{
    const int64_t start = esp_timer_get_time();
    for (;;) {
        const int64_t now = esp_timer_get_time();
        render((now - start) / 1e6f, now);
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void leds_start(void)
{
    const led_strip_config_t config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        /* Anneau de la Waveshare : composantes dans l'ordre rouge, vert, bleu (pas GRB). */
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&config, &rmt, &strip) != ESP_OK) {
        ESP_LOGE(TAG, "Anneau de LED indisponible");
        return;
    }
    led_strip_clear(strip);
    xTaskCreate(leds_task, "leds", 3072, NULL, 2, NULL);
}
