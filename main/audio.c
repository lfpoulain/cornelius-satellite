#include "audio.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "board.h"
#include "echo.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "link.h"
#include "state.h"

static const char *TAG = "audio";

/* 10 s de voix en PSRAM : le serveur l'envoie au rythme de la lecture, la marge est large. */
#define PLAY_BUFFER_BYTES (BOARD_SAMPLE_RATE * 2 * 10)
/* Avant de commencer à jouer : 200 ms, pour absorber les à-coups du Wi-Fi. */
#define PLAY_PREBUFFER_BYTES (BOARD_SAMPLE_RATE * 2 / 5)
/* Morceau écrit au codec : 60 ms. */
#define PLAY_CHUNK_BYTES 1920
/* Même seuil que Cornelius pour un son audible (RMS de 0,7 % de la pleine échelle). */
#define AUDIBLE_RMS (0.007f * 32768.0f)

static RingbufHandle_t play_buffer;
static volatile bool flush_requested;

/* Sonnerie : synthétisée ici, Cornelius n'envoie que « ring ». Motifs de 1,6 s (8 s pour un
 * rappel), volume de 35 % à 100 % en 30 s, comme sur le miroir. */
#define RING_CHUNK_SAMPLES 960
#define RING_PERIOD (BOARD_SAMPLE_RATE * 8 / 5)
#define RING_SOFT_PERIOD (BOARD_SAMPLE_RATE * 8)
#define RING_RAMP_US 30000000
#define TWO_PI 6.28318530718f

/* Un échantillon de la sonnerie, de -1 à 1, à la position `n` du motif. */
static float ring_value(uint32_t n)
{
    const float rate = BOARD_SAMPLE_RATE;
    if (satellite.ring_soft) {
        /* Carillon : deux notes montantes qui s'éteignent. */
        const float t = (n % RING_SOFT_PERIOD) / rate;
        float value = 0.6f * sinf(TWO_PI * 659.25f * t) * expf(-t / 0.25f);
        if (t >= 0.13f) {
            const float u = t - 0.13f;
            value += 0.5f * sinf(TWO_PI * 987.77f * u) * expf(-u / 0.3f);
        }
        return value;
    }
    const uint32_t at = n % RING_PERIOD;
    if (satellite.ring_sound == RING_ALERTE) {
        /* Quatre tons de 0,2 s, alternés 660 / 880 Hz. */
        const uint32_t step = BOARD_SAMPLE_RATE * 22 / 100, length = BOARD_SAMPLE_RATE / 5;
        const uint32_t index = at / step, offset = at % step;
        if (index >= 4 || offset >= length) return 0;
        const float t = offset / rate;
        const float edge = fminf(1.0f, fminf(t, (length - offset) / rate) / 0.01f);
        return 0.7f * edge * sinf(TWO_PI * (index % 2 ? 880.0f : 660.0f) * t);
    }
    /* Quatre bips carrés de 90 ms, à 1 046 Hz. */
    const uint32_t step = BOARD_SAMPLE_RATE * 16 / 100, length = BOARD_SAMPLE_RATE * 9 / 100;
    const uint32_t index = at / step, offset = at % step;
    if (index >= 4 || offset >= length) return 0;
    return sinf(TWO_PI * 1046.5f * (offset / rate)) >= 0 ? 0.45f : -0.45f;
}

/* Un morceau de sonnerie ; `started_us` : début de la sonnerie, pour la montée du volume. */
static void ring_chunk(int16_t *out, uint32_t *position, int64_t started_us)
{
    const float elapsed = (esp_timer_get_time() - started_us) / (float)RING_RAMP_US;
    const float gain = satellite.ring_soft ? 0.5f : 0.35f + 0.65f * fminf(1.0f, elapsed);
    for (size_t i = 0; i < RING_CHUNK_SAMPLES; i++)
        out[i] = (int16_t)(ring_value((*position)++) * gain * 0.5f * 32767.0f);
}

static void capture_task(void *arg)
{
    static int16_t frame[LINK_FRAME_SAMPLES];
    static int16_t raw[LINK_FRAME_SAMPLES * BOARD_MIC_CHANNELS];
    int64_t last = esp_timer_get_time();
    for (;;) {
        /* Le micro est toujours lu, pour que le tampon I2S ne déborde jamais ; la trame ne
         * part que si Cornelius la demande et que le micro n'est pas coupé. */
        if (board_mic_read(raw, LINK_FRAME_SAMPLES * BOARD_MIC_CHANNELS) != ESP_OK) {
            ESP_LOGW(TAG, "Lecture du micro échouée");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const int64_t now = esp_timer_get_time();
        if (now - last > 200000)
            ESP_LOGW(TAG, "Trou dans la lecture du micro : %lld ms", (now - last) / 1000);
        last = now;
        if (BOARD_MIC_CHANNELS == 1) {
            memcpy(frame, raw, sizeof(frame));
        } else if (echo_active()) {
            echo_process(raw, frame, LINK_FRAME_SAMPLES);
        } else {
            for (size_t i = 0; i < LINK_FRAME_SAMPLES; i++) frame[i] = raw[2 * i];
        }
        link_send_frame(frame);
    }
}

static size_t buffered(void)
{
    return PLAY_BUFFER_BYTES - xRingbufferGetCurFreeSize(play_buffer);
}

static bool audible(const int16_t *samples, size_t count)
{
    float energy = 0;
    for (size_t i = 0; i < count; i += 8) energy += (float)samples[i] * samples[i];
    const size_t used = (count + 7) / 8;
    return used && energy / used >= AUDIBLE_RMS * AUDIBLE_RMS;
}

static void playback_task(void *arg)
{
    bool playing = false;
    int64_t quiet_since = 0;
    static int16_t ring[RING_CHUNK_SAMPLES];
    uint32_t ring_position = 0;
    int64_t ring_started = 0;
    for (;;) {
        /* Une alarme sonne, hors conversation : la sonnerie passe avant tout le reste ;
         * board_speaker_write cadence la boucle au rythme du haut-parleur. */
        if (satellite.ringing && satellite.conversation == CONVERSATION_IDLE) {
            if (!ring_started) {
                ring_started = esp_timer_get_time();
                ring_position = 0;
            }
            ring_chunk(ring, &ring_position, ring_started);
            board_speaker_write(ring, RING_CHUNK_SAMPLES);
            continue;
        }
        ring_started = 0;
        if (flush_requested) {
            size_t size;
            void *item;
            while ((item = xRingbufferReceiveUpTo(play_buffer, &size, 0, PLAY_BUFFER_BYTES)))
                vRingbufferReturnItem(play_buffer, item);
            flush_requested = false;
            playing = false;
            satellite.speaking = false;
        }
        if (!playing) {
            if (buffered() < PLAY_PREBUFFER_BYTES) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            playing = true;
        }
        size_t size = 0;
        int16_t *chunk = xRingbufferReceiveUpTo(play_buffer, &size, pdMS_TO_TICKS(100),
                                                PLAY_CHUNK_BYTES);
        if (!chunk) {
            /* Plus rien : on attend de nouveau un peu d'avance avant de reprendre. */
            playing = false;
            satellite.speaking = false;
            continue;
        }
        const size_t samples = size / sizeof(int16_t);
        const int64_t now = esp_timer_get_time();
        if (audible(chunk, samples)) {
            satellite.speaking = true;
            quiet_since = now;
        } else if (now - quiet_since > 300000) {
            satellite.speaking = false;
        }
        board_speaker_write(chunk, samples);
        vRingbufferReturnItem(play_buffer, chunk);
    }
}

void audio_play_push(const uint8_t *data, size_t length)
{
    if (!play_buffer || length == 0 || satellite.conversation != CONVERSATION_READY) return;
    /* Tampon plein : la voix a pris trop de retard, ce morceau est perdu plutôt que tout
     * bloquer (la réception du WebSocket ne doit jamais attendre le haut-parleur). */
    if (xRingbufferSend(play_buffer, data, length & ~(size_t)1, 0) != pdTRUE)
        ESP_LOGW(TAG, "Tampon de lecture plein : %u octets perdus", (unsigned)length);
}

void audio_play_flush(void)
{
    flush_requested = true;
}

void audio_start(void)
{
    echo_start();
    play_buffer = xRingbufferCreateWithCaps(PLAY_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF,
                                            MALLOC_CAP_SPIRAM);
    if (!play_buffer) ESP_LOGE(TAG, "Pas de mémoire pour le tampon de lecture");
    /* 16 Ko : l'annulation d'écho d'ESP-SR travaille sur la pile de cette tâche. */
    xTaskCreatePinnedToCore(capture_task, "capture", 16384, NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(playback_task, "playback", 4096, NULL, 7, NULL, 1);
}
