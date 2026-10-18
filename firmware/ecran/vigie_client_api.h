/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_client_api.h
 * @role Connecteur client autonome vers le backend `alerte_mondiale_api` :
 *       - Construction dynamique de l'URL REST embarquée (`/v1/embedded/frame`)
 *         avec injection automatique de la position GPS/Marine (`lat`, `lon`,
 *         `marine=1`) et du profil d'écran (`device_type=waveshare-7b` ou `tab5`).
 *       - Suivi de fraîcheur des données (Live vs Cache NVS Hors-Ligne).
 *       - Réception de trame, mise à jour du cache NVS signé CRC32 et évaluation
 *         immédiate du déclenchement d'urgence (réveil dalle + alerte sonore).
 * @architecture_constraint 100 % C++ standard pur, ZÉRO allocation dynamique,
 *       compilable sur PC (g++ / clang++) comme sur ESP32-P4.
 * ============================================================================
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "../socle/vigie_modele_mondial.h"

struct VigieStatutClientApi {
    bool en_ligne = false;
    bool source_nvs_seule = false;
    uint32_t derniere_reception_ms = 0;
    uint32_t compteur_succes = 0;
    uint32_t compteur_echecs = 0;
    char derniere_url[256] = "";
};

// Construit l'URL de requête vers `alerte_mondiale_api` :
// - Avec fix GPS actif : "<base_url>/v1/embedded/frame?device_type=<device_type>&lat=48.85650&lon=2.35217&marine=1"
// - Sans fix GPS actif : "<base_url>/v1/embedded/frame?device_type=<device_type>"
bool vigie_construire_url_api(
    const char* base_url,
    const char* device_type,
    const VigieGpsFix& gps,
    char* buf,
    size_t taille);

// Formate l'indicateur de fraîcheur affiché dans le bandeau supérieur :
// ex. "LIVE · il y a 12 s", "HORS-LIGNE · Cache NVS", "EN ATTENTE RÉSEAU"
bool vigie_formater_statut_fraicheur(
    const VigieStatutClientApi& statut,
    uint32_t maintenant_ms,
    char* buf,
    size_t taille);

// Traite une trame reçue de l'API, met à jour `etat`, prépare le cache NVS (`g_vigie_bloc_nvs_cache`)
// et évalue si une alerte critique non acquittée nécessite le réveil de l'écran et la sirène.
bool vigie_client_api_recevoir_trame(
    const char* trame,
    VigieEtatMondial& etat,
    VigieStatutClientApi& statut,
    uint32_t maintenant_ms,
    VigieDecisionUrgence& out_urgence);

// Charge un bloc NVS au démarrage avant la connexion Wi-Fi.
bool vigie_client_api_charger_cache_nvs(
    const VigieBlocNvs& bloc_nvs,
    VigieEtatMondial& etat,
    VigieStatutClientApi& statut);

extern VigieStatutClientApi g_vigie_statut_api;
extern VigieBlocNvs g_vigie_bloc_nvs_cache;
