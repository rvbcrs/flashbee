# Legacy notes

Historical working notes from the original **Seeed XIAO + `src/main.ino`** era of
[haklein's Flash Bee](https://github.com/haklein/flashbee), kept here for
reference. They predate this repo's modular `fb_*` firmware and the Waveshare
AMOLED port, and they reference the legacy single-file sketch (`src/main.ino`),
not the current `fb_as3935` driver.

- **[critical-issues.md](critical-issues.md)** — a 2026-04-20 review of
  `src/main.ino` against the AS3935 datasheet. The fixes it identified are now
  implemented in `fb_as3935` and summarised in the main
  [README](../../README.md#as3935-datasheet-correctness-fixes).
- **[progress.md](progress.md)** — a live storm-debug log (May 2026).
