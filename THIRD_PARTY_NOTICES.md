# Notices des composants tiers

Le firmware est compilé avec ESP-IDF et les composants Espressif ci-dessous, téléchargés à la compilation (`managed_components/`, hors du dépôt) aux versions fixées par `dependencies.lock`. Ils gardent leur propre licence, dont le texte complet accompagne chaque composant.

| Composant | Version | Licence | Rôle |
| --- | --- | --- | --- |
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.6 (testée) | Apache 2.0 | système, Wi-Fi, TLS, I²S, NVS |
| [espressif/esp-sr](https://components.espressif.com/components/espressif/esp-sr) | 2.6.0 | ESPRESSIF MIT : usage réservé aux puces Espressif | annulation d’écho |
| [espressif/esp-dl](https://components.espressif.com/components/espressif/esp-dl) | 3.3.14 | MIT | dépendance d’esp-sr |
| [espressif/dl_fft](https://components.espressif.com/components/espressif/dl_fft) | 0.8.0 | MIT | dépendance d’esp-sr |
| [espressif/esp_new_jpeg](https://components.espressif.com/components/espressif/esp_new_jpeg) | 1.0.2 | ESPRESSIF MIT : usage réservé aux puces Espressif | dépendance d’esp-dl |
| [espressif/esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev) | 1.6.2 | Apache 2.0 | codecs ES8311 et ES7210 |
| [espressif/esp_websocket_client](https://components.espressif.com/components/espressif/esp_websocket_client) | 1.8.0 | Apache 2.0 | liaison avec Cornelius |
| [espressif/mdns](https://components.espressif.com/components/espressif/mdns) | 1.14.0 | Apache 2.0 | recherche de Cornelius sur le réseau |
| [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) | 3.1.0 | Apache 2.0 | anneau de LED |
| [espressif/esp_io_expander](https://components.espressif.com/components/espressif/esp_io_expander) et `esp_io_expander_tca95xx_16bit` | 1.2.1 et 2.0.2 | Apache 2.0 | extenseur TCA9555 (ampli) |
| [espressif/cjson](https://components.espressif.com/components/espressif/cjson) | 1.7.19 | MIT (Dave Gamble et contributeurs de cJSON) | messages JSON du protocole |

## xiaozhi-esp32

La configuration I²S et codecs de `main/board.c` reprend celle de la carte `waveshare/esp32-s3-audio-board` du projet [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32).

```text
MIT License

Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
Copyright (c) 2025 Project Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Seeed Grove Gesture

Table de registres PAJ7620 reprise dans `main/paj-registers.h`.

```text
    paj7620.h
    A library for Grove-Gesture 1.0

    Copyright (c) 2023 seeed technology inc.
    Website    : www.seeed.cc
    Author     : Wuruibin & Xiangnan
    Modified Time: June 2023

    The MIT License (MIT)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
```
