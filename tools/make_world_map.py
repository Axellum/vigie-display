"""Draws the world maps of the screen (firmware/images/world_map_<w>x<h>.png).

Source: Natural Earth 1:110m land (public domain), copied in tools/data/ne_110m_land.geojson.
Projection: the screen's one. The latitude band is read from firmware/core/vigie_text.h and
the size from every board layout (`board_map_width` / `board_map_height` of
firmware/packages/layout-*.yaml), so that a hazard dot lands at the right place on the map.

Usage: python tools/make_world_map.py           (writes the images)
       python tools/make_world_map.py --check   (fails if an image is missing or out of date)
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
PACKAGES = REPO / "firmware" / "packages"
SOURCE = REPO / "tools" / "data" / "ne_110m_land.geojson"
IMAGES = REPO / "firmware" / "images"

# Colours: the screen's palette (firmware/display/vigie_screen.cpp, kOcean).
OCEAN = (14, 21, 29)
LAND = (36, 49, 63)
COAST = (58, 75, 94)
GRATICULE = (22, 31, 41)
SUPERSAMPLING = 4  # drawn at 4x then reduced: smooth edges


def latitude_band() -> dict[str, float]:
    """Reads the kVigieMapLat* constants of the C++ header (single source)."""
    text = HEADER.read_text(encoding="utf-8")
    values = {}
    for name in ("LatNorth", "LatSouth"):
        match = re.search(rf"kVigieMap{name}\s*=\s*(-?[0-9.]+)", text)
        if match is None:
            sys.exit(f"kVigieMap{name} not found in {HEADER}")
        values[name] = float(match.group(1))
    return values


def map_sizes() -> list[tuple[int, int]]:
    """Map size of every board layout (single source: its substitutions)."""
    sizes = set()
    for package in sorted(PACKAGES.glob("layout-*.yaml")):
        text = package.read_text(encoding="utf-8")
        width = re.search(r'^\s*board_map_width:\s*"?(\d+)"?', text, re.M)
        height = re.search(r'^\s*board_map_height:\s*"?(\d+)"?', text, re.M)
        if width is None or height is None:
            sys.exit(f"{package.name}: board_map_width / board_map_height missing")
        sizes.add((int(width.group(1)), int(height.group(1))))
    return sorted(sizes)


def output(width: int, height: int) -> Path:
    return IMAGES / f"world_map_{width}x{height}.png"


def polygons(source: Path):
    """Outer ring and holes of every land mass, as (lon, lat)."""
    data = json.loads(source.read_text(encoding="utf-8"))
    for feature in data["features"]:
        geometry = feature["geometry"]
        if geometry["type"] == "Polygon":
            yield geometry["coordinates"]
        elif geometry["type"] == "MultiPolygon":
            yield from geometry["coordinates"]


def draw(band: dict[str, float], width: int, height: int) -> Image.Image:
    k = SUPERSAMPLING
    img = Image.new("RGB", (width * k, height * k), OCEAN)
    d = ImageDraw.Draw(img)

    def px(lon: float, lat: float) -> tuple[float, float]:
        x = (lon + 180.0) / 360.0 * width * k
        y = (band["LatNorth"] - lat) / (band["LatNorth"] - band["LatSouth"]) * height * k
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
    parser = argparse.ArgumentParser(description="Draws the world maps of the screen.")
    parser.add_argument("--check", action="store_true", help="fail if an image is missing or out of date")
    args = parser.parse_args()

    band = latitude_band()
    sizes = map_sizes()
    status = 0
    for width, height in sizes:
        img = draw(band, width, height)
        path = output(width, height)
        if args.check:
            if not path.exists():
                print(f"{path} missing: run python tools/make_world_map.py")
                status = 1
                continue
            current = Image.open(path).convert("RGB")
            if current.size != img.size or current.tobytes() != img.tobytes():
                print(f"{path} is out of date: run python tools/make_world_map.py")
                status = 1
                continue
            print(f"{path.name} up to date")
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        content = png_bytes(img)
        path.write_bytes(content)
        print(f"{path}: {len(content)} bytes, sha256 {hashlib.sha256(content).hexdigest()[:16]}")

    expected = {output(w, h).name for w, h in sizes}
    for stale in sorted(IMAGES.glob("world_map*.png")):
        if stale.name not in expected:
            print(f"{stale} is used by no board layout: remove it")
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
