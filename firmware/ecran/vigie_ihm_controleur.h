/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_ihm_controleur.h
 * @role Contrôleur IHM Multi-Écrans (Waveshare 7B 1024×600 & M5Stack Tab5 V2
 *       1280×720), navigation inter-zones en rouleau, formatage des fiches
 *       modales « Détail Catastrophe » et « Urgence Critique Tsunami/Séisme »,
 *       et séquenceur de profils sonores RTTTL calibrés sans saturation
 *       (<= 75 % sur ES8311 = 0 dB sans écrêtage).
 * @architecture_constraint 100 % C++ standard pur, ZÉRO allocation dynamique
 *       (ni malloc, ni new, ni std::string), testable nativement sur PC
 *       (g++ / pytest) comme sur ESP32-P4 @ 400 MHz.
 * ============================================================================
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "../socle/vigie_modele_mondial.h"

// Plafond matériel de sécurité audio sur ES8311 (Waveshare 7B) et ES8388 (Tab5) :
// <= 75 % correspond à 0 dB sans distorsion harmonique ni écrêtage de l'ampli.
constexpr uint8_t kVigieVolumeMaxSecurisePct = 75;

// ─── 1. Fiche formatée pour le Popup « Détail Catastrophe » / « Urgence » ───

struct VigieFicheDetailAlerte {
    bool valide = false;
    int8_t index_alerte = -1;
    VigieCategorieAlerte categorie = VigieCategorieAlerte::SEISME;
    VigieSeverite severite = VigieSeverite::VERT;
    bool alerte_tsunami = false;
    int16_t eta_tsunami_min = -1;

    char id[kVigieIdMax] = "";
    char badge_categorie[32] = "";      // ex: "TSUNAMI", "SÉISME", "OURAGAN"
    char badge_severite[32] = "";       // ex: "ROUGE · CRITIQUE", "ORANGE · SÉVÈRE"
    char titre[kVigieTitreAlerteMax] = "";
    char sources[64] = "";              // ex: "Sources : USGS, PTWC, GDACS"
    char coord_dms[40] = "";            // ex: "38°06'00\"N 142°51'36\"E"
    char coord_nautique[40] = "";       // ex: "38°06.00'N 142°51.60'E"
    char metrique_principale[64] = "";  // ex: "Mag 7.4 Mw · Prof. 32.0 km" ou "Vents 225 km/h (121 kt)"
    char distance_km_nm[64] = "";       // ex: "9714 km (5245 NM)"
    char relevement_cap[48] = "";       // ex: "Cap 033° NNE"
    char eta_tsunami[48] = "";          // ex: "ETA ~72 min" si alerte_tsunami est actif
    char consigne_securite[128] = "";   // Consigne opérationnelle marine / terrestre
};

// ─── 2. Profil Sonore d'Alerte Calibré (RTTTL & Volume <= 75 %) ─────────────

struct VigieProfilSonoreAlerte {
    bool actif = false;
    VigieCategorieAlerte categorie = VigieCategorieAlerte::SEISME;
    VigieSeverite severite = VigieSeverite::VERT;
    uint8_t volume_pct = 0;             // Toujours <= 75 % (kVigieVolumeMaxSecurisePct)
    uint8_t nb_repetitions = 0;         // Nombre de salves sonores
    uint16_t pause_inter_cycle_ms = 0;  // Pause entre deux salves (ms)
    const char* nom_profil = "";        // Libellé lisible du profil sonore
    const char* rtttl = "";             // Chaîne RTTTL standard pour le composant ESPHome rtttl
};

// ─── 3. État du Contrôleur IHM (Modales & Alerte Active) ────────────────────

struct VigieEtatIhm {
    bool popup_detail_ouvert = false;
    bool popup_urgence_ouvert = false;
    bool alarme_sonore_en_cours = false;
    int8_t alerte_selectionnee_idx = -1;
    VigieFicheDetailAlerte fiche_courante{};
    VigieProfilSonoreAlerte profil_sonore_courant{};
};

// ─── Navigation Inter-Zones (Rouleau) ───────────────────────────────────────

// Passe à la zone suivante dans le rouleau `(zone_active_idx + 1) % nb_zones`,
// recalcule les distances/azimuts et renvoie le nouvel index actif.
int8_t vigie_zone_suivante(VigieEtatMondial& etat);

// Passe à la zone précédente dans le rouleau `(zone_active_idx + nb_zones - 1) % nb_zones`,
// recalcule les distances/azimuts et renvoie le nouvel index actif.
int8_t vigie_zone_precedente(VigieEtatMondial& etat);

// Sélectionne directement la zone GPS mobile (`est_zone_gps_mobile == true` ou ID `GPS`/`BATEAU`/`LIVE`).
// Renvoie true si une zone GPS mobile a été trouvée et activée.
bool vigie_selectionner_zone_gps(VigieEtatMondial& etat);

// ─── Fiche Détail Catastrophe & Profils Sonores ─────────────────────────────

// Retourne le libellé français court d'une catégorie d'alerte ("SÉISME", "TSUNAMI", "OURAGAN"...).
const char* vigie_libelle_categorie(VigieCategorieAlerte categorie);

// Retourne le libellé français d'une sévérité ("ROUGE · CRITIQUE", "ORANGE · SÉVÈRE"...).
const char* vigie_libelle_severite(VigieSeverite severite);

// Formate l'intégralité des champs textuels du popup Détail Catastrophe / Urgence
// pour `etat.alertes[index_alerte]` sans aucune allocation dynamique.
bool vigie_preparer_fiche_detail_alerte(
    const VigieEtatMondial& etat,
    int index_alerte,
    VigieFicheDetailAlerte& fiche_out);

// Associe à une catégorie et une sévérité d'alerte un profil sonore RTTTL distinct
// avec volume plafonné à `<= 75 %` (0 dB sans saturation sur l'ES8311 de la Waveshare 7B).
VigieProfilSonoreAlerte vigie_profil_sonore_pour_alerte(
    VigieCategorieAlerte categorie,
    VigieSeverite severite,
    bool alerte_tsunami = false);

// Surcharge pratique prenant directement une structure `VigieAlerteCatastrophe`.
VigieProfilSonoreAlerte vigie_profil_sonore_pour_catastrophe(
    const VigieAlerteCatastrophe& alerte);

// ─── Orchestration des Modales Détail & Urgence Critique ────────────────────

// Ouvre le popup « Détail Catastrophe » sur l'alerte `index_alerte`.
bool vigie_ihm_ouvrir_detail_alerte(
    const VigieEtatMondial& etat,
    int index_alerte,
    VigieEtatIhm& ihm);

// Ferme le popup « Détail Catastrophe ».
void vigie_ihm_fermer_detail_alerte(VigieEtatIhm& ihm);

// Déclenche l'ouverture du popup « Urgence Critique » et arme le profil sonore RTTTL
// à partir d'une décision d'urgence issue de `vigie_evaluer_declenchement_alerte()`.
bool vigie_ihm_declencher_popup_urgence(
    const VigieEtatMondial& etat,
    const VigieDecisionUrgence& decision,
    VigieEtatIhm& ihm);

// Acquitte l'alerte urgente active dans `etat`, coupe l'alarme sonore et ferme la modale d'urgence.
bool vigie_ihm_acquitter_urgence_active(
    VigieEtatMondial& etat,
    VigieEtatIhm& ihm);

extern VigieEtatIhm g_vigie_etat_ihm;
