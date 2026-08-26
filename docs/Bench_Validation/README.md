# Bench validation logs

Reference datasets captured during Phase 1B bench testing. These are the
"known-good" firmware outputs used for later comparison against combine
install data and for regression checks after any firmware changes.

## Setup used for all logs in this folder

- **ESP32 board:** AITRIP 30-pin USB-C ESP-WROOM-32 (CP2102 USB serial)
- **Emitter:** CHANZON 940 nm IR LED, driven by 38 kHz PWM on GPIO 25
  through a 220 Ω current-limit resistor
- **Receiver:** Vishay TSOP4838 IR receiver on GPIO 26
- **Wiring:** Direct Dupont female-to-female jumpers between the ESP32
  header pins and the component legs. No breadboard.
- **Geometry:** LED and TSOP domes facing each other across ~3 inches
  of empty air, hand-held.
- **Firmware:** Phase 1B (paddle-aware occlusion detection), unmodified
  from what is currently in `firmware/phase1b_paddle/`.

## Log inventory

| File | Occluder | Wave rate | What it validates |
|---|---|---|---|
| `2026-08-26_finger_wave_fast.log` | Human finger | 3–13 Hz | Event detection at high rates, tiny baseline (~1.4 ms), high anomaly SNR |
| `2026-08-26_rhythmic_wave.log`   | Rigid card   | ~0.9 Hz  | Baseline convergence, rhythmic period lock, loaded-paddle spike detection |

## How to read a log line

Each line is one JSON report emitted once per second. Fields:

- `t_ms` — milliseconds since ESP32 boot
- `events_seen_total` — cumulative beam-break event count since boot
- `n_in_window` — how many events are in the rolling analysis window (max 40)
- `period_us` — median event-to-event spacing in µs (this is what
  `paddles_per_sec` is derived from)
- `paddles_per_sec` — 1e6 / period_us, i.e. detected event frequency
- `baseline_us` — the algorithm's model of a "normal" empty-paddle
  event width, in µs
- `last_width_us` — width of the most recently completed event, in µs
- `last_load_us` — max(0, last_width_us − baseline_us): the "excess"
  dwell time attributed to grain load
- `avg_load_us` — rolling average of last_load_us across the 40-event window
- `beam_state` — instantaneous state at the moment the report was
  emitted: `CLEAR` or `BLOCKED`

## Key numbers to compare against future logs

If a future firmware build (or a combine install log) deviates
significantly from these values under similar conditions, that's a
signal to investigate:

- **Baseline for fast small-object waves:** ~1–2 ms
- **Baseline for rhythmic card waves at ~1 Hz:** ~230–250 ms
- **Loaded-event ratio (last_width / baseline) for a deliberate dwell:**
  8× or more should be trivially detectable
- **paddles_per_sec jitter under steady input:** ≤ 1%
- **Time to converge baseline after fresh boot:** ~10–15 events

## What is NOT in these logs (and would be nice to add)

- Actual corn kernel drops (waiting on shop access to some kernels)
- A rigid rotating occluder simulating a real paddle chain
- Long-run stability (>10 minutes continuous)
- Behavior with sunlight or halogen ambient IR interference
- Behavior at longer LED-TSOP distances (6", 12")

These are candidates for future bench sessions before the combine install.

## History

- **26/08/2026** — first successful optical detection captured. Emitter
  side was silently working during earlier breadboard debugging; switching
  to direct Dupont jumpers eliminated a bad breadboard column and the
  full pipeline came alive immediately.
