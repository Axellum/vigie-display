/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_modele_mondial.cpp
 * @role Implémentation 100 % C++ standard pur (zéro allocation dynamique) du
 *       modèle mondial, des calculs géodésiques (Haversine, Azimut, Cardinaux,
 *       formatage nautique DMS/DMM) et du décodeur GNSS NMEA 0183 ($GxGGA /
 *       $GxRMC avec vérification du checksum XOR *HH).
 * @architecture_constraint Aucune dépendance à ESPHome, ESP-IDF ou LVGL.
 *       Compilable tel quel sur PC (g++ / clang++) pour les tests unitaires.
 * ============================================================================
 */
#include "vigie_modele_mondial.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegVersRad = kPi / 180.0;
constexpr double kRadVersDeg = 180.0 / kPi;
// Rayon moyen volumétrique terrestre IUGG (en kilomètres)
constexpr double kRayonTerreKm = 6371.0088;
// Vitesse moyenne de propagation d'un tsunami en océan profond (~700 km/h ≈ 11.67 km/min)
constexpr double kVitesseTsunamiKmParMin = 11.6666667;

constexpr const char* kCardinauxFr[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSO", "SO", "OSO", "O", "ONO", "NO", "NNO"
};

constexpr const char* kCardinauxEn[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
};

constexpr VigieProfilCarte kProfilsCartes[] = {
    {
        VigieCarteCible::WAVESHARE_7B,
        "waveshare-7b",
        "Waveshare ESP32-P4-WIFI6-Touch-LCD-7B",
        1024, 600,
        1024, 600,
        0,
        82,
        false, false, false,
        4, 5  // Connecteur P3 : GPIO4 (RX) / GPIO5 (TX)
    },
    {
        VigieCarteCible::M5STACK_TAB5,
        "tab5",
        "M5Stack Tab5 V2 (ESP32-P4 5\" 1280x720)",
        1280, 720,
        720, 1280,
        270,
        100,
        true, true, true,
        53, 54  // Port Grove HY2.0-4P (Port A) / M5-Bus pour Unit GPS SMA
    },
};

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Convertit une coordonnée NMEA "ddmm.mmmm" (lat) ou "dddmm.mmmm" (lon) + hémisphère ('N'/'S'/'E'/'W')
// en degrés décimaux signés. Renvoie false si le champ est vide ou mal formé.
bool decoder_coord_nmea(const char* champ_val, const char* champ_hemi, double max_abs, double& out_deg) {
    if (champ_val == nullptr || champ_val[0] == '\0' || champ_hemi == nullptr || champ_hemi[0] == '\0') {
        return false;
    }
    const char hemi = champ_hemi[0];
    if (hemi != 'N' && hemi != 'S' && hemi != 'E' && hemi != 'W') {
        return false;
    }

    char* fin = nullptr;
    const double brut = std::strtod(champ_val, &fin);
    if (fin == champ_val || !std::isfinite(brut) || brut < 0.0) {
        return false;
    }

    const int degres = static_cast<int>(brut / 100.0);
    const double minutes = brut - static_cast<double>(degres * 100);
    if (minutes < 0.0 || minutes >= 60.0) {
        return false;
    }

    double dec = static_cast<double>(degres) + (minutes / 60.0);
    if (dec > max_abs) {
        return false;
    }
    if (hemi == 'S' || hemi == 'W') {
        dec = -dec;
    }
    out_deg = dec;
    return true;
}

// Découpe une ligne NMEA (entre le caractère après '$' et '*') sur les virgules, sans allocation.
int decouper_champs_nmea(char* tampon, char* champs[], int max_champs) {
    int nb = 0;
    if (max_champs <= 0 || tampon == nullptr) return 0;
    champs[nb++] = tampon;
    for (char* p = tampon; *p != '\0'; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (nb < max_champs) {
                champs[nb++] = p + 1;
            } else {
                break;
            }
        }
    }
    return nb;
}

}  // namespace

const VigieProfilCarte& vigie_profil_carte(const char* code_carte) {
    if (code_carte != nullptr) {
        for (const auto& p : kProfilsCartes) {
            if (std::strcmp(code_carte, p.code) == 0) {
                return p;
            }
        }
    }
    return kProfilsCartes[0];
}

double vigie_haversine_km(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    if (!std::isfinite(lat1_deg) || !std::isfinite(lon1_deg) ||
        !std::isfinite(lat2_deg) || !std::isfinite(lon2_deg)) {
        return 0.0;
    }
    const double dlat = (lat2_deg - lat1_deg) * kDegVersRad;
    const double dlon = (lon2_deg - lon1_deg) * kDegVersRad;
    const double lat1 = lat1_deg * kDegVersRad;
    const double lat2 = lat2_deg * kDegVersRad;

    const double sin_dlat = std::sin(dlat * 0.5);
    const double sin_dlon = std::sin(dlon * 0.5);
    double a = sin_dlat * sin_dlat + std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kRayonTerreKm * c;
}

double vigie_azimut_deg(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    if (!std::isfinite(lat1_deg) || !std::isfinite(lon1_deg) ||
        !std::isfinite(lat2_deg) || !std::isfinite(lon2_deg)) {
        return 0.0;
    }
    const double lat1 = lat1_deg * kDegVersRad;
    const double lat2 = lat2_deg * kDegVersRad;
    const double dlon = (lon2_deg - lon1_deg) * kDegVersRad;

    const double y = std::sin(dlon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
    if (std::fabs(x) < 1e-12 && std::fabs(y) < 1e-12) {
        return 0.0;
    }
    double azimut = std::atan2(y, x) * kRadVersDeg;
    azimut = std::fmod(azimut + 360.0, 360.0);
    if (azimut < 0.0) azimut += 360.0;
    if (azimut >= 360.0) azimut -= 360.0;
    return azimut;
}

const char* vigie_point_cardinal(double azimut_deg) {
    if (!std::isfinite(azimut_deg)) return "N";
    double norm = std::fmod(azimut_deg, 360.0);
    if (norm < 0.0) norm += 360.0;
    const int idx = static_cast<int>(std::floor((norm + 11.25) / 22.5)) & 15;
    return kCardinauxFr[idx];
}

const char* vigie_point_cardinal_en(double azimut_deg) {
    if (!std::isfinite(azimut_deg)) return "N";
    double norm = std::fmod(azimut_deg, 360.0);
    if (norm < 0.0) norm += 360.0;
    const int idx = static_cast<int>(std::floor((norm + 11.25) / 22.5)) & 15;
    return kCardinauxEn[idx];
}

bool vigie_formater_coord_nautique(double lat_deg, double lon_deg, char* buf, size_t taille) {
    if (buf == nullptr || taille < 24 || !std::isfinite(lat_deg) || !std::isfinite(lon_deg)) {
        if (buf != nullptr && taille > 0) buf[0] = '\0';
        return false;
    }
    if (lat_deg < -90.0) lat_deg = -90.0;
    if (lat_deg > 90.0) lat_deg = 90.0;
    if (lon_deg < -180.0) lon_deg = -180.0;
    if (lon_deg > 180.0) lon_deg = 180.0;

    const char hemi_lat = (lat_deg >= 0.0) ? 'N' : 'S';
    const char hemi_lon = (lon_deg >= 0.0) ? 'E' : 'W';

    const double abs_lat = std::fabs(lat_deg);
    const double abs_lon = std::fabs(lon_deg);

    int deg_lat = static_cast<int>( std::floor(abs_lat) );
    double min_lat = (abs_lat - static_cast<double>(deg_lat)) * 60.0;
    // Protection contre l'arrondi à 60.00'
    long cent_min_lat = std::lround(min_lat * 100.0);
    if (cent_min_lat >= 6000) {
        cent_min_lat -= 6000;
        deg_lat += 1;
    }
    min_lat = static_cast<double>(cent_min_lat) / 100.0;

    int deg_lon = static_cast<int>( std::floor(abs_lon) );
    double min_lon = (abs_lon - static_cast<double>(deg_lon)) * 60.0;
    long cent_min_lon = std::lround(min_lon * 100.0);
    if (cent_min_lon >= 6000) {
        cent_min_lon -= 6000;
        deg_lon += 1;
    }
    min_lon = static_cast<double>(cent_min_lon) / 100.0;

    const int n = std::snprintf(buf, taille, "%02d\xC2\xB0%05.2f'%c %03d\xC2\xB0%05.2f'%c",
                                deg_lat, min_lat, hemi_lat, deg_lon, min_lon, hemi_lon);
    return n > 0 && static_cast<size_t>(n) < taille;
}

bool vigie_formater_coord_dms(double lat_deg, double lon_deg, char* buf, size_t taille) {
    if (buf == nullptr || taille < 26 || !std::isfinite(lat_deg) || !std::isfinite(lon_deg)) {
        if (buf != nullptr && taille > 0) buf[0] = '\0';
        return false;
    }
    if (lat_deg < -90.0) lat_deg = -90.0;
    if (lat_deg > 90.0) lat_deg = 90.0;
    if (lon_deg < -180.0) lon_deg = -180.0;
    if (lon_deg > 180.0) lon_deg = 180.0;

    const char hemi_lat = (lat_deg >= 0.0) ? 'N' : 'S';
    const char hemi_lon = (lon_deg >= 0.0) ? 'E' : 'W';

    long total_sec_lat = std::lround(std::fabs(lat_deg) * 3600.0);
    int deg_lat = static_cast<int>(total_sec_lat / 3600);
    int min_lat = static_cast<int>((total_sec_lat % 3600) / 60);
    int sec_lat = static_cast<int>(total_sec_lat % 60);

    long total_sec_lon = std::lround(std::fabs(lon_deg) * 3600.0);
    int deg_lon = static_cast<int>(total_sec_lon / 3600);
    int min_lon = static_cast<int>((total_sec_lon % 3600) / 60);
    int sec_lon = static_cast<int>(total_sec_lon % 60);

    const int n = std::snprintf(buf, taille, "%02d\xC2\xB0%02d'%02d\"%c %03d\xC2\xB0%02d'%02d\"%c",
                                deg_lat, min_lat, sec_lat, hemi_lat,
                                deg_lon, min_lon, sec_lon, hemi_lon);
    return n > 0 && static_cast<size_t>(n) < taille;
}

bool VigieGpsFix::formater_coord_nautique(char* buf, size_t taille) const {
    if (!valide) {
        if (buf != nullptr && taille > 0) {
            std::snprintf(buf, taille, "--\xC2\xB0--.--'- ---\xC2\xB0--.--'-");
        }
        return false;
    }
    return vigie_formater_coord_nautique(lat_deg, lon_deg, buf, taille);
}

bool VigieGpsFix::formater_coord_dms(char* buf, size_t taille) const {
    if (!valide) {
        if (buf != nullptr && taille > 0) {
            std::snprintf(buf, taille, "--\xC2\xB0--'--\"- ---\xC2\xB0--'--\"-");
        }
        return false;
    }
    return vigie_formater_coord_dms(lat_deg, lon_deg, buf, taille);
}

bool vigie_verifier_checksum_nmea(const char* ligne) {
    if (ligne == nullptr) return false;
    // Avancer jusqu'au '$'
    while (*ligne == ' ' || *ligne == '\t') ++ligne;
    if (*ligne != '$') return false;
    ++ligne;  // sauter '$'

    uint8_t xor_calc = 0;
    while (*ligne != '\0' && *ligne != '*' && *ligne != '\r' && *ligne != '\n') {
        xor_calc ^= static_cast<uint8_t>(*ligne);
        ++ligne;
    }
    if (*ligne != '*') return false;
    const int hi = hex_nibble(ligne[1]);
    const int lo = hex_nibble(ligne[2]);
    if (hi < 0 || lo < 0) return false;
    // Après les 2 caractères hex, seuls des espaces ou fins de ligne sont admis
    const char apres = ligne[3];
    if (apres != '\0' && apres != '\r' && apres != '\n' && apres != ' ') return false;

    const uint8_t attendu = static_cast<uint8_t>((hi << 4) | lo);
    return xor_calc == attendu;
}

bool vigie_parse_nmea_sentence(const char* ligne, VigieGpsFix& fix) {
    if (!vigie_verifier_checksum_nmea(ligne)) {
        return false;
    }
    while (*ligne == ' ' || *ligne == '\t') ++ligne;
    // Copie locale bornée sur la pile (une trame NMEA 0183 fait au maximum 82 octets)
    char tampon[128];
    const char* debut = ligne + 1;  // après '$'
    const char* etoile = std::strchr(debut, '*');
    if (etoile == nullptr) return false;
    const size_t longueur = static_cast<size_t>(etoile - debut);
    if (longueur < 6 || longueur >= sizeof(tampon)) return false;

    std::memcpy(tampon, debut, longueur);
    tampon[longueur] = '\0';

    char* champs[20] = {};
    const int nb_champs = decouper_champs_nmea(tampon, champs, 20);
    if (nb_champs < 1 || std::strlen(champs[0]) != 5) {
        return false;
    }

    const char* type_trame = champs[0] + 2;  // ex: "GGA" ou "RMC" après "GP"/"GN"/"GL"/"GA"

    if (std::strcmp(type_trame, "GGA") == 0) {
        // $GxGGA,hhmmss.ss,lat,N/S,lon,E/W,qualite,satellites,hdop,altitude,M,...
        if (nb_champs < 10) return false;

        const int qualite = std::atoi(champs[6]);
        if (champs[1][0] != '\0') {
            fix.heure_utc_hms = static_cast<uint32_t>(std::strtoul(champs[1], nullptr, 10));
        }
        fix.satellites = static_cast<uint8_t>(std::atoi(champs[7]));
        if (champs[8][0] != '\0') {
            const float h = std::strtof(champs[8], nullptr);
            if (std::isfinite(h) && h >= 0.0f) fix.hdop = h;
        }
        if (champs[9][0] != '\0') {
            const float alt = std::strtof(champs[9], nullptr);
            if (std::isfinite(alt)) fix.altitude_m = alt;
        }

        if (qualite <= 0) {
            fix.valide = false;
            return true;
        }

        double lat = 0.0;
        double lon = 0.0;
        if (!decoder_coord_nmea(champs[2], champs[3], 90.0, lat) ||
            !decoder_coord_nmea(champs[4], champs[5], 180.0, lon)) {
            fix.valide = false;
            return false;
        }
        fix.lat_deg = lat;
        fix.lon_deg = lon;
        fix.valide = true;
        return true;
    }

    if (std::strcmp(type_trame, "RMC") == 0) {
        // $GxRMC,hhmmss.ss,A/V,lat,N/S,lon,E/W,sog_kt,cog_deg,ddmmyy,...
        if (nb_champs < 9) return false;

        if (champs[1][0] != '\0') {
            fix.heure_utc_hms = static_cast<uint32_t>(std::strtoul(champs[1], nullptr, 10));
        }

        const char statut = champs[2][0];
        if (statut != 'A') {
            fix.valide = false;
            return statut == 'V';
        }

        double lat = 0.0;
        double lon = 0.0;
        if (!decoder_coord_nmea(champs[3], champs[4], 90.0, lat) ||
            !decoder_coord_nmea(champs[5], champs[6], 180.0, lon)) {
            fix.valide = false;
            return false;
        }

        fix.lat_deg = lat;
        fix.lon_deg = lon;
        fix.valide = true;

        if (champs[7][0] != '\0') {
            const float sog = std::strtof(champs[7], nullptr);
            if (std::isfinite(sog) && sog >= 0.0f) fix.vitesse_noeuds = sog;
        }
        if (champs[8][0] != '\0') {
            float cog = std::strtof(champs[8], nullptr);
            if (std::isfinite(cog)) {
                cog = std::fmod(cog, 360.0f);
                if (cog < 0.0f) cog += 360.0f;
                fix.cap_deg = cog;
            }
        }
        return true;
    }

    return false;
}

void vigie_recalculer_distances_gps(VigieEtatMondial& etat) {
    double ref_lat = 0.0;
    double ref_lon = 0.0;
    bool ref_disponible = false;

    if (etat.gps.valide) {
        ref_lat = etat.gps.lat_deg;
        ref_lon = etat.gps.lon_deg;
        ref_disponible = true;

        // Synchronise automatiquement la Zone 0 si elle correspond à la position Bateau / GPS Live
        if (etat.nb_zones > 0 && etat.zones[0].est_zone_gps_mobile) {
            etat.zones[0].lat = ref_lat;
            etat.zones[0].lon = ref_lon;
        }
    } else if (etat.zone_active_idx >= 0 && etat.zone_active_idx < static_cast<int8_t>(etat.nb_zones)) {
        ref_lat = etat.zones[etat.zone_active_idx].lat;
        ref_lon = etat.zones[etat.zone_active_idx].lon;
        ref_disponible = true;
    }

    if (!ref_disponible) return;

    const uint8_t max_a = (etat.nb_alertes <= kVigieMaxAlertes) ? etat.nb_alertes : kVigieMaxAlertes;
    for (uint8_t i = 0; i < max_a; ++i) {
        VigieAlerteCatastrophe& a = etat.alertes[i];
        const double dist = vigie_haversine_km(ref_lat, ref_lon, a.lat, a.lon);
        const double az = vigie_azimut_deg(ref_lat, ref_lon, a.lat, a.lon);
        a.distance_km = static_cast<float>(dist);
        a.azimut_deg = static_cast<float>(az);

        if (a.alerte_tsunami || a.categorie == VigieCategorieAlerte::TSUNAMI) {
            const int32_t eta = static_cast<int32_t>(std::lround(dist / kVitesseTsunamiKmParMin));
            a.eta_tsunami_min = static_cast<int16_t>(eta > 32767 ? 32767 : (eta < 1 ? 1 : eta));
        }
    }
}

void vigie_recalculer_compteurs_alertes(VigieEtatMondial& etat) {
    etat.nb_alertes_rouges = 0;
    etat.nb_alertes_oranges = 0;
    etat.nb_alertes_jaunes = 0;
    etat.nb_alertes_vertes = 0;

    const uint8_t max_a = (etat.nb_alertes <= kVigieMaxAlertes) ? etat.nb_alertes : kVigieMaxAlertes;
    for (uint8_t i = 0; i < max_a; ++i) {
        switch (etat.alertes[i].severite) {
            case VigieSeverite::ROUGE:
                ++etat.nb_alertes_rouges;
                break;
            case VigieSeverite::ORANGE:
                ++etat.nb_alertes_oranges;
                break;
            case VigieSeverite::JAUNE:
                ++etat.nb_alertes_jaunes;
                break;
            case VigieSeverite::VERT:
            default:
                ++etat.nb_alertes_vertes;
                break;
        }
    }
}

VigieNiveauSolaire vigie_niveau_solaire_depuis_kp(float kp) {
    if (!std::isfinite(kp) || kp < 4.0f) return VigieNiveauSolaire::CALME;
    if (kp < 5.0f) return VigieNiveauSolaire::ACTIF;
    if (kp < 6.0f) return VigieNiveauSolaire::G1_MINEURE;
    if (kp < 7.0f) return VigieNiveauSolaire::G2_MODEREE;
    if (kp < 8.0f) return VigieNiveauSolaire::G3_FORTE;
    if (kp < 9.0f) return VigieNiveauSolaire::G4_SEVERE;
    return VigieNiveauSolaire::G5_EXTREME;
}

void vigie_initialiser_etat_mondial(VigieEtatMondial& etat) {
    etat = VigieEtatMondial{};
}

VigieEtatMondial g_vigie_etat_mondial{};

