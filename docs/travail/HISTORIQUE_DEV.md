# Historique de Développement — Vigie Mondiale 7B (`alerte_mondiale_7b`)

`maj: 2026-10-18` · Journal chronologique (le plus récent en haut) et socle technique hérité.

---

## 📅 2026-10-18 — Socle C++ Mondial, Parseur GPS NMEA 0183, Mode Marine & Architecture Multi-Écrans (`waveshare-7b` + `tab5`)
- **Audits d'ingénierie consolidés** :
  - `docs/audits/BASE_CONNAISSANCE_WAVESHARE_7B.md` : synthèse exhaustive des 4 audits matériels 7B + architecture Multi-Écrans (`waveshare-7b` 1024×600 et `tab5` 1280×720).
  - `docs/audits/AUDIT_GPS_ET_MODE_MARINE_7B.md` : intégration GPS NMEA 0183 sur `P3` (`GPIO4/5`), bus `RS485` (`GPIO26/27` `THVD1406DR`) sur 7B, et **M5Stack Unit GPS SMA (AT6668)** sur **Port A Grove (`GPIO53/54`)** du M5Stack Tab5 V2.
- **Modèle C++ Mondial & Parseur GPS NMEA (`firmware/socle/vigie_modele_mondial.h/.cpp`)** :
  - Structures statiques sans allocation dynamique (`VigieGpsFix`, `VigiePluieNowcast`, `VigieEtatMarin`, `VigieZoneMeteo`, `VigieAlerteCatastrophe`, `VigieEtatMondial`, `VigieProfilCarte`).
  - Décodeur NMEA 0183 complet (`$GxGGA`, `$GxRMC`, checksum XOR `*HH`), formatage nautique DMS/DMM (`48°51.39'N 002°21.13'E`), Haversine, azimut et secteur cardinal 16 rhumbs (FR/EN) avec recalcul temps réel dès que le navire/véhicule bouge.
- **Paquets ESPHome 100 % autonomes (Zéro Home Assistant)** :
  - Purge complète des bouchons domotiques dans `carte-waveshare-7b.yaml` et `tab5-alertes.yaml`, ajout des paquets matériels `carte-tab5.yaml`, `geometrie-tab5.yaml`, `tab5-hardware.yaml`, `vigie-gps-nmea.yaml` et point d'entrée `vigie-mondiale-7b.yaml`.
  - Suite de tests unitaires `tests/test_socle_et_gps.py` validant la compilation C++ native (`g++`) et l'absence totale de dépendance Home Assistant.

---

## 📅 2026-10-18 — Création du projet & Extraction du socle Waveshare 7B
- **Décision stratégique d'Axel** : Création d'un projet et d'un dépôt totalement séparés de `00ProjetTab` pour concevoir un **terminal professionnel mondial de météo et d'alertes catastrophes naturelles** (séismes, tsunamis, volcans, ouragans, tornades, pluies extrêmes, feux, météo spatiale), **sans aucun lien avec Home Assistant**, articulé autour de la carte **Waveshare 7" (`ESP32-P4-WIFI6-Touch-LCD-7B`)** et d'un dépôt backend dédié (`alerte_mondiale_api`).
- **Initialisation du dépôt local `H:\AuxFilsDesIdees\alerte_mondiale_7b`** :
  - Mise en place de l'arborescence (`firmware/socle`, `firmware/ecran`, `firmware/paquets`, `firmware/ui_components`, `firmware/fonts`, `docs/travail`, `tools`, `tests`).
  - Importation ciblée des briques matérielles et graphiques éprouvées sur la Waveshare 7B (voir détail dans [`BRIQUES_REPRISES_TAB5.md`](BRIQUES_REPRISES_TAB5.md)).
  - Rédaction du kit documentaire complet (`README.md`, `OBJECTIFS.md`, `PLAN.md`, `HISTORIQUE_DEV.md`, `DECISIONS.md`, `TACHES.md`, `BRIQUES_REPRISES_TAB5.md`).

---

## 🧬 Héritage Technique Validé (Acquis des campagnes Waveshare 7B du 14 au 18/10/2026)

Avant la séparation en dépôt autonome, la carte matérielle **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** (P4 révision v3.2, MAC `E8:F6:0A:E8:BB:1A`) a fait l'objet de 4 audits matériels complets et d'une campagne intensive de validation que ce projet récupère intégralement dès le jour 1 :

1. **Processeur & Mémoire (P4 v3.2 @ 400 MHz)** :
   - Contrairement au M5Stack Tab5 (P4 v1.x limité à 360 MHz), la Waveshare 7B embarque un **ESP32-P4NRW32 révision v3.2** qui tourne nativement à **400 MHz** avec 32 Mo de Flash QSPI (`GD25Q256`) et 32 Mo de PSRAM Hex à **200 MHz** (`execute_from_psram: true`, cache L2 256 Ko).
2. **Dalle 7" IPS 1024×600 MIPI-DSI (`EK79007`) & Tactile (`GT911`)** :
   - Reset matériel de la dalle sur `GPIO33` : **0 ms d'attente au démarrage** (contre 1 000 ms sur le Tab5 qui devait attendre son expandeur I2C), permettant d'allumer le rétroéclairage en **0,97 s** et d'afficher la première image en **4,78 s**.
   - **Orientation optimale** : Carte montée en `carte_rotation: 0` (sens natif sans rotation logicielle LVGL) → temps de rendu plein écran abaissé de **83 ms à 69 ms**.
   - **Extinction réelle de la dalle (`tab5_dalle.cpp`)** : Le gradateur analogique (`AP3032` sur `GPIO32`) laissant deviner l'image à 0 %, l'extinction combine un voile noir LVGL + commandes MIPI-DSI `0x28` (Display Off) et `0x10` (Sleep In), ainsi que l'extinction pendant les mises à jour OTA.
   - **Tactile GT911** : Bus I2C `GPIO7`/`GPIO8` (400 kHz) avec `reset_pin: GPIO23` obligatoire pour garantir 100 % des appuis reconnus.
3. **Fiabilité Wi-Fi 6 SDIO (`ESP32-C6-MINI-1`) — Correctif Critique Reset CORE** :
   - Sur la Waveshare 7B, un simple `esp_restart()` (reset CPU seul) laissait le bus SDIO du coprocesseur C6 (`GPIO14-19`, reset `GPIO54`) dans un état bloqué après quelques redémarrages (`sdmmc_card_init failed`).
   - La brique reprise `tab5_lien_c6.cpp` intercepte tout redémarrage logiciel pour forcer un `esp_rom_software_reset_system()` (Reset CORE complet `rst:0x3`), validé à **20/20 redémarrages consécutifs sans échec**.
   - Désactivation de l'interrogation de version ESP-Hosted du C6 d'usine qui gelait la boucle pendant 1 seconde.
4. **Audio & Alertes Sonores (`ES8311` + `NS4150B` sur `GPIO53`)** :
   - Le registre de volume de l'`ES8311` applique un gain numérique de +32 dB à `0xFF` (100 %), provoquant une saturation audible. Plafonnement validé à l'oreille à **`volume_max: 75%` (`0xBF` = 0 dB)** pour un son d'alerte puissant et cristallin.
5. **Optimisations LVGL 9.5 Haute Performance** :
   - Purge des abonnements inutiles des labels au démarrage (`tab5_demarrage.cpp` : démarrage divisé par 4).
   - Garage des modales/popups fermés à `translate_y = 10000` (`tab5_registry.cpp`) pour éviter que `blur_walk_cb` de LVGL 9.5 ne parcoure les widgets cachés à chaque invalidation (−56 % de temps CPU).
   - Bascule de thème Clair/Sombre accélérée de **761 ms à 65 ms** (`tab5_theme.cpp`).
