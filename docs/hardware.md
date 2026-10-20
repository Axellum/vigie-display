# Hardware: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B

Facts below were measured or tested on a real board in the
[M5-Tab5-ESPHome-LVGL](https://github.com/Axellum/M5-Tab5-ESPHome-LVGL) project, which runs on the
same board (see its `docs/waveshare-7b.md` for the full write-up). Board package:
`firmware/packages/board-waveshare-7b.yaml`.

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

## Known traps (already handled)

- **ESPHome 2026.9.0** crashes at display start on a P4 v3: 2026.9.1 or newer, checked at compile
  time (`board_7b_version.h`).
- **Reboot loop of the Wi-Fi link**: after a few software restarts the SDIO link to the C6 may not
  come back; a CPU reset does not clear it, a CORE reset does (`c6_link.cpp`, 20 restarts in a row
  without failure).
- **Screen off**: the backlight alone leaves the image visible; the panel gets a black veil and the
  DCS commands "display off" + "sleep in" (`panel_power.cpp`, mode 2), and stays dark during a
  firmware update (it flickered).
- **Orientation**: `board_rotation: 0` is the panel's native orientation (no software rotation, 69 ms
  per full frame instead of 83 ms with 180).
