#pragma once
#include <stdbool.h>

/** Bouton BOOT (GPIO0) : un appui arrête la sonnerie, sinon coupe ou rouvre le micro. */
void button_start(void);

void button_request_mute(void);
