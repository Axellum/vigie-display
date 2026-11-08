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
- A line whose tag the display does not know is skipped, and the CRC still covers it. This is how
  vigie-api can add lines without breaking a display that is not updated yet.

## The sea of a surf spot: `SEA`, `ACT`, `CRIT`

For a request with `&spot=<id>` (a surf spot), vigie-api adds `SEA`, `ACT` and `CRIT` lines after
the `HAZ` lines: the sea of the spot (tides, sun, the strongest coastal warning) and, for each
activity (surf, kite, boat, shore fishing), a verdict, its 24 hourly marks and the criteria behind
it. Their columns are in vigie-api's `docs/embedded-frame.md`.

`firmware/core/vigie_sea.*` decodes them into a `VigieSea`, separate from the world state (that one
is saved in NVS, the sea is not). The rules it keeps:

- `vigie_update_sea()` replaces the sea only with a good frame that carries the block. A good
  frame without it (no spot asked, or the swell could not be read) is **unknown, not "nothing to
  do"**: the previous sea stays and gets older. `vigie_sea_freshness()` says how old: fresh, stale
  (the API serves a provider's last answer, or the data is over 10 minutes old: the screen says
  how old), expired (over 6 hours, or the 24 hours of the strips are over: the verdicts are not
  shown).
- An empty field stays unknown. Coastal warnings that could not be read are **not** "no warning"
  (`vigie_format_warning()` says they are unavailable); a warning level the firmware does not know
  counts as unreadable too.
- A `CRIT` line belongs to the `ACT` line just above it, for the same activity. A name or a state
  the firmware does not know is not shown: `vigie_criterion_state_text()` has a text only for the
  pairs of the contract's vocabulary table, and `tools/test_vocabulary.h` holds a copy of that
  table that the tests check against the decoder and the texts.
- Times are UTC; the frame has no UTC offset for the spot, so the screen uses the offset of the
  zone it shows. Every verdict is a rule of Vigie, not an official one, and not for safety
  (`SEA_RULES_NOTICE`); the tide coefficient is Vigie's estimate, not the SHOM's.
- `vigie_build_frame_url()` takes an optional spot id and adds `&spot=<id>`; an empty or invalid
  id (`vigie_spot_id_valid()`: a-z, 0-9, `-`, `_`) is left out, so a typo cannot stop the weather.

The firmware asks for the spot of the `vigie_spot` substitution (empty by default: no sea page) and
the screen shows the sea on its second page ([ADR 0004](decisions/0004-sea-page.md)). A spot the API
answers 404 or 422 to is not asked for again until the next boot (`vigie_note_spot_status()`), so
the weather keeps coming. `vigie_sea_hour_index()`, `vigie_tide_height_at()` and
`vigie_sea_coefficient_for()` give the screen the hour bar of now, the height of the water at a
time (a half cosine between the extremes, nothing outside them) and the coefficient of the next
high water.

## Fixtures

- `tests/fixtures/api_frame.txt` is a real frame captured from vigie-api (its `tools/capture_frame.py`);
  `tools/test_vigie_api_client.cpp` decodes it.
- `tests/fixtures/api_frame_activities.txt` is a real frame with the block above, captured for a
  surf spot of the Landes coast. `tools/test_vigie_sea.cpp` decodes it value by value;
  `tools/test_vigie_api_client.cpp` checks that the display accepts it, that removing the block and
  signing the frame again gives the same decoded world, and that altering a byte of the block is
  still refused by the CRC. `tools/test_vigie_model.cpp` checks the same with small frames and an
  unknown tag.
- `tests/fixtures/api_frame_activities_warning.txt` is the previous frame with an orange coastal
  warning on its `SEA` line, signed again, so that CI renders the banner. Its activities are those
  of the plain frame, not what the API would answer under a warning: it tests the display, not the
  rules. `tests/test_firmware.py` checks that nothing else differs.

Refresh the first two when the contract changes (`docs/development.md`).
