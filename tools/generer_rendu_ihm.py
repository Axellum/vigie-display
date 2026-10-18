#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Générateur de Rendu Hors-Tablette Haute Fidélité — Vigie Mondiale & Marine (7B & Tab5).

Génère les aperçus visuels pixel-perfect (SVG vectoriel + PNG bitmap via Pillow) de
l'interface « Centre de Situation Mondial & Marine » dans `docs/rendus/` pour les
deux cibles matérielles de l'architecture Multi-Écrans :
  - Waveshare 7B (`1024×600`) : `docs/rendus/aperçu_waveshare_7b_1024x600.svg` (et `.png`)
  - M5Stack Tab5 V2 (`1280×720`) : `docs/rendus/aperçu_m5stack_tab5_1280x720.svg` (et `.png`)

Utilise exactement la même formule de projection cartographique équirectangulaire et
la même géométrie Haversine/Azimut que `firmware/socle/vigie_modele_mondial.cpp`.
"""

from __future__ import annotations

import math
import pathlib

REPO = pathlib.Path(__file__).resolve().parent.parent
DOSSIER_RENDUS = REPO / "docs" / "rendus"

CIBLES = {
    "waveshare-7b": {
        "largeur": 1024,
        "hauteur": 600,
        "nom_ecran": "Waveshare ESP32-P4 7B · 1024×600 · MIPI-DSI / RGB",
        "fichier_base": "aperçu_waveshare_7b_1024x600",
    },
    "tab5": {
        "largeur": 1280,
        "hauteur": 720,
        "nom_ecran": "M5Stack Tab5 V2 · 1280×720 · MIPI-DSI + Grove GPS SMA",
        "fichier_base": "aperçu_m5stack_tab5_1280x720",
    },
}

# Scénario opérationnel réaliste (identique sur les 2 cibles pour comparer la mise à l'échelle)
SCENARIO = {
    "heure_utc": "14:28:45 UTC",
    "heure_locale": "16:28 LOC",
    "statut_api": "LIVE · il y a 6 s · WSS",
    "gps_nautique": "48°51.39'N 002°21.13'E · SOG 12.4 kt · COG 245° OSO",
    "gps_lat": 48.8565,
    "gps_lon": 2.3522,
    "kp": 6.3,
    "kp_label": "Kp 6.3 · G2 MODÉRÉE",
    "defcon_rouges": 2,
    "defcon_oranges": 3,
    "defcon_jaunes": 4,
    "zone_active": {
        "nom": "GOLFE DE GASCOGNE / BATEAU VIGIE-01",
        "temp_c": 18.4,
        "ressenti_c": 16.9,
        "condition": "Grain orageux & Houle longue Ouest",
        "vent_kt": 24.5,
        "rafales_kt": 34.0,
        "vent_dir": "255° OSO",
        "pression_hpa": 1004.8,
        "humidite_pct": 82,
        "uv": 4.2,
        "aqi": 24,
        "pluie_5m": [0.0, 0.2, 0.8, 2.4, 5.6, 8.2, 6.4, 3.8, 1.9, 0.6, 0.1, 0.0],
        "pluie_msg": "Pluie forte dans 15 min (pic 8.2 mm/h à +25 min)",
        "marin": {
            "vagues_m": 3.4,
            "periode_s": 11.5,
            "dir_vagues": "280° O",
            "houle_m": 2.9,
            "courant_kt": 1.8,
            "douglas": "Degré 5 — Mer Forte",
        },
    },
    "alertes": [
        {
            "id": "USGS-JP-2026-74",
            "categorie": "TSUNAMI",
            "severite": "ROUGE",
            "couleur": "#EF4444",
            "titre": "Séisme M7.4 Fosse du Japon (Honshu) — Alerte Tsunami Pacifique",
            "source": "USGS · PTWC · JMA",
            "lat": 38.10,
            "lon": 142.86,
            "valeur": "M 7.4 · Prof. 32 km",
            "dist_km": 9714,
            "dist_nm": 5245,
            "azimut": 33,
            "cardinal": "NNE",
        },
        {
            "id": "NOAA-ATL-2026-CAT4",
            "categorie": "OURAGAN",
            "severite": "ROUGE",
            "couleur": "#EF4444",
            "titre": "Ouragan Majeur Cat. 4 « HELENE » — Vents 225 km/h (121 kt)",
            "source": "NOAA NHC · GDACS",
            "lat": 24.50,
            "lon": -68.20,
            "valeur": "121 kt · 942 hPa",
            "dist_km": 6295,
            "dist_nm": 3399,
            "azimut": 261,
            "cardinal": "O",
        },
        {
            "id": "IMO-IS-2026-VOLC",
            "categorie": "VOLCAN",
            "severite": "ORANGE",
            "couleur": "#F59E0B",
            "titre": "Éruption Fissurale Reykjanes (Islande) — Code Aviation ORANGE",
            "source": "IMO · VAAC London",
            "lat": 63.88,
            "lon": -22.43,
            "valeur": "Panache FL240",
            "dist_km": 2248,
            "dist_nm": 1214,
            "azimut": 324,
            "cardinal": "NO",
        },
    ],
    "zones_watchlist": [
        ("BATEAU GPS", 48.86, 2.35, "18.4°C", "#38BDF8"),
        ("REYKJAVIK", 64.14, -21.94, "6.2°C", "#F59E0B"),
        ("MIAMI NHC", 25.76, -80.19, "28.9°C", "#EF4444"),
        ("TOKYO BAY", 35.68, 139.77, "21.0°C", "#EF4444"),
        ("PAPEETE", -17.54, -149.57, "27.4°C", "#10B981"),
    ],
}

# Silhouettes continentales stylisées (polygones lon, lat simplifiés pour le fond de carte)
CONTINENTS_LON_LAT = [
    # Amérique du Nord
    [(-165, 65), (-130, 70), (-80, 68), (-55, 50), (-75, 35), (-82, 25), (-97, 18), (-118, 32), (-140, 58)],
    # Amérique du Sud
    [(-80, 10), (-60, 8), (-35, -6), (-42, -23), (-68, -54), (-74, -45), (-70, -18), (-81, -4)],
    # Europe & Afrique
    [(-10, 36), (-5, 58), (25, 70), (50, 65), (40, 42), (35, 30), (50, 12), (40, -15), (20, -34), (12, -15), (-8, 5), (-17, 15), (-6, 35)],
    # Asie
    [(40, 42), (60, 68), (135, 70), (170, 65), (142, 40), (120, 22), (104, 2), (78, 8), (68, 24), (50, 28)],
    # Australie
    [(114, -22), (132, -12), (146, -15), (153, -28), (146, -39), (115, -34)],
]


def projeter_coord(lat_deg: float, lon_deg: float, x0: int, y0: int, l_carte: int, h_carte: int) -> tuple[int, int]:
    """Même projection équirectangulaire que `vigie_projeter_coord_carte()` en C++."""
    lat_c = max(-90.0, min(90.0, lat_deg))
    lon_c = max(-180.0, min(180.0, lon_deg))
    rx = (lon_c + 180.0) / 360.0
    ry = (90.0 - lat_c) / 180.0
    px = x0 + int(round(rx * l_carte))
    py = y0 + int(round(ry * h_carte))
    return px, py


def generer_svg_cible(code_carte: str, cfg: dict) -> str:
    W = cfg["largeur"]
    H = cfg["hauteur"]
    sx = W / 1024.0
    sy = H / 600.0
    s_min = min(sx, sy)

    def X(v: float) -> int:
        return int(round(v * sx))

    def Y(v: float) -> int:
        return int(round(v * sy))

    def S(v: float) -> int:
        return max(8, int(round(v * s_min)))

    # Cadre de la carte mondiale (identique à vigie_cadre_carte_ecran en C++)
    map_x = (16 * W + 512) // 1024
    map_y = (64 * H + 300) // 600
    map_w = (640 * W + 512) // 1024
    map_h = (360 * H + 300) // 600

    # Panneau droit (Météo Locale + Radar Pluie 60 min + Mode Marine)
    right_x = map_x + map_w + X(12)
    right_w = W - right_x - X(16)
    right_y = map_y
    right_h = map_h

    # Ticker inférieur d'alertes catastrophes + Watchlist
    bot_x = map_x
    bot_y = map_y + map_h + Y(10)
    bot_w = W - 2 * map_x
    bot_h = H - bot_y - Y(12)

    lignes = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">',
        "<defs>",
        '  <linearGradient id="bgGrad" x1="0" y1="0" x2="0" y2="1">',
        '    <stop offset="0%" stop-color="#070B14"/>',
        '    <stop offset="100%" stop-color="#0F172A"/>',
        "  </linearGradient>",
        '  <linearGradient id="glassCard" x1="0" y1="0" x2="0" y2="1">',
        '    <stop offset="0%" stop-color="#1E293B"/>',
        '    <stop offset="100%" stop-color="#0F172A"/>',
        "  </linearGradient>",
        '  <linearGradient id="oceanGrad" x1="0" y1="0" x2="0" y2="1">',
        '    <stop offset="0%" stop-color="#081426"/>',
        '    <stop offset="50%" stop-color="#0B1D36"/>',
        '    <stop offset="100%" stop-color="#071222"/>',
        "  </linearGradient>",
        f'  <clipPath id="mapClip"><rect x="{map_x}" y="{map_y}" width="{map_w}" height="{map_h}" rx="{S(12)}"/></clipPath>',
        "</defs>",
        f'<rect x="0" y="0" width="{W}" height="{H}" fill="url(#bgGrad)"/>',
    ]

    # 1. BANDEAU SUPÉRIEUR (y=10..54 sur 7B)
    hdr_x, hdr_y, hdr_w, hdr_h = map_x, Y(10), bot_w, Y(46)
    lignes.append(
        f'<rect x="{hdr_x}" y="{hdr_y}" width="{hdr_w}" height="{hdr_h}" rx="{S(10)}" '
        f'fill="url(#glassCard)" stroke="#334155" stroke-width="1.5"/>'
    )
    # Horloge UTC + Statut Live
    lignes.append(
        f'<text x="{hdr_x + X(14)}" y="{hdr_y + Y(20)}" fill="#F8FAFC" font-family="Segoe UI, Roboto, sans-serif" '
        f'font-size="{S(15)}" font-weight="bold">{SCENARIO["heure_utc"]} · {SCENARIO["heure_locale"]}</text>'
    )
    lignes.append(
        f'<circle cx="{hdr_x + X(18)}" cy="{hdr_y + Y(35)}" r="{S(4)}" fill="#10B981"/>'
        f'<text x="{hdr_x + X(27)}" y="{hdr_y + Y(39)}" fill="#34D399" font-family="Segoe UI, Roboto, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">{SCENARIO["statut_api"]} ({cfg["nom_ecran"]})</text>'
    )

    # Télémétrie GPS Nautique au centre du bandeau
    gps_box_x = hdr_x + X(310)
    gps_box_w = X(395)
    lignes.append(
        f'<rect x="{gps_box_x}" y="{hdr_y + Y(6)}" width="{gps_box_w}" height="{hdr_h - Y(12)}" rx="{S(7)}" '
        f'fill="#0B192C" stroke="#0284C7" stroke-width="1.2"/>'
        f'<text x="{gps_box_x + X(10)}" y="{hdr_y + Y(20)}" fill="#38BDF8" font-family="Consolas, monospace" '
        f'font-size="{S(11)}" font-weight="bold">GNSS NMEA 0183 · FIX 3D (11 SAT · HDOP 0.8)</text>'
        f'<text x="{gps_box_x + X(10)}" y="{hdr_y + Y(36)}" fill="#E0F2FE" font-family="Consolas, monospace" '
        f'font-size="{S(12)}" font-weight="bold">{SCENARIO["gps_nautique"]}</text>'
    )

    # Badges DEFCON Rouge / Orange / Jaune + Indice Solaire Kp à droite
    badge_x = hdr_x + hdr_w - X(275)
    for couleur_bg, couleur_txt, label in [
        ("#7F1D1D", "#FCA5A5", f'ROUGE {SCENARIO["defcon_rouges"]}'),
        ("#78350F", "#FDE68A", f'ORANGE {SCENARIO["defcon_oranges"]}'),
        ("#713F12", "#FEF08A", f'JAUNE {SCENARIO["defcon_jaunes"]}'),
    ]:
        lignes.append(
            f'<rect x="{badge_x}" y="{hdr_y + Y(7)}" width="{X(60)}" height="{Y(17)}" rx="{S(5)}" '
            f'fill="{couleur_bg}" stroke="{couleur_txt}" stroke-width="1"/>'
            f'<text x="{badge_x + X(30)}" y="{hdr_y + Y(19)}" text-anchor="middle" fill="{couleur_txt}" '
            f'font-family="Segoe UI, sans-serif" font-size="{S(10)}" font-weight="bold">{label}</text>'
        )
        badge_x += X(65)

    lignes.append(
        f'<text x="{hdr_x + hdr_w - X(14)}" y="{hdr_y + Y(39)}" text-anchor="end" fill="#C084FC" '
        f'font-family="Segoe UI, sans-serif" font-size="{S(11)}" font-weight="bold">'
        f'MÉTÉO SPATIALE : {SCENARIO["kp_label"]}</text>'
    )

    # 2. CARTE MONDIALE DES RISQUES & NAVIGATION MARINE
    lignes.append(
        f'<rect x="{map_x}" y="{map_y}" width="{map_w}" height="{map_h}" rx="{S(12)}" '
        f'fill="url(#oceanGrad)" stroke="#1E3A5F" stroke-width="1.5"/>'
    )
    lignes.append('<g clip-path="url(#mapClip)">')

    # Grille de méridiens (tous les 30°) et parallèles (tous les 30°)
    for lon_g in range(-150, 180, 30):
        gx, _ = projeter_coord(0.0, float(lon_g), map_x, map_y, map_w, map_h)
        stroke = "#1E3A5F" if lon_g == 0 else "#132842"
        lignes.append(f'<line x1="{gx}" y1="{map_y}" x2="{gx}" y2="{map_y + map_h}" stroke="{stroke}" stroke-width="1" stroke-dasharray="3,3"/>')
    for lat_g in (-60, -30, 0, 30, 60):
        _, gy = projeter_coord(float(lat_g), 0.0, map_x, map_y, map_w, map_h)
        stroke = "#1E3A5F" if lat_g == 0 else "#132842"
        lignes.append(f'<line x1="{map_x}" y1="{gy}" x2="{map_x + map_w}" y2="{gy}" stroke="{stroke}" stroke-width="1" stroke-dasharray="3,3"/>')

    # Silhouettes continentales
    for poly in CONTINENTS_LON_LAT:
        pts_str = " ".join(
            f"{projeter_coord(lat, lon, map_x, map_y, map_w, map_h)[0]},{projeter_coord(lat, lon, map_x, map_y, map_w, map_h)[1]}"
            for lon, lat in poly
        )
        lignes.append(f'<polygon points="{pts_str}" fill="#1E293B" stroke="#334155" stroke-width="1.2"/>')

    # Trajectoire orthodromique stylisée entre le Bateau GPS et les alertes majeures
    bx, by = projeter_coord(SCENARIO["gps_lat"], SCENARIO["gps_lon"], map_x, map_y, map_w, map_h)
    for al in SCENARIO["alertes"]:
        ax, ay = projeter_coord(al["lat"], al["lon"], map_x, map_y, map_w, map_h)
        lignes.append(
            f'<line x1="{bx}" y1="{by}" x2="{ax}" y2="{ay}" stroke="{al["couleur"]}" '
            f'stroke-width="1.2" stroke-opacity="0.55" stroke-dasharray="4,4"/>'
        )
        # Ondes concentriques de catastrophe
        for r_mul, opa in ((26, "0.20"), (16, "0.40"), (8, "0.85")):
            lignes.append(
                f'<circle cx="{ax}" cy="{ay}" r="{S(r_mul)}" fill="{al["couleur"]}" fill-opacity="{opa}" '
                f'stroke="{al["couleur"]}" stroke-width="1.5"/>'
            )
        lignes.append(
            f'<text x="{ax + X(10)}" y="{ay - Y(8)}" fill="#F8FAFC" font-family="Segoe UI, sans-serif" '
            f'font-size="{S(10)}" font-weight="bold">{al["categorie"]} · {al["valeur"]}</text>'
        )

    # Marqueur du Bateau / Position GPS Live
    lignes.append(
        f'<circle cx="{bx}" cy="{by}" r="{S(12)}" fill="#0284C7" fill-opacity="0.35" stroke="#38BDF8" stroke-width="2"/>'
        f'<circle cx="{bx}" cy="{by}" r="{S(5)}" fill="#38BDF8" stroke="#FFFFFF" stroke-width="1.5"/>'
        f'<text x="{bx + X(10)}" y="{by + Y(4)}" fill="#38BDF8" font-family="Consolas, monospace" '
        f'font-size="{S(11)}" font-weight="bold">NAVIRE GPS (48°51\'N 002°21\'E)</text>'
    )
    lignes.append("</g>")

    # Titre incrusté en haut de la carte
    lignes.append(
        f'<rect x="{map_x + X(10)}" y="{map_y + Y(8)}" width="{X(350)}" height="{Y(22)}" rx="{S(5)}" '
        f'fill="#0F172A" fill-opacity="0.85" stroke="#334155" stroke-width="1"/>'
        f'<text x="{map_x + X(18)}" y="{map_y + Y(23)}" fill="#E2E8F0" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">CARTE MONDIALE DES RISQUES &amp; RELÈVEMENTS NAUTIQUES</text>'
    )

    # 3. PANNEAU MÉTÉO LOCALE, RADAR PLUIE 60 MIN & ÉTAT DE LA MER (Colonne Droite)
    lignes.append(
        f'<rect x="{right_x}" y="{right_y}" width="{right_w}" height="{right_h}" rx="{S(12)}" '
        f'fill="url(#glassCard)" stroke="#334155" stroke-width="1.5"/>'
    )
    za = SCENARIO["zone_active"]
    lignes.append(
        f'<text x="{right_x + X(14)}" y="{right_y + Y(22)}" fill="#38BDF8" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">ZONE 0 (GPS MOBILE) · {za["nom"]}</text>'
        f'<text x="{right_x + X(14)}" y="{right_y + Y(56)}" fill="#F8FAFC" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(30)}" font-weight="bold">{za["temp_c"]:.1f}°C</text>'
        f'<text x="{right_x + X(135)}" y="{right_y + Y(42)}" fill="#94A3B8" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}">Ressenti {za["ressenti_c"]:.1f}°C · Hum {za["humidite_pct"]}%</text>'
        f'<text x="{right_x + X(135)}" y="{right_y + Y(58)}" fill="#E2E8F0" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">Pression {za["pression_hpa"]:.1f} hPa · UV {za["uv"]}</text>'
        f'<text x="{right_x + X(14)}" y="{right_y + Y(76)}" fill="#FDE68A" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">{za["condition"]}</text>'
        f'<text x="{right_x + X(14)}" y="{right_y + Y(94)}" fill="#CBD5E1" font-family="Consolas, monospace" '
        f'font-size="{S(11)}">Vent {za["vent_kt"]} kt (Raf. {za["rafales_kt"]} kt) · {za["vent_dir"]}</text>'
    )

    # Sous-carte Radar Pluie 60 min (12 barres de 5 min)
    rain_y = right_y + Y(106)
    rain_h = Y(112)
    lignes.append(
        f'<rect x="{right_x + X(10)}" y="{rain_y}" width="{right_w - X(20)}" height="{rain_h}" rx="{S(8)}" '
        f'fill="#0B1528" stroke="#1E3A5F" stroke-width="1"/>'
        f'<text x="{right_x + X(18)}" y="{rain_y + Y(18)}" fill="#38BDF8" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">NOWCAST PLUIE 60 MIN (PAS DE 5 MIN)</text>'
        f'<text x="{right_x + X(18)}" y="{rain_y + Y(33)}" fill="#93C5FD" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(10)}">{za["pluie_msg"]}</text>'
    )
    bar_x0 = right_x + X(20)
    bar_w = max(8, (right_w - X(44)) // 12 - X(4))
    bar_step = (right_w - X(40)) // 12
    bar_base_y = rain_y + rain_h - Y(18)
    max_bar_h = Y(52)
    for idx_b, val_mm in enumerate(za["pluie_5m"]):
        bh = max(3, int(round((val_mm / 9.0) * max_bar_h)))
        bx_bar = bar_x0 + idx_b * bar_step
        b_col = "#38BDF8" if val_mm < 4.0 else ("#F59E0B" if val_mm < 7.0 else "#EF4444")
        lignes.append(
            f'<rect x="{bx_bar}" y="{bar_base_y - bh}" width="{bar_w}" height="{bh}" rx="{S(2)}" fill="{b_col}"/>'
        )
        if idx_b % 2 == 0:
            lignes.append(
                f'<text x="{bx_bar + bar_w // 2}" y="{bar_base_y + Y(12)}" text-anchor="middle" fill="#64748B" '
                f'font-family="Consolas, monospace" font-size="{S(8)}">+{idx_b * 5}m</text>'
            )

    # Sous-carte État de la Mer & Houle (Mode Marine)
    sea_y = rain_y + rain_h + Y(8)
    sea_h = right_h - (sea_y - right_y) - Y(10)
    marin = za["marin"]
    lignes.append(
        f'<rect x="{right_x + X(10)}" y="{sea_y}" width="{right_w - X(20)}" height="{sea_h}" rx="{S(8)}" '
        f'fill="#091E26" stroke="#0D9488" stroke-width="1.2"/>'
        f'<text x="{right_x + X(18)}" y="{sea_y + Y(19)}" fill="#2DD4BF" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">ÉTAT DE LA MER &amp; HOULE (MODE MARINE)</text>'
        f'<text x="{right_x + X(18)}" y="{sea_y + Y(42)}" fill="#F8FAFC" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(18)}" font-weight="bold">Hs {marin["vagues_m"]} m · Tp {marin["periode_s"]} s ({marin["dir_vagues"]})</text>'
        f'<text x="{right_x + X(18)}" y="{sea_y + Y(62)}" fill="#99F6E4" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}">Houle primaire : {marin["houle_m"]} m · Courant : {marin["courant_kt"]} kt</text>'
        f'<text x="{right_x + X(18)}" y="{sea_y + Y(82)}" fill="#FDE68A" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(11)}" font-weight="bold">Échelle Douglas : {marin["douglas"]}</text>'
    )

    # 4. TICKER D'ALERTES CATASTROPHES & RELÈVEMENTS HAVERSINE TEMPS RÉEL (Bandeau Inférieur)
    lignes.append(
        f'<rect x="{bot_x}" y="{bot_y}" width="{bot_w}" height="{bot_h}" rx="{S(12)}" '
        f'fill="url(#glassCard)" stroke="#334155" stroke-width="1.5"/>'
        f'<text x="{bot_x + X(14)}" y="{bot_y + Y(20)}" fill="#F8FAFC" font-family="Segoe UI, sans-serif" '
        f'font-size="{S(12)}" font-weight="bold">'
        f'ALERTES CATASTROPHES MONDIALES ACTIVES (TRIÉES PAR CRITICITÉ &amp; PROXIMITÉ HAVERSINE)</text>'
    )

    row_y = bot_y + Y(28)
    row_h = (bot_h - Y(36)) // 3
    for al in SCENARIO["alertes"]:
        lignes.append(
            f'<rect x="{bot_x + X(12)}" y="{row_y}" width="{bot_w - X(24)}" height="{row_h - Y(4)}" rx="{S(6)}" '
            f'fill="#0F172A" stroke="{al["couleur"]}" stroke-width="1.2"/>'
            f'<rect x="{bot_x + X(12)}" y="{row_y}" width="{X(88)}" height="{row_h - Y(4)}" rx="{S(6)}" fill="{al["couleur"]}"/>'
            f'<text x="{bot_x + X(56)}" y="{row_y + row_h // 2 + Y(2)}" text-anchor="middle" fill="#0F172A" '
            f'font-family="Segoe UI, sans-serif" font-size="{S(10)}" font-weight="bold">{al["categorie"]}</text>'
            f'<text x="{bot_x + X(112)}" y="{row_y + Y(15)}" fill="#F8FAFC" font-family="Segoe UI, sans-serif" '
            f'font-size="{S(11)}" font-weight="bold">{al["titre"]}</text>'
            f'<text x="{bot_x + X(112)}" y="{row_y + Y(30)}" fill="#94A3B8" font-family="Segoe UI, sans-serif" '
            f'font-size="{S(10)}">Sources : {al["source"]} · Intensité : {al["valeur"]}</text>'
            f'<text x="{bot_x + bot_w - X(24)}" y="{row_y + Y(22)}" text-anchor="end" fill="#38BDF8" '
            f'font-family="Consolas, monospace" font-size="{S(11)}" font-weight="bold">'
            f'Dist. {al["dist_km"]} km ({al["dist_nm"]} NM) · Cap {al["azimut"]:03d}° ({al["cardinal"]})</text>'
        )
        row_y += row_h

    lignes.append("</svg>")
    return "\n".join(lignes)


def generer_png_pillow(code_carte: str, cfg: dict, chemin_png: pathlib.Path) -> bool:
    """Génère également un aperçu PNG matriciel si Pillow (PIL) est installé."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return False

    W = cfg["largeur"]
    H = cfg["hauteur"]
    sx = W / 1024.0
    sy = H / 600.0

    def X(v: float) -> int:
        return int(round(v * sx))

    def Y(v: float) -> int:
        return int(round(v * sy))

    img = Image.new("RGB", (W, H), (11, 17, 30))
    draw = ImageDraw.Draw(img)

    map_x = (16 * W + 512) // 1024
    map_y = (64 * H + 300) // 600
    map_w = (640 * W + 512) // 1024
    map_h = (360 * H + 300) // 600
    right_x = map_x + map_w + X(12)
    right_w = W - right_x - X(16)
    bot_y = map_y + map_h + Y(10)
    bot_w = W - 2 * map_x
    bot_h = H - bot_y - Y(12)

    # Bandeau haut, Carte, Panneau Météo/Marine, Ticker bas
    draw.rounded_rectangle([map_x, Y(10), map_x + bot_w, Y(56)], radius=8, fill=(24, 34, 52), outline=(51, 65, 85), width=2)
    draw.rounded_rectangle([map_x, map_y, map_x + map_w, map_y + map_h], radius=10, fill=(9, 22, 42), outline=(30, 58, 95), width=2)
    draw.rounded_rectangle([right_x, map_y, right_x + right_w, map_y + map_h], radius=10, fill=(24, 34, 52), outline=(51, 65, 85), width=2)
    draw.rounded_rectangle([map_x, bot_y, map_x + bot_w, bot_y + bot_h], radius=10, fill=(24, 34, 52), outline=(51, 65, 85), width=2)

    # Continents
    for poly in CONTINENTS_LON_LAT:
        pts = [projeter_coord(lat, lon, map_x, map_y, map_w, map_h) for lon, lat in poly]
        draw.polygon(pts, fill=(30, 41, 59), outline=(51, 65, 85))

    # Bateau et alertes sur la carte
    bx, by = projeter_coord(SCENARIO["gps_lat"], SCENARIO["gps_lon"], map_x, map_y, map_w, map_h)
    for al in SCENARIO["alertes"]:
        ax, ay = projeter_coord(al["lat"], al["lon"], map_x, map_y, map_w, map_h)
        col = (239, 68, 68) if al["severite"] == "ROUGE" else (245, 158, 11)
        draw.line([(bx, by), (ax, ay)], fill=col, width=1)
        for r in (18, 11, 5):
            draw.ellipse([ax - r, ay - r, ax + r, ay + r], outline=col, width=2)
    draw.ellipse([bx - 6, by - 6, bx + 6, by + 6], fill=(56, 189, 248), outline=(255, 255, 255), width=2)

    # Textes clés
    draw.text((map_x + X(14), Y(18)), f"{SCENARIO['heure_utc']} | {SCENARIO['gps_nautique']}", fill=(248, 250, 252))
    draw.text((map_x + X(14), Y(36)), f"{cfg['nom_ecran']} | {SCENARIO['kp_label']}", fill=(56, 189, 248))
    za = SCENARIO["zone_active"]
    draw.text((right_x + X(14), map_y + Y(16)), f"{za['nom']} : {za['temp_c']} C", fill=(248, 250, 252))
    draw.text((right_x + X(14), map_y + Y(40)), f"Pluie : {za['pluie_msg']}", fill=(147, 197, 253))
    draw.text((right_x + X(14), map_y + Y(70)), f"Mer : Hs {za['marin']['vagues_m']}m Tp {za['marin']['periode_s']}s", fill=(45, 212, 191))

    ty = bot_y + Y(24)
    for al in SCENARIO["alertes"]:
        draw.text(
            (map_x + X(16), ty),
            f"[{al['severite']}] {al['categorie']} - {al['titre']} ({al['dist_km']} km / {al['dist_nm']} NM {al['cardinal']})",
            fill=(248, 250, 252),
        )
        ty += Y(36)

    img.save(chemin_png, format="PNG")
    return True


def generer_tous_les_rendus() -> list[pathlib.Path]:
    DOSSIER_RENDUS.mkdir(parents=True, exist_ok=True)
    fichiers_crees: list[pathlib.Path] = []
    for code, cfg in CIBLES.items():
        svg_txt = generer_svg_cible(code, cfg)
        chemin_svg = DOSSIER_RENDUS / f"{cfg['fichier_base']}.svg"
        chemin_svg.write_text(svg_txt, encoding="utf-8")
        fichiers_crees.append(chemin_svg)

        chemin_png = DOSSIER_RENDUS / f"{cfg['fichier_base']}.png"
        if generer_png_pillow(code, cfg, chemin_png):
            fichiers_crees.append(chemin_png)
    return fichiers_crees


if __name__ == "__main__":
    crees = generer_tous_les_rendus()
    for p in crees:
        print(f"Rendu généré : {p}")
