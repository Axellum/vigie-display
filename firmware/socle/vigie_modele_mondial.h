/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_modele_mondial.h
 * @role Modèle de données C++ pur, géométrie sphérique (Haversine / Azimut) et
 *       décodeur GNSS NMEA 0183 ($GxGGA / $GxRMC) pour le terminal autonome
 *       Vigie Mondiale 7B (Waveshare ESP32-P4-WIFI6-Touch-LCD-7B).
 * @architecture_constraint 100 % C++ standard pur, ZÉRO allocation dynamique
 *       (ni malloc, ni new, ni std::vector, ni std::string), aucune dépendance
 *       à ESPHome, ESP-IDF ou LVGL. Compilable et testable nativement sur PC
 *       (g++ / pytest) comme sur ESP32-P4 @ 400 MHz.
 * ============================================================================
 */
#pragma once

#include <cstddef>
#include <cstdint>

// ─── Capacités statiques du modèle mondial ──────────────────────────────────
constexpr int kVigieMaxZones = 10;
constexpr int kVigieMaxAlertes = 30;
constexpr int kVigieMaxJoursPrevision = 10;
constexpr int kVigieCreneaux15m = 4;
constexpr int kVigieCreneaux5m = 12;

constexpr size_t kVigieIdMax = 32;
constexpr size_t kVigieNomZoneMax = 48;
constexpr size_t kVigieTitreAlerteMax = 96;
constexpr size_t kVigieSourcesMax = 48;

// ─── Énumérations métier (Catastrophes & Sévérités) ─────────────────────────

enum class VigieCategorieAlerte : uint8_t {
    SEISME = 0,
    TSUNAMI = 1,
    VOLCAN = 2,
    OURAGAN = 3,
    TORNADE = 4,
    INONDATION = 5,
    PLUIE_EXTREME = 6,
    FEU = 7,
    TEMPETE_SOLAIRE = 8,
};

enum class VigieSeverite : uint8_t {
    VERT = 0,
    JAUNE = 1,
    ORANGE = 2,
    ROUGE = 3,
};

// Niveau d'activité géomagnétique / tempête solaire (échelle NOAA G0..G5)
enum class VigieNiveauSolaire : uint8_t {
    CALME = 0,  // Kp < 4
    ACTIF = 1,  // Kp = 4
    G1_MINEURE = 2,
    G2_MODEREE = 3,
    G3_FORTE = 4,
    G4_SEVERE = 5,
    G5_EXTREME = 6,
};

// Cartes matérielles ESP32-P4 supportées par l'architecture Multi-Écrans
enum class VigieCarteCible : uint8_t {
    WAVESHARE_7B = 0,  // Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (7" 1024×600, EK79007, ES8311)
    M5STACK_TAB5 = 1,  // M5Stack Tab5 V2 (5" 1280×720, ST7123/ST7121/ILI9881C, ES8388, INA226, BMI270, RX8130)
    ECRAN_P4_HD = 2,   // Futurs écrans ESP32-P4 8" / 10.1" (1280×800 / 1024×600)
};

struct VigieProfilCarte {
    VigieCarteCible carte;
    const char* code;           // "waveshare-7b" ou "tab5"
    const char* nom;            // Nom complet de la carte
    int32_t ecran_l;            // Largeur paysage vue par LVGL (px)
    int32_t ecran_h;            // Hauteur paysage vue par LVGL (px)
    int32_t dalle_l;            // Largeur native du contrôleur MIPI-DSI (px)
    int32_t dalle_h;            // Hauteur native du contrôleur MIPI-DSI (px)
    int16_t rotation_defaut;    // Rotation LVGL par défaut (0, 180 ou 270)
    uint8_t police_pct;         // Facteur d'échelle des polices en % (82 sur 7B, 100 sur Tab5)
    bool a_imu;                 // BMI270 présente
    bool a_rtc_externe;         // RX8130CE présente
    bool a_jauge_ina226;        // INA226 présente
    int8_t gps_uart_rx_defaut;  // GPIO RX par défaut pour le GPS externe (GPIO4 sur P3 7B, GPIO53/Grove sur Tab5)
    int8_t gps_uart_tx_defaut;  // GPIO TX par défaut pour le GPS externe (GPIO5 sur P3 7B, GPIO54/Grove sur Tab5)
};

// Retourne le profil matériel descriptif d'une carte à partir de son code ("waveshare-7b", "tab5")
const VigieProfilCarte& vigie_profil_carte(const char* code_carte);

// ─── 1. Fix GNSS / GPS temps réel (Mode Bateau / Véhicule d'expédition) ─────

struct VigieGpsFix {
    bool valide = false;
    double lat_deg = 0.0;          // Latitude décimale (+N / -S), [-90..+90]
    double lon_deg = 0.0;          // Longitude décimale (+E / -W), [-180..+180]
    float vitesse_noeuds = 0.0f;   // SOG (Speed Over Ground) en nœuds (kt)
    float cap_deg = 0.0f;          // COG (Course Over Ground) en degrés [0..360[
    float altitude_m = 0.0f;       // Altitude orthométrique (MSL) en mètres
    uint8_t satellites = 0;        // Nombre de satellites utilisés dans la solution
    float hdop = 99.9f;            // Dilution horizontale de précision (HDOP)
    uint32_t heure_utc_hms = 0;    // Heure UTC de la trame au format entier HHMMSS (ex: 123519)

    // Formate la coordonnée au format nautique officiel degrés-minutes décimales :
    // ex. "48°51.39'N 002°21.13'E"
    bool formater_coord_nautique(char* buf, size_t taille) const;

    // Formate la coordonnée en degrés-minutes-secondes sexagésimales :
    // ex. "48°51'23\"N 002°21'08\"E"
    bool formater_coord_dms(char* buf, size_t taille) const;
};

// ─── 2. Nowcast Pluie Haute Précision (15 min / 5 min) ──────────────────────

struct VigiePluieNowcast {
    bool pluie_active = false;                  // Pluie détectée ou prévue dans l'heure
    int16_t pluie_dans_min = -1;                // Minutes avant la pluie (0 = en cours, -1 = pas de pluie < 60 min)
    float intensite_max_mmh = 0.0f;             // Intensité maximale sur l'heure en mm/h
    float creneaux_15m[kVigieCreneaux15m] = {}; // Intensité (mm/h) par tranche de 15 min (0-15, 15-30, 30-45, 45-60)
    float creneaux_5m[kVigieCreneaux5m] = {};   // Intensité (mm/h) par tranche de 5 min sur 60 min
};

// ─── 3. État Météo Marine (Vagues, Houle, Courant) ──────────────────────────

struct VigieEtatMarin {
    bool valide = false;
    float hauteur_vagues_m = 0.0f;       // Hauteur significative des vagues Hs (m)
    float periode_vagues_s = 0.0f;       // Période dominante des vagues Tp (s)
    uint16_t direction_vagues_deg = 0;   // Direction de provenance des vagues (0..360°)
    float hauteur_houle_m = 0.0f;        // Hauteur de la houle primaire (m)
    float courant_noeuds = 0.0f;         // Vitesse du courant marin de surface (kt)
};

// ─── 4. Prévision Journalière (10 jours) & Zone Météo Mondiale ──────────────

struct VigiePrevisionJour {
    char date_iso[12] = "";        // "YYYY-MM-DD"
    uint16_t code_meteo = 0;       // Code OMM / WMO (0 = clair, 61 = pluie, 95 = orage...)
    float tmin_c = 0.0f;
    float tmax_c = 0.0f;
    float pluie_mm = 0.0f;
    uint8_t pluie_prob_pct = 0;
    float vent_max_kmh = 0.0f;
    float uv_max = 0.0f;
};

struct VigieZoneMeteo {
    char id[kVigieIdMax] = "";
    char nom[kVigieNomZoneMax] = "";
    double lat = 0.0;
    double lon = 0.0;
    int32_t utc_offset_s = 0;          // Décalage horaire par rapport à UTC en secondes
    bool est_zone_gps_mobile = false;  // true pour la Zone 0 dynamique ("Position Bateau / GPS Live")

    // Météo courante
    float temp_c = 0.0f;
    float ressenti_c = 0.0f;
    uint8_t humidite_pct = 0;
    float pression_hpa = 1013.25f;
    float vent_kmh = 0.0f;
    float rafales_kmh = 0.0f;
    uint16_t vent_deg = 0;
    uint16_t code_meteo = 0;
    float uv = 0.0f;
    uint16_t aqi = 0;                  // Indice de qualité de l'air (Air Quality Index)

    // Sous-modèles Pluie 15 min et Marine
    VigiePluieNowcast pluie{};
    VigieEtatMarin marin{};

    // Prévisions 10 jours
    uint8_t nb_jours = 0;
    VigiePrevisionJour jours[kVigieMaxJoursPrevision] = {};
};

// ─── 5. Alerte Catastrophe Naturelle Mondiale ───────────────────────────────

struct VigieAlerteCatastrophe {
    char id[kVigieIdMax] = "";
    VigieCategorieAlerte categorie = VigieCategorieAlerte::SEISME;
    VigieSeverite severite = VigieSeverite::VERT;
    char titre[kVigieTitreAlerteMax] = "";
    char sources[kVigieSourcesMax] = "";  // ex: "USGS, PTWC, GDACS"
    uint32_t timestamp_utc = 0;           // Epoch Unix UTC de l'événement
    double lat = 0.0;
    double lon = 0.0;
    float valeur_principale = 0.0f;       // ex: Magnitude 7.3, vent 250 km/h, Kp 8.0
    float profondeur_km = 0.0f;           // Profondeur hypocentrale pour les séismes (km)
    bool alerte_tsunami = false;          // true si un avis/alerte tsunami est déclenché
    float distance_km = 0.0f;             // Distance orthodromique depuis le bateau ou la zone active (km)
    float azimut_deg = 0.0f;              // Relèvement initial vrai depuis le bateau/zone active [0..360[
    int16_t eta_tsunami_min = -1;         // Temps d'arrivée estimé de l'onde tsunami en minutes (-1 = N/A)
};

// ─── 6. État Global du Terminal Vigie Mondiale 7B ───────────────────────────

struct VigieEtatMondial {
    uint32_t derniere_maj_utc = 0;

    // Compteurs globaux par niveau de gravité
    uint16_t nb_alertes_rouges = 0;
    uint16_t nb_alertes_oranges = 0;
    uint16_t nb_alertes_jaunes = 0;
    uint16_t nb_alertes_vertes = 0;

    // Météo spatiale (NOAA SWPC)
    float kp = 0.0f;
    VigieNiveauSolaire niveau_solaire = VigieNiveauSolaire::CALME;

    // Position GNSS en direct (Bateau / Expédition)
    VigieGpsFix gps{};

    // Watchlist des zones mondiales (jusqu'à 10 zones ; zones[0] peut être la zone GPS mobile)
    uint8_t nb_zones = 0;
    int8_t zone_active_idx = 0;
    VigieZoneMeteo zones[kVigieMaxZones] = {};

    // Alertes catastrophes mondiales actives (jusqu'à 30 alertes triées par gravité/proximité)
    uint8_t nb_alertes = 0;
    VigieAlerteCatastrophe alertes[kVigieMaxAlertes] = {};
};

// ─── Fonctions Pures C++ : Géométrie Sphérique, NMEA 0183 & Temps Réel ──────

// Distance orthodromique (grand cercle) sur sphère terrestre moyenne (R = 6371.0088 km)
double vigie_haversine_km(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);

// Conversion kilomètres -> milles nautiques (1 NM = 1.852 km) et km/h <-> nœuds
constexpr double vigie_km_vers_nm(double km) { return km / 1.852; }
constexpr float vigie_noeuds_vers_kmh(float kt) { return kt * 1.852f; }
constexpr float vigie_kmh_vers_noeuds(float kmh) { return kmh / 1.852f; }

// Relèvement vrai initial (azimut de départ du point 1 vers le point 2) en degrés [0.0, 360.0[
double vigie_azimut_deg(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);

// Rose des vents à 16 secteurs en français ("N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
// "S", "SSO", "SO", "OSO", "O", "ONO", "NO", "NNO") et en anglais international ("W", "SW"...)
const char* vigie_point_cardinal(double azimut_deg);
const char* vigie_point_cardinal_en(double azimut_deg);

// Formatage autonome de coordonnées en format nautique degrés-minutes décimales ("48°51.39'N 002°21.13'E")
bool vigie_formater_coord_nautique(double lat_deg, double lon_deg, char* buf, size_t taille);

// Formatage autonome en degrés-minutes-secondes ("48°51'23\"N 002°21'08\"E")
bool vigie_formater_coord_dms(double lat_deg, double lon_deg, char* buf, size_t taille);

// Vérifie le checksum XOR NMEA 0183 entre '$' (exclu) et '*' (exclu) comparé aux 2 chiffres hex après '*'
bool vigie_verifier_checksum_nmea(const char* ligne);

// Décode une phrase NMEA 0183 ($GPGGA / $GNGGA / $GPRMC / $GNRMC) et met à jour `fix`.
// Renvoie true si la trame a un checksum XOR valide et un type supporté (GGA ou RMC).
bool vigie_parse_nmea_sentence(const char* ligne, VigieGpsFix& fix);

// Recalcule instantanément en local la distance (km), l'azimut (°) et l'ETA tsunami (si applicable)
// de toutes les alertes de `etat` par rapport au fix GPS valide (ou à défaut la zone active),
// et synchronise les coordonnées de la Zone 0 si elle est marquée `est_zone_gps_mobile`.
void vigie_recalculer_distances_gps(VigieEtatMondial& etat);

// Met à jour les compteurs `nb_alertes_rouges / oranges / jaunes / vertes` à partir de `etat.alertes[]`.
void vigie_recalculer_compteurs_alertes(VigieEtatMondial& etat);

// Calcule le niveau de tempête géomagnétique NOAA (G0..G5) à partir de l'indice planétaire Kp.
VigieNiveauSolaire vigie_niveau_solaire_depuis_kp(float kp);

// Réinitialise proprement l'état mondial complet (au démarrage du terminal).
void vigie_initialiser_etat_mondial(VigieEtatMondial& etat);

// Instance globale statique de l'état mondial du terminal.
extern VigieEtatMondial g_vigie_etat_mondial;

