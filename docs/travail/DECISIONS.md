# Décisions d'Architecture (ADR) — Vigie Mondiale 7B

`maj: 2026-10-18` · Choix tranchés et justifications techniques.

---

## ADR-0001 — Séparation en deux dépôts distincts (`alerte_mondiale_7b` + `alerte_mondiale_api`)
* **Date** : 2026-10-18 · **Statut** : Accepté
* **Contexte** : Le projet vise un produit professionnel mondial composé d'un terminal matériel 7" (`ESP32-P4`) et d'une plateforme d'agrégation de données météo/catastrophes en temps réel.
* **Décision** : Séparer strictement le **firmware de l'écran** (`alerte_mondiale_7b`) et le **backend d'agrégation** (`alerte_mondiale_api`) dans deux dépôts Git indépendants (et tous deux indépendants de `00ProjetTab`).
* **Pourquoi** :
  1. Le terminal ESP32-P4 ne doit jamais interroger directement 10 APIs gouvernementales hétérogènes (USGS, NOAA, GDACS, NASA, Open-Meteo) sous peine de bannissement IP, de saturation RAM par des flux GeoJSON/XML de plusieurs Mo, et de casse au moindre changement de format d'une agence.
  2. Le backend API constitue le cœur de valeur SaaS/B2B (geofencing, normalisation, cache mondial, push temps réel) et peut alimenter simultanément des milliers d'écrans 7B, un dashboard Web de salle de crise ou une application mobile.

## ADR-0002 — Zéro dépendance à Home Assistant
* **Date** : 2026-10-18 · **Statut** : Accepté
* **Contexte** : `00ProjetTab` était un écran esclave de Home Assistant.
* **Décision** : Supprimer toute dépendance à Home Assistant. L'écran est autonome : il se connecte directement au Wi-Fi local et dialogue en HTTPS / WebSocket (`WSS`) avec `alerte_mondiale_api`.
* **Pourquoi** : Un client professionnel (logistique, mairie, sécurité civile, entreprise) veut brancher l'écran en USB-C sur le secteur, renseigner son Wi-Fi et ses zones mondiales à surveiller, et obtenir un système opérationnel en 30 secondes sans installer de serveur domotique.

## ADR-0003 — Ciblage natif 1024×600 (`carte_rotation: 0`) sur Waveshare 7B
* **Date** : 2026-10-18 · **Statut** : Accepté
* **Contexte** : Sur `00ProjetTab`, l'interface avait été pensée en 1280×720 puis adaptée à la 7B.
* **Décision** : Concevoir toute la géométrie de `Vigie Mondiale 7B` **nativement en 1024×600 pixels** avec `carte_rotation: 0` par défaut (et option `180` selon le sens de pose du boîtier).
* **Pourquoi** : Gain de performance mesuré de ~14 ms par image pleine dalle (69 ms au lieu de 83 ms) et alignement au pixel près pour les cartes mondiales et les tableaux de bord d'alertes.

## ADR-0004 — Persistance NVS Multi-Zones & Alertes (Hors-ligne résilient)
* **Date** : 2026-10-18 · **Statut** : Accepté
* **Décision** : Conserver en mémoire flash non-volatile (NVS) la configuration des zones mondiales suivies, leurs dernières prévisions météo et les dernières alertes catastrophes reçues, avec horodatage UTC.
* **Pourquoi** : Affichage instantané dès la première image au démarrage (`< 5,6 s`, avant même que le Wi-Fi C6 ait négocié le bail DHCP à `~9,4 s`) et maintien des informations critiques à l'écran en cas de coupure Internet pendant une catastrophe.

## ADR-0005 — Architecture Multi-Écrans / Multi-Cartes (`waveshare-7b` 1024×600 & `tab5` 1280×720)
* **Date** : 2026-10-18 · **Statut** : Accepté (Directive d'Axel)
* **Contexte** : Le projet doit pouvoir tourner aussi bien sur le terminal 7" Waveshare (`1024×600`) que sur la tablette 5" M5Stack Tab5 V2 (`1280×720`, avec batterie INA226, IMU BMI270, codec ES8388) et de futurs écrans ESP32-P4 (`8"`, `10"`), sans dupliquer le code métier ni maintenir deux firmwares divergents.
* **Décision** : Piloter la cible matérielle par une unique substitution `vigie_carte` (`waveshare-7b` ou `tab5`) qui charge dynamiquement `geometrie-${vigie_carte}.yaml` et `carte-${vigie_carte}.yaml`, tandis que `firmware/socle/tab5_geometrie.h` et `vigie_modele_mondial.h` adaptent automatiquement les dimensions et polices à la résolution cible.
* **Pourquoi** : Un seul code C++ métier, zéro duplication de maintenance, et possibilité d'utiliser le M5Stack Tab5 V2 en mobilité (batterie intégrée + GPS Grove) ou le Waveshare 7B en poste fixe / table à cartes marine.

## ADR-0006 — Décodage GPS NMEA 0183 Autonome & Géométrie Haversine Temps Réel
* **Date** : 2026-10-18 · **Statut** : Accepté
* **Contexte** : En usage mobile ou maritime (bateau en mer, véhicule d'intervention), la position de l'utilisateur change continuellement et le terminal doit pouvoir fonctionner même sans connexion Internet ou avec un bus NMEA 0183 de bord.
* **Décision** : Implémenter dans `firmware/socle/vigie_modele_mondial.cpp` un décodeur NMEA 0183 pur C++ (`$GxGGA`, `$GxRMC` avec validation XOR `*HH`) et un moteur trigonométrique Haversine + Azimut 16 secteurs recalculant en temps réel la distance et le relèvement de chaque catastrophe mondiale par rapport à la position GPS courante. Support matériel sur connecteur `P3` (`GPIO4/5`) ou `RS485` (`GPIO26/27`) pour la Waveshare 7B, et sur **Port A Grove (`GPIO53/54`) avec M5Stack Unit GPS SMA (AT6668)** pour le M5Stack Tab5 V2.

