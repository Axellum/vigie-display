# Base de Connaissances Matérielle & Logicielle — Waveshare ESP32-P4-WIFI6-Touch-LCD-7B

`maj: 2026-10-18` · Référence d'ingénierie consolidée pour le développement autonome de **Vigie Mondiale 7B** (`alerte_mondiale_7b`).
Sources : Schéma électronique officiel Waveshare (`ESP32-P4-WIFI6-Touch-LCD-7B_schema.pdf`), BSP Waveshare `3.0.1`, code source ESPHome `2026.9.1`, et campagnes d'essais matériels sur la carte cible (`P4 v3.2`, MAC `E8:F6:0A:E8:BB:1A`) du 14 au 18/10/2026.

> [!IMPORTANT]
> Ce document constitue la **source de vérité matérielle** du dépôt `alerte_mondiale_7b`. Aucune broche GPIO, fréquence de bus, séquence d'extinction MIPI-DSI ou règle de reset SDIO ne doit être modifiée sans vérifier les contraintes documentées ci-dessous.

---

## 1. Architecture Processeur & Mémoire (ESP32-P4 v3.2 @ 400 MHz)

| Composant | Référence Schéma | Caractéristiques & Réglages Impératifs | Justification Technique & Pièges Évités |
| :--- | :--- | :--- | :--- |
| **SoC Principal** | `U13` — `ESP32-P4NRW32` | Révision **v3.2** de série (`engineering_sample: false`), Dual-Core RISC-V @ **400 MHz** | Sur un P4 v3.x, **400 MHz** est la fréquence nominale unique (`CONFIG_ESP32P4_REV_MIN_300`). Dans ESP-IDF 5.5, l'option 360 MHz (`ESP_DEFAULT_CPU_FREQ_MHZ_360`) dépend de `ESP32P4_SELECTS_REV_LESS_V3` et est ignorée sur v3.2. L'avertissement d'ESPHome sur les 400 MHz ne concerne que les anciens échantillons v1.x. |
| **Flash QSPI** | `U22` — `GD25Q256EYIGR` | **32 Mo** (`flash_size: 32MB`), QSPI dédiée | Capacité double des configurations génériques 16 Mo. Requiert `CONFIG_SPI_FLASH_YIELD_DURING_ERASE: "y"` pour maintenir la pile réseau active durant l'effacement OTA. |
| **PSRAM** | Intégrée boîtier (`NRW32`) | **32 Mo** en mode **Hex** à **200 MHz** (`execute_from_psram: true`) | **Critique** : Le défaut d'ESPHome est `20MHz`, ce qui provoque ~30 erreurs `lcd.dsi underrun` par seconde et déchire l'affichage. `speed: 200MHz` avec cache L2 de 256 Ko (`CONFIG_CACHE_L2_CACHE_256KB: "y"`) et segment BSS en SPIRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY: "y"`) est obligatoire. |
| **Horloges Quartz** | `Y1` (40 MHz) / `Y3` (32,768 kHz) | Quartz externe 32 kHz câblé sur **`GPIO0`** et **`GPIO1`** | **Interdiction** d'utiliser `GPIO0` et `GPIO1` pour des E/S générales. |
| **Plancher ESPHome** | Garde-fou `carte_7b_version.h` | **ESPHome ≥ 2026.9.1** vérifié par `static_assert` | ESPHome `2026.9.0` configurait une horloge PHY MIPI-DSI « legacy » incompatible avec le silicium P4 v3 (`esphome#18984`), provoquant un bootloop immédiat puis le passage en *safe mode*. |

---

## 2. Affichage MIPI-DSI 7" (EK79007 — 1024×600) & Rétroéclairage

### 2.1. Pilote MIPI-DSI et Temps de Démarrage (0 ms d'attente)
- **Contrôleur et Dalle** : Dalle IPS 7" **1024×600** (350 cd/m², angle 160°), contrôleur **EK79007** sur connecteur `P2` (30 broches, MIPI-DSI 2 voies à 900 Mbps/voie, horloge pixel 52 MHz, `pixel_mode: 16bit` RGB565).
- **Alimentation PHY DSI** : Fournie par le LDO interne du P4 sur le **canal 3 à 2,5 V** (`esp_ldo: channel: 3, voltage: 2.5V`). Les rails analogiques de la dalle sont générés par `U10` (`RT9193-18`, 1,8 V) et `U9` (`AP3012` : `AVDD` 9,6 V, `VGH` 18 V, `VGL` −6 V).
- **Broche de Reset (`GPIO33`) & Démarrage Instantané** :
  - Contrairement au M5Stack Tab5 dont le reset écran dépend d'un expandeur I2C lent (imposant 1 000 ms d'attente), la Waveshare 7B relie `RESET_LCD` directement sur **`GPIO33`** du P4.
  - Le pilote `mipi_dsi` (modèle `WAVESHARE-ESP32-P4-WIFI6-TOUCH-LCD-7B`) pilote `GPIO33` nativement (5 ms haut → 5 ms bas → 120 ms avant *Sleep Out*).
  - **Réglage impératif** : `carte_attente_ecran_ms: "0"`. Ce réglage permet d'allumer le rétroéclairage dès **0,97 s** après le boot et d'afficher la première image complète en **4,78 s à 5,6 s** (gain de 1 s sèche au démarrage).

### 2.2. Orientation Matérielle vs Rotation Logicielle (`carte_rotation: 0`)
- La dalle EK79007 ne possède pas de registre matériel de retournement (`no_transform` dans ESPHome).
- En `carte_rotation: 180`, LVGL applique une rotation logicielle accélérée par le PPA à chaque image.
- **Mesures réelles sur la carte (campagne du 16/10/2026, 4 passes par configuration)** :
  - `carte_rotation: 180` : **82,6 ms** par écran entier (8,5 – 9,2 ms au repos).
  - `carte_rotation: 0` : **69,4 ms** par écran entier (6,2 – 7,4 ms au repos), soit **−16 % de temps de rendu (~13,2 ms gagnés par frame)**.
- **Décision `alerte_mondiale_7b` (ADR-0003)** : L'interface **Vigie Mondiale 7B** cible nativement **1024×600 en `carte_rotation: 0`** (et conserve la compatibilité `180` par simple substitution si le boîtier est posé dans l'autre sens).

### 2.3. Rétroéclairage Analogique (`AP3032`) et Extinction Réelle (`tab5_dalle.cpp`)
- **Matériel** : Le pilote LED `U12` (`AP3032`) est commandé par **`GPIO32`** (`BL_CTRL`, logique inversée `inverted: true`, PWM LEDC à **5 000 Hz**). Le signal traverse un filtre RC (`10 kΩ` / `100 nF`) vers la broche `FB` (contre-réaction analogique).
- **Problème physique constaté** : À `0 %` de luminosité PWM, le gradateur analogique `AP3032` ne coupe pas totalement le courant des LED : une lueur résiduelle laisse voir l'image figée dans la pénombre (risque de marquage IPS et gêne nocturne). De plus, lors d'une mise à jour OTA, la dalle scintillait (perte momentanée de synchro HSYNC/VSYNC).
- **Solution implémentée (`firmware/ecran/tab5_dalle.cpp`, `carte_dalle_extinction: "2"`, `carte_dalle_noire_ota: "1"`)** :
  1. **Voile noir système** : Avant de mettre LVGL en pause (`lvgl.pause`), un objet plein écran noir absolu (`lv_color_black()`) est affiché sur `lv_layer_sys()` et forcé immédiatement via `lv_refr_now(nullptr)`. Sur une dalle IPS, les pixels noirs bloquent la lumière résiduelle et absorbent les appuis pendant une OTA.
  2. **Commandes MIPI-DSI DCS `0x28` + `0x10`** : Accès direct au canal `MipiDsi::io_handle_` (par instanciation explicite de template C++ sans modifier ESPHome) pour envoyer `0x28` (*Display Off*) suivi de `0x10` (*Sleep In*). Comme le contrôleur tactile GT911 est séparé de l'EK79007 (contrairement aux dalles TDDI ST7123), *Sleep In* éteint l'électronique de la dalle (abaissant la consommation totale à **1,24 W**) tout en laissant le tactile actif pour le réveil !
  3. **Séquence de réveil conforme norme DCS** : Au rallumage (`dalle_reveiller` puis `dalle_rallumer`), envoi de `0x11` (*Sleep Out*), attente de **120 ms** obligatoires avant `0x29` (*Display On*), retrait du voile noir et reprise LVGL (`lvgl.resume`).

---

## 3. Dalle Tactile Capacitive (GT911)

- **Connectique** : Contrôleur **GT911** (5 points, adresse I2C `0x5D` ou `0x14`) sur le bus I2C principal `bsp_bus` (**`SDA: GPIO7`**, **`SCL: GPIO8`**, fréquence **400 kHz**).
- **Broche Reset obligatoire (`reset_pin: GPIO23`)** :
  - Bien que le BSP Waveshare déclare parfois `GPIO_NUM_NC`, le schéma électronique montre que **`GPIO23`** pilote `RESET_TP` via `R54`.
  - **Preuve terrain (14/10/2026)** : Sans `reset_pin: GPIO23`, le GT911 démarre dans un état erratique (1 seul appui reconnu en 5 minutes). Avec `reset_pin: GPIO23`, **100 % des appuis sont reconnus instantanément**.
- **Absence d'interruption matérielle (`INT_TP`)** : Sur le schéma Waveshare, la broche `INT_TP` du connecteur `J3` ne va qu'au point de test `TP1` et n'est reliée à aucun GPIO de l'ESP32-P4. Le composant `gt911` doit donc impérativement fonctionner en **sondage périodique (`update_interval: 50ms`)**.
- **Alignement des coordonnées** : Le bloc `transform:` (`swap_xy: false`, `mirror_x: true`, `mirror_y: true`) cale le repère physique de la nappe tactile sur la dalle ; ESPHome applique ensuite automatiquement la rotation logicielle éventuelle (`LvglComponent::rotate_coordinates`).

---

## 4. Coprocesseur Wi-Fi 6 / BLE (ESP32-C6-MINI-1 en SDIO)

### 4.1. Câblage SDIO 4 bits & Paramètres `esp32_hosted`
La puce `U1` (**`ESP32-C6-MINI-1-N4`**, Wi-Fi 6 **2,4 GHz uniquement** + BLE 5.0) dialogue avec l'ESP32-P4 via le contrôleur SDIO Slot 1 :
- **`clk_pin: GPIO18`**, **`cmd_pin: GPIO19`**
- **`d0_pin: GPIO14`**, **`d1_pin: GPIO15`**, **`d2_pin: GPIO16`**, **`d3_pin: GPIO17`**
- **`reset_pin: GPIO54`** (relié par `R33` 0 Ω à `C6_CHIP_PU`) avec **`active_high: true`** obligatoire (`false` provoque `sdmmc_card_init failed`).
- **Fréquence et mémoire** : `sdio_frequency: 20MHz` (plus robuste que 40 MHz face au firmware C6 d'usine) et `use_psram: true` (`CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`).

### 4.2. Obligation du Reset CORE (`tab5_lien_c6.cpp`)
- **Bug critique résolu (16/10/2026)** : Après quelques redémarrages logiciels classiques (`esp_restart()`), le bus SDIO tombait en timeout (`0x107`) au boot suivant, déclenchant une boucle de reboots jusqu'au *safe mode*.
- **Cause racine** : `esp_restart()` n'exécute qu'un reset des cœurs CPU (`rst:0xc`, `SW_CPU_RESET`), laissant les contrôleurs périphériques SDIO/DMA du P4 dans un état verrouillé.
- **Correctif définitif (`firmware/ecran/tab5_lien_c6.cpp`)** : Enregistrement dès les constructeurs C++ (`__attribute__((constructor))`) d'un gestionnaire d'arrêt `esp_register_shutdown_handler` qui appelle **`esp_rom_software_reset_system()`** (Reset CORE complet du SoC, `rst:0x3`, `RESET_REASON_CORE_SW`).
- **Validation** : **20/20 redémarrages logiciels consécutifs réussis** sans aucune perte du lien SDIO. Le fichier journalise également en direct (`esp_rom_printf`) tout événement `ESP_HOSTED_EVENT_TRANSPORT_FAILURE`.

### 4.3. Interdiction d'interroger la version du C6 d'usine (`carte_c6_version: "false"`)
- Le firmware d'usine flashé par Waveshare dans l'`ESP32-C6` est antérieur au protocole `esp_hosted 2.12.12` d'ESPHome `2026.9.1` : il assure parfaitement le trafic Wi-Fi mais ne répond pas à la requête RPC `esp_hosted_get_coprocessor_fwversion()`.
- Chaque appel bloque la boucle principale pendant **1 seconde** (timeout RPC) et échoue. Il ne faut donc **jamais** interroger la version du C6 au démarrage tant que le C6 n'a pas été explicitement mis à jour en `2.12.12` (via `update: platform: esp32_hosted` ou via le connecteur 4 broches `H4 C6-UART` : `TXD, RXD, GND, IO9`).

---

## 5. Architecture Audio : Alertes Sonores (ES8311 + NS4150B) & Micros (ES7210)

### 5.1. Codec DAC `ES8311` (`0x18`) et Plafonnement à 0 dB (`volume_max: 75%`)
- **Bus I2S partagé (`mic_bus`)** : `LRCK: GPIO10`, `BCLK/SCLK: GPIO12`, `MCLK: GPIO13`.
- **Sortie Haut-Parleur** : DAC `U16` **ES8311** (adresse I2C `0x18` sur `GPIO7/8`), données I2S `DSDIN` sur **`GPIO9`**, mono 48 kHz 16 bits, suivi de l'amplificateur Classe D `U19` **NS4150B** piloté par **`GPIO53`** (`speaker_enable`, actif haut) vers le connecteur HP `H7` (PH2.0 2 broches).
- **Règle d'or `volume_max: 75%` (Essai acoustique du 17/10/2026)** :
  - Dans le pilote `es8311.cpp` d'ESPHome, le volume `[0.0 .. 1.0]` est écrit linéairement dans le registre `REG32` (`0x00 .. 0xFF`).
  - Or, sur l'ES8311, **`0xBF` (75 %) correspond à 0 dB (gain unitaire)**, tandis que **`0xFF` (100 %) applique un gain numérique de +32 dB**, provoquant un écrêtage massif et une distorsion sévère sur le NS4150B !
  - En fixant **`volume_max: 75%`** sur le `media_player`, ESPHome remappe `100 %` de l'interface utilisateur sur `0xBF` (`0 dB`) : les sirènes d'alerte Rouge/Tsunami sont ainsi puissantes, claires et sans aucune saturation.

### 5.2. Double Microphone & ADC `ES7210` (`0x40`) avec Boucle AEC Matérielle
- **Entrée Micro** : ADC 4 canaux `U17` **ES7210** (adresse I2C `0x40` sur `GPIO7/8`), sortie I2S `ASDOUT` sur **`GPIO11`** (`16 kHz`, `16bit`, `mic_gain: 30dB`, `correct_dc_offset: true`).
- **Topologie des voies** :
  - `MIC1` et `MIC2` : Deux microphones analogiques omnidirectionnels intégrés en façade.
  - `MIC3` : **Boucle de retour AEC matérielle** — le schéma Waveshare renvoie le signal de sortie du haut-parleur sur l'entrée `MIC3` de l'ES7210 pour fournir une référence matérielle d'annulation d'écho acoustique.
- **Règle du bus I2S partagé** : Un seul périphérique I2S (`mic_bus`) cadencé par `GPIO10/12/13` alimente l'ES8311 (48 kHz) et l'ES7210 (16 kHz). Toujours stopper la capture micro avant de jouer une alerte sonore ou une annonce vocale pour éviter tout conflit d'horloge ou claquement au démarrage du flux.

---

## 6. Alimentation, Gestion Batterie (`GPIO20`) & Pile RTC (`ML1220`)

### 6.1. Architecture d'Alimentation et Trois Ports USB
- **Trois connecteurs USB sur la carte** :
  1. **USB-C « USB »** : Port USB 1.1 Full-Speed natif de l'ESP32-P4 (`GPIO24` D−, `GPIO25` D+) utilisé pour le `USB_SERIAL_JTAG` (logs et flash rapide).
  2. **USB-C « USB TO UART »** : Convertisseur `U6` **CH343P** relié à l'`UART0` du P4 (`TX: GPIO37`, `RX: GPIO38`) avec circuit d'auto-reset / auto-boot (`DTR/RTS` vers `EN` et `GPIO35`).
  3. **USB-A « USB-OTG »** : Port hôte USB 2.0 High-Speed alimenté via le commutateur `U2` (`DIO7003`).
- **Aiguillage d'alimentation** : Diodes idéales (`Q2, Q3, Q5, Q6` + `MMDT3906`) couplant les deux entrées USB-C et l'étage batterie, suivies d'un interrupteur matériel **ON/OFF (`SW2` / transistor `Q8`)** qui coupe le rail principal `Core_5V`. Régulateurs abaisseurs `U3` (`MP1658GTF-Z`, 5 V → 3,3 V / 3 A) et `U5` (`MP1605GTF-Z`, 1,2 V cœur `VDD_HP`).

### 6.2. Circuit Batterie Li-ion 1S & Mesure ADC sur `GPIO20`
Contrairement aux premières documentations publiques, le schéma électronique révèle une chaîne batterie complète :
- **Connecteur `J4`** : Format **MX1.25 2 broches** (`BAT+` / `BAT-`) pour accumulateur Li-ion / LiPo 1S (3,7 V nominal, 4,2 V pleine charge).
- **Chargeur `U20` (`ETA6098`)** : Chargeur autonome Li-ion 1 cellule avec LED d'état `STAT` (non reliée à un GPIO du P4).
- **Survolteur `U21` (`SCT12A0DHKR`)** : Convertisseur Boost synchrone élevant la tension batterie vers le rail `5 V` du système.
- **Mesure de tension batterie (`GPIO20`)** :
  - La tension `VBAT` attaque un pont diviseur résistif **`200 kΩ / 100 kΩ`** dont le point milieu est relié à **`GPIO20`** (`ADC1` de l'ESP32-P4).
  - La tension présente sur `GPIO20` vaut exactement :
    $$V_{\text{GPIO20}} = V_{\text{BAT}} \times \frac{100}{200 + 100} = \frac{V_{\text{BAT}}}{3}$$
  - Dans ESPHome, la lecture s'effectue via `platform: adc` sur `pin: GPIO20` (`attenuation: 12db`) avec un filtre **`multiply: 3.0`**.

### 6.3. Support de Pile RTC (`BAT1`) : ML1220 Rechargeable Uniquement !
> [!CAUTION]
> **Sécurité Matérielle — Risque de destruction de pile** : Le support `BAT1` (format 1220) alimente le domaine `VBAT` (RTC interne de l'ESP32-P4). Sur le schéma Waveshare, le rail `3V3` recharge en permanence ce support à travers une diode Schottky **`B5819`**.
> Il faut **impérativement utiliser une cellule rechargeable Lithium `ML1220` (3,0 V – 3,3 V)** et **JAMAIS une pile primaire non rechargeable `CR1220`** (qui risquerait de gonfler, fuir ou exploser sous l'effet du courant de charge).

---

## 7. Cartographie Complète des Broches (Pinout ESP32-P4)

| Groupe Fonctionnel | Broches ESP32-P4 | Détails & Contraintes |
| :--- | :--- | :--- |
| **Quartz 32,768 kHz** | `GPIO0`, `GPIO1` | Réservés oscillateur RTC (`Y3`). |
| **Bus I2C Système (`bsp_bus`)** | `GPIO7` (SDA), `GPIO8` (SCL) | 400 kHz. Partagé entre GT911 (`0x5D`), ES8311 (`0x18`), ES7210 (`0x40`), caméra CSI (`J2`) et connecteur externe `H10` (via `M1 NDC7002N`). |
| **Audio I2S (`mic_bus`) & Ampli** | `GPIO9` (DOUT), `GPIO10` (LRCK), `GPIO11` (DIN), `GPIO12` (BCLK), `GPIO13` (MCLK), `GPIO53` (PA_CTRL) | `GPIO9` → ES8311 ; `GPIO11` ← ES7210 ; `GPIO53` → Ampli NS4150B. |
| **Lien SDIO Wi-Fi C6** | `GPIO14..17` (D0..D3), `GPIO18` (CLK), `GPIO19` (CMD), `GPIO54` (RST), `GPIO6` (C6_IO2) | Slot SDIO 1 @ 20 MHz, `GPIO54` actif haut. `GPIO6` relié par `R31` 0 Ω à `C6_IO2` (inutilisé en SDIO). |
| **Mesure Batterie ADC** | `GPIO20` | Entrée analogique `VBAT / 3` (pont 200k / 100k). |
| **Bus CAN (`U14 TJA1051T/3`)** | `GPIO21` (RX), `GPIO22` (TX) | Émetteur-récepteur CAN haute vitesse intégré (protection `SM24CANB`, cavalier 120 Ω `H5`). Idéal pour **NMEA 2000**. |
| **Tactile GT911** | `GPIO23` (RESET_TP) | Reset matériel actif. Pas de broche INT reliée. |
| **USB 1.1 Natif (Serial/JTAG)** | `GPIO24` (D−), `GPIO25` (D+) | Connecteur USB-C « USB ». |
| **Bus RS485 (`U11 THVD1406DR`)** | `GPIO26` (RX P4 / RO), `GPIO27` (TX P4 / DI) | Sens automatique (sans DE/RE), protection parafoudre/TVS, cavalier 120 Ω `H6`. Idéal pour **NMEA 0183 Marine**. |
| **Écran MIPI-DSI & Rétroéclairage** | `GPIO32` (BL_CTRL), `GPIO33` (RESET_LCD) | `GPIO32` PWM 5 kHz inversé (AP3032) ; `GPIO33` reset dalle EK79007. |
| **Bouton BOOT & UART0** | `GPIO35` (BOOT), `GPIO37` (TX0), `GPIO38` (RX0) | Reliés au pont `CH343P` (USB-C « USB TO UART ») et au connecteur `H8`. |
| **Lecteur microSD (`SD1`)** | `GPIO39..42` (D0..D3), `GPIO43` (CLK), `GPIO44` (CMD), `GPIO45` (Power EN) | Alimentation microSD commandée par `Q1` sur `GPIO45` (actif bas). |
| **Connecteur d'extension `P3`** | **`GPIO2, 3, 4, 5, 28, 29, 30, 31, 34, 36`** | **10 GPIO libres** sur connecteur PH2.0 12 broches (+ `3V3`, `GND`). Note : `GPIO36` possède un pull-up de 10 kΩ vers `3V3`. **`GPIO4`/`GPIO5` dédiés au GPS UART externe**. |
| **Connecteur d'extension `P1`** | **`GPIO46, 47, 48, 49, 50, 51, 52`** | **7 GPIO libres** sur connecteur PH2.0 12 broches (+ `BAT`, `3V3`, `VO4`, `GND`). |

---

## 8. Optimisations Critiques LVGL 9.5 (Zéro-Lag & Démarrage Rapide)

Trois mécanismes C++ de haute performance, validés par profilage `callgrind` et mesures sur cible Waveshare 7B, doivent impérativement être préservés dans `firmware/ecran/` :

1. **Pré-résolution et Purge des Abonnements de Labels au Boot (`firmware/ecran/tab5_demarrage.cpp`)** :
   - *Problème LVGL 9.5* : Par défaut, ESPHome initialise l'écran LVGL en `240×240` et ne applique `1024×600` qu'après avoir instancié tous les widgets, déclenchant un recalcul global de thème. De plus, chaque `lv_label` créé s'abonne à `LV_EVENT_UPDATE_LAYOUT_COMPLETED` sur le `lv_display_t`, rendant chaque `LV_EVENT_REFR_REQUEST` quadratique $\mathcal{O}(N^2)$ pendant `setup()`.
   - *Solution (`demarrage_preparer_lvgl`)_ : Appelée dès le premier style (`style_repere_demarrage`), elle fixe immédiatement la résolution à `1024×600` (`kEcranL × kEcranH`) et vide la file des labels abonnés dès que leur nombre atteint `64` (`kAbonnesMax`). Résultat : instructions CPU divisées par 11 au démarrage (**14,5 G → 1,3 G instructions**, première image en **~4,8 s** au lieu de **20,7 s**).

2. **Garage Hors-Écran des Modales Cachées (`firmware/ecran/tab5_registry.cpp`)** :
   - *Problème LVGL 9.5* : À chaque invalidation de zone (`lv_obj_invalidate`), la fonction interne `blur_walk_cb` de LVGL 9.5 parcourt récursivement tous les objets dont la boîte englobante recouvre la zone, **y compris les popups cachés (`LV_OBJ_FLAG_HIDDEN`)**.
   - *Solution (`ModalRegistry::garer`)_ : Lors de `LV_EVENT_REFR_START`, toute fenêtre modale cachée reçoit `lv_obj_set_style_translate_y(obj, 10000, LV_PART_MAIN)` + `lv_obj_refr_pos(obj)`. Projetée à `Y = +10000 px`, son rectangle n'intersecte plus jamais l'écran `1024×600` (**−56 % de charge CPU au rafraîchissement**).

3. **Bascule de Thème Atomique en 65 ms (`firmware/ecran/tab5_theme.cpp`)** :
   - *Solution (`theme_bascule_debut` / `theme_bascule_fin`)_ : Coupe temporairement le rafraîchissement individuel des styles (`lv_obj_enable_style_refresh(false)`) et l'invalidation d'affichage (`lv_display_enable_invalidation(disp, false)`) tout en activant `lvgl_vidage_abonnes(true)` pendant la mise à jour des palettes, puis exécute un unique `lv_obj_invalidate(lv_screen_active())`. Le changement de thème Clair/Sombre passe ainsi de **761 ms (voire 3,2 s) à 65 ms**.

---

## 9. Architecture Multi-Écrans / Multi-Cartes (`waveshare-7b` & `tab5`)

Conformément à la directive architecturale d'Axel, le firmware **Vigie Météo & Alerte Catastrophe Mondiale** est conçu dès sa fondation pour supporter plusieurs cibles matérielles ESP32-P4 à partir d'**un unique socle métier C++/LVGL** (`firmware/socle/` et `firmware/ecran/`), en changeant simplement la substitution `vigie_carte` dans `vigie-mondiale-7b.yaml` :

| Caractéristique | Cible `waveshare-7b` (Référence 7") | Cible `tab5` (M5Stack Tab5 V2 5") | Futurs écrans `waveshare-8b` / `10b` |
|---|---|---|---|
| **Paquets inclus** | `geometrie-waveshare-7b.yaml` + `carte-waveshare-7b.yaml` | `geometrie-tab5.yaml` + `carte-tab5.yaml` (`tab5-hardware.yaml`) | `geometrie-<cible>.yaml` + `carte-<cible>.yaml` |
| **Résolution native** | **`1024 × 600 px`** (`carte_police_echelle: 0.82`) | **`1280 × 720 px`** (`carte_police_echelle: 1.0`) | `1280 × 800 px` (MIPI-DSI) |
| **Interface écran** | RGB DPI 16 bits (`rpi_dpi_rgb`) | MIPI-DSI 2 lanes (`ili9881c` / `st7123` / `st7703`) | MIPI-DSI 2 lanes |
| **Contrôleur tactile** | `GT911` (`I2C 0x5D`/`0x14`, `GPIO7/8`) | `GT911` (`ili9881c`) ou `ST7123` (`GPIO31/32`) | `GT911` (`I2C`) |
| **Codec Audio I2S** | `ES8311` (`0x18`) + `ES7210` (`0x40`) | `ES8388` (`0x10`) + `ES7210` (`0x40`) | `ES8311` + `ES7210` |
| **Expanders I/O** | Aucun (GPIO directs) | 2 × `PI4IOE5V6408` (`0x43` et `0x44`) | Selon carte |
| **Jauge Batterie** | ADC `GPIO20` (`multiply: 3.0`) | `INA226` (`I2C 0x41`, tension + courant) | ADC `GPIO20` |
| **Port GPS NMEA** | Connecteur `P3` (`GPIO4/5`) ou RS485 (`GPIO26/27`) | **Port A Grove HY2.0-4P** (`GPIO53/54`, Unit GPS SMA AT6668) | Connecteur `P3` / RS485 |

### Mise à l'échelle géométrique unifiée (`firmware/socle/tab5_geometrie.h` & `vigie_modele_mondial.h`)
- Les constantes `kEcranL`, `kEcranH`, `kCarteL`, `kCarteH`, `kCorpsW`, `kCorpsH` ainsi que les helpers `largeur_carte()`, `hauteur_carte()`, `vigie_x_depuis_7b()` et `vigie_y_depuis_7b()` adaptent automatiquement toutes les coordonnées entre `1024×600` et `1280×720` sans aucune duplication du code d'affichage ni des modèles de données.
- `vigie_obtenir_profil_carte(VigieCarteCible)` expose au runtime les capacités exactes de chaque carte (dimensions, DPI, échelle de police, présence RS485 ou Grove).

