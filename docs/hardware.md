# Hardware

Vigie display runs on two ESP32-P4 touch screens. Both use an ESP32-C6 co-processor for Wi-Fi
(over SDIO) and a MIPI-DSI panel. Pick one with `vigie_board`:

| | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B | M5Stack Tab5 |
| :--- | :--- | :--- |
| `vigie_board` | `waveshare-7b` (default) | `tab5` |
| Screen | 7" IPS, 1024×600, landscape panel | 5" IPS, 1280×720, portrait panel turned by LVGL (`rotation: 270`) |
| Layout | the reference one (scale 100, map 664×266) | 120 % (map 800×320, weather panel wider) |
| ESP32-P4 | v3, 400 MHz, 32 MB flash | v1.3, 360 MHz, 16 MB flash |
| PSRAM | 32 MB, hex, 200 MHz | 32 MB, hex, 200 MHz |
| Touch | GT911 | in the panel chip (ST7123 / ST7121) or GT911 (ILI9881C) |
| Vigie firmware (CI build, 2026-10-28) | 2.0 MB (12 % of its app partition), RAM 25.5 % | 2.2 MB (27 % of its app partition), RAM 29.9 % |
| Status | ✅ tested on the board (2026-10-20) | 🧪 compiled; board settings tested daily in the Tab5 project, Vigie itself not run on a Tab5 yet |

Board packages: `firmware/packages/board-<board>.yaml` (hardware) and
`firmware/packages/layout-<board>.yaml` (screen size, rotation, map and fonts). Both sets of
board facts come from the [M5-Tab5-ESPHome-LVGL](https://github.com/Axellum/M5-Tab5-ESPHome-LVGL)
project, where they were measured or tested on real boards; do not change a pin, a reset or a
delay without a new test on the board.

Neither board needs anything soldered or plugged in: USB-C for power and the first flash, Wi-Fi
for the rest.

## Waveshare ESP32-P4-WIFI6-Touch-LCD-7B

See the Tab5 project's `docs/waveshare-7b.md` for the full write-up.

| Part | Detail |
| :--- | :--- |
| SoC | ESP32-P4 v3 at 400 MHz (`engineering_sample: false`), 32 MB flash, 32 MB hex PSRAM at 200 MHz |
| Display | 7" IPS 1024×600, EK79007 over MIPI-DSI, reset GPIO33 (driven by ESPHome's `mipi_dsi` model), PHY supply LDO3 2.5 V |
| Backlight | LEDC on GPIO32, inverted, 5 kHz; analog dimming that never goes fully dark (hence `panel_power.cpp`) |
| Touch | GT911 on I²C GPIO7/GPIO8, `reset_pin: GPIO23` required, mirrored X and Y |
| Wi-Fi | ESP32-C6 over SDIO (GPIO14-19, reset GPIO54), `active_high: true` |
| Audio (not used yet) | ES8311 DAC + NS4150B amplifier (enable GPIO53); `volume_max: 75%` = 0 dB, 100 % saturates (+32 dB) |
| Microphones (not used) | ES7210, two microphones |
| Connectors for a GPS (not used yet) | P3 UART header (GPIO4 RX / GPIO5 TX), RS485 terminal (GPIO26/27) |
| Battery (not used) | 1S Li-ion on J4, voltage on GPIO20 through a 200k/100k divider; RTC holder: ML1220 rechargeable only, never CR1220 |

### Known traps (already handled)

- **ESPHome 2026.9.0** crashes at display start on a P4 v3: 2026.9.1 or newer, checked at compile
  time (`board_7b_version.h`).
- **Reboot loop of the Wi-Fi link**: after a few software restarts the SDIO link to the C6 may not
  come back; a CPU reset does not clear it, a CORE reset does (`c6_link.cpp`, 20 restarts in a row
  without failure).
- **Screen off**: the backlight alone leaves the image visible; the panel gets a black veil and the
  DCS commands "display off" + "sleep in" (`panel_power.cpp`, mode 2), and stays dark during a
  firmware update (it flickered).
- **Orientation**: `board_rotation: 0` is the panel's native orientation (no software rotation, 69 ms
  per full frame instead of 83 ms with 180). For a board mounted the other way up:
  `-s board_rotation 180`.

## M5Stack Tab5

| Part | Detail |
| :--- | :--- |
| SoC | ESP32-P4 v1.3 at 360 MHz (400 MHz needs a v3 chip), 16 MB flash, 32 MB hex PSRAM at 200 MHz |
| Display | 5" IPS 720×1280 portrait panel over MIPI-DSI, shown in landscape by LVGL (`rotation: 270`); reset through I/O expander `pi4ioe1` pin 4; PHY supply LDO3 2.5 V |
| I/O expanders | two PI4IOE5V6408 on I²C (0x43, 0x44): panel reset, touch reset |
| I²C | GPIO31 (SDA) / GPIO32 (SCL), 400 kHz |
| Backlight | LEDC on GPIO22, 1 kHz |
| Touch | interrupt GPIO23, reset `pi4ioe1` pin 5; chip depends on the revision (below) |
| Wi-Fi | ESP32-C6 over SDIO (GPIO8-13, reset GPIO15), `active_high: true` |
| Not used by Vigie | ES8388 DAC and speaker, ES7210 microphones, IMU, RTC, battery, camera |

### Revisions

M5Stack has shipped the Tab5 with three display controllers. The chip is printed on the
sticker on the back, above the Espressif logo (a unit labelled "ST7123" may carry either an
ST7123 or an ST7121: try both).

| Display chip | Units made | Build with | Status |
| :--- | :--- | :--- | :--- |
| **ST7123** (display and touch in one chip) | 14/10/2025 → 28/04/2026 | default | board settings in daily use in the Tab5 project |
| **ST7121** (display and touch in one chip) | from 28/04/2026 | `-s board_tab5_panel M5STACK-TAB5-ST7121` | runs the Tab5 project on a user's unit (touch through the `st7123` driver) |
| **ILI9881C** + separate GT911 | 09/05/2025 → 14/10/2025 | `-s board_tab5_panel M5STACK-TAB5 -s board_tab5_touch gt911 -s board_tab5_touch_interval never` | compiles, never run |

### Known traps (already handled)

- **Black screen after a software restart**: the panel reset goes through the PI4IOE5V6408
  expander, which needs about a second after start-up. The board package blocks for 1 s at
  `on_boot` priority 700, before the display starts. It must stay a blocking C `delay()`: an
  ESPHome `delay:` action does not hold the display's setup back.
- **Wi-Fi link after a restart**: same C6 link as the 7B, same CORE reset (`c6_link.cpp`).
- **Screen off**: black veil only (`panel_power.cpp`, mode 0). On the ST7123 and ST7121 the touch
  lives in the panel chip: "sleep in" would probably stop the touch that wakes the screen.
- **No mirroring**: display or touch mirroring corrupts the image on an ST7121; rotation is done
  by LVGL and touches follow it.
