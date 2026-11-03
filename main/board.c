/*
 * Waveshare ESP32-S3-AUDIO-Board : câblage relevé sur le wiki Waveshare et la carte
 * « esp32-s3-audio-board » de xiaozhi-esp32 (licence MIT), dont la configuration I²S est
 * reprise : sortie ES8311 en mode standard, entrée ES7210 en TDM sur 4 voies.
 */
#include "board.h"
#include "gestures.h"
#include "leds.h"
#include "state.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board";

#define I2C_SDA GPIO_NUM_11
#define I2C_SCL GPIO_NUM_10
#define I2S_MCLK GPIO_NUM_12
#define I2S_BCLK GPIO_NUM_13
#define I2S_WS GPIO_NUM_14
#define I2S_DIN GPIO_NUM_15  /* ES7210 → ESP32 */
#define I2S_DOUT GPIO_NUM_16 /* ESP32 → ES8311 */
/* L'ampli du haut-parleur s'allume par la sortie EXIO8 de l'extenseur TCA9555. */
#define EXPANDER_PA_PIN IO_EXPANDER_PIN_NUM_8

static i2c_master_bus_handle_t i2c_bus;
static esp_io_expander_handle_t expander;
static i2s_chan_handle_t tx_handle;
static i2s_chan_handle_t rx_handle;
static esp_codec_dev_handle_t speaker;
static esp_codec_dev_handle_t microphone;

static esp_err_t init_i2c(void)
{
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &i2c_bus);
}

static esp_err_t init_expander(void)
{
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca95xx_16bit(
                            i2c_bus, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &expander),
                        TAG, "extenseur TCA9555 absent");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(expander, EXPANDER_PA_PIN, IO_EXPANDER_OUTPUT),
                        TAG, "sortie de l'ampli");
    return esp_io_expander_set_level(expander, EXPANDER_PA_PIN, 1);
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = 240;
    chan.auto_clear_after_cb = true; /* silence quand rien n'est à jouer */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &tx_handle, &rx_handle), TAG, "canaux I2S");

    const i2s_std_config_t std = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    const i2s_tdm_config_t tdm = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM,
        },
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std), TAG, "I2S sortie");
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(rx_handle, &tdm), TAG, "I2S entrée");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "I2S sortie");
    return i2s_channel_enable(rx_handle);
}

static esp_err_t init_codecs(void)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *out_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es8311_codec_cfg_t es8311 = {
        .ctrl_if = out_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1, /* ampli piloté par l'extenseur */
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    esp_codec_dev_cfg_t out_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_codec_new(&es8311),
        .data_if = data_if,
    };
    speaker = esp_codec_dev_new(&out_cfg);

    i2c_cfg.addr = ES7210_CODEC_DEFAULT_ADDR;
    const audio_codec_ctrl_if_t *in_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    es7210_codec_cfg_t es7210 = {
        .ctrl_if = in_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    esp_codec_dev_cfg_t in_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7210_codec_new(&es7210),
        .data_if = data_if,
    };
    microphone = esp_codec_dev_new(&in_cfg);
    if (!speaker || !microphone) return ESP_FAIL;

    /* Voies TDM : MIC1, MIC3 (référence du haut-parleur, pour l'annulation d'écho), MIC2, MIC4.
     * Seul le micro 1 part vers Cornelius tant que l'annulation d'écho n'est pas faite ici. */
    esp_codec_dev_sample_info_t in_fs = {
        .bits_per_sample = 16,
        .channel = 4,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)
#ifdef CONFIG_SATELLITE_AEC
                        | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)
#endif
        ,
        .sample_rate = BOARD_SAMPLE_RATE,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(microphone, &in_fs) == ESP_CODEC_DEV_OK, ESP_FAIL,
                        TAG, "ouverture des micros");
    esp_codec_dev_set_in_channel_gain(microphone, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
                                      CONFIG_SATELLITE_MIC_GAIN_DB);
#ifdef CONFIG_SATELLITE_AEC
    /* La référence est le signal du haut-parleur lui-même : sans gain, pour ne pas saturer. */
    esp_codec_dev_set_in_channel_gain(microphone, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1), 0);
#endif

    esp_codec_dev_sample_info_t out_fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = BOARD_SAMPLE_RATE,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(speaker, &out_fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG,
                        "ouverture du haut-parleur");
    esp_codec_dev_set_out_vol(speaker, 70);
    return ESP_OK;
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "bus I2C");
    gestures_init(i2c_bus);
    ESP_RETURN_ON_ERROR(init_expander(), TAG, "extenseur");
    ESP_RETURN_ON_ERROR(init_i2s(), TAG, "I2S");
    ESP_RETURN_ON_ERROR(init_codecs(), TAG, "codecs");
    ESP_LOGI(TAG, "Carte prête : micro et haut-parleur à %d Hz", BOARD_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t board_mic_read(int16_t *samples, size_t count)
{
    return esp_codec_dev_read(microphone, samples, count * sizeof(int16_t)) == ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t board_speaker_write(const int16_t *samples, size_t count)
{
    return esp_codec_dev_write(speaker, (void *)samples, count * sizeof(int16_t)) ==
                   ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}

static int current_volume=-1;
int board_get_volume(void) { return current_volume; }

void board_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if (esp_codec_dev_set_out_vol(speaker, volume)!=ESP_CODEC_DEV_OK) return;
    const bool changed=current_volume>=0 && current_volume!=volume;
    current_volume=volume;
    if (changed && satellite.online) leds_volume_feedback(volume);
}
