# Banc d'Essais et Réponses Communauté — Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (7B)

> Date : 19/10/2026  
> Dépôt : `Axellum/alerte-mondiale-7b`  
> Objectif : Fiches de tests unitaires, configuration YAML validée et réponses techniques prêtes à l'emploi pour les dépôts GitHub tiers (`homeassistant_espscreen`, `esp32p4-android-auto`, `RetroESP32-P4`, `HomeTiles`, `moybyte`, `esphome`).

---

## 📋 Sommaire des Lots de Test

1. [Lot 1 : Audio ES8311 + NS4150B + ES7210 (homeassistant_espscreen #271 & moybyte #58)](#lot-1--audio-es8311--ns4150b--es7210)
2. [Lot 2 : Démarrage DSI EK79007 & Silicium v3.2 sans écran noir (HomeTiles #41 & RetroESP32-P4 #4)](#lot-2--démarrage-dsi-ek79007--silicium-v32-sans-écran-noir)
3. [Lot 3 : Correction Bruit PPA YUV420 & Bus CAN TJA1051T/3 (esp32p4-android-auto #5 & #4)](#lot-3--correction-bruit-ppa-yuv420--bus-can-tja1051t3)
4. [Lot 4 : Stabilité SDIO ESP32-C6 & Reset CORE (esphome #20191 & moybyte #58)](#lot-4--stabilité-sdio-esp32-c6--reset-core)
5. [YAML de Banc d'Essai Autonome Clé en Main](#yaml-de-banc-dessai-autonome-clé-en-main)
6. [Réponses Prêtes à Poster sur GitHub (Anglais Technique)](#réponses-prêtes-à-poster-sur-github)

---

## Lot 1 : Audio ES8311 + NS4150B + ES7210

### Problème communautaire
* `homeassistant_espscreen #271` cherche des testeurs physiques pour valider le haut-parleur ES8311 (TTS / son) et les microphones ES7210.
* `moybyte #58` recherche la séquence d'init des registres et le contrôle de l'amplificateur.

### Constatations & Pièges Matériels Validés sur Matériel Réel
1. **Contrôle Amplificateur Classe D (`NS4150B`)** :
   * L'amplificateur est commandé par **`GPIO53`** (`PA_CTRL`, actif HAUT).
   * Sans `GPIO53: HIGH`, le DAC ES8311 fonctionne mais aucun son ne sort sur le haut-parleur physique (connecteur PH2.0 `H7`).
2. **Piège critique du volume (`volume_max: 75%` obligatoire)** :
   * Dans le driver ESPHome `es8311.cpp`, le volume `0.0 .. 1.0` est écrit linéairement dans le registre `REG32` (`0x00 .. 0xFF`).
   * **`0xBF` (75 %) correspond à 0 dB (gain unitaire)**.
   * **`0xFF` (100 %) applique un gain numérique de +32 dB**, causant un écrêtage sévère et une saturation inaudible sur le haut-parleur.
   * La directive `volume_max: 75%` remappe les 100 % de l'utilisateur sur `0 dB` (son cristallin et puissant).
3. **Double Microphone & Annulation d'écho (`ES7210`)** :
   * Les micros d'ambiance sont sur `MIC1` et `MIC2`.
   * La voie `MIC3` est une boucle de retour matérielle (AEC loopback) reliée à la sortie HP pour l'annulation d'écho vocale.
   * Fréquence recommandée : `16 kHz`, `16bit`, gain micro `30dB`.
4. **Horloge I2S partagée** :
   * `MCLK: GPIO13`, `BCLK: GPIO12`, `LRCK: GPIO10`.
   * `DOUT (ES8311): GPIO9`, `DIN (ES7210): GPIO11`.

---

## Lot 2 : Démarrage DSI EK79007 & Silicium v3.2 sans écran noir

### Problème communautaire
* `HomeTiles #41` et `RetroESP32-P4 #4` subissent un écran noir au boot avec le silicium P4 révision v3.2 livré actuellement par Waveshare.

### Constatations & Configuration Validée
1. **Fréquence CPU nominale** :
   * Sur un P4 v3.x, régler `cpu_frequency: 400MHz` et `engineering_sample: false`. (360 MHz n'est réservé qu'aux premiers samples v1.x).
2. **Alimentation PHY MIPI-DSI (`esp_ldo`)** :
   * Le PHY DSI nécessite l'activation explicite du **LDO interne canal 3 à 2,5 V** :
     ```yaml
     esp_ldo:
       - channel: 3
         voltage: 2.5V
     ```
3. **Reset Écran direct sur GPIO33** :
   * La broche `RESET_LCD` est câblée en direct sur **`GPIO33`** (pas d'expandeur I2C).
   * La dalle requiert : 5 ms HAUT → 5 ms BAS → 120 ms d'attente avant la commande *Sleep Out* (`0x11`).
4. **PSRAM Hex 200 MHz obligatoire pour le rafraîchissement DSI** :
   * Si la PSRAM reste au défaut 20 MHz d'ESPHome, le flux DSI subit des `underruns` systématiques (déchirement d'écran ou noir complet).
   * Configuration : `mode: hex`, `speed: 200MHz`, L2 Cache `256KB`.

---

## Lot 3 : Correction Bruit PPA YUV420 & Bus CAN TJA1051T/3

### Problème communautaire
* `esp32p4-android-auto #5` observe des zébrures verticales sur la dalle 7B en utilisant le PPA (Pixel Processing Accelerator) en mode paysage 0°.
* `esp32p4-android-auto #4` demande comment raccorder le bus CAN.

### Constatations & Solutions
1. **Zébrures PPA en YUV420 sur 1024×600** :
   * Le bloc matériel PPA du P4 impose des contraintes d'alignement de lignes (pitch) de 64 ou 128 octets en mémoire externe (PSRAM).
   * En YUV420, les plans U et V ont une largeur divisée par 2 ($1024 / 2 = 512$ octets, compatible), mais la hauteur $600 / 2 = 300$ n'est pas un multiple de 16 blocs macro.
   * Si le framebuffer de sortie n'est pas alloué avec `MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM` et aligné sur les lignes de cache (64 octets), ou si l'invalidation de cache `esp_cache_msync()` n'est pas synchronisée avant l'appel DSI, le buffer vidéo présente un motif zébré (données désynchronisées entre le cache L2 et la PSRAM).
   * En ESP-IDF 5.5 / 6.x, utiliser `ppa_srm_oper_config_t` avec un buffer de sortie alloué à `1024×608` (multiple de 16) paddé, puis cropper les 8 lignes inférieures à l'affichage.
2. **Émetteur-récepteur CAN embarqué (`TJA1051T/3`)** :
   * Émetteur-récepteur haute vitesse déjà présent sur la carte (`U14`) avec diode TVS de protection `SM24CANB` !
   * **`CAN_RX: GPIO21`**
   * **`CAN_TX: GPIO22`**
   * Cavalier `H5` : permet d'activer la résistance de terminaison de bus de **120 Ω**.
   * Compatible standard ISO 11898-2 et NMEA 2000.

---

## Lot 4 : Stabilité SDIO ESP32-C6 & Reset CORE

### Problème communautaire
* `esphome #20191` et `moybyte #58` rencontrent des blocages SDIO (`sdmmc timeout 0x107`) après redémarrage logiciel.

### Constatations & Solution
1. **Câblage SDIO 4 bits** :
   * `CLK: GPIO18`, `CMD: GPIO19`, `D0: GPIO14`, `D1: GPIO15`, `D2: GPIO16`, `D3: GPIO17`.
   * `RESET: GPIO54` avec **`active_high: true`**.
   * Fréquence SDIO optimale : **20 MHz** (`sdio_frequency: 20MHz`).
2. **Le piège du CPU Reset vs CORE Reset** :
   * `esp_restart()` standard n'exécute qu'un `SW_CPU_RESET` (`rst:0xc`). Les registres DMA et le contrôleur SDIO du P4 ne sont pas réinitialisés, causant un timeout au reboot suivant.
   * **Solution validée** : enregistrer un shutdown handler appelant `esp_rom_software_reset_system()` (`rst:0x3`, `RESET_REASON_CORE_SW`). 100 % de succès sur 20 redémarrages consécutifs.
3. **Firmware C6 d'usine** :
   * Ne supporte pas la commande RPC de version `esp_hosted_get_coprocessor_fwversion()`. Ne pas tenter de lire la version sous peine d'un timeout de 1 seconde par cycle.

---

## YAML de Banc d'Essai Autonome Clé en Main

Fichier prêt à être compilé et téléversé sur la Waveshare 7B :
`firmware/tests/banc_essai_7b_communaute.yaml`

Ce banc d'essai inclut :
* Écran DSI 1024×600 + Rétroéclairage PWM 5 kHz
* Dalle tactile GT911 (reset GPIO23, update 50ms)
* Audio ES8311 avec plafonnement 75 % (0 dB) + Ampli GPIO53
* Micro ES7210 (16 kHz, 16bit, 30dB)
* Diagnostic de mémoire Heap temps réel
* Jauge batterie analogique GPIO20 (facteur 3.0)

---

## 🔬 Résultats d'Essais Réels sur la Carte Cible (Campagne du 19/10/2026)

* **Matériel connecté** :
  * Carte : `Waveshare ESP32-P4-WIFI6-Touch-LCD-7B` (révision silicium `v3.2`, MAC `E8:F6:0A:E8:BB:1A`) sur `COM8` / `192.168.0.197`.
  * Voltmètre USB : `FNIRSI FNB-58` (`VID 2E3C / PID 5558`) sur `COM9`.
* **Relevé de puissance électrique (FNB-58 via HID, 100 éch/s)** :
  * Régime nominal (Wi-Fi actif, affichage 1024×600 actif) : **`5,028 V` / `309,8 mA` = `1,558 W`** (Percentile 95 : `1,569 W`, max `1,626 W`).
* **Validation Audio en direct (Home Assistant → 7B)** :
  * Déclenchement de la synthèse vocale TTS locale (`tts.piper`) vers `media_player..._2`.
  * **Comportement acoustique** : Lecture de 7 secondes sans distorsion ni claquement. Remappage 0 dB (`volume_max: 75%`) parfaitement respecté.
  * **Confirmation utilisateur in-situ** : *« Son parfait sur le haut-parleur du pack fourni avec l'écran »*. Puissance acoustique nette et intelligible sans aucune saturation numérique.
  * Transition d'état : `idle` → `playing` (07:35:24 UTC) → `idle` (07:35:31 UTC).
* **Validation Extinction & Réveil DSI (`AP3032` + `EK79007`)** :
  * Commande `light.turn_off` : extinction complète de la dalle et mise en pause LVGL.
  * Commande `light.turn_on` (`brightness: 180`) : séquence de réveil DCS (`0x11` Sleep Out → 120 ms → `0x29` Display On → reprise LVGL) exécutée avec succès sans corruption d'affichage ni désynchronisation.

---

## 🌐 Retours Communautaires Postés sur GitHub

1. **`MaxGramser/homeassistant_espscreen` (PR #271)** :
   * [Commentaire #6262284848](https://github.com/MaxGramser/homeassistant_espscreen/pull/271#issuecomment-6262284848) (Validation physique HP/TTS, micro ES7210, GPIO53 et volume_max 75%).
2. **`payalneg/esp32p4-android-auto` (Issue #5 & #4)** :
   * [Commentaire #6262325949](https://github.com/payalneg/esp32p4-android-auto/issues/5#issuecomment-6262325949) (Zébrures PPA YUV420 en 1024×600, padding 1024×608, alignement PSRAM et broches CAN GPIO21/22).
3. **`GalusPeres/HomeTiles` (Issue #41)** :
   * [Commentaire #6262329768](https://github.com/GalusPeres/HomeTiles/issues/41#issuecomment-6262329768) (Résolution écran noir sur silicium P4 v3.2 : LDO canal 3 à 2,5 V, reset GPIO33 et PSRAM 200 MHz).


