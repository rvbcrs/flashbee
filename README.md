# Flash Bee

Handheld lightning sensing and ranging device built around the AMS **AS3935**
lightning sensor: it shows strike distance, per-strike energy, a short history
of recent strikes, and a big red `SHELTER` warning when a strike lands within
~10 km.

> **This is a port of [haklein's Flash Bee](https://github.com/haklein/flashbee)
> PlatformIO firmware** — which is itself a port of
> [gokux's original "Flash Bee"](https://www.instructables.com/Flash-Bee-Handheld-Lighting-Sensing-and-Ranging-De/)
> (Instructables). haklein's version targets the Seeed XIAO + Round Display; this
> one re-targets it to the **Waveshare ESP32-S3-Touch-AMOLED-1.75** (1.75″ CO5300
> QSPI AMOLED, CST9217 capacitive touch), adds a custom parametric 3D-printable
> case (build123d), and rebuilds the firmware as a modular `fb_*` stack (display,
> touch, audio, power, networking/MQTT, RTC). Full credit for the original concept,
> mechanical design, and photos goes to **gokux**, and to **haklein** for the
> PlatformIO port + safety-critical AS3935 datasheet fixes this builds on — see
> [Credits](#credits).

## Credits

- Original project, mechanical design, and photos — **gokux**:
  <https://www.instructables.com/Flash-Bee-Handheld-Lighting-Sensing-and-Ranging-De/>
- PlatformIO firmware port + safety-critical AS3935 datasheet fixes this is
  based on — **haklein**: <https://github.com/haklein/flashbee>
- Lightning sensor: AMS **AS3935**; modules sold as CJMCU-3935 / WCMCU-3935 /
  Seeed [Grove AS3935](https://www.seeedstudio.com/Grove-Lightning-Sensor-AS3935-p-5603.html)
- Host board: [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)

> **⚠️ Safety.** This is a hobby device. Do **not** use it as your sole basis for
> deciding whether it is safe to be outdoors. The AS3935 is a statistical
> single-antenna detector with ±1-bin distance uncertainty and well-known
> disturber / false-positive behaviour. When in doubt, follow the
> [NWS 30/30 rule](https://www.weather.gov/safety/lightning) and official weather
> advisories. The trustworthy output is the `!! SHELTER !!` warning — treat the
> km number as a best-effort estimate.

## Hardware

The device is **one Waveshare dev board + one sensor module + a battery + a
3D-printed case**. The board already bundles the display, touch, power management,
audio and RTC, so the only thing you wire up is the AS3935.

| Part | Role |
|------|------|
| **Waveshare ESP32-S3-Touch-AMOLED-1.75** | ESP32-S3 (16 MB flash, OPI PSRAM); 1.75″ **CO5300** AMOLED (466×466, round, QSPI); **CST9217** capacitive touch; **AXP2101** PMU + LiPo charger; **ES8311** audio codec + speaker; **PCF85063** RTC |
| **AS3935 module** (CJMCU-3935 / Grove AS3935) | Franklin lightning sensor on its own I²C bus (see [Wiring](#wiring--as3935-sensor)) |
| **LiPo pouch** — HY103450 (10 × 34 × 50 mm) | Battery; charged over the board's USB-C via the AXP2101 |
| **3D-printed case** | Two-part snap-fit enclosure (see [3D-printed case](#3d-printed-case)) |

## Wiring — AS3935 sensor

The only soldering is the AS3935 module to a spare header. It runs on its **own
I²C bus** (`Wire`), separate from the board's onboard peripherals (`Wire1`), at
100 kHz, address `0x03`:

| AS3935 pin | → ESP32-S3 |
|------------|------------|
| VCC        | 3V3        |
| GND        | GND        |
| SDA        | **GPIO 17** |
| SCL        | **GPIO 18** |
| INT        | **GPIO 16** |

The **INT** wire is essential: the firmware is fully interrupt-driven (no
polling), and the boot-time [antenna calibration](#first-time-setup--antenna-calibration)
measures the LC-tank resonance on this pin. Override it at build time with
`-DAS3935_INT_PIN=<n>`.

### Full pin map

| GPIO            | Signal                                              |
|-----------------|-----------------------------------------------------|
| 4 / 5 / 6 / 7   | AMOLED QSPI data D0–D3                              |
| 38              | AMOLED QSPI SCLK                                    |
| 12              | AMOLED QSPI CS                                      |
| 39              | AMOLED reset                                        |
| 15 / 14         | Onboard I²C (`Wire1`) SDA / SCL                     |
| 11 / 40         | Touch INT / RST (CST9217)                           |
| 17 / 18         | AS3935 I²C (`Wire`) SDA / SCL                       |
| 16              | AS3935 INT                                          |
| 42 / 9 / 45 / 8 / 10 | Audio I²S MCLK / BCLK / LRCLK / DOUT / DIN (ES8311) |
| 46              | Audio amplifier enable                              |

Two I²C buses:

- **`Wire`** — GPIO 17/18, 100 kHz: AS3935 `0x03`.
- **`Wire1`** — GPIO 15/14, 400 kHz: CST9217 touch `0x5A`, AXP2101 PMU, ES8311
  codec, PCF85063 RTC `0x51`. (`pio run -e s3scan` scans this bus.)

## Build & flash

Requires [PlatformIO Core](https://platformio.org/install/cli). The firmware is
the modular `fb_*.cpp` stack, built by the `s3` environment:

```bash
pio run -e s3                  # build the full firmware
pio run -e s3 -t upload        # flash over USB-C
pio device monitor -b 115200   # serial log
```

The first build fetches WiFiManager, PubSubClient, **SensorLib** (pinned) and
**XPowersLib** (pinned) into `.pio/libdeps/`. **Arduino_GFX** is vendored in
`lib/` (it carries a manifest-name patch PlatformIO needs); **ES8311** is vendored
too.

| Env | Board | Purpose |
|-----|-------|---------|
| `s3` | esp32-s3-devkitc-1 (S3, 16 MB, PSRAM) | **Full firmware** (AMOLED) |
| `s3test` | esp32-s3-devkitc-1 | Standalone AS3935 serial test — strikes to the monitor, no display |
| `s3scan` | esp32-s3-devkitc-1 | I²C bus scanner |
| `seeed_xiao_esp32c3` / `…c6` | Seeed XIAO | **Legacy** — builds the original single-file `main.ino` for haklein's Seeed XIAO + Round Display target, *not* the `fb_*` firmware |

## What it shows

The UI has **four themes** — Legacy, Aurora, Neon, Watch — swappable on the
Settings screen and saved to flash, and **two screens** you switch with a
horizontal swipe.

**Main screen**

- **Big number** — estimated distance in km, or `OVERHEAD` / `>40` /
  `-- distance unknown`.
- **Energy gauge** — last-strike energy (AS3935 21-bit word).
- **Inward-pulsing rings** — a "listening" indicator. Purely radial: the AS3935
  is non-directional, so nothing implies bearing.
- **Strikes / energy history** — running count and a recent-energy bar chart
  (marked `(stale)` once the last strike is >5 min old).
- **AS3935 status line** — `OUT/IN WD:n SR:n` (AFE mode + filter levels). Turns
  amber as filters tighten, red `ENV TOO NOISY` when the noise floor hits the
  hardware ceiling and readings are no longer in-spec.
- **`!! SHELTER !!` overlay** — blinks when a strike is within ~10 km, with a
  count-up timer; clears automatically 30 min after the last close strike.
- **`SENSOR LOST` overlay** — shown after repeated I²C failures; the firmware
  keeps retrying until the sensor returns.

**Settings screen** (swipe horizontally) — INDOOR/OUTDOOR AFE gain, sensitivity
profile (STORM / NORMAL / NOISY), sound volume, theme, screen-blank timeout,
light-sleep timeout, RESET filters, **TUNE** (re-run antenna calibration), TEST,
and a live battery readout from the AXP2101.

## Web UI, MQTT & Home Assistant

On first boot with no saved WiFi, the device opens a non-blocking captive-portal
AP **`FlashBee-setup`** (WiFiManager). Join it to enter your WiFi and, optionally,
an MQTT broker. After that it's reachable at **`http://flashbee.local`**:

| Route | Purpose |
|-------|---------|
| `GET /` | Single-page web app — Live / History / Settings tabs |
| `GET /api` | JSON: current state + strike history + config |
| `GET /strikes.csv` | Raw strike log (CSV) |
| `POST /save` | Save MQTT broker host / port / user / pass |
| `POST /resetwifi` | Wipe WiFi credentials and reboot to the setup AP |

With a broker configured it publishes **Home Assistant discovery** for seven
entities — distance, nearest, energy, strike count, battery %, RSSI, and a
`shelter` binary sensor — under `flashbee/<id>/…`, with a 10 s state heartbeat
plus an immediate publish on every strike.

Strikes are logged to **LittleFS** (`/strikes.csv`, `epoch,km,energy`) with a
64-entry in-RAM ring. Timestamps come from the **PCF85063 RTC**, which is set
from **NTP** once online and kept in UTC.

## First-time setup — antenna calibration

The AS3935's antenna LC tank must be trimmed (`TUN_CAP`) to its board-specific
value; the factory default is usually a few % off, which biases every distance
reading. The firmware **auto-runs the calibration sweep on every boot** — you can
also re-run it from the **TUNE** tap on the Settings screen, or by typing `tune`
on the serial console:

1. Issue `PRESET_DEFAULT` + `CALIB_RCO`.
2. Route the LC oscillator to the **INT pin** (GPIO 16), divided down to ~3906 Hz.
3. Sweep `TUN_CAP` 0–15, counting edges per step.
4. Pick the value closest to 3906 Hz (±3.5 % tolerance) and save it to flash (NVS).

The screen shows a live progress bar and a `TUNED` / `OUT OF RANGE` summary.
Serial output looks like:

```
TUN_CAP= 8 -> 3900 Hz  (-6)
TUN_CAP= 9 -> 3906 Hz   (0)
best: TUN_CAP=9 @ 3906 Hz (WITHIN TOL)
```

If the sweep sees **zero edges** (`check INT wire`), the AS3935 `INT` → GPIO 16
wire is missing or on a different pin, and the detector falls back to the
compile-time default — it still works, but distance estimates use the untuned LC
tank.

## Power management

- **Interrupt-driven** AS3935 (GPIO 16 IRQ) — no polling; the I²C bus is quiet
  between events.
- **Display timeout** (default 2 min; 30 s … NEVER) blanks the AMOLED via the
  CO5300 panel. Disturber / noise events are still processed silently and don't
  wake the screen.
- **Light sleep** (default NEVER; 5 min … 2 h) — `esp_light_sleep_start()`,
  waking on an AS3935 INT (strike), a touch, or a 1 h timer.
- Battery voltage / percent are read from the **AXP2101** every 2 s; charge over
  USB-C.

Runtime depends on the cell (the case fits an HY103450 LiPo pouch); the AMOLED +
ESP32-S3 draw noticeably more than the original tiny-cell build, so size the pack
accordingly.

## 3D-printed case

A fully parametric **build123d** model — [`cad/flashbee_case.py`](cad/flashbee_case.py) —
for a two-part snap-fit handheld enclosure (round display head + grip/battery body
+ antenna nub), designed around the Waveshare board.

**Generate the STLs** (written to `cad/out/`):

```bash
cd cad
python3 -m venv .venv && .venv/bin/pip install build123d   # first time only
FB_SHOW=0 .venv/bin/python flashbee_case.py                # headless export
# (omit FB_SHOW=0 to also push a live preview to the OCP CAD Viewer)
```

Exported parts — each as `.stl` + `.step`, plus a combined `assembly.step`:

- **`front_shell`** — bezel / lid (holds the AMOLED glass)
- **`back_shell`** — tub (battery + sensor compartment)
- **`plunger`** ×2 — side buttons
- **`sensor_pin`** ×2 — AS3935 hold-down pins

**Printing & assembly**

- Print `front_shell`, `back_shell`, 2× `plunger`, 2× `sensor_pin`. ABS or PETG;
  the shells print open-side / floor down.
- The two shells **snap together** (tongue-and-groove rim with click detents).
- The **AMOLED glass** drops into the front bezel pocket and is captured by the
  lip when the shells close.
- The **AS3935 board** sits on two bosses in the back; push a `sensor_pin`
  through each mount hole into the boss. The pins are sized to **slide in and be
  fixed with a drop of CA glue** (small FDM holes vary too much for a reliable
  friction fit).
- The **LiPo** sits in the grip; a low rib on the lid holds it down (its leads
  run underneath, so it rides a little high).
- The **USB-C** port and side buttons have openings on the rim; the `plunger`s
  transmit the button presses.

The large **vendor reference models** (the board / sensor / battery `.step`,
`.dwg`, `.pdf`) are **not** committed — they're large and third-party. The case
and STLs build without them; they're only needed for the optional "ghost"
assembly preview (`FB_SHOW=1`). Download them from the
[Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75) and
the sensor/battery vendors and drop them in `cad/` if you want the preview.

## Repository layout

```
platformio.ini          # build environments, flags, lib_deps
src/fb_*.{h,cpp}         # modular firmware: as3935, display, gfx, touch, audio,
                         #   power, net (web/MQTT), time (RTC/NTP)
src/as3935_test.cpp      # standalone AS3935 serial test  (env s3test)
src/i2c_scan.cpp         # I²C scanner                     (env s3scan)
src/main.ino             # legacy single-file firmware     (Seeed XIAO envs)
lib/Arduino_GFX/         # vendored display lib (manifest-name patch)
lib/ES8311/              # vendored audio-codec lib
cad/flashbee_case.py     # parametric build123d enclosure
cad/dimensions.md        # measured component dimensions
```

## AS3935 datasheet-correctness fixes

These fixes are inherited from [haklein's port](https://github.com/haklein/flashbee),
where the original Instructables sketch was reviewed against the AS3935 datasheet
(rev 1.07 §8.10–§8.11). The `fb_as3935` driver here carries them forward:

**Distance accuracy**

- **AFE gain byte was encoded wrong.** `AFE_GB` is `REG0x00[5:1]`, so the 5-bit
  field must be shifted up one bit. The original wrote `0x12` aiming for the
  indoor code `0b10010`, which actually placed `9` into the field — outside the
  valid `{14 outdoor, 18 indoor}` pair. Now uses `(0b01110<<1)=0x1C` outdoor
  (default — this is a handheld) or `(0b10010<<1)=0x24` indoor.
- **`PRESET_DEFAULT` + `CALIB_RCO` never ran**, so the RCO timebase used to
  measure strike energy was uncalibrated. Both now run on every init, with
  `TRCO`/`SRCO` done-bits verified.
- **Antenna `TUN_CAP` was never set** — now auto-tuned (see
  [calibration](#first-time-setup--antenna-calibration)) and stored in NVS.
- **Distance `0x00` was coerced to `OVERHEAD`.** The datasheet only defines
  `0x01` (overhead) and `0x3F` (out of range); `0x00` is undefined, so it now
  shows `-- distance unknown` instead of the scariest reading.

**Robustness during a storm**

- **Noise-floor ratchet only went up** — a minute of EMI could permanently
  deafen the detector. `NF` now decays one step per 60 s of quiet.
- **High-noise fault was hidden.** When `NF` hits maximum and `INT_NH` still
  fires, the UI shows `ENV TOO NOISY` so you know readings aren't trustworthy.
- **No I²C error detection** — reads blindly returned `0xFF`. Calls now propagate
  a result; after 8 consecutive failures the sensor is marked lost (`SENSOR
  LOST`), and `Wire.setTimeOut(50)` prevents bus hangs during ESD events.
- **No stall watchdog** — a re-init now fires after 10 min of zero interrupts.

**Clarity**

- `increaseSensitivity()`/`decreaseSensitivity()` were named *opposite* to their
  datasheet effect (higher `WDTH`/`SREJ` = less sensitive) → renamed
  `tightenFilters()`/`loosenFilters()`.
- Every register/field/bit has a named symbol; reg `0x02` writes preserve
  `CL_STAT_EN`/`CL_STAT` (the original clobbered them).

> These datasheet-correctness fixes were bench-validated on the original Seeed
> XIAO + Round Display + Grove AS3935 hardware (haklein); this port reuses the
> same `fb_as3935` driver unchanged, on a new host board. **No real cloud-to-
> ground strike has been cross-checked yet** — live data so far is indoor EMI,
> which the AS3935 reports as a mix of disturbers and overhead "strikes"
> (expected indoors). Treat `!! SHELTER !!` as the trustworthy output and the km
> number as a best-effort estimate.

## License

Same spirit as the whole upstream chain — **hobby / educational, non-commercial,
with attribution**. This is a port of
[haklein/flashbee](https://github.com/haklein/flashbee) (no formal license —
"same spirit as the Instructables source"), which ports
[gokux's original Flash Bee](https://www.instructables.com/Flash-Bee-Handheld-Lighting-Sensing-and-Ranging-De/).
Please keep crediting **gokux** and **haklein**, and don't use it commercially.
