# Historique des changements

Cette page retrace les versions du firmware, de la plus récente à la plus ancienne. La version est celle de `PROJECT_VER` dans `CMakeLists.txt`, annoncée à Cornelius dans `hello` et affichée dans **Administration → Satellites audio**.

## 0.4.0 : sonnerie des alarmes (2026-11-02)

**Ajouté**

- Sonnerie des réveils, minuteurs et rappels réglés sur ce satellite : `hello` annonce `features: ["ring"]`, Cornelius envoie `ring` (`start`, `stop`). La sonnerie est synthétisée sur la carte, en boucle, volume de 35 % à 100 % en 30 s : « Bips » ou « Alerte » (tout autre son demandé joue les bips), carillon doux pour un rappel. L’anneau bat en ambre. Elle se tait pendant une conversation.
- Un appui bref sur le bouton pendant la sonnerie l’arrête (`ring`, `action: stop`) au lieu de couper le micro.

## 0.3.0 : annulation d’écho et écoute attentive (2026-10-19)

**Ajouté**

- Annulation d’écho ESP-SR (mode communication) sur le micro 1, avec la voie de référence du haut-parleur de l’ES7210. Le satellite l’annonce à Cornelius (`aec`), qui garde le micro ouvert pendant qu’il parle si l’écoute attentive est activée. Mesurée : la voix de Cornelius baisse de 35 à 55 dB.
- Niveaux de l’annulation d’écho envoyés au journal de Cornelius pendant qu’il parle.

**Modifié**

- Pile de 16 Ko pour la capture, file du micro en PSRAM, envoi qui tolère 10 s d’à-coup TCP sans couper la connexion.

## 0.2.0 : installation sans saisie (2026-10-19)

**Ajouté**

- Portail de configuration (point d’accès, page, DNS captif) : seul le Wi-Fi de la maison est demandé. Il rouvre si le Wi-Fi ne répond plus.
- Cornelius trouvé par mDNS sur le même sous-réseau, adresse fixe facultative, nouvelle recherche après 30 s sans réponse.
- Association sans saisie : la clé de Cornelius est retenue au premier contact, le jeton reçu après **Associer** dans l’administration.
- Réglages de l’administration appliqués et gardés dans la carte : volume, couleur et luminosité de l’anneau, veilleuse.
- Remise à zéro par le bouton BOOT tenu 5 secondes.

**Corrigé**

- Couleurs de l’anneau, dans l’ordre RGB de la carte.

## 0.1.0 : premier firmware (2026-10-19)

**Ajouté**

- Firmware ESP-IDF pour la Waveshare ESP32-S3-AUDIO-Board, suivant le protocole des satellites v1 de Cornelius : `hello`, `welcome`, `listen`, `wake`, `busy`, `session`, `volume`, `mic`.
- Micro envoyé seulement quand Cornelius le demande (PCM 16 kHz, trames de 80 ms), voix jouée depuis un tampon en PSRAM.
- ES8311 et ES7210 en I²S duplex (TDM sur 4 voies), ampli commandé par le TCA9555.
- Anneau de LED d’état, bouton BOOT pour couper le micro.

**Corrigé, après l’essai sur la carte**

- Cornelius reconnu à l’empreinte SHA-256 de sa clé publique, plutôt qu’à un certificat embarqué qui changeait avec les adresses de l’ordinateur.
- Micro envoyé par une file et une tâche à part, pour qu’un à-coup du Wi-Fi ne ferme plus la connexion ; verrou d’envoi WebSocket séparé de la réception, tampons TCP de 16 Ko.
