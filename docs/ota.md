# Mises à jour Wi-Fi authentifiées (optionnelles)

Cette extension permet de mettre à jour un satellite ESP32-S3 depuis un PC Windows, sans câble après une première installation USB. Elle est désactivée par défaut. Aucune modification du serveur Cornelius n'est requise.

## Fonctionnement et limites

Le PC envoie l'application sur TCP 8081. Un défi aléatoire valable 60 secondes et utilisable une seule fois est authentifié par HMAC-SHA256 ; la signature couvre le défi, le SHA256 de l'image et sa taille. La clé n'est jamais envoyée. Le SHA256 du contenu reçu et la validité du binaire ESP sont vérifiés avant de modifier la partition de démarrage. Chaque carte doit posséder une clé distincte.

Le transport HTTP ne chiffre pas le firmware. Réserver ce service au réseau local de confiance, sans redirection WAN. Une personne sur le réseau peut consulter MAC/version/état et perturber la disponibilité ; l'authentification vise à empêcher l'installation d'une image non autorisée, pas à garantir la disponibilité contre une attaque réseau. La flash n'est pas chiffrée : l'accès physique peut révéler les identifiants et la clé. Ne pas diffuser de binaires compilés avec une clé réelle.

Les outils PC fournis utilisent Windows DPAPI pour protéger la copie locale de la clé ; ils nécessitent le même compte Windows pour les mises à jour suivantes. Aucun secret n'est saisi dans une commande ou affiché. Les outils ne nécessitent que Python et sa bibliothèque standard.

Une conversation en cours empêche une mise à jour. Pendant l'écriture, l'envoi micro est suspendu et l'anneau pulse en orange ; l'état de mute enregistré n'est pas modifié. Après redémarrage, le firmware attend dix secondes de connexion stable à Cornelius pour confirmer la nouvelle version. Un échec pendant la validation déclenche un retour arrière ; une coupure ou un crash avant validation est traité par le bootloader au démarrage suivant. Garder Cornelius disponible pendant ce contrôle.

## Préparation d'une carte

1. Sauvegarder la flash complète par USB et noter la MAC de la carte. Les sauvegardes peuvent contenir les identifiants Wi-Fi et le jeton : conserver ces fichiers en privé, avec des droits limités au propriétaire.
2. Générer une clé locale (PowerShell, depuis ce dépôt) :

   ```powershell
   python tools/provision-ota-key.py --firmware-dir . --key-file ota-private/device.dpapi
   ```

   Le fichier `main/ota_key.h` généré et la copie DPAPI sont ignorés par Git. Sur un dossier partagé, limiter les ACL du dossier privé et de cet en-tête au propriétaire et SYSTEM avant génération. Conserver la copie DPAPI pour cette carte.
3. Compiler avec `sdkconfig.defaults` **et** `sdkconfig.ota.defaults`. Pour une configuration existante, activer `SATELLITE_OTA`, `BOOTLOADER_APP_ROLLBACK_ENABLE`, la table personnalisée et `partitions-ota.csv` via menuconfig ; des valeurs déjà enregistrées peuvent prendre priorité sur les defaults.
4. Vérifier le sdkconfig généré du **bootloader**, pas seulement celui de l'application : `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` doit être défini. Reconfigurer/recompiler le bootloader s'il provient d'un ancien cache.
5. Vérifier la table décodée : NVS `0x9000`/24 Kio et PHY `0xf000`/4 Kio restent inchangées ; `ota_0` est à `0x10000`, `ota_1` à `0x310000` (3 Mio chacune), `otadata` à `0x610000` (8 Kio).
6. Écrire par USB le bootloader, la table, l'application dans `ota_0` et les métadonnées OTA initiales vierges. Contrôler les offsets dans les arguments de flash du build. **Ne pas effacer toute la flash**, sous peine de perdre le Wi-Fi et l'association. Une autre disposition NVS nécessite un plan de migration adapté.

Pour revenir à l'installation antérieure à cette migration, restaurer par USB une sauvegarde cohérente du bootloader, de la table, de l'application et des données. Restaurer uniquement l'ancienne application ne suffit pas.

## Préparer et envoyer une application

Définir une version identifiable dans `PROJECT_VER`. Recompiler avec la clé existante de cette carte. Préparer un dossier d'artefacts privé propre à cette carte :

```powershell
python tools/package-ota.py --build-dir build --output-dir ota-artifacts/device --key-file ota-private/device.dpapi --mac <MAC> --version <VERSION>
```

Le préparateur attend les noms de fichiers PlatformIO (`firmware.bin`, `bootloader.bin`, `partitions.bin`, `ota_data_initial.bin`) directement dans ce dossier de build. Pour un build ESP-IDF standard, copier d'abord `cornelius-satellite.bin` sous `firmware.bin`, `bootloader/bootloader.bin` sous `bootloader.bin`, `partition_table/partition-table.bin` sous `partitions.bin` et `ota_data_initial.bin` dans un dossier de préparation privé ; passer ce dossier comme `--build-dir`.

Le préparateur vérifie que la clé et la version attendues sont présentes dans le binaire. Le manifeste `firmware.bin.ota.json` lie l'empreinte, la version, la MAC et la clé ; il est exigé par le client pour éviter les confusions entre cartes. Ce manifeste ne remplace pas l'authentification HMAC côté appareil.

```powershell
python tools/ota-client.py --host <IP> --mac <MAC> --key-file ota-private/device.dpapi --image ota-artifacts/device/firmware.bin --expected-version <VERSION>
```

Sans `--image`, le client lit seulement l'état. Avec une image, il contrôle l'identité, le manifeste et la disponibilité avant l'envoi, puis attend le changement de partition, la version, l'état OTA VALID et la reconnexion. Un timeout n'est pas une preuve d'échec : inspecter l'état avant toute nouvelle tentative. Ne pas ouvrir de moniteur série pendant la validation : certains outils basculent DTR/RTS et réinitialisent la carte.

## Validation de la contribution

Le mécanisme a été essayé sur deux Waveshare ESP32-S3-AUDIO-Board-EN avec 16 Mio de flash : transfert Wi-Fi, changement de partition, redémarrage et reconnexion confirmés. Les essais ont refusé une signature incorrecte, une image altérée, un défi réutilisé et la clé de l'autre carte. Les images de ce test comportaient aussi des adaptations audio/LED/caméra locales qui ne font pas partie de cette contribution. La compilation de cette contribution isolée est vérifiée séparément. Aucune panne électrique n'a été simulée. Les autres cartes et dispositions de flash ne sont pas qualifiées.
