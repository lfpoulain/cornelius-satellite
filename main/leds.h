#pragma once

/** Anneau de LED (GPIO38) : animé d'après l'état du satellite, 30 images par seconde. */
void leds_start(void);

void leds_volume_feedback(int volume);
