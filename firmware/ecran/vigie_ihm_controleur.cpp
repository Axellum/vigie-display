/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_ihm_controleur.cpp
 * @role Implémentation C++ standard pur (zéro allocation dynamique) du
 *       contrôleur IHM Multi-Écrans :
 *       - Navigation circulaire inter-zones (rouleau) et sélection directe GPS.
 *       - Préparation de la fiche complète « Détail Catastrophe » / « Urgence »
 *         (DMS, Nautique, Magnitude/Vents, Distance km/NM, Cap ° + Cardinal,
 *         Compte à rebours `ETA ~XX min` Tsunami et consignes de sécurité).
 *       - Séquenceur de profils sonores RTTTL calibrés par catégorie/sévérité
 *         avec plafond matériel sécurisé `<= 75 %` (0 dB sans saturation sur
 *         l'ES8311 de la Waveshare 7B).
 * @architecture_constraint Aucune dépendance à ESPHome, ESP-IDF ou LVGL.
 *       Compilable et testable nativement sur PC (g++ / pytest).
 * ============================================================================
 */
#include "vigie_ihm_controleur.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Vitesse moyenne de propagation d'une onde tsunami en océan profond (~700 km/h)
constexpr float kVitesseTsunamiKmParMin = 11.6666667f;

// Mélodies RTTTL distinctes par famille de risque majeur
constexpr const char* kRtttlTsunami =
    "tsunami:d=8,o=6,b=180:c,e,g,c7,p,c,e,g,c7,p,4g,4c7";
constexpr const char* kRtttlSeisme =
    "seisme:d=16,o=6,b=200:a,p,a,p,a,p,f,p,a,p,a,p,4d7";
constexpr const char* kRtttlOuraganMarin =
    "ouragan:d=8,o=5,b=160:4d,4a,4d6,8a,8f,4d,p,4d,4a,4d6";
constexpr const char* kRtttlVolcanSolaire =
    "volcan:d=8,o=6,b=150:e,b,e7,b,e,p,g,d7,g";
constexpr const char* kRtttlSilence = "";

void copier_chaine_ihm(char* dst, size_t dst_size, const char* src) {
    if (dst == nullptr || dst_size == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

}  // namespace

int8_t vigie_zone_suivante(VigieEtatMondial& etat) {
    if (etat.nb_zones == 0) {
        etat.zone_active_idx = 0;
        return 0;
    }
    const int nb = static_cast<int>(etat.nb_zones <= kVigieMaxZones ? etat.nb_zones : kVigieMaxZones);
    int cur = static_cast<int>(etat.zone_active_idx);
    if (cur < 0 || cur >= nb) {
        cur = 0;
    } else {
        cur = (cur + 1) % nb;
    }
    etat.zone_active_idx = static_cast<int8_t>(cur);
    vigie_recalculer_distances_gps(etat);
    return etat.zone_active_idx;
}

int8_t vigie_zone_precedente(VigieEtatMondial& etat) {
    if (etat.nb_zones == 0) {
        etat.zone_active_idx = 0;
        return 0;
    }
    const int nb = static_cast<int>(etat.nb_zones <= kVigieMaxZones ? etat.nb_zones : kVigieMaxZones);
    int cur = static_cast<int>(etat.zone_active_idx);
    if (cur < 0 || cur >= nb) {
        cur = 0;
    } else {
        cur = (cur + nb - 1) % nb;
    }
    etat.zone_active_idx = static_cast<int8_t>(cur);
    vigie_recalculer_distances_gps(etat);
    return etat.zone_active_idx;
}

bool vigie_selectionner_zone_gps(VigieEtatMondial& etat) {
    const uint8_t nb = (etat.nb_zones <= kVigieMaxZones) ? etat.nb_zones : kVigieMaxZones;
    for (uint8_t i = 0; i < nb; ++i) {
        const VigieZoneMeteo& z = etat.zones[i];
        if (z.est_zone_gps_mobile ||
            std::strcmp(z.id, "GPS") == 0 ||
            std::strcmp(z.id, "BATEAU") == 0 ||
            std::strcmp(z.id, "LIVE") == 0) {
            etat.zone_active_idx = static_cast<int8_t>(i);
            vigie_recalculer_distances_gps(etat);
            return true;
        }
    }
    return false;
}

const char* vigie_libelle_categorie(VigieCategorieAlerte categorie) {
    switch (categorie) {
        case VigieCategorieAlerte::SEISME:
            return "SÉISME";
        case VigieCategorieAlerte::TSUNAMI:
            return "TSUNAMI";
        case VigieCategorieAlerte::VOLCAN:
            return "VOLCAN";
        case VigieCategorieAlerte::OURAGAN:
            return "OURAGAN";
        case VigieCategorieAlerte::TORNADE:
            return "TORNADE";
        case VigieCategorieAlerte::INONDATION:
            return "INONDATION";
        case VigieCategorieAlerte::PLUIE_EXTREME:
            return "PLUIE EXTRÊME";
        case VigieCategorieAlerte::FEU:
            return "FEU DE FORÊT";
        case VigieCategorieAlerte::TEMPETE_SOLAIRE:
            return "TEMPÊTE SOLAIRE";
        default:
            return "ALERTE";
    }
}

const char* vigie_libelle_severite(VigieSeverite severite) {
    switch (severite) {
        case VigieSeverite::ROUGE:
            return "ROUGE · CRITIQUE";
        case VigieSeverite::ORANGE:
            return "ORANGE · SÉVÈRE";
        case VigieSeverite::JAUNE:
            return "JAUNE · VIGILANCE";
        case VigieSeverite::VERT:
        default:
            return "VERT · INFO";
    }
}

bool vigie_preparer_fiche_detail_alerte(
    const VigieEtatMondial& etat,
    int index_alerte,
    VigieFicheDetailAlerte& fiche_out) {
    fiche_out = VigieFicheDetailAlerte{};
    const int nb = static_cast<int>(etat.nb_alertes <= kVigieMaxAlertes ? etat.nb_alertes : kVigieMaxAlertes);
    if (index_alerte < 0 || index_alerte >= nb) {
        return false;
    }

    const VigieAlerteCatastrophe& a = etat.alertes[index_alerte];
    fiche_out.valide = true;
    fiche_out.index_alerte = static_cast<int8_t>(index_alerte);
    fiche_out.categorie = a.categorie;
    fiche_out.severite = a.severite;
    copier_chaine_ihm(fiche_out.id, sizeof(fiche_out.id), a.id);
    copier_chaine_ihm(fiche_out.badge_categorie, sizeof(fiche_out.badge_categorie), vigie_libelle_categorie(a.categorie));
    copier_chaine_ihm(fiche_out.badge_severite, sizeof(fiche_out.badge_severite), vigie_libelle_severite(a.severite));

    if (a.titre[0] != '\0') {
        copier_chaine_ihm(fiche_out.titre, sizeof(fiche_out.titre), a.titre);
    } else {
        std::snprintf(fiche_out.titre, sizeof(fiche_out.titre), "%s — %s",
                      vigie_libelle_categorie(a.categorie), a.id);
    }

    if (a.sources[0] != '\0') {
        std::snprintf(fiche_out.sources, sizeof(fiche_out.sources), "Sources : %s", a.sources);
    } else {
        copier_chaine_ihm(fiche_out.sources, sizeof(fiche_out.sources), "Sources : USGS, PTWC, GDACS");
    }

    vigie_formater_coord_dms(a.lat, a.lon, fiche_out.coord_dms, sizeof(fiche_out.coord_dms));
    vigie_formater_coord_nautique(a.lat, a.lon, fiche_out.coord_nautique, sizeof(fiche_out.coord_nautique));

    // Formatage de la métrique principale selon la catégorie de catastrophe
    switch (a.categorie) {
        case VigieCategorieAlerte::SEISME:
        case VigieCategorieAlerte::TSUNAMI:
            std::snprintf(fiche_out.metrique_principale, sizeof(fiche_out.metrique_principale),
                          "Mag %.1f Mw · Prof. %.0f km", a.valeur_principale, a.profondeur_km);
            break;
        case VigieCategorieAlerte::OURAGAN:
        case VigieCategorieAlerte::TORNADE:
            std::snprintf(fiche_out.metrique_principale, sizeof(fiche_out.metrique_principale),
                          "Vents %.0f km/h (%.0f kt)",
                          a.valeur_principale, vigie_kmh_vers_noeuds(a.valeur_principale));
            break;
        case VigieCategorieAlerte::VOLCAN:
            std::snprintf(fiche_out.metrique_principale, sizeof(fiche_out.metrique_principale),
                          "Indice VEI %.1f · Panache %.0f km", a.valeur_principale, a.profondeur_km);
            break;
        case VigieCategorieAlerte::INONDATION:
        case VigieCategorieAlerte::PLUIE_EXTREME:
            std::snprintf(fiche_out.metrique_principale, sizeof(fiche_out.metrique_principale),
                          "Intensité %.1f mm/h", a.valeur_principale);
            break;
        case VigieCategorieAlerte::FEU:
            std::snprintf(fiche_out.metrique_principale, sizeof(fiche_out.metrique_principale),
                          "Puissance radiative %.0f MW", a.valeur_principale);
            break;
        case VigieCategorieAlerte::TEMPETE_SOLAIRE:
            std::snprintf(fiche_out.metrique_principale, sizeof(fiche_out.metrique_principale),
                          "Indice Géomagnétique Kp %.1f", a.valeur_principale);
            break;
    }

    // Distance orthodromique en km et en milles nautiques (NM)
    const double dist_km = (a.distance_km >= 0.0f && std::isfinite(a.distance_km))
                               ? static_cast<double>(a.distance_km)
                               : 0.0;
    const double dist_nm = vigie_km_vers_nm(dist_km);
    if (dist_km < 10.0) {
        std::snprintf(fiche_out.distance_km_nm, sizeof(fiche_out.distance_km_nm),
                      "%.1f km (%.1f NM)", dist_km, dist_nm);
    } else {
        std::snprintf(fiche_out.distance_km_nm, sizeof(fiche_out.distance_km_nm),
                      "%.0f km (%.0f NM)", dist_km, dist_nm);
    }

    // Relèvement vrai (Cap ° + Point cardinal)
    int cap_int = static_cast<int>(std::lround(a.azimut_deg)) % 360;
    if (cap_int < 0) cap_int += 360;
    std::snprintf(fiche_out.relevement_cap, sizeof(fiche_out.relevement_cap),
                  "Cap %03d\xC2\xB0 %s", cap_int, vigie_point_cardinal(a.azimut_deg));

    // Compte à rebours ETA Tsunami et consigne de sécurité
    const bool tsunami_actif = a.alerte_tsunami || (a.categorie == VigieCategorieAlerte::TSUNAMI);
    fiche_out.alerte_tsunami = tsunami_actif;
    if (tsunami_actif) {
        int eta = static_cast<int>(a.eta_tsunami_min);
        if (eta <= 0 && dist_km > 0.0) {
            eta = static_cast<int>(std::lround(dist_km / static_cast<double>(kVitesseTsunamiKmParMin)));
        }
        if (eta < 1) eta = 1;
        fiche_out.eta_tsunami_min = static_cast<int16_t>(eta > 32767 ? 32767 : eta);
        std::snprintf(fiche_out.eta_tsunami, sizeof(fiche_out.eta_tsunami),
                      "ETA ~%d min (Onde Tsunami)", static_cast<int>(fiche_out.eta_tsunami_min));
        copier_chaine_ihm(
            fiche_out.consigne_securite,
            sizeof(fiche_out.consigne_securite),
            "ALERTE TSUNAMI : Gagner les fonds > 100 m au large ou rejoindre un point haut > 30 m immédiatement.");
    } else {
        fiche_out.eta_tsunami_min = -1;
        copier_chaine_ihm(fiche_out.eta_tsunami, sizeof(fiche_out.eta_tsunami), "Aucun avis tsunami");
        if (a.categorie == VigieCategorieAlerte::OURAGAN || a.categorie == VigieCategorieAlerte::TORNADE) {
            copier_chaine_ihm(
                fiche_out.consigne_securite,
                sizeof(fiche_out.consigne_securite),
                "CONSIGNE MARINE : Éviter le demi-cercle dangereux, sécuriser le pont et surveiller la chute barométrique.");
        } else if (a.categorie == VigieCategorieAlerte::SEISME) {
            copier_chaine_ihm(
                fiche_out.consigne_securite,
                sizeof(fiche_out.consigne_securite),
                "CONSIGNE SÉISME : Surveiller les répliques et les bulletins côtiers PTWC / GDACS.");
        } else {
            copier_chaine_ihm(
                fiche_out.consigne_securite,
                sizeof(fiche_out.consigne_securite),
                "Surveiller l'évolution du phénomène et maintenir la veille VHF Canal 16 / GNSS.");
        }
    }

    return true;
}

VigieProfilSonoreAlerte vigie_profil_sonore_pour_alerte(
    VigieCategorieAlerte categorie,
    VigieSeverite severite,
    bool alerte_tsunami) {
    VigieProfilSonoreAlerte p{};
    p.categorie = categorie;
    p.severite = severite;

    if (severite == VigieSeverite::VERT && !alerte_tsunami) {
        p.actif = false;
        p.volume_pct = 0;
        p.nb_repetitions = 0;
        p.pause_inter_cycle_ms = 0;
        p.nom_profil = "Silence (Vert)";
        p.rtttl = kRtttlSilence;
        return p;
    }

    p.actif = true;
    // Calibration stricte du volume <= 75 % (0 dB sans saturation sur l'ES8311 de la Waveshare 7B)
    if (alerte_tsunami || severite == VigieSeverite::ROUGE) {
        p.volume_pct = kVigieVolumeMaxSecurisePct;  // 75 % max sécurisé
        p.nb_repetitions = 3;
        p.pause_inter_cycle_ms = 1200;
    } else if (severite == VigieSeverite::ORANGE) {
        p.volume_pct = 68;
        p.nb_repetitions = 2;
        p.pause_inter_cycle_ms = 1800;
    } else {
        p.volume_pct = 55;
        p.nb_repetitions = 1;
        p.pause_inter_cycle_ms = 2500;
    }

    if (p.volume_pct > kVigieVolumeMaxSecurisePct) {
        p.volume_pct = kVigieVolumeMaxSecurisePct;
    }

    if (alerte_tsunami || categorie == VigieCategorieAlerte::TSUNAMI) {
        p.nom_profil = "Sirène Tsunami Tri-Ton";
        p.rtttl = kRtttlTsunami;
        return p;
    }

    switch (categorie) {
        case VigieCategorieAlerte::SEISME:
            p.nom_profil = "Alerte Sismique Rapide";
            p.rtttl = kRtttlSeisme;
            break;
        case VigieCategorieAlerte::OURAGAN:
        case VigieCategorieAlerte::TORNADE:
        case VigieCategorieAlerte::INONDATION:
        case VigieCategorieAlerte::PLUIE_EXTREME:
            p.nom_profil = "Alerte Ouragan & Grain Marin";
            p.rtttl = kRtttlOuraganMarin;
            break;
        case VigieCategorieAlerte::VOLCAN:
        case VigieCategorieAlerte::FEU:
        case VigieCategorieAlerte::TEMPETE_SOLAIRE:
        default:
            p.nom_profil = "Alerte Volcan & Météo Spatiale";
            p.rtttl = kRtttlVolcanSolaire;
            break;
    }

    return p;
}

VigieProfilSonoreAlerte vigie_profil_sonore_pour_catastrophe(
    const VigieAlerteCatastrophe& alerte) {
    return vigie_profil_sonore_pour_alerte(alerte.categorie, alerte.severite, alerte.alerte_tsunami);
}

bool vigie_ihm_ouvrir_detail_alerte(
    const VigieEtatMondial& etat,
    int index_alerte,
    VigieEtatIhm& ihm) {
    if (!vigie_preparer_fiche_detail_alerte(etat, index_alerte, ihm.fiche_courante)) {
        return false;
    }
    ihm.alerte_selectionnee_idx = static_cast<int8_t>(index_alerte);
    ihm.popup_detail_ouvert = true;
    ihm.profil_sonore_courant = vigie_profil_sonore_pour_catastrophe(etat.alertes[index_alerte]);
    return true;
}

void vigie_ihm_fermer_detail_alerte(VigieEtatIhm& ihm) {
    ihm.popup_detail_ouvert = false;
}

bool vigie_ihm_declencher_popup_urgence(
    const VigieEtatMondial& etat,
    const VigieDecisionUrgence& decision,
    VigieEtatIhm& ihm) {
    if (!decision.declencher || decision.alerte_idx < 0 ||
        decision.alerte_idx >= static_cast<int8_t>(etat.nb_alertes)) {
        return false;
    }
    if (!vigie_preparer_fiche_detail_alerte(etat, decision.alerte_idx, ihm.fiche_courante)) {
        return false;
    }
    ihm.alerte_selectionnee_idx = decision.alerte_idx;
    ihm.popup_urgence_ouvert = true;
    ihm.popup_detail_ouvert = true;
    ihm.profil_sonore_courant = vigie_profil_sonore_pour_catastrophe(etat.alertes[decision.alerte_idx]);
    ihm.alarme_sonore_en_cours = decision.alerte_sonore && ihm.profil_sonore_courant.actif;
    return true;
}

bool vigie_ihm_acquitter_urgence_active(
    VigieEtatMondial& etat,
    VigieEtatIhm& ihm) {
    bool ack_ok = false;
    if (ihm.fiche_courante.valide && ihm.fiche_courante.id[0] != '\0') {
        ack_ok = vigie_acquitter_alerte(etat, ihm.fiche_courante.id);
    } else if (ihm.alerte_selectionnee_idx >= 0 &&
               ihm.alerte_selectionnee_idx < static_cast<int8_t>(etat.nb_alertes)) {
        ack_ok = vigie_acquitter_alerte(etat, etat.alertes[ihm.alerte_selectionnee_idx].id);
    }
    ihm.alarme_sonore_en_cours = false;
    ihm.popup_urgence_ouvert = false;
    ihm.popup_detail_ouvert = false;
    return ack_ok;
}

VigieEtatIhm g_vigie_etat_ihm{};
