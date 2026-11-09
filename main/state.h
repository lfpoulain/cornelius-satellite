#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Conversation de ce satellite, telle que Cornelius l'annonce (message « session »). */
typedef enum {
    CONVERSATION_IDLE,
    CONVERSATION_CONNECTING,
    CONVERSATION_READY,
} conversation_t;

/**
 * État partagé entre les tâches (Wi-Fi, liaison, audio, LED, bouton). Chaque champ a un seul
 * écrivain ; les lectures d'un octet ou d'un entier aligné sont atomiques sur l'ESP32-S3.
 */
typedef struct {
    volatile bool wifi;          /* Wi-Fi connecté (wifi.c) */
    volatile bool online;        /* « welcome » reçu (link.c) */
    volatile bool listen;        /* le serveur demande le micro (link.c) */
    volatile bool muted;         /* bouton de vie privée (button.c) */
    volatile conversation_t conversation; /* (link.c) */
    /* Le serveur sait fermer la conversation au bouton (« welcome », features : session_end) */
    volatile bool session_end;
    volatile int64_t conversation_at_us; /* début de la conversation en cours (link.c) */
    /* Appui « fin de conversation » : le micro se tait aussitôt, sans attendre le serveur
     * (button.c). Il compte tant qu'il est plus récent que conversation_at_us. */
    volatile int64_t end_pressed_at_us;
    volatile int64_t wake_at_us; /* dernier « wake », pour le signal lumineux (link.c) */
    volatile int64_t busy_at_us; /* dernier « busy » (link.c) */
    volatile bool speaking;      /* le haut-parleur joue un son audible (audio.c) */
    /* Alarme qui sonne ici (message « ring ») (link.c, button.c) */
    volatile bool ringing;
    volatile bool ring_soft;     /* rappel seul : carillon doux et espacé */
    volatile uint8_t ring_sound; /* RING_BIPS ou RING_ALERTE */
    /* Réglages de l'administration (message « settings », gardés en NVS) (link.c, config.c) */
    volatile uint8_t led_r, led_g, led_b; /* couleur principale de l'anneau */
    volatile uint8_t led_brightness;      /* 0 à 10 ; 0 : alertes seulement */
    volatile bool night_light;            /* lueur faible en veille */
    volatile bool attentive;              /* écoute attentive (avec annulation d'écho) */
    volatile bool setup;                  /* portail de configuration ouvert (portal.c) */
    volatile bool pairing;                /* en attente d'association (link.c) */
    volatile int64_t identify_until_us;   /* « Faire clignoter » demandé (link.c) */
    volatile int64_t reset_at_us;         /* bouton tenu : réinitialisation (button.c) */
} satellite_state_t;

/** Sonneries que le satellite sait jouer ; un autre son demandé joue les bips. */
enum { RING_BIPS, RING_ALERTE };

extern satellite_state_t satellite;
