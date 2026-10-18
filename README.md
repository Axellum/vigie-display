# Vigie Mondiale 7B — Terminal Pro Météo & Alertes Catastrophes (Waveshare ESP32-P4 7")

> **Statut :** Initialisation architecture & extraction du socle Waveshare 7B (`2026-10-18`)
> **Matériel cible :** Waveshare `ESP32-P4-WIFI6-Touch-LCD-7B` (7" IPS 1024×600 MIPI-DSI EK79007, Tactile GT911, Wi-Fi 6 ESP32-C6 SDIO, Audio ES8311 + NS4150B, 32 Mo Flash, 32 Mo PSRAM Hex 200 MHz, P4 v3.2 @ 400 MHz).
> **Indépendance totale :** **Zéro lien avec Home Assistant**. Connexion directe HTTPS / WebSocket (`WSS`) vers le backend dédié [`alerte_mondiale_api`](../alerte_mondiale_api/README.md).

---

## 🎯 Vision Produit (Outil Pro Mondial)

Transformer la carte **Waveshare 7 pouces ESP32-P4** en une **appliance matérielle autonome de veille environnementale, météorologique et sismique/catastrophes en temps réel** (« Situational Awareness Terminal ») destinée aux professionnels (logistique, sécurité civile, mairies, transport maritime/aérien, sites industriels internationaux) et aux utilisateurs exigeants.

### Fonctionnalités Clés
1. **Surveillance Multi-Zones Mondiales (Watchlist / Geofencing)** :
   - Suivi simultané de **N zones stratégiques** dans le monde (ex. : Siège Europe, Usine Asie, Port Amérique, Zone Pacifique) + vue globale mondiale.
   - Fuseaux horaires multiples, météo locale hyper-précise, radars de précipitations et prévisions à 10 jours / par heure (conservées en NVS dès la 1re image au boot).
2. **Centre d'Alertes Catastrophes Naturelles Temps Réel** :
   - **Séismes** (USGS / EMSC : magnitude, profondeur, épicentre, distance aux actifs surveillés).
   - **Tsunamis** (NOAA PTWC : alertes côtières, niveau de menace).
   - **Volcans** (Smithsonian GVP / USGS : codes couleurs aviation et éruptions en cours).
   - **Ouragans, Typhons, Cyclones & Tornades** (GDACS / NOAA NHC / NWS : catégorie, trajectoire, vents max).
   - **Inondations, Pluies diluviennes, Feux de forêts & Météo Spatiale** (GDACS, NASA FIRMS, NOAA SWPC tempêtes géomagnétiques Kp).
3. **Matrice d'Alerte Critique (Vert / Jaune / Orange / Rouge)** :
   - Réveil automatique de la dalle (`sleep out` MIPI-DSI), bandeau visuel prioritaire et alerte sonore calibrée (codec ES8311 à 0 dB sans saturation) en cas d'événement critique dans une zone surveillée.

---

## 📂 Structure du Dépôt

| Dossier / Fichier | Rôle |
| :--- | :--- |
| [`docs/travail/INDEX.md`](docs/travail/INDEX.md) | Point d'entrée du kit documentaire (Objectifs, Plan, Historique, Décisions, Tâches) |
| [`docs/travail/OBJECTIFS.md`](docs/travail/OBJECTIFS.md) | Objectifs mesurables du produit et critères de validation |
| [`docs/travail/PLAN.md`](docs/travail/PLAN.md) | Plan de développement par phases (Lots 1 à 6) et architecture logicielle |
| [`docs/travail/HISTORIQUE_DEV.md`](docs/travail/HISTORIQUE_DEV.md) | Journal chronologique de développement et héritage technique du projet Tab5 |
| [`docs/travail/BRIQUES_REPRISES_TAB5.md`](docs/travail/BRIQUES_REPRISES_TAB5.md) | Inventaire exact des briques matérielles/LVGL reprises de `00ProjetTab` et des éléments purgés |
| [`docs/travail/DECISIONS.md`](docs/travail/DECISIONS.md) | Décisions d'architecture (ADR) |
| [`docs/travail/TACHES.md`](docs/travail/TACHES.md) | Board des tâches ouvertes du firmware |
| `firmware/socle/` | Socle C++ pur (structures météo/alertes, parseurs zéro-allocation, thèmes, i18n 7 langues, économie) |
| `firmware/ecran/` | Couche matérielle Waveshare 7B (dalle EK79007, reset C6 SDIO, barre boot, LVGL 9.5, NVS) |
| `firmware/paquets/` | Paquets de configuration matérielle et graphique Waveshare 7B |
| `firmware/ui_components/` | Composants d'interface LVGL (cartes météo, popups d'alerte, modales) |
| `firmware/fonts/` | Polices vectorielles (`IconeMeteo.ttf`, `materialdesignicons-webfont.ttf`) |
