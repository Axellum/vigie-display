# The API frame (display side)

The contract is owned by **vigie-api**: `docs/embedded-frame.md` in that repository is the single
source (every line type, every column, units). This page only says how the display uses it.

- Request: `GET <vigie_api_url>/v1/embedded/frame?device_type=<vigie_board>` (plus
  `&lat=…&lon=…&marine=1` once a GPS fix exists), every `vigie_api_interval`.
- Lines `HEAD`, `ZONE`, `RAIN5M`, `DAY`, `HAZ`, then `END|<CRC32>`: IEEE 802.3 CRC32, 8 hex digits,
  of every byte before the `END|` line. The display refuses a response without that line, with a
  wrong CRC, with a status other than 200, or that filled its buffer (`vigie_api_max_bytes`).
- Empty field = unknown: a `ZONE` without weather columns shows "No weather data"; an empty rain
  probability or air quality is not displayed.
- `DAY` labels are ISO dates (`2026-10-20`); the display turns them into day names in its language.
- At most 10 zones and 30 hazards are kept (`kVigieMaxZones`, `kVigieMaxHazards`), most severe first.

`tests/fixtures/api_frame.txt` is a real frame captured from vigie-api (its `tools/capture_frame.py`);
`tools/test_vigie_api_client.cpp` decodes it. Refresh it when the contract changes.
