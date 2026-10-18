/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_client_api.cpp
 * @role Implémentation du connecteur client autonome vers `alerte_mondiale_api`,
 *       gestion du statut Live / Cache NVS Hors-Ligne et déclenchement d'urgence.
 * ============================================================================
 */
#include "vigie_client_api.h"

#include <cstdio>
#include <cstring>

VigieStatutClientApi g_vigie_statut_api{};
VigieBlocNvs g_vigie_bloc_nvs_cache{};

bool vigie_construire_url_api(
    const char* base_url,
    const char* device_type,
    const VigieGpsFix& gps,
    char* buf,
    size_t taille) {
    if (buf == nullptr || taille < 32 || base_url == nullptr || base_url[0] == '\0') {
        if (buf != nullptr && taille > 0) buf[0] = '\0';
        return false;
    }

    const char* dev = (device_type != nullptr && device_type[0] != '\0') ? device_type : "waveshare-7b";

    // Supprimer un éventuel '/' final sur base_url
    size_t base_len = std::strlen(base_url);
    while (base_len > 0 && base_url[base_len - 1] == '/') {
        --base_len;
    }

    int n = 0;
    if (gps.valide) {
        n = std::snprintf(
            buf,
            taille,
            "%.*s/v1/embedded/frame?device_type=%s&lat=%.5f&lon=%.5f&marine=1",
            static_cast<int>(base_len),
            base_url,
            dev,
            gps.lat_deg,
            gps.lon_deg);
    } else {
        n = std::snprintf(
            buf,
            taille,
            "%.*s/v1/embedded/frame?device_type=%s",
            static_cast<int>(base_len),
            base_url,
            dev);
    }

    return n > 0 && static_cast<size_t>(n) < taille;
}

bool vigie_formater_statut_fraicheur(
    const VigieStatutClientApi& statut,
    uint32_t maintenant_ms,
    char* buf,
    size_t taille) {
    if (buf == nullptr || taille < 16) return false;

    if (statut.en_ligne && statut.compteur_succes > 0) {
        const uint32_t delta_s = (maintenant_ms >= statut.derniere_reception_ms)
                                     ? ((maintenant_ms - statut.derniere_reception_ms) / 1000UL)
                                     : 0UL;
        int n = 0;
        if (delta_s < 60UL) {
            n = std::snprintf(buf, taille, "LIVE \xC2\xB7 il y a %lus", static_cast<unsigned long>(delta_s));
        } else {
            n = std::snprintf(buf, taille, "LIVE \xC2\xB7 il y a %lum", static_cast<unsigned long>(delta_s / 60UL));
        }
        return n > 0 && static_cast<size_t>(n) < taille;
    }

    if (statut.source_nvs_seule) {
        const int n = std::snprintf(buf, taille, "HORS-LIGNE \xC2\xB7 Cache NVS");
        return n > 0 && static_cast<size_t>(n) < taille;
    }

    const int n = std::snprintf(buf, taille, "HORS-LIGNE \xC2\xB7 En attente");
    return n > 0 && static_cast<size_t>(n) < taille;
}

bool vigie_client_api_recevoir_trame(
    const char* trame,
    VigieEtatMondial& etat,
    VigieStatutClientApi& statut,
    uint32_t maintenant_ms,
    VigieDecisionUrgence& out_urgence) {
    out_urgence = VigieDecisionUrgence{};
    if (!vigie_decoder_trame_api(trame, etat)) {
        ++statut.compteur_echecs;
        return false;
    }

    statut.en_ligne = true;
    statut.source_nvs_seule = false;
    statut.derniere_reception_ms = maintenant_ms;
    ++statut.compteur_succes;

    // Mise à jour du bloc NVS prêt à être persisté en Flash
    vigie_preparer_bloc_nvs(etat, g_vigie_bloc_nvs_cache);

    // Évaluation immédiate du besoin de réveil dalle + alerte sonore
    out_urgence = vigie_evaluer_declenchement_alerte(etat);
    return true;
}

bool vigie_client_api_charger_cache_nvs(
    const VigieBlocNvs& bloc_nvs,
    VigieEtatMondial& etat,
    VigieStatutClientApi& statut) {
    if (!vigie_restaurer_bloc_nvs(bloc_nvs, etat)) {
        return false;
    }
    statut.en_ligne = false;
    statut.source_nvs_seule = true;
    return true;
}
