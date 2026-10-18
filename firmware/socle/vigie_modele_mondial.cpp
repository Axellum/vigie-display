/**
 * ============================================================================
 * [AI-CONTEXT]
 * @file vigie_modele_mondial.cpp
 * @role Implémentation 100 % C++ standard pur (zéro allocation dynamique) du
 *       modèle mondial, des calculs géodésiques (Haversine, Azimut, Cardinaux,
 *       formatage nautique DMS/DMM), du décodeur GNSS NMEA 0183 ($GxGGA /
 *       $GxRMC avec vérification du checksum XOR *HH), du décodeur de trame API
 *       zéro-allocation, de la projection cartographique Multi-Écrans, de la
 *       persistance NVS CRC32 et de la logique d'urgence DEFCON.
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

// Copie bornée et sûre d'une chaîne C avec terminaison nulle garantie.
void copier_chaine(char* dst, size_t dst_size, const char* src) {
    if (dst == nullptr || dst_size == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
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

// Découpe une chaîne modifiable sur un séparateur donné (`','` ou `'|'`), sans allocation.
int decouper_champs_delim(char* tampon, char delim, char* champs[], int max_champs) {
    int nb = 0;
    if (max_champs <= 0 || tampon == nullptr) return 0;
    champs[nb++] = tampon;
    for (char* p = tampon; *p != '\0'; ++p) {
        if (*p == delim) {
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

VigieCategorieAlerte decoder_categorie_alerte(const char* s) {
    if (s == nullptr || s[0] == '\0') return VigieCategorieAlerte::SEISME;
    if (s[0] >= '0' && s[0] <= '8' && s[1] == '\0') {
        return static_cast<VigieCategorieAlerte>(s[0] - '0');
    }
    if (std::strcmp(s, "SEISME") == 0 || std::strcmp(s, "EQ") == 0 || std::strcmp(s, "EARTHQUAKE") == 0)
        return VigieCategorieAlerte::SEISME;
    if (std::strcmp(s, "TSUNAMI") == 0 || std::strcmp(s, "TSU") == 0)
        return VigieCategorieAlerte::TSUNAMI;
    if (std::strcmp(s, "VOLCAN") == 0 || std::strcmp(s, "VO") == 0 || std::strcmp(s, "VOLCANO") == 0)
        return VigieCategorieAlerte::VOLCAN;
    if (std::strcmp(s, "OURAGAN") == 0 || std::strcmp(s, "HURRICANE") == 0 ||
        std::strcmp(s, "TC") == 0 || std::strcmp(s, "CYCLONE") == 0 || std::strcmp(s, "TYPHOON") == 0)
        return VigieCategorieAlerte::OURAGAN;
    if (std::strcmp(s, "TORNADE") == 0 || std::strcmp(s, "TO") == 0 || std::strcmp(s, "TORNADO") == 0)
        return VigieCategorieAlerte::TORNADE;
    if (std::strcmp(s, "INONDATION") == 0 || std::strcmp(s, "FL") == 0 || std::strcmp(s, "FLOOD") == 0)
        return VigieCategorieAlerte::INONDATION;
    if (std::strcmp(s, "PLUIE_EXTREME") == 0 || std::strcmp(s, "EXTREME_RAIN") == 0 || std::strcmp(s, "RAIN") == 0)
        return VigieCategorieAlerte::PLUIE_EXTREME;
    if (std::strcmp(s, "FEU") == 0 || std::strcmp(s, "WILDFIRE") == 0 ||
        std::strcmp(s, "WF") == 0 || std::strcmp(s, "FIRE") == 0)
        return VigieCategorieAlerte::FEU;
    if (std::strcmp(s, "TEMPETE_SOLAIRE") == 0 || std::strcmp(s, "SOLAR_STORM") == 0 ||
        std::strcmp(s, "SWPC") == 0 || std::strcmp(s, "SOLAR") == 0)
        return VigieCategorieAlerte::TEMPETE_SOLAIRE;
    return VigieCategorieAlerte::SEISME;
}

VigieSeverite decoder_severite_alerte(const char* s) {
    if (s == nullptr || s[0] == '\0') return VigieSeverite::VERT;
    if (s[0] >= '0' && s[0] <= '3' && s[1] == '\0') {
        return static_cast<VigieSeverite>(s[0] - '0');
    }
    if (std::strcmp(s, "ROUGE") == 0 || std::strcmp(s, "RED") == 0 || std::strcmp(s, "EXTREME") == 0)
        return VigieSeverite::ROUGE;
    if (std::strcmp(s, "ORANGE") == 0 || std::strcmp(s, "SEVERE") == 0)
        return VigieSeverite::ORANGE;
    if (std::strcmp(s, "JAUNE") == 0 || std::strcmp(s, "YELLOW") == 0 || std::strcmp(s, "MODERATE") == 0)
        return VigieSeverite::JAUNE;
    if (std::strcmp(s, "VERT") == 0 || std::strcmp(s, "GREEN") == 0 || std::strcmp(s, "MINOR") == 0)
        return VigieSeverite::VERT;
    return VigieSeverite::VERT;
}

void trier_alertes(VigieEtatMondial& etat) {
    const uint8_t n = (etat.nb_alertes <= kVigieMaxAlertes) ? etat.nb_alertes : kVigieMaxAlertes;
    for (uint8_t i = 0; i < n; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < n; ++j) {
            const VigieAlerteCatastrophe& a = etat.alertes[i];
            const VigieAlerteCatastrophe& b = etat.alertes[j];
            bool echanger = false;
            if (static_cast<uint8_t>(b.severite) > static_cast<uint8_t>(a.severite)) {
                echanger = true;
            } else if (b.severite == a.severite) {
                if (b.alerte_tsunami && !a.alerte_tsunami) {
                    echanger = true;
                } else if (b.alerte_tsunami == a.alerte_tsunami && b.valeur_principale > a.valeur_principale) {
                    echanger = true;
                }
            }
            if (echanger) {
                VigieAlerteCatastrophe tmp = etat.alertes[i];
                etat.alertes[i] = etat.alertes[j];
                etat.alertes[j] = tmp;
            }
        }
    }
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

    int deg_lat = static_cast<int>(std::floor(abs_lat));
    double min_lat = (abs_lat - static_cast<double>(deg_lat)) * 60.0;
    long cent_min_lat = std::lround(min_lat * 100.0);
    if (cent_min_lat >= 6000) {
        cent_min_lat -= 6000;
        deg_lat += 1;
    }
    min_lat = static_cast<double>(cent_min_lat) / 100.0;

    int deg_lon = static_cast<int>(std::floor(abs_lon));
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
    while (*ligne == ' ' || *ligne == '\t') ++ligne;
    if (*ligne != '$') return false;
    ++ligne;

    uint8_t xor_calc = 0;
    while (*ligne != '\0' && *ligne != '*' && *ligne != '\r' && *ligne != '\n') {
        xor_calc ^= static_cast<uint8_t>(*ligne);
        ++ligne;
    }
    if (*ligne != '*') return false;
    const int hi = hex_nibble(ligne[1]);
    const int lo = hex_nibble(ligne[2]);
    if (hi < 0 || lo < 0) return false;
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
    char tampon[128];
    const char* debut = ligne + 1;
    const char* etoile = std::strchr(debut, '*');
    if (etoile == nullptr) return false;
    const size_t longueur = static_cast<size_t>(etoile - debut);
    if (longueur < 6 || longueur >= sizeof(tampon)) return false;

    std::memcpy(tampon, debut, longueur);
    tampon[longueur] = '\0';

    char* champs[20] = {};
    const int nb_champs = decouper_champs_delim(tampon, ',', champs, 20);
    if (nb_champs < 1 || std::strlen(champs[0]) != 5) {
        return false;
    }

    const char* type_trame = champs[0] + 2;

    if (std::strcmp(type_trame, "GGA") == 0) {
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

// ============================================================================
// VAGUE 2 & 3 : DÉCODEUR DE TRAME API (CRC32), PROJECTION CARTE, NVS & URGENCE
// ============================================================================

bool vigie_verifier_crc_trame_api(const char* trame) {
    if (trame == nullptr || trame[0] == '\0') {
        return false;
    }

    const char* p = trame;
    while (*p != '\0') {
        const char* debut_ligne = p;
        while (*debut_ligne == ' ' || *debut_ligne == '\t') {
            ++debut_ligne;
        }

        if (std::strncmp(debut_ligne, "END", 3) == 0 &&
            (debut_ligne[3] == '|' || debut_ligne[3] == '\0' ||
             debut_ligne[3] == '\r' || debut_ligne[3] == '\n')) {
            if (debut_ligne[3] != '|') {
                return false;
            }
            uint32_t crc_attendu = 0;
            for (int i = 0; i < 8; ++i) {
                const int nib = hex_nibble(debut_ligne[4 + i]);
                if (nib < 0) {
                    return false;
                }
                crc_attendu = (crc_attendu << 4) | static_cast<uint32_t>(nib);
            }
            const char apres = debut_ligne[12];
            if (apres != '\0' && apres != '\r' && apres != '\n' && apres != ' ' && apres != '\t') {
                return false;
            }

            // Tous les octets précédant le début de la ligne `END|` (incluant le `\n` juste avant)
            const size_t nb_octets = static_cast<size_t>(p - trame);
            const uint32_t crc_calc = vigie_nvs_calculer_crc32(trame, nb_octets);
            return crc_calc == crc_attendu;
        }

        while (*p != '\0' && *p != '\n') {
            ++p;
        }
        if (*p == '\n') {
            ++p;
        }
    }

    // Pas de ligne END| : la trame est acceptée (mode non signé / rétrocompatible)
    return true;
}

bool vigie_decoder_trame_api(const char* trame, VigieEtatMondial& etat) {
    if (trame == nullptr || trame[0] == '\0') {
        return false;
    }
    if (!vigie_verifier_crc_trame_api(trame)) {
        return false;
    }

    bool au_moins_un_enregistrement = false;
    const char* curseur = trame;

    while (*curseur != '\0') {
        // Sauter les retours à la ligne et espaces de tête
        while (*curseur == '\r' || *curseur == '\n' || *curseur == ' ' || *curseur == '\t') {
            ++curseur;
        }
        if (*curseur == '\0') break;

        // Trouver la fin de la ligne courante
        const char* fin_ligne = curseur;
        while (*fin_ligne != '\0' && *fin_ligne != '\r' && *fin_ligne != '\n') {
            ++fin_ligne;
        }

        const size_t len = static_cast<size_t>(fin_ligne - curseur);
        curseur = fin_ligne;

        if (len == 0 || * (curseur - len) == '#') {
            continue;
        }

        char ligne_buf[512];
        const size_t copy_len = (len < sizeof(ligne_buf) - 1) ? len : (sizeof(ligne_buf) - 1);
        std::memcpy(ligne_buf, fin_ligne - len, copy_len);
        ligne_buf[copy_len] = '\0';

        char* champs[32] = {};
        const int nb = decouper_champs_delim(ligne_buf, '|', champs, 32);
        if (nb < 2) continue;

        const char* tag = champs[0];

        if (std::strcmp(tag, "END") == 0) {
            break;
        }

        if (std::strcmp(tag, "HEAD") == 0 || std::strcmp(tag, "VIGIE1") == 0) {
            // HEAD|<timestamp_utc>|<kp>|[zone_active_idx]
            etat.derniere_maj_utc = static_cast<uint32_t>(std::strtoul(champs[1], nullptr, 10));
            if (nb >= 3 && champs[2][0] != '\0') {
                etat.kp = std::strtof(champs[2], nullptr);
                etat.niveau_solaire = vigie_niveau_solaire_depuis_kp(etat.kp);
            }
            if (nb >= 4 && champs[3][0] != '\0') {
                const int z_idx = std::atoi(champs[3]);
                if (z_idx >= 0 && z_idx < kVigieMaxZones) {
                    etat.zone_active_idx = static_cast<int8_t>(z_idx);
                }
            }
            // Réinitialise les listes de zones et d'alertes pour le nouveau snapshot API
            // tout en préservant `etat.gps` et `etat.alertes_acquittees`.
            etat.nb_zones = 0;
            etat.nb_alertes = 0;
            au_moins_un_enregistrement = true;
            continue;
        }

        if (std::strcmp(tag, "ZONE") == 0) {
            // ZONE|<id>|<nom>|<lat>|<lon>|<utc_offset_s>|<temp_c>|<ressenti_c>|<hum_pct>|
            //      <pression_hpa>|<vent_kmh>|<rafales_kmh>|<vent_deg>|<code_meteo>|<uv>|<aqi>|
            //      <pluie_dans_min>|<intensite_max_mmh>|<c15_csv>|<vagues_m>|<periode_s>|<dir_vagues_deg>|<houle_m>|<courant_kt>
            if (nb < 5 || etat.nb_zones >= kVigieMaxZones) continue;

            VigieZoneMeteo& z = etat.zones[etat.nb_zones++];
            z = VigieZoneMeteo{};
            copier_chaine(z.id, sizeof(z.id), champs[1]);
            copier_chaine(z.nom, sizeof(z.nom), champs[2]);
            z.lat = std::strtod(champs[3], nullptr);
            z.lon = std::strtod(champs[4], nullptr);
            if (std::strcmp(z.id, "GPS") == 0 || std::strcmp(z.id, "BATEAU") == 0 || std::strcmp(z.id, "LIVE") == 0) {
                z.est_zone_gps_mobile = true;
            }
            if (nb > 5 && champs[5][0] != '\0') z.utc_offset_s = static_cast<int32_t>(std::strtol(champs[5], nullptr, 10));
            if (nb > 6 && champs[6][0] != '\0') z.temp_c = std::strtof(champs[6], nullptr);
            if (nb > 7 && champs[7][0] != '\0') z.ressenti_c = std::strtof(champs[7], nullptr);
            if (nb > 8 && champs[8][0] != '\0') z.humidite_pct = static_cast<uint8_t>(std::atoi(champs[8]));
            if (nb > 9 && champs[9][0] != '\0') z.pression_hpa = std::strtof(champs[9], nullptr);
            if (nb > 10 && champs[10][0] != '\0') z.vent_kmh = std::strtof(champs[10], nullptr);
            if (nb > 11 && champs[11][0] != '\0') z.rafales_kmh = std::strtof(champs[11], nullptr);
            if (nb > 12 && champs[12][0] != '\0') z.vent_deg = static_cast<uint16_t>(std::atoi(champs[12]));
            if (nb > 13 && champs[13][0] != '\0') z.code_meteo = static_cast<uint16_t>(std::atoi(champs[13]));
            if (nb > 14 && champs[14][0] != '\0') z.uv = std::strtof(champs[14], nullptr);
            if (nb > 15 && champs[15][0] != '\0') z.aqi = static_cast<uint16_t>(std::atoi(champs[15]));

            if (nb > 16 && champs[16][0] != '\0') {
                z.pluie.pluie_dans_min = static_cast<int16_t>(std::atoi(champs[16]));
                z.pluie.pluie_active = (z.pluie.pluie_dans_min >= 0);
            }
            if (nb > 17 && champs[17][0] != '\0') {
                z.pluie.intensite_max_mmh = std::strtof(champs[17], nullptr);
                if (z.pluie.intensite_max_mmh > 0.0f) z.pluie.pluie_active = true;
            }
            if (nb > 18 && champs[18][0] != '\0') {
                char* c15[kVigieCreneaux15m] = {};
                const int n15 = decouper_champs_delim(champs[18], ',', c15, kVigieCreneaux15m);
                for (int k = 0; k < n15; ++k) {
                    z.pluie.creneaux_15m[k] = std::strtof(c15[k], nullptr);
                }
            }
            if (nb > 19 && champs[19][0] != '\0') {
                z.marin.hauteur_vagues_m = std::strtof(champs[19], nullptr);
                z.marin.valide = (z.marin.hauteur_vagues_m >= 0.0f);
            }
            if (nb > 20 && champs[20][0] != '\0') z.marin.periode_vagues_s = std::strtof(champs[20], nullptr);
            if (nb > 21 && champs[21][0] != '\0') z.marin.direction_vagues_deg = static_cast<uint16_t>(std::atoi(champs[21]));
            if (nb > 22 && champs[22][0] != '\0') z.marin.hauteur_houle_m = std::strtof(champs[22], nullptr);
            if (nb > 23 && champs[23][0] != '\0') z.marin.courant_noeuds = std::strtof(champs[23], nullptr);

            au_moins_un_enregistrement = true;
            continue;
        }

        if (std::strcmp(tag, "RAIN5M") == 0) {
            // RAIN5M|<id_zone>|<v0,v1,...,v11>
            if (nb < 3) continue;
            for (uint8_t i = 0; i < etat.nb_zones; ++i) {
                if (std::strcmp(etat.zones[i].id, champs[1]) == 0) {
                    char* c5[kVigieCreneaux5m] = {};
                    const int n5 = decouper_champs_delim(champs[2], ',', c5, kVigieCreneaux5m);
                    for (int k = 0; k < n5; ++k) {
                        etat.zones[i].pluie.creneaux_5m[k] = std::strtof(c5[k], nullptr);
                    }
                    au_moins_un_enregistrement = true;
                    break;
                }
            }
            continue;
        }

        if (std::strcmp(tag, "DAY") == 0) {
            // DAY|<id_zone>|<date_iso>|<code_meteo>|<tmin_c>|<tmax_c>|<pluie_mm>|<pluie_prob_pct>|<vent_max_kmh>|<uv_max>
            if (nb < 6) continue;
            for (uint8_t i = 0; i < etat.nb_zones; ++i) {
                VigieZoneMeteo& z = etat.zones[i];
                if (std::strcmp(z.id, champs[1]) == 0 && z.nb_jours < kVigieMaxJoursPrevision) {
                    VigiePrevisionJour& d = z.jours[z.nb_jours++];
                    copier_chaine(d.date_iso, sizeof(d.date_iso), champs[2]);
                    d.code_meteo = static_cast<uint16_t>(std::atoi(champs[3]));
                    d.tmin_c = std::strtof(champs[4], nullptr);
                    d.tmax_c = std::strtof(champs[5], nullptr);
                    if (nb > 6 && champs[6][0] != '\0') d.pluie_mm = std::strtof(champs[6], nullptr);
                    if (nb > 7 && champs[7][0] != '\0') d.pluie_prob_pct = static_cast<uint8_t>(std::atoi(champs[7]));
                    if (nb > 8 && champs[8][0] != '\0') d.vent_max_kmh = std::strtof(champs[8], nullptr);
                    if (nb > 9 && champs[9][0] != '\0') d.uv_max = std::strtof(champs[9], nullptr);
                    au_moins_un_enregistrement = true;
                    break;
                }
            }
            continue;
        }

        if (std::strcmp(tag, "HAZ") == 0 || std::strcmp(tag, "ALERT") == 0) {
            // HAZ|<id>|<categorie>|<severite>|<lat>|<lon>|<valeur_principale>|<profondeur_km>|<alerte_tsunami>|<timestamp_utc>|<sources>|<titre>
            if (nb < 7 || etat.nb_alertes >= kVigieMaxAlertes) continue;

            VigieAlerteCatastrophe& a = etat.alertes[etat.nb_alertes++];
            a = VigieAlerteCatastrophe{};
            copier_chaine(a.id, sizeof(a.id), champs[1]);
            a.categorie = decoder_categorie_alerte(champs[2]);
            a.severite = decoder_severite_alerte(champs[3]);
            a.lat = std::strtod(champs[4], nullptr);
            a.lon = std::strtod(champs[5], nullptr);
            a.valeur_principale = std::strtof(champs[6], nullptr);
            if (nb > 7 && champs[7][0] != '\0') a.profondeur_km = std::strtof(champs[7], nullptr);
            if (nb > 8 && champs[8][0] != '\0') {
                a.alerte_tsunami = (champs[8][0] == '1' || champs[8][0] == 'T' || champs[8][0] == 't');
            }
            if (nb > 9 && champs[9][0] != '\0') {
                a.timestamp_utc = static_cast<uint32_t>(std::strtoul(champs[9], nullptr, 10));
            }
            if (nb > 10 && champs[10][0] != '\0') {
                copier_chaine(a.sources, sizeof(a.sources), champs[10]);
            }
            if (nb > 11 && champs[11][0] != '\0') {
                copier_chaine(a.titre, sizeof(a.titre), champs[11]);
            }
            au_moins_un_enregistrement = true;
            continue;
        }
    }

    if (au_moins_un_enregistrement) {
        trier_alertes(etat);
        vigie_recalculer_distances_gps(etat);
        vigie_recalculer_compteurs_alertes(etat);
    }
    return au_moins_un_enregistrement;
}

VigieCadreCarte vigie_cadre_carte_ecran(const char* code_carte) {
    const VigieProfilCarte& p = vigie_profil_carte(code_carte);
    // Cadre de référence sur Waveshare 7B (1024×600) : x=16, y=64, l=640, h=360
    // Mis à l'échelle proportionnelle exacte sur toute autre cible (ex: Tab5 1280×720 -> x=20, y=77, l=800, h=432)
    VigieCadreCarte c{};
    c.x = (16 * p.ecran_l + 512) / 1024;
    c.y = (64 * p.ecran_h + 300) / 600;
    c.largeur = (640 * p.ecran_l + 512) / 1024;
    c.hauteur = (360 * p.ecran_h + 300) / 600;
    return c;
}

VigiePointPixel vigie_projeter_coord_carte(double lat_deg, double lon_deg, const VigieCadreCarte& cadre) {
    VigiePointPixel pt{};
    if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg) || cadre.largeur <= 0 || cadre.hauteur <= 0) {
        return pt;
    }

    const bool dedans = (lat_deg >= -90.0 && lat_deg <= 90.0 && lon_deg >= -180.0 && lon_deg <= 180.0);
    double lat_clamp = lat_deg;
    double lon_clamp = lon_deg;
    if (lat_clamp < -90.0) lat_clamp = -90.0;
    if (lat_clamp > 90.0) lat_clamp = 90.0;
    if (lon_clamp < -180.0) lon_clamp = -180.0;
    if (lon_clamp > 180.0) lon_clamp = 180.0;

    const double ratio_x = (lon_clamp + 180.0) / 360.0;
    const double ratio_y = (90.0 - lat_clamp) / 180.0;

    pt.x = cadre.x + static_cast<int32_t>(std::lround(ratio_x * static_cast<double>(cadre.largeur)));
    pt.y = cadre.y + static_cast<int32_t>(std::lround(ratio_y * static_cast<double>(cadre.hauteur)));
    pt.dans_cadre = dedans;
    return pt;
}

VigiePointPixel vigie_projeter_coord_carte_ecran(double lat_deg, double lon_deg, const char* code_carte) {
    return vigie_projeter_coord_carte(lat_deg, lon_deg, vigie_cadre_carte_ecran(code_carte));
}

uint32_t vigie_nvs_calculer_crc32(const void* donnees, size_t taille) {
    if (donnees == nullptr || taille == 0) return 0;
    const auto* octets = static_cast<const uint8_t*>(donnees);
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < taille; ++i) {
        crc ^= static_cast<uint32_t>(octets[i]);
        for (int b = 0; b < 8; ++b) {
            const uint32_t masque = -(crc & 1UL);
            crc = (crc >> 1) ^ (0xEDB88320UL & masque);
        }
    }
    return ~crc;
}

void vigie_preparer_bloc_nvs(const VigieEtatMondial& etat, VigieBlocNvs& bloc) {
    bloc.magic = kVigieNvsMagic;
    bloc.version = kVigieNvsVersion;
    bloc.taille_etat = static_cast<uint16_t>(sizeof(VigieEtatMondial));
    bloc.etat = etat;
    bloc.crc32 = vigie_nvs_calculer_crc32(&bloc.etat, sizeof(VigieEtatMondial));
}

bool vigie_restaurer_bloc_nvs(const VigieBlocNvs& bloc, VigieEtatMondial& etat) {
    if (bloc.magic != kVigieNvsMagic ||
        bloc.version != kVigieNvsVersion ||
        bloc.taille_etat != static_cast<uint16_t>(sizeof(VigieEtatMondial))) {
        return false;
    }
    const uint32_t crc_calc = vigie_nvs_calculer_crc32(&bloc.etat, sizeof(VigieEtatMondial));
    if (crc_calc != bloc.crc32) {
        return false;
    }
    etat = bloc.etat;
    return true;
}

bool vigie_alerte_est_acquittee(const VigieEtatMondial& etat, const char* id_alerte) {
    if (id_alerte == nullptr || id_alerte[0] == '\0') return false;
    const uint8_t max_ack = (etat.nb_acquittees <= kVigieMaxAlertesAcquittees)
                                ? etat.nb_acquittees
                                : kVigieMaxAlertesAcquittees;
    for (uint8_t i = 0; i < max_ack; ++i) {
        if (std::strcmp(etat.alertes_acquittees[i], id_alerte) == 0) {
            return true;
        }
    }
    return false;
}

bool vigie_acquitter_alerte(VigieEtatMondial& etat, const char* id_alerte) {
    if (id_alerte == nullptr || id_alerte[0] == '\0') return false;
    if (vigie_alerte_est_acquittee(etat, id_alerte)) return true;

    const uint8_t slot = etat.curseur_acquittees % kVigieMaxAlertesAcquittees;
    copier_chaine(etat.alertes_acquittees[slot], kVigieIdMax, id_alerte);
    etat.curseur_acquittees = static_cast<uint8_t>((slot + 1) % kVigieMaxAlertesAcquittees);
    if (etat.nb_acquittees < kVigieMaxAlertesAcquittees) {
        ++etat.nb_acquittees;
    }
    return true;
}

VigieDecisionUrgence vigie_evaluer_declenchement_alerte(
    const VigieEtatMondial& etat,
    float rayon_geofencing_orange_km) {
    VigieDecisionUrgence decision{};
    const uint8_t max_a = (etat.nb_alertes <= kVigieMaxAlertes) ? etat.nb_alertes : kVigieMaxAlertes;

    for (uint8_t i = 0; i < max_a; ++i) {
        const VigieAlerteCatastrophe& a = etat.alertes[i];
        if (a.id[0] == '\0' || vigie_alerte_est_acquittee(etat, a.id)) {
            continue;
        }

        bool critique = false;
        if (a.severite == VigieSeverite::ROUGE) {
            critique = true;
        } else if (a.severite == VigieSeverite::ORANGE) {
            if (a.alerte_tsunami ||
                (a.distance_km > 0.0f && a.distance_km <= rayon_geofencing_orange_km)) {
                critique = true;
            }
        }

        if (critique) {
            decision.declencher = true;
            decision.reveiller_dalle = true;
            decision.alerte_sonore = true;
            decision.severite = a.severite;
            decision.alerte_idx = static_cast<int8_t>(i);
            copier_chaine(decision.alerte_id, sizeof(decision.alerte_id), a.id);
            decision.distance_km = a.distance_km;
            return decision;
        }
    }

    return decision;
}

VigieEtatMondial g_vigie_etat_mondial{};
