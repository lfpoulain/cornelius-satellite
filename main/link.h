#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Version du protocole des satellites (docs/developpement/satellites-protocole.md). */
#define SATELLITE_PROTOCOL 1
/** Trame du micro : 1 280 échantillons, 80 ms, 2 560 octets. */
#define LINK_FRAME_SAMPLES 1280

/** Ouvre le WebSocket vers Cornelius (reconnexion automatique) et suit le protocole. */
void link_start(void);

/** Envoie une trame du micro, si la conversation ou la veille la demandent. */
void link_send_frame(const int16_t *frame);

/** Bouton de vie privée : prévient le serveur que le micro est coupé ou rouvert. */
void link_send_mic(bool muted);

/** Bouton pendant la sonnerie : demande à Cornelius d'arrêter ce qui sonne ici. */
void link_send_ring_stop(void);

/** Mesure à inscrire dans le journal de Cornelius (200 caractères, une par seconde au plus). */
void link_send_log(const char *message);
