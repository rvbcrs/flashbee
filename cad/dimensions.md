# Flash Bee — component dimensions (for build123d enclosure)

Units: mm. Source: caliper / vendor drawings.

## Display board — Waveshare ESP32-S3-Touch-AMOLED-1.75 (bare)
- PCB: **round, Ø 46.00**
- Cover-glass / AMOLED module: outer **Ø 48.8** (confirmed by owner), active area **Ø ~44.16**
  - glass (48.8) overhangs the PCB (46.0) by ~1.4 mm/side → front pocket seats the glass, board sits behind
- Thickness: total **10.40**, sub-step **6.90**; front glass layers 2.05 / 1.10 / 0.60
- Mounting holes: **M2.0** (M2.00-H3.5) + 2× Ø1.80
- **From official STEP (global, rel. to screen centre):** assembly 48.96×48.96×12.7
  - USB-C: LEFT edge (−X 20), vertical centre (0) ✓
  - KEY1/KEY2 buttons: RIGHT edge (+X 17), vertical **±11.31** (on PCB back side)
- Top connectors (MX1.25): **SPK + BAT**, 15.00 apart
- USB-C: **right edge**, ~16.50 reference from centre (vertical)
- 8-pin header: **bottom edge**, 2.54 pitch, ~12.70 span, 13.75 each side of centre
- Mount-hole pattern refs: 34.00 (×2), 20.50, 18.68, 18.30, 14.70 (exact XY TBD)

## Lightning sensor — WCMCU/CJMCU-3935 "MA5532-AE" (arrow/T-shape)
- Overall ~30 W × 28 H
- **Neck (antenna end): 12.3 W × 15.2 L**, then tapers ("schuin") out to **28.2 W** wide body
  → wide-body height ≈ 28 − 15.2 ≈ 12.8
- PCB ~1.6 thick; wired (no straight header) → envelope ~4 deep ASSUMED
- **MOUNTS AT TOP**: neck points UP and protrudes through a top "antenna pod"
  (away from battery/EMI), like the original Flash Bee top bump
- **From official STEP:** board 28.51 × 30.12 × 1.6 (4.48 w/ components)
  - Mount holes (Ø2.0): (−9.7,+7.2) & (+10.3,+7.2) → **20.0 mm c-c** (≈ measured 19.4)
  - Lower holes (Ø3.1): (−3.6,−5.2) & (+4.1,−5.2) → 7.7 mm c-c
  - (board frame: X=width −13.9..14.6, Z=length −15.1..15.0)
- Layout top→bottom: antenna nub → sensor pod → display head → grip+battery → lanyard tip

## Battery — HY103450 LiPo
- **Cell 10 (thick) × 34 (wide) × 50 (long)**, 2000 mAh, 3.7 V
- **From STEP:** bbox 38.45 × 10.05 × 63.2 → cell ~34×50×10, +leads/JST extend
  ~+4 mm width & ~+13 mm length → reserve lead clearance at the connector end
- Connector: MX1.25 2-pin (matches board BAT)
- Sits in the grip below the head; drives grip width (34)

## Notes
- Bare board → enclosure must fully wrap it (front bezel + back shell).
- Edge-capture design preferred (Ø46 PCB pocket) so exact mount-hole XY is optional.
