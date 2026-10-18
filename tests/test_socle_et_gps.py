# -*- coding: utf-8 -*-
"""Tests unitaires et structurels du socle autonome Multi-Écrans, Mode Marine & GPS NMEA 0183 (Vague 1 & 2).

Vérifie :
1. La pureté C++ standard de `firmware/socle/vigie_modele_mondial.h/.cpp` et
   `firmware/ecran/vigie_client_api.h/.cpp` (aucune dépendance LVGL, ESPHome ou
   Home Assistant, aucune allocation dynamique `malloc`/`new`), et la compilation
   native via `g++`/`clang++` lorsqu'un compilateur C++ hôte est présent.
2. Le vérificateur de checksum XOR NMEA 0183 (`*HH`) et le décodage complet des trames
   `$GNGGA` / `$GPGGA` et `$GNRMC` / `$GPRMC` (valides, corrompues, sans fix).
3. Le formatage des coordonnées nautiques DMS/DMM (`48°51.39'N 002°21.13'E`).
4. Les formules trigonométriques sphériques (distance Haversine, azimut initial vrai,
   rose des vents à 16 secteurs FR/EN) et le recalcul temps réel des alertes catastrophes
   lorsque le bateau ou le véhicule se déplace.
5. Le décodeur de trame API (`vigie_decoder_trame_api`), la projection cartographique
   Multi-Écrans (`vigie_projeter_coord_carte`), la persistance NVS CRC32 (`vigie_nvs_calculer_crc32`,
   `vigie_preparer_bloc_nvs`, `vigie_restaurer_bloc_nvs`), la logique d'urgence DEFCON
   (`vigie_evaluer_declenchement_alerte`, `vigie_acquitter_alerte`) et le connecteur Client API.
6. La génération des aperçus visuels hors-tablette (`docs/rendus/`) pour `waveshare-7b`
   (`1024×600`) et `tab5` (`1280×720`).
7. L'absence totale de dépendance à Home Assistant (`api:`, `homeassistant.*`) dans tous
   les fichiers YAML du dépôt `alerte_mondiale_7b`.
"""

from __future__ import annotations

import binascii
import math
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import pytest
import yaml

REPO = pathlib.Path(__file__).resolve().parent.parent
SOCLE_H = REPO / "firmware" / "socle" / "vigie_modele_mondial.h"
SOCLE_CPP = REPO / "firmware" / "socle" / "vigie_modele_mondial.cpp"
CLIENT_H = REPO / "firmware" / "ecran" / "vigie_client_api.h"
CLIENT_CPP = REPO / "firmware" / "ecran" / "vigie_client_api.cpp"
IHM_H = REPO / "firmware" / "ecran" / "vigie_ihm_controleur.h"
IHM_CPP = REPO / "firmware" / "ecran" / "vigie_ihm_controleur.cpp"
GEOM_H = REPO / "firmware" / "socle" / "tab5_geometrie.h"
TEST_CPP = REPO / "tools" / "test_vigie_modele_mondial.cpp"
PAQUETS = REPO / "firmware" / "paquets"

if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

import generer_rendu_ihm  # noqa: E402


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
VITESSE_TSUNAMI_KM_PAR_MIN = 11.6666667
NVS_MAGIC = 0x56494731
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


def cadre_carte_ecran(code_carte: str) -> tuple[int, int, int, int]:
    """Miroir exact de `vigie_cadre_carte_ecran()`."""
    ecran_l, ecran_h = (1280, 720) if code_carte == "tab5" else (1024, 600)
    x = (16 * ecran_l + 512) // 1024
    y = (64 * ecran_h + 300) // 600
    largeur = (640 * ecran_l + 512) // 1024
    hauteur = (360 * ecran_h + 300) // 600
    return x, y, largeur, hauteur


def projeter_coord_carte_ecran(lat_deg: float, lon_deg: float, code_carte: str) -> tuple[int, int, bool]:
    """Miroir exact de `vigie_projeter_coord_carte_ecran()`."""
    x, y, largeur, hauteur = cadre_carte_ecran(code_carte)
    dedans = -90.0 <= lat_deg <= 90.0 and -180.0 <= lon_deg <= 180.0
    lat_c = max(-90.0, min(90.0, lat_deg))
    lon_c = max(-180.0, min(180.0, lon_deg))
    px = x + int(round(((lon_c + 180.0) / 360.0) * largeur))
    py = y + int(round(((90.0 - lat_c) / 180.0) * hauteur))
    return px, py, dedans


def calculer_crc32_ieee(donnees: bytes) -> int:
    """Miroir exact de `vigie_nvs_calculer_crc32()` (polynôme IEEE 802.3 0xEDB88320)."""
    crc = 0xFFFFFFFF
    for b in donnees:
        crc ^= b
        for _ in range(8):
            masque = -(crc & 1) & 0xFFFFFFFF
            crc = (crc >> 1) ^ (0xEDB88320 & masque)
    return (~crc) & 0xFFFFFFFF


def verifier_crc_trame_api_python(trame: str) -> bool:
    """Miroir exact de `vigie_verifier_crc_trame_api()`."""
    if not trame:
        return False
    pos = 0
    n = len(trame)
    while pos < n:
        fin_ligne = trame.find("\n", pos)
        if fin_ligne == -1:
            fin_ligne = n
        ligne_brute = trame[pos:fin_ligne]
        ligne_strip = ligne_brute.lstrip(" \t").rstrip("\r")
        if ligne_strip == "END" or ligne_strip.startswith("END|"):
            if not ligne_strip.startswith("END|"):
                return False
            reste = ligne_strip[4:].strip()
            if len(reste) != 8:
                return False
            try:
                crc_attendu = int(reste, 16)
            except ValueError:
                return False
            octets_precedents = trame[:pos].encode("utf-8")
            crc_calc = calculer_crc32_ieee(octets_precedents)
            return crc_calc == crc_attendu
        pos = fin_ligne + 1
    return True


def decoder_trame_api_python(trame: str, gps_fix: dict | None = None, etat_existant: dict | None = None) -> tuple[bool, dict]:
    """Miroir exact de `vigie_decoder_trame_api()` avec vérification `END|<CRC32_HEX8>`."""
    etat_base = etat_existant or {
        "derniere_maj_utc": 0,
        "kp": 0.0,
        "zone_active_idx": 0,
        "zones": [],
        "alertes": [],
        "acquittees": set(),
        "gps": gps_fix or {"fix_valide": False},
    }
    if not verifier_crc_trame_api_python(trame):
        return False, etat_base

    etat = dict(etat_base)
    if gps_fix is not None:
        etat["gps"] = gps_fix

    cat_map = {
        "SEISME": "SEISME", "EQ": "SEISME", "EARTHQUAKE": "SEISME",
        "TSUNAMI": "TSUNAMI", "TSU": "TSUNAMI",
        "VOLCAN": "VOLCAN", "VO": "VOLCAN", "VOLCANO": "VOLCAN",
        "OURAGAN": "OURAGAN", "HURRICANE": "OURAGAN", "TC": "OURAGAN", "CYCLONE": "OURAGAN",
        "TORNADE": "TORNADE", "TORNADO": "TORNADE", "TO": "TORNADE",
        "INONDATION": "INONDATION", "FLOOD": "INONDATION", "FL": "INONDATION",
        "PLUIE_EXTREME": "PLUIE_EXTREME", "EXTREME_RAIN": "PLUIE_EXTREME", "RAIN": "PLUIE_EXTREME",
        "FEU": "FEU", "WILDFIRE": "FEU", "WF": "FEU", "FIRE": "FEU",
        "TEMPETE_SOLAIRE": "TEMPETE_SOLAIRE", "SOLAR_STORM": "TEMPETE_SOLAIRE", "SWPC": "TEMPETE_SOLAIRE",
    }
    sev_map = {
        "VERT": "VERT", "GREEN": "VERT", "0": "VERT",
        "JAUNE": "JAUNE", "YELLOW": "JAUNE", "1": "JAUNE",
        "ORANGE": "ORANGE", "SEVERE": "ORANGE", "2": "ORANGE",
        "ROUGE": "ROUGE", "RED": "ROUGE", "EXTREME": "ROUGE", "3": "ROUGE",
    }
    sev_rang = {"VERT": 0, "JAUNE": 1, "ORANGE": 2, "ROUGE": 3}

    au_moins_un = False
    for ligne in trame.splitlines():
        ligne = ligne.strip()
        if not ligne or ligne.startswith("#"):
            continue
        champs = ligne.split("|")
        tag = champs[0]
        if tag == "END":
            break
        if tag in ("HEAD", "VIGIE1"):
            etat["derniere_maj_utc"] = int(champs[1])
            etat["kp"] = float(champs[2]) if len(champs) > 2 and champs[2] else 0.0
            etat["zones"] = []
            etat["alertes"] = []
            au_moins_un = True
        elif tag == "ZONE" and len(champs) >= 5:
            z = {
                "id": champs[1],
                "nom": champs[2],
                "lat": float(champs[3]),
                "lon": float(champs[4]),
                "est_zone_gps_mobile": champs[1] in ("GPS", "BATEAU", "LIVE"),
                "temp_c": float(champs[6]) if len(champs) > 6 and champs[6] else 0.0,
                "creneaux_15m": [float(x) for x in champs[18].split(",")] if len(champs) > 18 and champs[18] else [],
                "vagues_m": float(champs[19]) if len(champs) > 19 and champs[19] else 0.0,
                "creneaux_5m": [],
                "jours": [],
            }
            etat["zones"].append(z)
            au_moins_un = True
        elif tag == "RAIN5M" and len(champs) >= 3:
            for z in etat["zones"]:
                if z["id"] == champs[1]:
                    z["creneaux_5m"] = [float(x) for x in champs[2].split(",")]
                    au_moins_un = True
        elif tag == "DAY" and len(champs) >= 6:
            for z in etat["zones"]:
                if z["id"] == champs[1]:
                    z["jours"].append({"date": champs[2], "code": int(champs[3]), "tmin": float(champs[4]), "tmax": float(champs[5])})
                    au_moins_un = True
        elif tag in ("HAZ", "ALERT") and len(champs) >= 7:
            al = {
                "id": champs[1],
                "categorie": cat_map.get(champs[2], "SEISME"),
                "severite": sev_map.get(champs[3], "VERT"),
                "lat": float(champs[4]),
                "lon": float(champs[5]),
                "valeur_principale": float(champs[6]),
                "profondeur_km": float(champs[7]) if len(champs) > 7 and champs[7] else 0.0,
                "alerte_tsunami": len(champs) > 8 and champs[8] in ("1", "T", "t"),
                "sources": champs[10] if len(champs) > 10 else "",
                "titre": champs[11] if len(champs) > 11 else "",
            }
            etat["alertes"].append(al)
            au_moins_un = True

    if not au_moins_un:
        return False, etat_base

    # Tri par sévérité décroissante, puis tsunami, puis valeur principale
    etat["alertes"].sort(
        key=lambda a: (sev_rang.get(a["severite"], 0), 1 if a["alerte_tsunami"] else 0, a["valeur_principale"]),
        reverse=True,
    )

    recalculer_distances_etat_python(etat)
    return True, etat


def recalculer_distances_etat_python(etat: dict) -> None:
    ref_lat, ref_lon = None, None
    if etat["gps"].get("fix_valide"):
        ref_lat = etat["gps"]["latitude_deg"]
        ref_lon = etat["gps"]["longitude_deg"]
        if etat["zones"] and etat["zones"][0]["est_zone_gps_mobile"]:
            etat["zones"][0]["lat"] = ref_lat
            etat["zones"][0]["lon"] = ref_lon
    elif etat["zones"] and 0 <= etat["zone_active_idx"] < len(etat["zones"]):
        ref_lat = etat["zones"][etat["zone_active_idx"]]["lat"]
        ref_lon = etat["zones"][etat["zone_active_idx"]]["lon"]

    if ref_lat is not None and ref_lon is not None:
        for a in etat["alertes"]:
            a["distance_km"] = calculer_distance_haversine_km(ref_lat, ref_lon, a["lat"], a["lon"])
            a["azimut_deg"] = calculer_azimut_deg(ref_lat, ref_lon, a["lat"], a["lon"])
            a["eta_tsunami_min"] = max(1, int(round(a["distance_km"] / VITESSE_TSUNAMI_KM_PAR_MIN))) if (a["alerte_tsunami"] or a["categorie"] == "TSUNAMI") else -1


def zone_suivante_python(etat: dict) -> int:
    if not etat["zones"]:
        etat["zone_active_idx"] = 0
        return 0
    etat["zone_active_idx"] = (etat["zone_active_idx"] + 1) % len(etat["zones"])
    recalculer_distances_etat_python(etat)
    return etat["zone_active_idx"]


def zone_precedente_python(etat: dict) -> int:
    if not etat["zones"]:
        etat["zone_active_idx"] = 0
        return 0
    etat["zone_active_idx"] = (etat["zone_active_idx"] - 1) % len(etat["zones"])
    recalculer_distances_etat_python(etat)
    return etat["zone_active_idx"]


def selectionner_zone_gps_python(etat: dict) -> bool:
    for i, z in enumerate(etat["zones"]):
        if z.get("est_zone_gps_mobile") or z["id"] in ("GPS", "BATEAU", "LIVE"):
            etat["zone_active_idx"] = i
            recalculer_distances_etat_python(etat)
            return True
    return False


def formater_coordonnees_dms(lat_deg: float, lon_deg: float) -> str:
    if not (math.isfinite(lat_deg) and math.isfinite(lon_deg)):
        return "--°--'--\"- ---°--'--\"-"
    lat_hemi = "N" if lat_deg >= 0.0 else "S"
    lon_hemi = "E" if lon_deg >= 0.0 else "W"
    sec_lat = int(round(min(abs(lat_deg), 90.0) * 3600.0))
    sec_lon = int(round(min(abs(lon_deg), 180.0) * 3600.0))
    return (
        f"{sec_lat // 3600:02d}°{(sec_lat % 3600) // 60:02d}'{sec_lat % 60:02d}\"{lat_hemi} "
        f"{sec_lon // 3600:03d}°{(sec_lon % 3600) // 60:02d}'{sec_lon % 60:02d}\"{lon_hemi}"
    )


def preparer_fiche_detail_alerte_python(etat: dict, index_alerte: int) -> dict | None:
    if index_alerte < 0 or index_alerte >= len(etat["alertes"]):
        return None
    a = etat["alertes"][index_alerte]
    dist_km = a.get("distance_km", 0.0)
    dist_nm = dist_km / 1.852
    cap_int = int(round(a.get("azimut_deg", 0.0))) % 360
    card = azimut_vers_cardinal(a.get("azimut_deg", 0.0), anglais=False)
    tsunami = a.get("alerte_tsunami", False) or a["categorie"] == "TSUNAMI"
    eta_min = a.get("eta_tsunami_min", -1)
    if tsunami and eta_min <= 0:
        eta_min = max(1, int(round(dist_km / VITESSE_TSUNAMI_KM_PAR_MIN)))
    return {
        "id": a["id"],
        "titre": a["titre"] or a["id"],
        "sources": f"Sources : {a['sources'] or 'USGS, PTWC, GDACS'}",
        "coord_dms": formater_coordonnees_dms(a["lat"], a["lon"]),
        "coord_nautique": formater_coordonnees_nautiques(a["lat"], a["lon"]),
        "distance_km_nm": f"{dist_km:.0f} km ({dist_nm:.0f} NM)",
        "relevement_cap": f"Cap {cap_int:03d}° {card}",
        "eta_tsunami": f"ETA ~{eta_min} min (Onde Tsunami)" if tsunami else "Aucun avis tsunami",
        "alerte_tsunami": tsunami,
        "eta_tsunami_min": eta_min,
    }


def profil_sonore_pour_alerte_python(categorie: str, severite: str, alerte_tsunami: bool = False) -> dict:
    if severite == "VERT" and not alerte_tsunami:
        return {"actif": False, "volume_pct": 0, "rtttl": ""}
    vol = 75 if (alerte_tsunami or severite == "ROUGE") else (68 if severite == "ORANGE" else 55)
    assert vol <= 75
    if alerte_tsunami or categorie == "TSUNAMI":
        rtttl = "tsunami:d=8,o=6,b=180:c,e,g,c7,p,c,e,g,c7,p,4g,4c7"
    elif categorie == "SEISME":
        rtttl = "seisme:d=16,o=6,b=200:a,p,a,p,a,p,f,p,a,p,a,p,4d7"
    elif categorie in ("OURAGAN", "TORNADE", "INONDATION", "PLUIE_EXTREME"):
        rtttl = "ouragan:d=8,o=5,b=160:4d,4a,4d6,8a,8f,4d,p,4d,4a,4d6"
    else:
        rtttl = "volcan:d=8,o=6,b=150:e,b,e7,b,e,p,g,d7,g"
    return {"actif": True, "volume_pct": vol, "rtttl": rtttl}


# ==============================================================================
# 1. TESTS DE PURETÉ C++ ET HARNAIS DE COMPILATION (VAGUES 1, 2 & 3)
# ==============================================================================

def test_socle_client_et_ihm_cpp_purs_sans_dependance_lvgl_ni_esphome_ni_allocation():
    """Le socle C++ mondial, le client API et le contrôleur IHM doivent être 100 % purs C++ standard et sans allocation dynamique."""
    for f in (SOCLE_H, SOCLE_CPP, CLIENT_H, CLIENT_CPP, IHM_H, IHM_CPP, TEST_CPP):
        assert f.is_file(), f"Fichier manquant : {f}"

    for fichier in (SOCLE_H, SOCLE_CPP, CLIENT_H, CLIENT_CPP, IHM_H, IHM_CPP):
        texte = fichier.read_text(encoding="utf-8")
        inclus = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', texte, re.M))
        autorises = {
            "vigie_modele_mondial.h",
            "../socle/vigie_modele_mondial.h",
            "vigie_client_api.h",
            "vigie_ihm_controleur.h",
            "cstdint", "cstddef", "cmath", "cstring", "cstdio", "cstdlib", "algorithm",
        }
        assert inclus <= autorises, f"{fichier.name} inclut des en-têtes non autorisés : {inclus - autorises}"
        code_sans_comm = re.sub(r"//[^\n]*|/\*.*?\*/", "", texte, flags=re.S)
        interdits = re.findall(
            r"\blv_\w+|\besphome\b|\bhomeassistant\b|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b|\bnew\b|\bdelete\b",
            code_sans_comm,
        )
        assert not interdits, f"Symboles interdits dans {fichier.name} : {interdits}"
        assert code_sans_comm.count("{") == code_sans_comm.count("}")
        assert code_sans_comm.count("(") == code_sans_comm.count(")")


def test_signatures_et_constantes_cpp_conformes_au_miroir():
    """Vérifie que toutes les fonctions et constantes des Vagues 1, 2 & 3 sont présentes et exactes."""
    h_txt = SOCLE_H.read_text(encoding="utf-8")
    cpp_txt = SOCLE_CPP.read_text(encoding="utf-8")
    client_h_txt = CLIENT_H.read_text(encoding="utf-8")
    client_cpp_txt = CLIENT_CPP.read_text(encoding="utf-8")
    ihm_h_txt = IHM_H.read_text(encoding="utf-8")
    ihm_cpp_txt = IHM_CPP.read_text(encoding="utf-8")

    for struct_name in (
        "struct VigieGpsFix",
        "struct VigiePluieNowcast",
        "struct VigieEtatMarin",
        "struct VigiePrevisionJour",
        "struct VigieZoneMeteo",
        "struct VigieAlerteCatastrophe",
        "struct VigieEtatMondial",
        "struct VigieProfilCarte",
        "struct VigieCadreCarte",
        "struct VigiePointPixel",
        "struct VigieBlocNvs",
        "struct VigieDecisionUrgence",
    ):
        assert struct_name in h_txt, f"Structure manquante : {struct_name}"

    for struct_ihm in (
        "struct VigieFicheDetailAlerte",
        "struct VigieProfilSonoreAlerte",
        "struct VigieEtatIhm",
    ):
        assert struct_ihm in ihm_h_txt, f"Structure IHM manquante : {struct_ihm}"

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
        "vigie_verifier_crc_trame_api",
        "vigie_decoder_trame_api",
        "vigie_cadre_carte_ecran",
        "vigie_projeter_coord_carte",
        "vigie_projeter_coord_carte_ecran",
        "vigie_nvs_calculer_crc32",
        "vigie_preparer_bloc_nvs",
        "vigie_restaurer_bloc_nvs",
        "vigie_alerte_est_acquittee",
        "vigie_acquitter_alerte",
        "vigie_evaluer_declenchement_alerte",
    ):
        assert fn in h_txt and fn in cpp_txt, f"Fonction socle manquante : {fn}"

    for fn_client in (
        "vigie_construire_url_api",
        "vigie_formater_statut_fraicheur",
        "vigie_client_api_recevoir_trame",
        "vigie_client_api_charger_cache_nvs",
    ):
        assert fn_client in client_h_txt and fn_client in client_cpp_txt, f"Fonction client API manquante : {fn_client}"

    for fn_ihm in (
        "vigie_zone_suivante",
        "vigie_zone_precedente",
        "vigie_selectionner_zone_gps",
        "vigie_preparer_fiche_detail_alerte",
        "vigie_profil_sonore_pour_alerte",
        "vigie_profil_sonore_pour_catastrophe",
        "vigie_ihm_ouvrir_detail_alerte",
        "vigie_ihm_fermer_detail_alerte",
        "vigie_ihm_declencher_popup_urgence",
        "vigie_ihm_acquitter_urgence_active",
    ):
        assert fn_ihm in ihm_h_txt and fn_ihm in ihm_cpp_txt, f"Fonction contrôleur IHM manquante : {fn_ihm}"

    assert "0x56494731UL" in h_txt
    assert "0xEDB88320UL" in cpp_txt
    assert "kVigieVolumeMaxSecurisePct = 75" in ihm_h_txt

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
                f"-I{REPO / 'firmware' / 'ecran'}",
                str(TEST_CPP),
                str(SOCLE_CPP),
                str(CLIENT_CPP),
                str(IHM_CPP),
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
    gga = "$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*4B"
    assert parser_nmea_sentence(gga, fix, timestamp_ms=1000) is True
    assert fix["fix_valide"] is True
    assert fix["satellites"] == 11
    assert math.isclose(fix["hdop"], 0.8, abs_tol=1e-3)
    assert math.isclose(fix["altitude_m"], 35.2, abs_tol=1e-2)
    assert math.isclose(fix["latitude_deg"], 48.8565, abs_tol=1e-4)
    assert math.isclose(fix["longitude_deg"], 2.352167, abs_tol=1e-4)
    assert fix["coords_nautiques"] == "48°51.39'N 002°21.13'E"

    rmc_corrompue = "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*FF"
    assert parser_nmea_sentence(rmc_corrompue, fix, timestamp_ms=1500) is False
    assert fix["coords_nautiques"] == "48°51.39'N 002°21.13'E"

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
# 4. TESTS VAGUE 2 & 3 : DÉCODEUR API (END|CRC32), IHM, AUDIO & RENDUS
# ==============================================================================

def test_decodeur_trame_api_et_tri_urgence():
    fix: dict = {}
    rmc_brest = "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*6C"
    assert parser_nmea_sentence(rmc_brest, fix) is True

    trame = (
        "HEAD|1792332000|6.3|0\n"
        "ZONE|GPS|Golfe de Gascogne|48.0|-4.0|7200|18.4|16.9|82|1004.8|45.4|63.0|255|95|4.2|24|15|8.2|0.5,3.2,7.8,2.1|3.4|11.5|280|2.9|1.8\n"
        "RAIN5M|GPS|0.0,0.2,0.8,2.4,5.6,8.2,6.4,3.8,1.9,0.6,0.1,0.0\n"
        "DAY|GPS|2026-10-19|61|13.2|19.1|14.5|85|58.0|4.0\n"
        "HAZ|IMO-IS-2026|VOLCANO|ORANGE|63.88|-22.43|4.0|5.0|0|1792331000|IMO|Eruption Reykjanes\n"
        "HAZ|USGS-ACORES-01|TSUNAMI|RED|38.5|-28.0|7.4|18.0|1|1792331500|USGS,PTWC|Seisme M7.4 Acores\n"
    )
    ok, etat = decoder_trame_api_python(trame, gps_fix=fix)
    assert ok is True
    assert etat["derniere_maj_utc"] == 1792332000
    assert math.isclose(etat["kp"], 6.3, abs_tol=1e-2)
    assert len(etat["zones"]) == 1
    assert math.isclose(etat["zones"][0]["lat"], 48.39, abs_tol=1e-3)
    assert len(etat["zones"][0]["creneaux_5m"]) == 12
    assert len(etat["zones"][0]["jours"]) == 1
    assert len(etat["alertes"]) == 2
    assert etat["alertes"][0]["id"] == "USGS-ACORES-01"
    assert etat["alertes"][0]["eta_tsunami_min"] > 100


def test_verification_crc32_trame_api_valide_et_corrompue():
    """Vérifie que END|<CRC32_HEX8> valide est acceptée et qu'une trame corrompue est rejetée sans corrompre `etat`."""
    corps = (
        "HEAD|1792335000|5.0|0\n"
        "ZONE|GPS|Bateau|48.39|-4.48|0|17.5|16.0|80|1012.0|25.0|35.0|270|3|3.0|18|-1|0.0|0,0,0,0|2.0|9.5|270|1.5|1.0\n"
        "HAZ|USGS-JP-01|EARTHQUAKE|RED|38.10|142.86|7.4|32.0|1|1792334800|USGS,PTWC|Seisme M7.4 Honshu\n"
        "HAZ|NHC-02|HURRICANE|ORANGE|24.50|-68.20|225.0|0.0|0|1792334900|NOAA NHC|Ouragan Helene\n"
    )
    crc_hex = f"{calculer_crc32_ieee(corps.encode('utf-8')):08X}"
    trame_valide = f"{corps}END|{crc_hex}\n"
    assert verifier_crc_trame_api_python(trame_valide) is True

    ok, etat = decoder_trame_api_python(trame_valide)
    assert ok is True
    assert len(etat["alertes"]) == 2
    assert etat["alertes"][0]["id"] == "USGS-JP-01"
    assert etat["alertes"][0]["categorie"] == "SEISME"
    assert etat["alertes"][1]["categorie"] == "OURAGAN"

    # Trame avec CRC32 corrompu ou tronqué sur le Wi-Fi : rejetée sans modifier `etat`
    trame_corrompue = f"{corps}END|DEADBEEF\n"
    assert verifier_crc_trame_api_python(trame_corrompue) is False
    ok_ko, etat_apres = decoder_trame_api_python(trame_corrompue, etat_existant=etat)
    assert ok_ko is False
    assert etat_apres["derniere_maj_utc"] == 1792335000
    assert len(etat_apres["alertes"]) == 2
    assert etat_apres["alertes"][0]["id"] == "USGS-JP-01"

    trame_tronquee = f"{corps}END|123\n"
    assert verifier_crc_trame_api_python(trame_tronquee) is False


def test_controleur_ihm_navigation_zones_fiche_detail_et_audio_rtttl():
    """Teste la navigation en rouleau des zones, la fiche détail avec ETA Tsunami et les profils sonores RTTTL <= 75 %."""
    trame = (
        "HEAD|1792336000|6.3|0\n"
        "ZONE|GPS|Golfe de Gascogne|48.39|-4.486|0|18.4|16.9|82|1004.8|45.4|63.0|255|95|4.2|24|15|8.2|0,0,0,0|3.4|11.5|280|2.9|1.8\n"
        "ZONE|REYK|Reykjavik|64.14|-21.94|0|6.2|3.0|78|998.0|30.0|45.0|180|61|1.2|12|-1|0.0|0,0,0,0|2.1|9.0|240|1.8|0.9\n"
        "ZONE|TYO|Tokyo Bay|35.68|139.77|32400|21.0|20.5|65|1014.0|18.0|25.0|90|1|5.0|30|-1|0.0|0,0,0,0|1.1|7.0|110|0.8|0.5\n"
        "HAZ|USGS-JP-2026-74|TSUNAMI|RED|38.10|142.86|7.4|32.0|1|1792335500|USGS,PTWC,JMA|Seisme M7.4 Honshu\n"
        "HAZ|EONET-WF-01|WILDFIRE|YELLOW|34.05|-118.25|450.0|0.0|0|1792335600|NASA EONET|Feu Californie\n"
    )
    ok, etat = decoder_trame_api_python(trame)
    assert ok is True
    assert etat["zone_active_idx"] == 0
    assert zone_suivante_python(etat) == 1
    assert zone_suivante_python(etat) == 2
    assert zone_suivante_python(etat) == 0
    assert zone_precedente_python(etat) == 2
    assert selectionner_zone_gps_python(etat) is True
    assert etat["zone_active_idx"] == 0

    fiche = preparer_fiche_detail_alerte_python(etat, 0)
    assert fiche is not None
    assert fiche["alerte_tsunami"] is True
    assert "USGS" in fiche["sources"]
    assert "38°06'00\"N 142°51'36\"E" == fiche["coord_dms"]
    assert "km" in fiche["distance_km_nm"] and "NM" in fiche["distance_km_nm"]
    assert fiche["relevement_cap"].startswith("Cap ")
    assert fiche["eta_tsunami"].startswith("ETA ~") and "min" in fiche["eta_tsunami"]

    # Vérification des profils sonores RTTTL et du plafond matériel <= 75 % sur l'ES8311
    for cat in ("TSUNAMI", "SEISME", "OURAGAN", "VOLCAN", "TEMPETE_SOLAIRE"):
        for sev in ("JAUNE", "ORANGE", "ROUGE"):
            prof = profil_sonore_pour_alerte_python(cat, sev, alerte_tsunami=(cat == "TSUNAMI"))
            assert prof["actif"] is True
            assert 0 < prof["volume_pct"] <= 75
            assert ":" in prof["rtttl"]

    prof_vert = profil_sonore_pour_alerte_python("SEISME", "VERT", alerte_tsunami=False)
    assert prof_vert["actif"] is False and prof_vert["volume_pct"] == 0


def test_projection_cartographique_multi_ecrans_7b_et_tab5():
    x7b, y7b, l7b, h7b = cadre_carte_ecran("waveshare-7b")
    assert (x7b, y7b, l7b, h7b) == (16, 64, 640, 360)
    px_eq7b, py_eq7b, ok7b = projeter_coord_carte_ecran(0.0, 0.0, "waveshare-7b")
    assert ok7b and (px_eq7b, py_eq7b) == (16 + 320, 64 + 180)

    x_t5, y_t5, l_t5, h_t5 = cadre_carte_ecran("tab5")
    assert (x_t5, y_t5, l_t5, h_t5) == (20, 77, 800, 432)
    px_eq_t5, py_eq_t5, ok_t5 = projeter_coord_carte_ecran(0.0, 0.0, "tab5")
    assert ok_t5 and (px_eq_t5, py_eq_t5) == (20 + 400, 77 + 216)


def test_crc32_nvs_conforme_ieee_802_3():
    vecteur = b"123456789"
    crc_notre = calculer_crc32_ieee(vecteur)
    crc_std = binascii.crc32(vecteur) & 0xFFFFFFFF
    assert crc_notre == crc_std == 0xCBF43926


def test_generateur_rendu_ihm_hors_tablette_et_urgence_tsunami_7b_et_tab5():
    fichiers = generer_rendu_ihm.generer_tous_les_rendus()
    noms = {p.name for p in fichiers}
    attendus = {
        "aperçu_waveshare_7b_1024x600.svg",
        "aperçu_waveshare_7b_1024x600.png",
        "aperçu_m5stack_tab5_1280x720.svg",
        "aperçu_m5stack_tab5_1280x720.png",
        "aperçu_urgence_tsunami_7b_1024x600.svg",
        "aperçu_urgence_tsunami_7b_1024x600.png",
        "aperçu_urgence_tsunami_tab5_1280x720.svg",
        "aperçu_urgence_tsunami_tab5_1280x720.png",
    }
    assert attendus <= noms

    svg_7b = (REPO / "docs" / "rendus" / "aperçu_waveshare_7b_1024x600.svg").read_text(encoding="utf-8")
    svg_tab5 = (REPO / "docs" / "rendus" / "aperçu_m5stack_tab5_1280x720.svg").read_text(encoding="utf-8")
    svg_urg_7b = (REPO / "docs" / "rendus" / "aperçu_urgence_tsunami_7b_1024x600.svg").read_text(encoding="utf-8")
    svg_urg_tab5 = (REPO / "docs" / "rendus" / "aperçu_urgence_tsunami_tab5_1280x720.svg").read_text(encoding="utf-8")

    assert 'width="1024" height="600"' in svg_7b and 'width="1024" height="600"' in svg_urg_7b
    assert 'width="1280" height="720"' in svg_tab5 and 'width="1280" height="720"' in svg_urg_tab5
    for motif in ("48°51.39'N 002°21.13'E", "NOWCAST PLUIE 60 MIN", "ÉTAT DE LA MER", "TSUNAMI", "OURAGAN"):
        assert motif in svg_7b and motif in svg_tab5
    for motif_urg in ("ALERTE ROUGE CRITIQUE", "ETA ~42 min", "VOL 75%", "ACQUITTER"):
        assert motif_urg in svg_urg_7b and motif_urg in svg_urg_tab5


# ==============================================================================
# 5. TESTS DE L'ARCHITECTURE MULTI-ÉCRANS & GARDE ZÉRO HOME ASSISTANT
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

    geom_h_txt = GEOM_H.read_text(encoding="utf-8")
    assert "vigie_x_depuis_7b" in geom_h_txt
    assert "vigie_y_depuis_7b" in geom_h_txt


def test_point_entree_gps_nmea_client_api_et_ihm_alertes_configures():
    racine_yaml = REPO / "vigie-mondiale-7b.yaml"
    gps_yaml = PAQUETS / "vigie-gps-nmea.yaml"
    api_yaml = PAQUETS / "vigie-client-api.yaml"
    ihm_yaml = PAQUETS / "vigie-ihm-alertes.yaml"
    assert racine_yaml.is_file() and gps_yaml.is_file() and api_yaml.is_file() and ihm_yaml.is_file()

    racine = _charger_yaml(racine_yaml)
    assert "vigie_carte" in racine["substitutions"]
    pkgs = racine["packages"]
    assert "gps_nmea" in pkgs and "client_api" in pkgs and "ihm_alertes" in pkgs

    ihm_txt = ihm_yaml.read_text(encoding="utf-8")
    for action_id in (
        "vigie_action_acquitter_urgence",
        "vigie_action_zone_suivante",
        "vigie_action_zone_precedente",
        "vigie_action_ouvrir_alerte",
    ):
        assert action_id in ihm_txt


def test_zero_dependance_home_assistant_dans_tous_les_yaml():
    """Aucun fichier YAML de alerte_mondiale_7b ne doit contenir de bloc `api:` ni `homeassistant.`."""
    fichiers_yaml = [REPO / "vigie-mondiale-7b.yaml", *PAQUETS.glob("*.yaml")]
    assert len(fichiers_yaml) >= 17
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

