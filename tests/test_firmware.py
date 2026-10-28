"""Guards of the Vigie display firmware, run on a plain PC (no ESPHome toolchain).

- The firmware never talks to Home Assistant.
- firmware/core stays pure C++ (no ESPHome, LVGL, allocation) and every local include is
  flat (ESPHome copies `includes:` files flat into its build).
- Every file listed in an `includes:` exists; every C++ file of firmware/ is listed.
- Every MDI icon drawn by the C++ is in a font's `glyphs:` and every listed glyph is drawn.
- Every board package gives the layout sizes; each world map image matches its generator,
  the projection constants and a board's map size.
- The C++ tests of tools/ compile and pass (g++, clang++, or zig through `python -m ziglang`).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

import pytest
import yaml

REPO = Path(__file__).resolve().parent.parent
FIRMWARE = REPO / "firmware"
CORE = FIRMWARE / "core"
FIXTURE = REPO / "tests" / "fixtures" / "api_frame.txt"
YAML_FILES = [REPO / "vigie.yaml", REPO / "vigie-render.yaml", *sorted((FIRMWARE / "packages").glob("*.yaml"))]
CPP_FILES = sorted(FIRMWARE.rglob("*.cpp")) + sorted(FIRMWARE.rglob("*.h"))


class EsphomeLoader(yaml.SafeLoader):
    """Loads ESPHome YAML, keeping tags (!include, !lambda…) as plain values."""


EsphomeLoader.add_multi_constructor("!", lambda loader, suffix, node: loader.construct_scalar(node)
                                    if isinstance(node, yaml.ScalarNode) else None)


def load(path: Path) -> dict:
    return yaml.load(path.read_text(encoding="utf-8"), Loader=EsphomeLoader) or {}


def strip_comments(code: str) -> str:
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", code, flags=re.S)


def test_no_home_assistant():
    for path in YAML_FILES:
        config = load(path)
        assert "api" not in config, f"{path.name}: no `api:` (the display does not talk to Home Assistant)"
        assert "homeassistant" not in path.read_text(encoding="utf-8").lower().replace(
            "no home assistant", ""
        ), f"{path.name} mentions homeassistant"
    for path in CPP_FILES:
        assert "homeassistant" not in strip_comments(path.read_text(encoding="utf-8")), path.name


def test_core_is_pure_cpp():
    allowed_std = {"cstddef", "cstdint", "cmath", "cstdio", "cstdlib", "cstring", "limits"}
    for path in sorted(CORE.glob("*.*")):
        code = strip_comments(path.read_text(encoding="utf-8"))
        includes = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', code, re.M))
        local = {i for i in includes if i.endswith(".h")}
        assert includes - local <= allowed_std, f"{path.name}: {includes - local - allowed_std}"
        for header in local:
            assert (CORE / header).exists(), f"{path.name} includes {header}, not a core header"
        banned = re.findall(r"\blv_\w+|\besphome\b|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\(|\bnew\b|\bdelete\b|std::string|std::vector", code)
        assert not banned, f"{path.name}: {sorted(set(banned))}"


def test_local_includes_are_flat():
    headers = [p.name for p in FIRMWARE.rglob("*.h")]
    assert len(headers) == len(set(headers)), "a header name must be unique across firmware/ folders"
    names = set(headers)
    for path in CPP_FILES:
        for header in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(encoding="utf-8"), re.M):
            if header.startswith("esphome/") or header in ("lvgl.h",) or not header.endswith(".h"):
                continue
            if header.split("/")[-1] in names:
                assert "/" not in header, f'{path.name}: #include "{header}" must not name a folder'


def listed_includes() -> set[str]:
    listed: set[str] = set()
    for path in YAML_FILES:
        for item in (load(path).get("esphome") or {}).get("includes") or []:
            listed.add(item)
    return listed


def test_includes_exist_and_cover_every_source():
    listed = listed_includes()
    for item in listed:
        assert (REPO / item).is_file(), f"listed in includes: but missing: {item}"
    sources = {p.relative_to(REPO).as_posix() for p in CPP_FILES}
    assert sources == listed, f"not listed: {sorted(sources - listed)}; listed but absent: {sorted(listed - sources)}"


def font_glyphs() -> dict[str, set[int]]:
    fonts = load(FIRMWARE / "packages" / "vigie-screen.yaml")["font"]
    out = {}
    for font in fonts:
        if "materialdesignicons" in font["file"]:
            out[font["id"]] = {ord(g) for g in font.get("glyphs", [])}
    return out


def test_mdi_glyphs_listed_and_used():
    fonts = font_glyphs()
    assert set(fonts) == {"font_icons", "font_weather_icon"}
    used = set()
    for path in CPP_FILES:
        used |= {int(code, 16) for code in re.findall(r"\\U000(F[0-9A-Fa-f]{4})", strip_comments(path.read_text(encoding="utf-8")))}
    listed = fonts["font_icons"] | fonts["font_weather_icon"]
    missing = used - listed
    assert not missing, f"drawn but not in any MDI font (renders blank): {[hex(c) for c in sorted(missing)]}"
    unused = listed - used
    assert not unused, f"listed but never drawn: {[hex(c) for c in sorted(unused)]}"
    # Every weather icon exists at both sizes (big current icon, small forecast icon).
    weather = set(re.findall(r'"\\U000(F[0-9A-F]{4})"', (CORE / "vigie_text.cpp").read_text(encoding="utf-8").split("vigie_hazard_name")[0]))
    assert {int(c, 16) for c in weather} <= fonts["font_weather_icon"] & fonts["font_icons"]


LAYOUT_KEYS = {
    "board_screen_width", "board_screen_height", "board_panel_width", "board_panel_height", "board_rotation",
    "board_layout_scale", "board_map_width", "board_map_height", "board_font_small", "board_font_body",
    "board_font_title", "board_font_temperature", "board_font_icons", "board_font_weather_icon",
}
BOARD_KEYS = {"board_panel_off_mode", "board_panel_dark_during_ota"}


def test_board_packages_define_the_layout():
    """Every board has a layout (sizes the screen package and the C++ need) and includes it."""
    packages = FIRMWARE / "packages"
    boards = sorted(p.stem.removeprefix("board-") for p in packages.glob("board-*.yaml"))
    layouts = sorted(p.stem.removeprefix("layout-") for p in packages.glob("layout-*.yaml"))
    assert boards == layouts, "one layout-<board>.yaml per board-<board>.yaml"
    assert set(boards) >= {"waveshare-7b", "tab5"}
    for board in boards:
        package = load(packages / f"board-{board}.yaml")
        assert package["packages"]["layout"] == f"layout-{board}.yaml", f"board-{board}.yaml must include its layout"
        assert not BOARD_KEYS - set(package.get("substitutions") or {}), f"board-{board}.yaml"
        subs = load(packages / f"layout-{board}.yaml")["substitutions"]
        assert set(subs) == LAYOUT_KEYS, f"layout-{board}.yaml: {sorted(set(subs) ^ LAYOUT_KEYS)}"
        assert not set(subs) & set(package.get("substitutions") or {}), f"board-{board}.yaml repeats its layout"
        width, height = int(subs["board_map_width"]), int(subs["board_map_height"])
        assert abs(width / height - 2.5) < 0.01, f"layout-{board}.yaml: the map must be 2.5 : 1 (360° × 144°)"
        assert (FIRMWARE / "images" / f"world_map_{width}x{height}.png").is_file(), board
    # The 7B is the reference layout: scale 100, and the C++ defaults are its values.
    reference = load(packages / "layout-waveshare-7b.yaml")["substitutions"]
    assert reference["board_layout_scale"] == "100"
    header = (CORE / "vigie_text.h").read_text(encoding="utf-8")
    assert f"#define VIGIE_MAP_WIDTH {reference['board_map_width']}" in header
    assert f"#define VIGIE_MAP_HEIGHT {reference['board_map_height']}" in header


def test_world_map_up_to_date():
    result = subprocess.run([sys.executable, str(REPO / "tools" / "make_world_map.py"), "--check"], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_fixture_is_a_signed_frame():
    data = FIXTURE.read_bytes()
    body, _, trailer = data.rpartition(b"END|")
    assert body.endswith(b"\n") and len(trailer.strip()) == 8
    assert f"{zlib.crc32(body) & 0xFFFFFFFF:08X}" == trailer.strip().decode().upper()
    assert b"\r" not in data, "the fixture must keep LF line endings (.gitattributes)"


def cxx_command() -> list[str] | None:
    if os.environ.get("VIGIE_CXX"):
        return os.environ["VIGIE_CXX"].split()
    for name in ("g++", "clang++"):
        if shutil.which(name):
            return [name]
    try:
        import ziglang  # noqa: F401
    except ImportError:
        return None
    return [sys.executable, "-m", "ziglang", "c++"]


CPP_TESTS = {
    "test_vigie_model": ["firmware/core/vigie_model.cpp"],
    "test_vigie_text": ["firmware/core/vigie_text.cpp", "firmware/core/vigie_model.cpp"],
    "test_vigie_api_client": ["firmware/core/vigie_api_client.cpp", "firmware/core/vigie_text.cpp", "firmware/core/vigie_model.cpp"],
}


def test_every_cpp_test_is_run():
    assert {p.stem for p in (REPO / "tools").glob("test_*.cpp")} == set(CPP_TESTS)


@pytest.mark.parametrize("name", sorted(CPP_TESTS))
def test_cpp(name, tmp_path):
    cxx = cxx_command()
    if cxx is None:
        pytest.skip("no C++ compiler (g++, clang++ or `pip install ziglang`)")
    exe = tmp_path / (name + (".exe" if os.name == "nt" else ""))
    cmd = [*cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Ifirmware/core", "-Itools",
           f"tools/{name}.cpp", *CPP_TESTS[name], "-o", str(exe)]
    build = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    assert build.returncode == 0, build.stderr[-4000:]
    run = subprocess.run([str(exe), str(FIXTURE)], cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace")
    assert run.returncode == 0, run.stdout[-4000:]
    assert run.stdout.strip().startswith("OK:"), run.stdout
