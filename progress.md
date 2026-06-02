# Flashbee — storm-detection debug, in progress

Continuation notes for picking this up on another machine. Most recent
session was 2026-05-09/10, during an actual thunderstorm.

## Original symptom

Audible lightning happening, but the device only fired `INT_L` once
shortly after power-on, then went silent for the rest of the cell.
Touch / UI / I²C to the AS3935 all healthy throughout.

## What's been done (chronological, in commits)

1. **`f16747b` as3935: mask disturbers, floor filters, add INT-path check**
   - `MASK_DIST=1` in normal operation (datasheet §8.5 — `INT_D` is
     informational, chip already rejects disturbers internally). Stops
     per-disturber `tightenFilters()` ratchet that walks WD/SR to 10/11
     in seconds during real activity.
   - `loosenFilters` / `lowerNoiseFloor` floor at the AS3935 init
     defaults (2/2/2). Without the floor and with disturbers masked,
     they walked NF/WD/SR all the way to 0/0/0 — observed live.
   - Added `checkint` serial command: routes LCO to INT for one
     tune-window, prints Hz. Non-destructive (no NVS write). Proved
     live during the storm that the chip + INT line are healthy
     (3935 Hz, antenna in tune at TUN_CAP=9).

2. **`afa8dcd` cli: add maskoff/maskon/stat for live AS3935 debug**
   - `maskoff` / `maskon` flip MASK_DIST at runtime. Note: `maskoff`
     is reverted by the 10-min `initAS3935()` watchdog re-init — this
     is intentional for now (it's a probe, not a setting).
   - `stat` dumps regs 0x00–0x03, 0x07, 0x08 + runtime state.

## Current state of the diagnosis

Chip is **alive** and **tuned** but **completely silent** in normal
operation in the current location. Multi-hour log on 2026-05-10:
zero `INT_L`, zero `INT_D`, zero `INT_NH` — just the 10-min watchdog
re-init pattern firing in a loop.

Key signal: `INT_NH` is *not* firing at `NF=2`. The chip isn't picking
up enough RF to reach the noise threshold at all. If it were classifying
real strikes as disturbers, we'd at least see `[D]` flooding during
the `maskoff` window — we don't.

`stat` confirms the chip is configured exactly as intended:
```
[stat] AFE=0x1C NF_WDG=0x22 CLSTAT_SREJ=0xC2 LCO_INT=0x20 DIST=0x3F TUN=0x09
[stat] runtime: NF=2 WD=2 SR=2 AFE=OUT strikes=0
```

`checkint` consistently reports ~3935 Hz (expected 3906 Hz, well inside
±3.5% tolerance) — INT path good, antenna tuned, chip responsive.

## Leading hypothesis

**AFE gain too low for the location.** `AFE_GB_OUTDOOR` (lower gain)
assumes open-air RF; indoors / sheltered the walls attenuate the
~500 kHz band enough that signals don't even reach the noise-floor
threshold. The chip is correctly classifying "nothing" as nothing.

## To try next

1. **Switch to `AFE_GB_INDOOR`** via the settings screen (swipe to
   settings, tap `IN`). Expect: `[NH]` starts firing immediately,
   then `[D]` and (during thunder) `INT_L`. Persists in NVS.

2. If that doesn't help, physically relocate the device — outdoors
   in outdoor mode, or near a window.

3. If indoor mode + good location *still* yields zero events during
   audible thunder, suspect the AS3935 module hardware (antenna,
   AFE input filter caps). The chip itself is known good (`checkint`).

## Stuff to clean up eventually (not blocking)

- `maskoff` reverts on watchdog re-init. Fine for a probe, but if we
  want it to stick we'd need a runtime flag honored by `initAS3935()`.
- The 10-min watchdog re-init also re-applies all init regs every
  time the chip is silent. That's correct for a wedged chip but
  spammy when the chip is just genuinely quiet. Possibly distinguish
  "chip not responding to I²C" from "chip responding but no IRQs".
- `critical-issues.md` is deleted in the working tree; decide whether
  to keep that deletion (was a planning doc, probably stale now).
- `resume/` directory is untracked — unrelated to this work.

## Runtime serial commands (current)

| cmd        | effect                                                     |
|------------|------------------------------------------------------------|
| `checkint` | Verify INT line + antenna tune. Non-destructive.           |
| `maskoff`  | Set MASK_DIST=0 (disturbers visible). Reverts on re-init.  |
| `maskon`   | Set MASK_DIST=1 (default; disturbers suppressed).          |
| `stat`     | Dump AS3935 regs + runtime state.                          |

Aliases: `checkint` also accepts `i`.

## Build / flash

```
pio run -e seeed_xiao_esp32c6 -t upload --upload-port /dev/ttyACM0
```

USB-CDC native — DTR/RTS reset works via pyserial. By-id path is
`/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_98:A3:16:8F:9A:50-if00`
(pinning logger to this avoids grabbing the wrong device when the
port enumerates ahead of a Morserino-like sibling).
