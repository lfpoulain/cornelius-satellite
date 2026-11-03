#pragma once
#include <stdbool.h>

/** Bouton BOOT (GPIO0) : un appui coupe ou rouvre le micro (vie privée). */
void button_start(void);

void button_request_mute(bool muted);
