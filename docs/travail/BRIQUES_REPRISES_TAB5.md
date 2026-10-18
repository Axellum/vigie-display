# Briques Reprises du Projet Tab5 vs Éléments Purgés

`maj: 2026-10-18` · Traçabilité de l'extraction initiale depuis `H:\AuxFilsDesIdees\00ProjetTab`.

---

## ✅ 1. Ce qui a été copié et conservé dans `alerte_mondiale_7b`

### A. Couche Matérielle & Système (`firmware/paquets/` & `firmware/ecran/`)
| Fichier | Rôle dans Vigie Mondiale 7B |
| :--- | :--- |
| `carte-waveshare-7b.yaml` | Configuration matérielle complète de la Waveshare 7" (P4 400 MHz, MIPI-DSI EK79007, GT911 GPIO23, ES8311 75 %, C6 SDIO, PSRAM 200 MHz) — *à épurer des bouchons domotiques Tab5 au Lot 1* |
| `geometrie-waveshare-7b.yaml` | Dimensions 1024×600, échelle de polices `0.82` et rotation |
| `carte_7b_version.h` | Garde-fou de compilation (`static_assert` ESPHome >= 2026.9.1 requis pour l'horloge DSI du P4 v3) |
| `tab5_lien_c6.h/.cpp` | Reset CORE `esp_rom_software_reset_system()` garantissant la stabilité du lien SDIO vers l'ESP32-C6 |
| `tab5_dalle.h/.cpp` | Extinction physique MIPI-DSI (`0x28` + `0x10`) + voile noir + extinction pendant OTA |
| `tab5_barre.h` & `tab5_barre_demarrage.h/.cpp` | Barre de progression native dessinée sur le framebuffer dès le boot avant l'init LVGL |
| `tab5_demarrage.h/.cpp` | Chronométrage des étapes de boot et optimisation LVGL 9.5 (labels) |
| `tab5_journal.h/.cpp` | Tampon circulaire des journaux système consultable à l'écran |

### B. Socle C++ Pur & Moteur Graphique (`firmware/socle/` & `firmware/ecran/`)
| Fichier | Rôle dans Vigie Mondiale 7B |
| :--- | :--- |
| `tab5_core.h/.cpp`, `tab5_parse.h/.cpp`, `tab5_champs.h/.cpp` | Parseurs sans allocation dynamique (`malloc`-free) pour décoder les trames de l'API |
| `tab5_tokens.h`, `tab5_themes_data.h`, `tab5_theme.h/.cpp` | Système de thèmes Clair/Sombre haute lisibilité (bascule en 65 ms) et tokens de couleurs |
| `tab5_i18n.h/.cpp`, `tab5_i18n_data.h` | Moteur de traduction multilingue (7 langues : FR, EN, ES, DE, IT, PT, TR) |
| `tab5_economie.h/.cpp` | Gestion intelligente du rétroéclairage, veille et cadence d'images |
| `tab5_geometrie.h`, `tab5_decoupe.h` | Calculs géométriques et découpage d'écran sans flottants lourds |
| `tab5_registry.h/.cpp` | Gestionnaire de fenêtres modales avec garage `translate_y = 10000` (anti-ralentissement LVGL 9.5) |
| `tab5_anim.h/.cpp`, `tab5_text.h/.cpp` | Utilitaires d'animation et formatage UTF-8 sécurisé |

### C. Briques Météo & Alertes (`firmware/ecran/` & `firmware/ui_components/`)
| Fichier | Rôle dans Vigie Mondiale 7B |
| :--- | :--- |
| `tab5_forecast.h/.cpp` | Persistance NVS des prévisions météo (affichage instantané dès la 1re image au boot) — *à étendre en multi-zones mondiales* |
| `tab5_meteo.h/.cpp` & `meteo_popup.yaml` | Tracés vectoriels des courbes de température/pluie et vue détaillée 10 jours |
| `tab5_alertes.h/.cpp` & `alertes_popup.yaml` | Socle d'affichage et d'historique des alertes — *à spécialiser pour les catastrophes mondiales (Séismes, Tsunamis, Volcans, Cyclones)* |
| `IconeMeteo.ttf` & `materialdesignicons-webfont.ttf` | Polices d'icônes météo et symboles d'alerte/danger |

---

## 🗑️ 2. Ce qui a été volontairement exclu (Purgé)

Pour garantir un code léger, maintenable et 100 % dédié à l'outil pro mondial :
- **Zéro composant Home Assistant** : `blueprints/`, `custom_components/`, `packages/` HA, `tab5_modele_ha.h`.
- **Zéro domotique résidentielle** : `tab5_tuiles*`, `tab5_maison*`, `tab5_clim*`, `tab5_energie*`, `tab5_froid*`, `tab5_suivi*`, `tab5_reglables*`, `tab5_telecommande*`, `tab5_lecteur*`, `tab5_cameras*`, `tab5_serveur_ia*`, `pot_sensors.yaml`.
- **Zéro console de jeu** : `Tab5/jeux/*` (26 fichiers), `ChessPieces.ttf`, `tab5-arcade.yaml`.
- **Zéro dépendance matérielle propre au M5Stack Tab5** : `INA226`, expandeurs `PI4IOE5V6408`, RTC `RX8130`, IMU `BMI270`, codec `ES8388`.
