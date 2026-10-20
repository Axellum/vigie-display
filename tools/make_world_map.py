"""Draws the world map of the screen (firmware/images/world_map.png).

Source: Natural Earth 1:110m land (public domain), copied in tools/data/ne_110m_land.geojson.
Projection: the screen's one, read from firmware/core/vigie_text.h (size and latitude band),
so that a hazard dot lands at the right place on the map.

Usage: python tools/make_world_map.py           (writes the image)
       python tools/make_world_map.py --check   (fails if the image is out of date)
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
HEADER = REPO / "firmware" / "core" / "vigie_text.h"
SOURCE = REPO / "tools" / "data" / "ne_110m_land.geojson"
OUTPUT = REPO / "firmware" / "images" / "world_map.png"

# Colours: the screen's palette (firmware/display/vigie_screen.cpp, kOcean).
OCEAN = (14, 21, 29)
LAND = (36, 49, 63)
COAST = (58, 75, 94)
GRATICULE = (22, 31, 41)
SUPERSAMPLING = 4  # drawn at 4x then reduced: smooth edges


def constants() -> dict[str, float]:
    """Reads the kVigieMap* constants of the C++ header (single source)."""
    text = HEADER.read_text(encoding="utf-8")
    values = {}
    for name in ("Width", "Height", "LatNorth", "LatSouth"):
        match = re.search(rf"kVigieMap{name}\s*=\s*(-?[0-9.]+)", text)
        if match is None:
            sys.exit(f"kVigieMap{name} not found in {HEADER}")
        values[name] = float(match.group(1))
    return values


def polygons(source: Path):
    """Outer ring and holes of every land mass, as (lon, lat)."""
    data = json.loads(source.read_text(encoding="utf-8"))
    for feature in data["features"]:
        geometry = feature["geometry"]
        if geometry["type"] == "Polygon":
            yield geometry["coordinates"]
        elif geometry["type"] == "MultiPolygon":
            yield from geometry["coordinates"]


def draw(c: dict[str, float]) -> Image.Image:
    width, height = int(c["Width"]), int(c["Height"])
    k = SUPERSAMPLING
    img = Image.new("RGB", (width * k, height * k), OCEAN)
    d = ImageDraw.Draw(img)

    def px(lon: float, lat: float) -> tuple[float, float]:
        x = (lon + 180.0) / 360.0 * width * k
        y = (c["LatNorth"] - lat) / (c["LatNorth"] - c["LatSouth"]) * height * k
        return x, y

    # Graticule every 30° of longitude, and at 30° S, the equator, 30° N and 60° N.
    for lon in range(-150, 180, 30):
        x, _ = px(lon, 0)
        d.line([(x, 0), (x, height * k)], fill=GRATICULE, width=k)
    for lat in (-30, 0, 30, 60):
        _, y = px(0, lat)
        d.line([(0, y), (width * k, y)], fill=GRATICULE, width=k)

    for rings in polygons(SOURCE):
        d.polygon([px(lon, lat) for lon, lat in rings[0]], fill=LAND, outline=COAST, width=k)
        for hole in rings[1:]:
            d.polygon([px(lon, lat) for lon, lat in hole], fill=OCEAN, outline=COAST, width=k)

    return img.resize((width, height), Image.Resampling.LANCZOS)


def png_bytes(img: Image.Image) -> bytes:
    stream = io.BytesIO()
    img.save(stream, format="PNG", optimize=True)
    return stream.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description="Draws the world map of the screen.")
    parser.add_argument("--check", action="store_true", help="fail if the image is out of date")
    args = parser.parse_args()

    img = draw(constants())
    if args.check:
        if not OUTPUT.exists():
            print(f"{OUTPUT} missing: run python tools/make_world_map.py")
            return 1
        current = Image.open(OUTPUT).convert("RGB")
        if current.size != img.size or current.tobytes() != img.tobytes():
            print(f"{OUTPUT} is out of date: run python tools/make_world_map.py")
            return 1
        print(f"{OUTPUT.name} up to date ({img.size[0]}x{img.size[1]})")
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    content = png_bytes(img)
    OUTPUT.write_bytes(content)
    print(f"{OUTPUT}: {img.size[0]}x{img.size[1]}, {len(content)} bytes, sha256 {hashlib.sha256(content).hexdigest()[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
