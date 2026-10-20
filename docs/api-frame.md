# The API frame (display side)

The contract is owned by **vigie-api**: `docs/embedded-frame.md` in that repository is the single
source (every line type, every column, units). This page only says how the display uses it.

- Request: `GET <vigie_api_url>/v1/embedded/frame?device_type=<vigie_board>` (plus
  `&lat=…&lon=…&marine=1` once a GPS fix exists), every `vigie_api_interval`.
- Lines `HEAD`, `ZONE`, `RAIN5M`, `DAY`, `HAZ`, then `END|<CRC32>`: IEEE 802.3 CRC32, 8 hex digits,
  of every byte before the `END|` line. The display refuses a response without that line, with a
  wrong CRC, with a status other than 200, or that filled its buffer (`vigie_api_max_bytes`).
- Empty field = unknown, and it stays unknown on the display (`kVigieUnknown`, `kVigieUnknownCode`,
  `kVigieUnknownPct` in `vigie_model.h`); it is never turned into 0:
  - a `ZONE` without weather columns shows "No weather data";
  - a single unknown value (gusts, air quality, rain within the hour, a day's rain probability…)
    hides its line; an unknown forecast temperature reads "–";
  - an empty severity in `HEAD` means no hazard source answered: the alert panel says
    "Alert sources unreachable" and its counts read "–", never "No active alert";
  - a zone without a UTC offset (the GPS zone when no provider answered) shows the clock in UTC
    and its date says so.
- `HEAD` counts every active hazard; the display shows those counts, not the number of `HAZ` lines.
  `HEAD` field 3 is reserved and is not read.
- `DAY` labels are ISO dates (`2026-10-20`); the display turns them into day names in its language.
- At most 10 zones and 30 hazards are kept (`kVigieMaxZones`, `kVigieMaxHazards`), most severe first.

`tests/fixtures/api_frame.txt` is a real frame captured from vigie-api (its `tools/capture_frame.py`);
`tools/test_vigie_api_client.cpp` decodes it. Refresh it when the contract changes.
