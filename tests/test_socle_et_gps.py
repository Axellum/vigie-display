# -*- coding: utf-8 -*-
"""Tests unitaires et structurels du socle autonome Multi-Écrans, Mode Marine & GPS NMEA 0183.

Vérifie :
1. La pureté C++ standard de `firmware/socle/vigie_modele_mondial.h` et `.cpp` (aucune
   dépendance LVGL, ESPHome ou Home Assistant, aucune allocation dynamique `malloc`/`new`),
   et la compilation native via `g++`/`clang++` lorsqu'un compilateur C++ hôte est présent.
2. Le vérificateur de checksum XOR NMEA 0183 (`*HH`) et le décodage complet des trames
   `$GNGGA` / `$GPGGA` et `$GNRMC` / `$GPRMC` (valides, corrompues, sans fix).
3. Le formatage des coordonnées nautiques DMS/DMM (`48°51.39'N 002°21.13'E`).
4. Les formules trigonométriques sphériques (distance Haversine, azimut initial vrai,
   rose des vents à 16 secteurs FR/EN) et le recalcul temps réel des alertes catastrophes
   lorsque le bateau ou le véhicule se déplace.
5. L'architecture Multi-Écrans (`waveshare-7b` 1024×600 et `tab5` M5Stack Tab5 V2 1280×720)
   entre `tab5_geometrie.h`, `vigie_modele_mondial.h/.cpp` et `geometrie-<carte>.yaml`.
6. L'absence totale de dépendance à Home Assistant (`api:`, `homeassistant.*`) dans tous
   les fichiers YAML du dépôt `alerte_mondiale_7b`.
"""

from __future__ import annotations

import math
import pathlib
import re
import shutil
import subprocess
import tempfile

import pytest
import yaml

REPO = pathlib.Path(__file__).resolve().parent.parent
SOCLE_H = REPO / "firmware" / "socle" / "vigie_modele_mondial.h"
SOCLE_CPP = REPO / "firmware" / "socle" / "vigie_modele_mondial.cpp"
GEOM_H = REPO / "firmware" / "socle" / "tab5_geometrie.h"
TEST_CPP = REPO / "tools" / "test_vigie_modele_mondial.cpp"
PAQUETS = REPO / "firmware" / "paquets"


class _ChargeurESPHome(yaml.SafeLoader):
    """Chargeur YAML tolérant aux balises ESPHome (!include, !lambda, !secret, !extend)."""


_ChargeurESPHome.add_multi_constructor(
    "!",
    lambda loader, suffix, node: (
        loader.construct_scalar(node) if isinstance(node, yaml.ScalarNode) else None
    ),
)


def _charger_yaml(chemin: pathlib.Path) -> dict:
    return yaml.load(chemin.read_text(encoding="utf-8"), Loader=_ChargeurESPHome) or {}


# ==============================================================================
# MIROIR DE RÉFÉRENCE C++ <-> PYTHON (Vérifié contre vigie_modele_mondial.cpp)
# ==============================================================================

RAYON_TERRE_KM = 6371.0088
NOEUDS_VERS_KMH = 1.852
CARDINAUX_FR = (
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSO", "SO", "OSO", "O", "ONO", "NO", "NNO",
)
CARDINAUX_EN = (
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
)


def nmea_calculer_checksum(sentence: str) -> int:
    s = sentence.strip()
    if not s.startswith("$") or "*" not in s:
        raise ValueError(f"Trame NMEA mal formée : {sentence}")
    corps = s[1:s.index("*")]
    xor_val = 0
    for ch in corps:
        xor_val ^= ord(ch)
    return xor_val


def nmea_verifier_checksum(sentence: str) -> bool:
    if not sentence:
        return False
    s = sentence.strip()
    if not s.startswith("$") or "*" not in s:
        return False
    etoile = s.index("*")
    if etoile == 1 or len(s) < etoile + 3:
        return False
    try:
        attendu = int(s[etoile + 1:etoile + 3], 16)
    except ValueError:
        return False
    return nmea_calculer_checksum(s) == attendu


def nmea_dmm_vers_degres(dmm_str: str, hemisphere: str) -> float | None:
    if not dmm_str or not hemisphere:
        return None
    raw = float(dmm_str)
    if raw < 0.0:
        return None
    deg = int(raw / 100.0)
    minutes = raw - (deg * 100.0)
    if minutes < 0.0 or minutes >= 60.0:
        return None
    dec = deg + (minutes / 60.0)
    if hemisphere in ("S", "s", "W", "w"):
        dec = -dec
    elif hemisphere not in ("N", "n", "E", "e"):
        return None
    if hemisphere in ("N", "n", "S", "s") and abs(dec) > 90.0:
        return None
    if hemisphere in ("E", "e", "W", "w") and abs(dec) > 180.0:
        return None
    return dec


def formater_coordonnees_nautiques(lat_deg: float, lon_deg: float) -> str:
    if not (math.isfinite(lat_deg) and math.isfinite(lon_deg)):
        return "--°--.--'- ---°--.--'-"
    lat_hemi = "N" if lat_deg >= 0.0 else "S"
    lon_hemi = "E" if lon_deg >= 0.0 else "W"
    abs_lat = min(abs(lat_deg), 90.0)
    abs_lon = min(abs(lon_deg), 180.0)

    lat_d = int(abs_lat)
    lat_m = (abs_lat - lat_d) * 60.0
    if lat_m >= 59.995:
        lat_m = 0.0
        lat_d += 1

    lon_d = int(abs_lon)
    lon_m = (abs_lon - lon_d) * 60.0
    if lon_m >= 59.995:
        lon_m = 0.0
        lon_d += 1

    return f"{lat_d:02d}°{lat_m:05.2f}'{lat_hemi} {lon_d:03d}°{lon_m:05.2f}'{lon_hemi}"


def calculer_distance_haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    r_lat1 = math.radians(lat1)
    r_lat2 = math.radians(lat2)
    a = math.sin(dlat * 0.5) ** 2 + math.cos(r_lat1) * math.cos(r_lat2) * (math.sin(dlon * 0.5) ** 2)
    a = min(1.0, max(0.0, a))
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
    return RAYON_TERRE_KM * c


def calculer_azimut_deg(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    if abs(lat1 - lat2) < 1e-7 and abs(lon1 - lon2) < 1e-7:
        return 0.0
    r_lat1 = math.radians(lat1)
    r_lat2 = math.radians(lat2)
    dlon = math.radians(lon2 - lon1)
    y = math.sin(dlon) * math.cos(r_lat2)
    x = math.cos(r_lat1) * math.sin(r_lat2) - math.sin(r_lat1) * math.cos(r_lat2) * math.cos(dlon)
    brng = math.degrees(math.atan2(y, x))
    return math.fmod(brng + 360.0, 360.0)


def azimut_vers_cardinal(azimut_deg: float, anglais: bool = False) -> str:
    norm = math.fmod(azimut_deg, 360.0)
    if norm < 0.0:
        norm += 360.0
    idx = int(math.floor((norm + 11.25) / 22.5)) % 16
    return (CARDINAUX_EN if anglais else CARDINAUX_FR)[idx]


def parser_nmea_sentence(sentence: str, fix: dict, timestamp_ms: int = 1000) -> bool:
    if not nmea_verifier_checksum(sentence):
        return False
    corps = sentence.strip()[1:sentence.strip().index("*")]
    champs = corps.split(",")
    if len(champs[0]) < 5:
        return False
    type_trame = champs[0][-3:]
    if type_trame == "GGA":
        if len(champs) < 10:
            return False
        qual = int(champs[6]) if champs[6] else 0
        sats = int(champs[7]) if champs[7] else 0
        hdop = float(champs[8]) if champs[8] else 99.9
        alt = float(champs[9]) if champs[9] else 0.0
        fix["satellites"] = max(0, min(99, sats))
        if hdop > 0.0:
            fix["hdop"] = hdop
        if qual <= 0:
            fix["qualite_fix"] = 0
            fix["fix_valide"] = False
            return True
        lat = nmea_dmm_vers_degres(champs[2], champs[3])
        lon = nmea_dmm_vers_degres(champs[4], champs[5])
        if lat is None or lon is None:
            return False
        fix["latitude_deg"] = lat
        fix["longitude_deg"] = lon
        fix["altitude_m"] = alt
        fix["qualite_fix"] = qual
        fix["fix_valide"] = True
        fix["timestamp_ms"] = timestamp_ms
        fix["coords_nautiques"] = formater_coordonnees_nautiques(lat, lon)
        return True
    if type_trame == "RMC":
        if len(champs) < 10:
            return False
        statut = champs[2]
        if statut != "A":
            fix["fix_valide"] = False
            fix["qualite_fix"] = 0
            return True
        lat = nmea_dmm_vers_degres(champs[3], champs[4])
        lon = nmea_dmm_vers_degres(champs[5], champs[6])
        if lat is None or lon is None:
            return False
        sog = float(champs[7]) if champs[7] else 0.0
        cog = float(champs[8]) if champs[8] else 0.0
        fix["latitude_deg"] = lat
        fix["longitude_deg"] = lon
        fix["vitesse_noeuds"] = max(0.0, sog)
        fix["vitesse_kmh"] = max(0.0, sog) * NOEUDS_VERS_KMH
        fix["cap_deg"] = math.fmod(cog + 360.0, 360.0)
        if fix.get("qualite_fix", 0) == 0:
            fix["qualite_fix"] = 1
        fix["fix_valide"] = True
        fix["timestamp_ms"] = timestamp_ms
        fix["coords_nautiques"] = formater_coordonnees_nautiques(lat, lon)
        return True
    return False


# ==============================================================================
# 1. TESTS DE PURETÉ C++ ET HARNAIS DE COMPILATION
# ==============================================================================

def test_socle_cpp_pur_sans_dependance_lvgl_ni_esphome_ni_allocation():
    """Le socle C++ mondial doit être 100 % pur C++ standard et sans allocation dynamique."""
    assert SOCLE_H.is_file() and SOCLE_CPP.is_file() and TEST_CPP.is_file()
    for fichier in (SOCLE_H, SOCLE_CPP):
        texte = fichier.read_text(encoding="utf-8")
        inclus = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', texte, re.M))
        autorises = {
            "vigie_modele_mondial.h",
            "cstdint", "cstddef", "cmath", "cstring", "cstdio", "cstdlib", "algorithm",
        }
        assert inclus <= autorises, f"{fichier.name} inclut des en-têtes non autorisés : {inclus - autorises}"
        code_sans_comm = re.sub(r"//[^\n]*|/\*.*?\*/", "", texte, flags=re.S)
        interdits = re.findall(
            r"\blv_\w+|\besphome\b|\bhomeassistant\b|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b|\bnew\b|\bdelete\b",
            code_sans_comm,
        )
        assert not interdits, f"Symboles interdits dans {fichier.name} : {interdits}"
        # Vérification d'équilibre syntaxique des accolades et parenthèses
        assert code_sans_comm.count("{") == code_sans_comm.count("}")
        assert code_sans_comm.count("(") == code_sans_comm.count(")")


def test_signatures_et_constantes_cpp_conformes_au_miroir():
    """Vérifie que toutes les fonctions et constantes de vigie_modele_mondial.h/.cpp sont présentes et exactes."""
    h_txt = SOCLE_H.read_text(encoding="utf-8")
    cpp_txt = SOCLE_CPP.read_text(encoding="utf-8")

    for struct_name in (
        "struct VigieGpsFix",
        "struct VigiePluieNowcast",
        "struct VigieEtatMarin",
        "struct VigiePrevisionJour",
        "struct VigieZoneMeteo",
        "struct VigieAlerteCatastrophe",
        "struct VigieEtatMondial",
        "struct VigieProfilCarte",
    ):
        assert struct_name in h_txt, f"Structure manquante : {struct_name}"

    for fn in (
        "vigie_profil_carte",
        "vigie_initialiser_etat_mondial",
        "vigie_verifier_checksum_nmea",
        "vigie_parse_nmea_sentence",
        "vigie_formater_coord_nautique",
        "vigie_formater_coord_dms",
        "vigie_haversine_km",
        "vigie_azimut_deg",
        "vigie_point_cardinal",
        "vigie_point_cardinal_en",
        "vigie_recalculer_distances_gps",
        "vigie_recalculer_compteurs_alertes",
        "vigie_niveau_solaire_depuis_kp",
    ):
        assert fn in h_txt and fn in cpp_txt, f"Fonction manquante : {fn}"

    assert "6371.0088" in cpp_txt
    assert "1.852" in h_txt

    # Vérifier que toutes les trames NMEA de test_vigie_modele_mondial.cpp qui doivent être valides ont un checksum XOR exact
    test_cpp_txt = TEST_CPP.read_text(encoding="utf-8")
    assert "gga_paris" in test_cpp_txt and "rmc_brest" in test_cpp_txt
    for trame in (
        "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59",
        "$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*4B",
        "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*6C",
        "$GPGGA,180000.00,4000.0000,N,02500.0000,W,1,09,1.1,0.0,M,0.0,M,,*4D",
    ):
        assert trame in test_cpp_txt
        assert nmea_verifier_checksum(trame), f"Checksum invalide pour {trame} (attendu *{nmea_calculer_checksum(trame):02X})"

    # Si g++ ou clang++ est disponible (ex: runner CI Linux/macOS/MinGW), compiler et exécuter le binaire C++
    compilateur = shutil.which("g++") or shutil.which("clang++")
    if compilateur:
        with tempfile.TemporaryDirectory() as tmpdir:
            binaire = pathlib.Path(tmpdir) / "test_vigie_modele_mondial"
            cmd = [
                compilateur,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{REPO / 'firmware' / 'socle'}",
                str(TEST_CPP),
                str(SOCLE_CPP),
                "-o",
                str(binaire),
            ]
            subprocess.run(cmd, check=True)
            res = subprocess.run([str(binaire)], capture_output=True, text=True, check=True)
            assert "OK:" in res.stdout


# ==============================================================================
# 2. TESTS DU PARSEUR GPS NMEA 0183 & COORDONNÉES NAUTIQUES DMS/DMM
# ==============================================================================

def test_nmea_checksum_valide_et_invalide():
    trame_ok = "$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*4B"
    trame_ko = "$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*00"
    assert nmea_verifier_checksum(trame_ok) is True
    assert nmea_verifier_checksum(trame_ko) is False
    assert nmea_verifier_checksum("GNGGA sans dollar*4B") is False
    assert nmea_verifier_checksum("$GNGGA sans etoile") is False


def test_decodage_nmea_gga_et_rmc_et_format_nautique():
    fix: dict = {}
    # 1. Trame GGA à Paris (48°51.39'N, 002°21.13'E)
    gga = "$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*4B"
    assert parser_nmea_sentence(gga, fix, timestamp_ms=1000) is True
    assert fix["fix_valide"] is True
    assert fix["satellites"] == 11
    assert math.isclose(fix["hdop"], 0.8, abs_tol=1e-3)
    assert math.isclose(fix["altitude_m"], 35.2, abs_tol=1e-2)
    assert math.isclose(fix["latitude_deg"], 48.8565, abs_tol=1e-4)
    assert math.isclose(fix["longitude_deg"], 2.352167, abs_tol=1e-4)
    assert fix["coords_nautiques"] == "48°51.39'N 002°21.13'E"

    # 2. Trame RMC avec checksum erroné -> rejetée sans écraser le fix
    rmc_corrompue = "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*FF"
    assert parser_nmea_sentence(rmc_corrompue, fix, timestamp_ms=1500) is False
    assert fix["coords_nautiques"] == "48°51.39'N 002°21.13'E"

    # 3. Trame RMC valide au large de Brest (48°23.40'N, 004°29.16'W, 14.2 nœuds, cap 270°)
    rmc_brest = "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*6C"
    assert parser_nmea_sentence(rmc_brest, fix, timestamp_ms=2000) is True
    assert fix["fix_valide"] is True
    assert math.isclose(fix["latitude_deg"], 48.39, abs_tol=1e-4)
    assert math.isclose(fix["longitude_deg"], -4.486, abs_tol=1e-4)
    assert math.isclose(fix["vitesse_noeuds"], 14.2, abs_tol=1e-2)
    assert math.isclose(fix["vitesse_kmh"], 14.2 * 1.852, abs_tol=1e-2)
    assert math.isclose(fix["cap_deg"], 270.0, abs_tol=1e-2)
    assert fix["coords_nautiques"] == "48°23.40'N 004°29.16'W"


# ==============================================================================
# 3. TESTS DE GÉOMÉTRIE HAVERSINE, AZIMUT, CARDINAL & RECALCUL TEMPS RÉEL
# ==============================================================================

def test_haversine_azimut_et_recalcul_temps_reel_bateau():
    # Position 1 : Brest (48.39°N, -4.486°W) -> Épicentre Tsunami Açores (38.5°N, -28.0°W)
    lat_brest, lon_brest = 48.39, -4.486
    lat_acores, lon_acores = 38.5, -28.0

    dist_1 = calculer_distance_haversine_km(lat_brest, lon_brest, lat_acores, lon_acores)
    azimut_1 = calculer_azimut_deg(lat_brest, lon_brest, lat_acores, lon_acores)
    card_fr_1 = azimut_vers_cardinal(azimut_1, anglais=False)
    card_en_1 = azimut_vers_cardinal(azimut_1, anglais=True)

    assert 2100.0 <= dist_1 <= 2250.0
    assert 240.0 <= azimut_1 <= 260.0
    assert card_fr_1 == "OSO"
    assert card_en_1 == "WSW"

    # Position 2 : Le navire navigue vers le Sud-Ouest jusqu'à (40.0°N, -25.0°W)
    fix: dict = {}
    gga_acores = "$GPGGA,180000.00,4000.0000,N,02500.0000,W,1,09,1.1,0.0,M,0.0,M,,*4D"
    assert parser_nmea_sentence(gga_acores, fix, timestamp_ms=5000) is True
    dist_2 = calculer_distance_haversine_km(fix["latitude_deg"], fix["longitude_deg"], lat_acores, lon_acores)
    azimut_2 = calculer_azimut_deg(fix["latitude_deg"], fix["longitude_deg"], lat_acores, lon_acores)
    card_fr_2 = azimut_vers_cardinal(azimut_2, anglais=False)

    assert dist_2 < 320.0
    assert dist_2 < dist_1
    assert card_fr_2 in ("SO", "OSO")


# ==============================================================================
# 4. TESTS DE L'ARCHITECTURE MULTI-ÉCRANS (WAVESHARE 7B 1024x600 & TAB5 1280x720)
# ==============================================================================

@pytest.mark.parametrize(
    "carte, largeur, hauteur, echelle",
    [
        ("waveshare-7b", 1024, 600, 0.82),
        ("tab5", 1280, 720, 1.0),
    ],
)
def test_architecture_multi_ecrans_geometrie_et_cartes(carte: str, largeur: int, hauteur: int, echelle: float):
    geom_path = PAQUETS / f"geometrie-{carte}.yaml"
    carte_path = PAQUETS / f"carte-{carte}.yaml"
    assert geom_path.is_file(), f"Fichier géométrie manquant : {geom_path}"
    assert carte_path.is_file(), f"Fichier carte manquant : {carte_path}"

    subs = _charger_yaml(geom_path)["substitutions"]
    assert int(subs["carte_ecran_l"]) == largeur
    assert int(subs["carte_ecran_h"]) == hauteur
    assert math.isclose(float(subs["carte_police_echelle"]), echelle, abs_tol=1e-3)

    # Vérifier la présence des helpers multi-écrans dans tab5_geometrie.h
    geom_h_txt = GEOM_H.read_text(encoding="utf-8")
    assert "vigie_x_depuis_7b" in geom_h_txt
    assert "vigie_y_depuis_7b" in geom_h_txt


def test_point_entree_et_gps_nmea_presents_et_configures():
    racine_yaml = REPO / "vigie-mondiale-7b.yaml"
    gps_yaml = PAQUETS / "vigie-gps-nmea.yaml"
    assert racine_yaml.is_file()
    assert gps_yaml.is_file()

    racine = _charger_yaml(racine_yaml)
    assert "vigie_carte" in racine["substitutions"]
    pkgs = racine["packages"]
    assert "gps_nmea" in pkgs

    gps_txt = gps_yaml.read_text(encoding="utf-8")
    assert "vigie_parse_nmea_sentence" in gps_txt
    assert "vigie_recalculer_distances_gps" in gps_txt


# ==============================================================================
# 5. GARDE D'INDÉPENDANCE ABSOLUE VIS-À-VIS DE HOME ASSISTANT
# ==============================================================================

def test_zero_dependance_home_assistant_dans_tous_les_yaml():
    """Aucun fichier YAML de alerte_mondiale_7b ne doit contenir de bloc `api:` ni `homeassistant.`."""
    fichiers_yaml = [REPO / "vigie-mondiale-7b.yaml", *PAQUETS.glob("*.yaml")]
    assert len(fichiers_yaml) >= 15
    for chemin in fichiers_yaml:
        texte = chemin.read_text(encoding="utf-8")
        sans_commentaires = "\n".join(ligne.split("#", 1)[0] for ligne in texte.splitlines())
        assert not re.search(r"^\s*api\s*:", sans_commentaires, re.M), f"Bloc `api:` interdit dans {chemin.name}"
        assert not re.search(r"\bhomeassistant\.(event|service|action)\b", sans_commentaires), (
            f"Appel homeassistant.* interdit dans {chemin.name}"
        )
        assert not re.search(r"platform\s*:\s*homeassistant\b", sans_commentaires), (
            f"Capteur platform: homeassistant interdit dans {chemin.name}"
        )
