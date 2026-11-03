#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#include "sdkconfig.h"

/** Fréquence du son, dans les deux sens : celle du protocole des satellites. */
#define BOARD_SAMPLE_RATE 16000

/* Voies lues : micro 1, et la référence du haut-parleur quand l'écho est annulé. */
#ifdef CONFIG_SATELLITE_AEC
#define BOARD_MIC_CHANNELS 2
#else
#define BOARD_MIC_CHANNELS 1
#endif

/** Bus I²C, extenseur (ampli allumé), codecs ES8311 (haut-parleur) et ES7210 (micros). */
esp_err_t board_init(void);

/** Lit `count` échantillons 16 bits : micro 1, alterné avec la référence si BOARD_MIC_CHANNELS
 * vaut 2. */
esp_err_t board_mic_read(int16_t *samples, size_t count);

/** Joue `count` échantillons mono 16 bits ; bloque au rythme du haut-parleur. */
esp_err_t board_speaker_write(const int16_t *samples, size_t count);

/** Volume du haut-parleur, de 0 à 100. */
void board_set_volume(int volume);

int board_get_volume(void);
