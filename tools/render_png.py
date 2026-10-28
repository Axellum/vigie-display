"""Turns the BMPs written by vigie-render.yaml into upright PNGs.

The snapshot is the panel as its driver sees it; LVGL draws on it turned by `board_rotation`
(firmware/packages/layout-<board>.yaml): the Tab5's portrait panel shows a landscape screen
turned by 270°. Each BMP is rotated back (Pillow turns counter-clockwise), checked against the
board's landscape size, saved as PNG, and the BMP removed.

Usage: python tools/render_png.py <folder> <board>
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent


def layout(board: str) -> dict[str, int]:
    text = (REPO / "firmware" / "packages" / f"layout-{board}.yaml").read_text(encoding="utf-8")
    values = {}
    for key in ("board_screen_width", "board_screen_height", "board_rotation"):
        match = re.search(rf'^\s*{key}:\s*"?(\d+)"?', text, re.M)
        if match is None:
            sys.exit(f"layout-{board}.yaml: {key} missing")
        values[key] = int(match.group(1))
    return values


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    folder, board = Path(sys.argv[1]), sys.argv[2]
    sizes = layout(board)
    expected = (sizes["board_screen_width"], sizes["board_screen_height"])
    bmps = sorted(folder.glob("*.bmp"))
    if not bmps:
        print(f"no BMP in {folder}")
        return 1
    for bmp in bmps:
        with Image.open(bmp) as image:
            angle = sizes["board_rotation"]
            upright = image.rotate(angle, expand=True) if angle else image.copy()
        if upright.size != expected:
            print(f"{bmp.name}: {upright.size[0]}x{upright.size[1]}, expected {expected[0]}x{expected[1]}")
            return 1
        png = bmp.with_suffix(".png")
        upright.convert("RGB").save(png, optimize=True)
        bmp.unlink()
        print(f"{png} ({expected[0]}x{expected[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
