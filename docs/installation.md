# Installation

From a bare board to a Vigie screen: about 30 minutes, most of it the first build. Nothing to
solder; a computer, a USB-C cable that carries data, and a Wi-Fi network.

1. [What you need](#1-what-you-need)
2. [Choose the API](#2-choose-the-api)
3. [Install ESPHome](#3-install-esphome)
4. [Create the signing key](#4-create-the-signing-key)
5. [Build](#5-build)
6. [First flash over USB](#6-first-flash-over-usb)
7. [Wi-Fi](#7-wi-fi)
8. [Check it works](#8-check-it-works)
9. [Updates over Wi-Fi](#9-updates-over-wi-fi)
10. [If it does not work](#10-if-it-does-not-work)

## 1. What you need

| | |
| :--- | :--- |
| A screen | a **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** or an **M5Stack Tab5** ([hardware](hardware.md)). On a Tab5, read the display chip on the sticker on the back (ST7123, ST7121 or ILI9881C): it changes the build command |
| A computer | Windows, macOS or Linux, with Python 3.11 or newer; Chrome or Edge if you flash from the browser |
| A cable | USB-C that carries data (a charge-only cable shows no serial port) |
| Wi-Fi | 2.4 GHz or 5 GHz (the ESP32-C6 does both) |
| A vigie-api | public or your own, next step |

## 2. Choose the API

The screen shows what [vigie-api](https://github.com/Axellum/vigie-api) sends it: world hazards
and the weather of its watched zones. It asks for one compact frame every minute
(`GET /v1/embedded/frame`).

| | URL to build with | Zones on the screen |
| :--- | :--- | :--- |
| **Public API** (personal, non-commercial project; rate-limited, no guarantee of service) | `https://vigie.auxfilsdesidees.fr` | the instance's demo zones (Paris, Tokyo, Pointe-à-Pitre, San Francisco, Reykjavik in October 2026) |
| **Your own vigie-api** on a PC, a NAS or a Raspberry Pi of your network ([Run](https://github.com/Axellum/vigie-api#run)) | `http://<its address>:8080` | the zones you add to it (`POST /v1/zones`) |

The display never talks to the data sources itself, and never to Home Assistant.
Vigie does not replace official warnings: follow the instructions of your authorities.

## 3. Install ESPHome

ESPHome **2026.9.1 or newer** (2026.9.0 crashes at display start on the 7B's chip).

```bash
python -m venv .venv
.venv/bin/pip install "esphome>=2026.9.1"          # Windows: .venv\Scripts\pip install "esphome>=2026.9.1"
git clone https://github.com/Axellum/vigie-display.git
cd vigie-display
```

On Windows, run ESPHome from PowerShell or cmd, not Git Bash (ESP-IDF refuses the MSYS
environment). The first build downloads the ESP-IDF toolchain (about 1 GB).

## 4. Create the signing key

The firmware is signed: once flashed, the screen only accepts an update over Wi-Fi signed with
the same key. Create it once and keep a copy somewhere safe (lost key = updates over USB only).

```bash
openssl genrsa -out vigie_signing_key.pem 3072
```

Without OpenSSL: `python -m espsecure generate-signing-key --version 2 --scheme rsa3072 vigie_signing_key.pem`
(espsecure comes with ESPHome). `*.pem` is ignored by git: never commit it.

## 5. Build

Every setting is a substitution, given with `-s <name> <value>`.

**Waveshare 7B:**

```bash
esphome -s vigie_api_url https://vigie.auxfilsdesidees.fr -s vigie_signing_key vigie_signing_key.pem compile vigie.yaml
```

**M5Stack Tab5 (ST7123, the default):**

```bash
esphome -s vigie_board tab5 -s vigie_api_url https://vigie.auxfilsdesidees.fr -s vigie_signing_key vigie_signing_key.pem compile vigie.yaml
```

Tab5 with an **ST7121**: add `-s board_tab5_panel M5STACK-TAB5-ST7121`. With an **ILI9881C**: add
`-s board_tab5_panel M5STACK-TAB5 -s board_tab5_touch gt911 -s board_tab5_touch_interval never`.

| Substitution | Default | Meaning |
| :--- | :--- | :--- |
| `vigie_board` | `waveshare-7b` | `waveshare-7b` or `tab5` |
| `vigie_api_url` | `http://vigie-api.local:8080` | base URL of vigie-api, without `/v1` |
| `vigie_language` | `en` | screen language: `en` or `fr` |
| `vigie_api_interval` | `60s` | refresh period |
| `vigie_signing_key` | `vigie_signing_key.pem` | path of the signing key |
| `vigie_version` | `0.1.0-dev` | version written in the firmware |
| `board_rotation` | `0` (7B), `270` (Tab5) | a 7B mounted the other way up: `180` |

The first build takes several minutes, the next ones one or two. It ends with
`Successfully compiled program` and three files in `.esphome/build/vigie/build/` (under
`$ESPHOME_DATA_DIR/build/vigie/build/` if you set that variable): `firmware.factory.bin` (first flash),
`firmware.ota.bin` (updates) and `firmware.elf` (to decode a crash; keep it with the version).

## 6. First flash over USB

Plug the board into the computer. On the 7B, use the USB-C port of the **ESP32-P4** (the one
marked USB or UART, not the one of the C6 if yours has two). Then either:

- **From ESPHome**, which finds the port:

  ```bash
  esphome -s vigie_board tab5 -s vigie_api_url https://vigie.auxfilsdesidees.fr -s vigie_signing_key vigie_signing_key.pem upload vigie.yaml
  ```

  (the same `-s` as the build, so that ESPHome finds the same build).
- **From the browser**: open [ESPHome Web](https://web.esphome.io) in Chrome or Edge, *Connect*,
  pick the port (« USB JTAG/serial debug unit »), *Install*, and choose `firmware.factory.bin`.

The first install erases what the board had (the Tab5's M5Stack demo, another firmware).

## 7. Wi-Fi

The firmware holds no Wi-Fi password. Give it one of two ways; it is kept on the board and
survives updates:

- **Improv over USB**, right after the flash: in [ESPHome Web](https://web.esphome.io), *Connect*
  to the board, then *Configure Wi-Fi* (the firmware answers Improv on its USB serial port).
- **The access point**: a board with no known network opens `Vigie setup` after about a minute.
  Join it from a phone, a page opens (or go to `http://192.168.4.1`), pick your network, give its
  password.

## 8. Check it works

Within a minute of joining the network, the status bar shows « Up to date · 12 s ago » with a green dot,
the map fills with dots and the alert list with rows. Touch the weather panel to go to the next
zone. With the USB cable plugged in, the log says it too:

```bash
esphome -s vigie_board tab5 -s vigie_signing_key vigie_signing_key.pem logs vigie.yaml
```

`[vigie.api] Frame received: 5 zones, 23 hazards` every minute is the sign that all is well.

## 9. Updates over Wi-Fi

Pull the repository, build again with the same command and key, then:

```bash
esphome -s vigie_board tab5 -s vigie_api_url https://vigie.auxfilsdesidees.fr -s vigie_signing_key vigie_signing_key.pem upload --device <screen IP> vigie.yaml
```

The IP is in your router's list of devices (name `vigie`). The firmware has no network API: logs
come over USB only.

## 10. If it does not work

| What you see | What to do |
| :--- | :--- |
| `Could not find file vigie_signing_key.pem` | step 4, or give the key's path with `-s vigie_signing_key` |
| `ESPHome 2026.9.1 or newer is required` | update ESPHome (step 3) |
| No serial port | another cable (charge-only cables show none), another USB port. Tab5: hold its reset button about 2 s until the green LED blinks fast (download mode), try again, then a short press on reset |
| Screen stays black after the flash | the wrong Tab5 display chip, maybe: a sticker saying ST7123 may hide an ST7121; build for the other one |
| Image fine, touch does nothing (Tab5) | same as above: the touch chip depends on the revision |
| « Waiting for the API » that never ends | the board is not on the network (step 7), or the API URL is wrong: open `<url>/v1/embedded/frame` in a browser on the same network, it must show text ending with `END|` |
| Red dot, « API unreachable » | the API stopped answering: the screen keeps the last data and says how old it is |
| « Alert sources unreachable » | the API answered, but none of its hazard sources did: a problem on the API side |
| An update over Wi-Fi is refused | it was built with another key: flash over USB (step 6) |

Something else: [open an issue](https://github.com/Axellum/vigie-display/issues) with the board,
the command and the end of the log.
