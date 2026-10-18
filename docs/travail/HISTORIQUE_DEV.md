# Historique de Développement — Vigie Mondiale 7B (`alerte_mondiale_7b`)

`maj: 2026-10-18` · Journal chronologique (le plus récent en haut) et socle technique hérité.

---

## 📅 2026-10-18 (Vague 2) — Décodeur Trame API Zéro-Allocation, Projection Cartographique, Persistance NVS CRC32, Client API & Rendus Hors-Tablette (7B & Tab5)
- **Décodeur de trame API & Projection Cartographique (`firmware/socle/vigie_modele_mondial.h/.cpp`)** :
  - `vigie_decoder_trame_api()` : décodage déterministe sans allocation dynamique des enregistrements `HEAD|...`, `ZONE|...`, `RAIN5M|...`, `DAY|...` et `HAZ|...`, tri automatique par gravité (`ROUGE` > `ORANGE` > `JAUNE` > `VERT`) et recalcul immédiat des distances Haversine, azimuts et ETA Tsunami par rapport au fix GPS actif.
  - `vigie_cadre_carte_ecran()` & `vigie_projeter_coord_carte()` : projection équirectangulaire pixel-perfect adaptée automatiquement aux résolutions `1024×600` (`waveshare-7b`) et `1280×720` (`tab5`).
  - `vigie_nvs_calculer_crc32()`, `vigie_preparer_bloc_nvs()`, `vigie_restaurer_bloc_nvs()` : persistance binaire NVS signée par magic number `0x56494731` (`VIG1`) et CRC32 IEEE 802.3 (`0xEDB88320`).
  - `vigie_evaluer_declenchement_alerte()` & `vigie_acquitter_alerte()` : détection d'urgence DEFCON (réveil dalle MIPI-DSI + alarme sonore sur alerte ROUGE ou ORANGE dans le rayon de geofencing / tsunami) avec anti-répétition par ID acquitté.
- **Connecteur Client API (`firmware/ecran/vigie_client_api.h/.cpp` & `firmware/paquets/vigie-client-api.yaml`)** :
  - Construction dynamique de l'URL `/v1/embedded/frame?device_type=<cible>&lat=...&lon=...&marine=1` dès qu'un fix GPS NMEA est valide, gestion du statut de fraîcheur (`LIVE · il y a Xs` vs `HORS-LIGNE · Cache NVS`).
- **Générateur de Rendu Hors-Tablette Haute Fidélité (`tools/generer_rendu_ihm.py` & `docs/rendus/`)** :
  - Génération des aperçus SVG et PNG pour **Waveshare 7B (`1024×600`)** (`docs/rendus/aperçu_waveshare_7b_1024x600.svg/.png`) et **M5Stack Tab5 V2 (`1280×720`)** (`docs/rendus/aperçu_m5stack_tab5_1280x720.svg/.png`).
  - Suite `tests/test_socle_et_gps.py` étendue à **13/13 tests verts**.

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
