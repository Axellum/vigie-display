# Plan de Développement — Vigie Mondiale 7B (`alerte_mondiale_7b`)

`maj: 2026-10-18` · Architecture modulaire C++ / LVGL 9.5 sur Waveshare 7B (1024×600).

---

## 1. Architecture Logicielle Cible

```mermaid
graph TD
    subgraph API_CLOUD["Backend alerte_mondiale_api (FastAPI)"]
        AGG["Agrégateur Mondial<br/>(USGS, GDACS, NOAA, Open-Meteo, NASA, SWPC)"]
        NORM["Normaliseur & Geofencing<br/>(Payload compact + Flux WebSocket / REST)"]
        AGG --> NORM
    end

    subgraph TERMINAL_7B["Terminal Waveshare 7B (ESP32-P4 @ 400 MHz — 1024×600)"]
        NET["Client Réseau Autonome<br/>(Wi-Fi C6 SDIO + Client HTTPS / WSS)"]
        NVS["Cache NVS Résilient<br/>(Zones, Prévisions, 50 dernières alertes)"]
        PARSE["Parseur Zéro-Allocation (socle/)<br/>(Décodage rapide JSON/Compact -> Modèle C++)"]
        UI["Interface LVGL 9.5 (1024×600)<br/>(Carte Mondiale, Watchlist Zones, Radar Alertes)"]
        AUDIO["Alerteur Sonore & Veille<br/>(ES8311 + NS4150B + Réveil Dalle EK79007)"]

        NORM ==>|HTTPS / WSS| NET
        NET --> PARSE
        PARSE --> NVS
        PARSE --> UI
        PARSE --> AUDIO
    end
```

---

## 2. Découpage en Lots (Feuille de Route)

### Lot 1 — Purge Domotique & Socle Matériel Autonome 7B *(En cours)*
- Extraction des briques matérielles validées de `00ProjetTab` (`carte-waveshare-7b.yaml`, `tab5_dalle`, `tab5_lien_c6`, `tab5_barre_demarrage`, `tab5_demarrage`, `tab5_theme`, `tab5_i18n`).
- Suppression de tous les appels à Home Assistant (`homeassistant.*`, services HA domotiques, jeux, volets, lumières, froid, suivi, caméras).
- Création du point d'entrée autonome `vigie-mondiale-7b.yaml` compilable en 1024×600 (`rotation: 0`).

### Lot 2 — Modèle de Données Mondial (`socle/`) & Contrat API
- Remplacement de `tab5_modele_ha.h` par `vigie_modele_mondial.h` :
  - `WorldZoneWeather` : météo temps réel + prévisions horaires/10j + fuseau UTC offset + indice UV / AQI / Kp pour jusqu'à 10 zones mondiales configurées.
  - `NaturalHazardAlert` : identifiant unique, catégorie (`SEISME`, `TSUNAMI`, `VOLCAN`, `OURAGAN`, `TORNADE`, `INONDATION`, `PLUIE_EXTREME`, `FEU`, `SOLAIRE`), sévérité (`VERT`, `JAUNE`, `ORANGE`, `ROUGE`), latitude/longitude, magnitude/intensité, rayon d'impact, distance à la zone surveillée la plus proche, horodatage UTC.
- Sauvegarde et relecture instantanée en NVS au boot.

### Lot 3 — Connecteur Réseau Direct (`ecran/vigie_client_api.h/.cpp`)
- Client HTTP/REST + flux temps réel (SSE ou WebSocket) natif ESP-IDF vers `alerte_mondiale_api`.
- Gestion automatique de la reconnexion, backoff exponentiel, indicateur de fraîcheur des données (« Live · il y a 12 s » / « Hors-ligne · Cache NVS »).

### Lot 4 — IHM Principale « Centre de Situation 1024×600 »
- **Bandeau supérieur** : Horloge UTC + Heure locale de la zone active, statut connexion Live, compteur d'alertes actives par couleur (Rouge / Orange / Jaune), indice géomagnétique Kp, bouton Paramètres/Langue.
- **Zone Centrale / Carte & Radar d'Alertes** :
  - Carte du monde vectorielle / matricielle sombre avec marqueurs lumineux temps réel des catastrophes actives (cercles concentriques selon la magnitude/sévérité) et position des zones surveillées.
  - Liste déroulante temps réel (Ticker) des dernières alertes sismiques, volcaniques, cycloniques et tsunamis.
- **Bandeau / Rouleau des Zones Mondiales** :
  - Navigation fluide par swipe entre les zones surveillées (ex. Paris, Tokyo, New York, Reykjavik, Singapour...) avec météo détaillée, radar de pluie à 1 h, vent, pression et prévisions 10 jours.

### Lot 5 — Popups Spécialisés & Mode Urgence (DEFCON)
- **Popup Détail Catastrophe** : Fiche complète d'un séisme / volcan / cyclone / tsunami (coordonnées, profondeur, source USGS/GDACS/NOAA, historique des répliques).
- **Popup Météo Mondiale Multi-Zones** : Courbes comparatives et détails horaires.
- **Écran d'Alerte Rouge Prioritaire** : Réveil forcé de l'écran + alerte sonore ES8311 + acquittement tactile.

### Lot 6 — Rendu Hors-Tablette (PC Host 1024×600) & Outillage Pro
- Adaptation du moteur de rendu natif PC (`vigie-rendu-host.yaml`) pour générer et vérifier visuellement tous les écrans 1024×600 avec des scénarios de catastrophes simulés (Séisme M7.8 Japon, Alerte Tsunami Pacifique, Ouragan Cat. 5 Atlantique, Éruption Islande).
- Portail de configuration des zones surveillées (Watchlist) et clés API.
