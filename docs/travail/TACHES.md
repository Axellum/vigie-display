# Tâches — Vigie Mondiale 7B (`alerte_mondiale_7b`)

`maj: 2026-10-18` · **Convention** : `[statut] #id — lot — prio — description — dépend`
**Statuts** : `⏳ todo` · `🔄 wip` · `🧪 test` · `✅ done` · `🧊 gelé`

---

## Lot 1 — Socle Matériel Autonome Waveshare 7B
- ✅ `#E01` — lot1 — **P0** — Créer le dépôt local `alerte_mondiale_7b`, l'arborescence et la documentation complète (`OBJECTIFS`, `PLAN`, `HISTORIQUE_DEV`, `DECISIONS`, `BRIQUES_REPRISES_TAB5`).
- ✅ `#E02` — lot1 — **P0** — Extraire les briques C++ (`socle/`, `ecran/`), polices (`fonts/`) et paquets Waveshare 7B depuis `00ProjetTab` sans les jeux ni la domotique.
- ⏳ `#E03` — lot1 — **P0** — Épurer `carte-waveshare-7b.yaml` des bouchons domotiques hérités du Tab5 (`ina226_batterie`, `headphone_detect`, `wifi_power`...) et créer le point d'entrée autonome `vigie-mondiale-7b.yaml` — dépend `#E02`.
- ⏳ `#E04` — lot1 — **P1** — Vérifier la compilation locale autonome ESPHome 2026.9.1 (`1024×600`, `rotation: 0`) sans connexion Home Assistant — dépend `#E03`.

## Lot 2 — Modèle de Données Mondial & Parseur
- ⏳ `#E05` — lot2 — **P0** — Créer `firmware/socle/vigie_modele_mondial.h` (structures `WorldZoneWeather` pour 10 zones mondiales + `NaturalHazardAlert` pour séismes, tsunamis, volcans, cyclones, tornades, inondations, feux, météo spatiale) + tests unitaires PC — dépend `#E02`.
- ⏳ `#E06` — lot2 — **P1** — Adapter la persistance NVS (`tab5_forecast.cpp` + alertes) au modèle multi-zones mondial — dépend `#E05`.

## Lot 3 — Client API Direct (HTTPS / WebSocket)
- ⏳ `#E07` — lot3 — **P0** — Implémenter `firmware/ecran/vigie_client_api.h/.cpp` (requêtes REST périodiques + écoute push temps réel vers `alerte_mondiale_api`) — dépend `#E05` et `#A03` (côté API).

## Lot 4 & 5 — Interface « Centre de Situation 1024×600 » & Alertes Sonores
- ⏳ `#E08` — lot4 — **P0** — Concevoir l'écran principal 1024×600 : Carte mondiale des risques actifs + Ticker d'alertes (Vert/Jaune/Orange/Rouge) + Rouleau des zones météo mondiales — dépend `#E05`.
- ⏳ `#E09` — lot5 — **P1** — Créer le popup Détail Catastrophe (Séisme / Tsunami / Volcan / Ouragan) et le mode Urgence Rouge (réveil dalle + alerte sonore ES8311) — dépend `#E08`.
- ⏳ `#E10` — lot6 — **P1** — Configurer le rendu hors-tablette PC (`vigie-rendu-host.yaml`) en 1024×600 pour valider visuellement tous les écrans et scénarios d'alerte — dépend `#E08`.
