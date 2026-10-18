# Tâches — Vigie Mondiale 7B (`alerte_mondiale_7b`)

`maj: 2026-10-18` · **Convention** : `[statut] #id — lot — prio — description — dépend`
**Statuts** : `⏳ todo` · `🔄 wip` · `🧪 test` · `✅ done` · `🧊 gelé`

---

## Lot 1 — Socle Matériel Autonome Multi-Écrans (`waveshare-7b` & `tab5`)
- ✅ `#E01` — lot1 — **P0** — Créer le dépôt local `alerte_mondiale_7b`, l'arborescence et la documentation complète (`OBJECTIFS`, `PLAN`, `HISTORIQUE_DEV`, `DECISIONS`, `BRIQUES_REPRISES_TAB5`).
- ✅ `#E02` — lot1 — **P0** — Extraire les briques C++ (`socle/`, `ecran/`), polices (`fonts/`) et paquets Waveshare 7B depuis `00ProjetTab` sans les jeux ni la domotique.
- ✅ `#E03` — lot1 — **P0** — Épurer `carte-waveshare-7b.yaml` des bouchons domotiques hérités du Tab5 (`ina226_batterie`, `headphone_detect`, `wifi_power`...), ajouter le support Multi-Écrans (`carte-tab5.yaml`, `geometrie-tab5.yaml`, `tab5-hardware.yaml`) et créer `vigie-gps-nmea.yaml` + `vigie-mondiale-7b.yaml` — dépend `#E02`.
- ✅ `#E03b` — lot1 — **P0** — Rédiger `docs/audits/BASE_CONNAISSANCE_WAVESHARE_7B.md` et `docs/audits/AUDIT_GPS_ET_MODE_MARINE_7B.md` (incluant M5Stack Unit GPS SMA sur Port A Grove du Tab5) — dépend `#E01`.
- ⏳ `#E04` — lot1 — **P1** — Vérifier la compilation locale autonome ESPHome 2026.9.1 (`1024×600` et `1280×720`, `rotation: 0`) sans connexion Home Assistant — dépend `#E03`.

## Lot 2 — Modèle de Données Mondial, Marine, Parseur GPS NMEA & NVS
- ✅ `#E05` — lot2 — **P0** — Créer `firmware/socle/vigie_modele_mondial.h/.cpp` (structures `VigieGpsFix`, `VigieZoneMeteo`, `VigieEtatMarin`, `VigieAlerteCatastrophe`, décodeur NMEA `$GxGGA`/`$GxRMC`, Haversine/Azimut temps réel, profils Multi-Écrans) + tests unitaires PC (`tests/test_socle_et_gps.py`) — dépend `#E02`.
- ✅ `#E06` — lot2 — **P0** — Implémenter le décodeur de trame API zéro-allocation (`vigie_decoder_trame_api`), la projection cartographique Multi-Écrans (`vigie_projeter_coord_carte`) et la persistance binaire NVS signée CRC32 (`vigie_preparer_bloc_nvs` / `vigie_restaurer_bloc_nvs`) — dépend `#E05`.

## Lot 3 — Client API Direct (HTTPS / REST Embarqué)
- ✅ `#E07` — lot3 — **P0** — Implémenter `firmware/ecran/vigie_client_api.h/.cpp` et `firmware/paquets/vigie-client-api.yaml` (construction dynamique d'URL `/v1/embedded/frame` avec GPS/Marine, fraîcheur Live vs Cache NVS, déclenchement d'urgence) — dépend `#E05` et `#E06`.

## Lot 4, 5 & 6 — Interface « Centre de Situation », Urgence DEFCON & Rendu Hors-Tablette
- ✅ `#E08` — lot4 — **P0** — Concevoir la géométrie et la projection de l'écran principal Multi-Écrans (`1024×600` & `1280×720`) : Carte mondiale des risques actifs + Panneau Météo/Pluie 60 min/Marine + Ticker d'alertes Haversine — dépend `#E05`.
- ✅ `#E09` — lot5 — **P0** — Implémenter la logique C++ du mode Urgence DEFCON (`vigie_evaluer_declenchement_alerte` + anti-répétition `vigie_acquitter_alerte`, réveil dalle + alerte sonore) — dépend `#E08`.
- ✅ `#E10` — lot6 — **P0** — Créer le générateur de rendu hors-tablette PC (`tools/generer_rendu_ihm.py`) produisant les aperçus SVG/PNG en `1024×600` (`waveshare-7b`) et `1280×720` (`tab5`) dans `docs/rendus/` — dépend `#E08`.

