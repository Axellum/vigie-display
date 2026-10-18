# Objectifs — Vigie Mondiale 7B (`alerte_mondiale_7b`)

`maj: 2026-10-18` · Un objectif = une preuve mesurable.

## O-1 — Autonomie totale (Zéro Home Assistant)
**actif** · depuis 2026-10-18
Le terminal ne dépend d'aucun serveur domotique local. Il se configure en Wi-Fi (portail captif / Improv Serial USB-C) et communique directement en HTTPS / WebSocket (`WSS`) avec l'API centrale [`alerte_mondiale_api`](../../../alerte_mondiale_api/README.md).
* **Preuve attendue** : Démarrage à froid hors réseau HA, connexion directe à l'API d'alertes, réception du flux mondial et affichage complet en `< 6 s`.

## O-2 — Exploitation native du matériel Waveshare 7B (1024×600)
**actif** · hérité des campagnes de mesures du 14–18/10/2026
Tirer parti à 100 % de l'`ESP32-P4-WIFI6-Touch-LCD-7B` (P4 v3.2 @ 400 MHz, 32 Mo PSRAM Hex 200 MHz, dalle MIPI-DSI EK79007 1024×600 en rotation native `0` à ~69 ms/frame, GT911 sur GPIO23, ES8311 plafonné à 75 % = 0 dB, reset CORE `esp_rom_software_reset_system()` pour le C6 SDIO).
* **Preuve attendue** : 0 reboot loop SDIO sur 20 redémarrages logiciels, 1re image affichée en `< 5,6 s`, consommation maîtrisée (~1,96 W à 75 % de luminosité, 1,24 W écran éteint avec `sleep in` 0x10).

## O-3 — Veille Multi-Zones Mondiales (Watchlist Pro)
**actif** · depuis 2026-10-18
Permettre de surveiller simultanément une vue mondiale globale et jusqu'à **10 zones géographiques stratégiques** (villes, ports, usines, régions sismiques) avec heure locale du fuseau, météo actuelle, pluie dans l'heure, vent, qualité de l'air et prévisions 10 jours.
* **Preuve attendue** : Bascule fluide (`< 50 ms`) entre les zones mondiales par glissement tactile (rouleau) et conservation des prévisions en NVS dès la première image au démarrage.

## O-4 — Couverture Multi-Risques & Catastrophes Naturelles
**actif** · depuis 2026-10-18
Afficher et hiérarchiser en temps réel les alertes mondiales normalisées par notre API :
1. **Sismologie & Tsunamis** : Tremblements de terre (magnitude, profondeur, distance) et alertes Tsunami (PTWC / USGS / EMSC).
2. **Volcanologie** : Éruptions actives, niveau d'alerte volcanique et code couleur aviation (GVP / USGS).
3. **Phénomènes Cycloniques & Sévères** : Ouragans, typhons, cyclones, tornades, tempêtes (GDACS / NOAA).
4. **Hydrologie, Feux & Espace** : Pluies extrêmes, inondations, feux de forêts majeurs (NASA FIRMS) et tempêtes géomagnétiques / solaires (indice Kp NOAA SWPC).

## O-5 — Système d'Alerte Critique Prioritaire (Visuel + Sonore)
**actif** · depuis 2026-10-18
En cas d'alerte **ORANGE** ou **ROUGE** (globale majeure ou dans le rayon de geofencing d'une zone surveillée) :
- Sortie immédiate de veille de la dalle MIPI-DSI (`dalle_reveiller`).
- Ouverture du panneau d'urgence dédié avec carte de localisation, nature du danger, horodatage UTC/Local et niveau d'impact.
- Signal sonore différencié sur le haut-parleur amplifié (NS4150B GPIO53 + ES8311), acquittable au toucher.

## O-6 — Internationalisation & Standard Industriel B2B
**actif** · depuis 2026-10-18
Interface multilingue native dès la conception (FR, EN, ES, DE, IT, PT, TR via le moteur `i18n` en table compacte), unités configurables (°C/°F, km/h / kt / mph, hPa / inHg), thèmes Haute Lisibilité Jour/Nuit (bascule en `< 65 ms`), fonctionnement dégradé hors-ligne sur cache NVS.

## 🚫 Hors Scope Explicite
- **Aucune entité domotique locale** (ni lumières, ni volets, ni thermostat de salon, ni capteurs de plantes, ni caméras RTSP maison).
- **Aucun jeu d'arcade** (les 8 consoles du Tab5 sont exclues pour garder une empreinte Flash/RAM dédiée à la cartographie et aux données mondiales).
- **Aucune dépendance au protocole natif ESPHome API de Home Assistant** pour les données métier.
