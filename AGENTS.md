# AGENTS.md — Instructions Partagées (Vigie Mondiale 7B)

> Ce fichier est la source unique de consignes pour tous les agents IA (Antigravity, Claude Code, Cursor, Moteur d'Agents) travaillant sur `alerte_mondiale_7b`.

## 1. Identité du Dépôt
- **Matériel cible** : `Waveshare ESP32-P4-WIFI6-Touch-LCD-7B` (7" IPS 1024×600, P4 v3.2 @ 400 MHz, 32 Mo Flash, 32 Mo PSRAM Hex 200 MHz, C6 Wi-Fi SDIO, GT911 GPIO23, ES8311 + NS4150B GPIO53).
- **Règle d'Or** : **ZÉRO lien avec Home Assistant**. Toute donnée météo ou alerte catastrophe provient du backend [`alerte_mondiale_api`](../alerte_mondiale_api/README.md) ou du cache NVS local.
- **Documentation de travail** : Toujours lire [`docs/travail/INDEX.md`](docs/travail/INDEX.md) avant d'intervenir et mettre à jour [`docs/travail/HISTORIQUE_DEV.md`](docs/travail/HISTORIQUE_DEV.md) et [`docs/travail/TACHES.md`](docs/travail/TACHES.md) après chaque avancée vérifiée.

## 2. Garde-fous Matériels Waveshare 7B (Ne jamais casser)
1. **Lien SDIO C6** : Toujours conserver `tab5_lien_c6.h/.cpp` (`esp_rom_software_reset_system()`). Ne jamais interroger la version ESP-Hosted du C6 d'usine (`carte_c6_version: "false"`).
2. **Dalle EK79007 (1024×600)** : Conserver `tab5_dalle.h/.cpp` (`sleep in` 0x10 + voile noir à l'extinction et pendant l'OTA). Pas d'attente d'1 s au boot (`carte_attente_ecran_ms: "0"`, reset direct sur `GPIO33`).
3. **Tactile GT911** : Toujours garder `reset_pin: GPIO23`.
4. **Audio ES8311** : Ne jamais dépasser `volume_max: 75%` (`0xBF` = 0 dB) pour éviter la saturation (+32 dB à 100 %). Ampli `NS4150B` sur `GPIO53`.
5. **LVGL 9.5** : Garder les modales fermées garées à `translate_y = 10000` (`tab5_registry.cpp`) pour éviter le parcours `blur_walk_cb`.
