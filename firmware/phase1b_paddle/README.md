[phase1b_paddle_README (1).md](https://github.com/user-attachments/files/31430066/phase1b_paddle_README.1.md)
# Phase 1B firmware — paddle-aware occlusion detection

Evolves the Phase 1 bench firmware from **"count every beam-break"** to
**"detect paddles, then measure grain-loading modulation on each paddle."**

Same hardware as Phase 1. Same wiring. Different signal-processing model.

> **Firmware requirement:** this sketch uses the ESP32 Arduino Core 3.x
> LEDC API (`ledcAttach()` / `ledcWrite(pin, duty)`). It will **NOT** compile
> against Core 2.x, which used the older `ledcSetup()` / `ledcAttachPin()`
> API. Install **esp32 by Espressif Systems, version 3.0.0 or later** from
> Boards Manager. See
> [the migration guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html)
> for background.

## Why Phase 1B exists (in one paragraph)

Phase 1 assumed each beam-break was an independent event — good for
hand-waves and kernel drops on the bench, wrong for a real clean grain
elevator. The 1480's paddle chain presents **periodic occlusions** at
roughly 1–20 Hz whether there's grain or not. The yield signal lives in
the **width** of each paddle's beam-break: an empty paddle blocks the
beam for a baseline duration; a grain-loaded paddle blocks it longer,
because kernels ride on the paddle and trail off the leading edge.
Phase 1B measures per-event widths, learns the empty-paddle baseline at
runtime, and reports the excess as the grain-loading signal.

See `docs/recon_results/1480_measurements.md` for the mechanical reasoning
that drove this design.

---

## What the firmware does

1. Same 38 kHz IR emitter on GPIO 25.
2. Same TSOP4838 input with pin-change interrupt on GPIO 26.
3. **New:** every completed beam-break is logged to a 128-slot ring
   buffer as `{start_us, width_us}`.
4. **New:** every 1000 ms the main loop analyzes the most recent 40
   events and emits a JSON summary with:
   - **`period_us`** — median inter-paddle interval
   - **`paddles_per_sec`** — inverse of the period
   - **`baseline_us`** — mean of the narrowest 25% of recent widths
     (this is the "empty paddle" occlusion floor)
   - **`last_width_us`** — width of the most recent event
   - **`last_load_us`** — `last_width_us − baseline_us`, floored at 0
     (the grain-loading estimate for the most recent paddle)
   - **`avg_load_us`** — rolling average of `(width − baseline)` across
     the window
5. **Optional:** compile with `EMIT_CSV_PER_EVENT` to also emit one CSV
   line per beam-break — useful for offline analysis in Python or a
   spreadsheet.

---

## Bench validation checklist

Before flashing, confirm the Phase 1 sketch already worked — this
firmware has the same wiring dependency. If Phase 1 counts hand-waves,
Phase 1B will too.

Then flash and open Serial Monitor at 115200:

- [ ] JSON reports print every ~1000 ms with all fields present.
- [ ] Beam clear → `paddles_per_sec = 0`, `baseline_us = 0`, `last_load_us = 0`.
- [ ] Steady hand-wave at ~2 Hz → `paddles_per_sec` settles near 2.0
      after a few seconds of history builds.
- [ ] `baseline_us` settles to roughly your typical finger-through dwell.
- [ ] Slow, deliberate wave ("loaded paddle") → `last_load_us` jumps
      positive on that event.
- [ ] Resume normal waves → `last_load_us` drops back toward zero on
      subsequent events (baseline doesn't shift much because the narrow
      quartile is unchanged).
- [ ] Wide occluder (pen, ruler) → `last_load_us` spikes to a large value.

If the counts are wildly wrong or every event has width < 1 ms, the ISR
is rejecting them as noise — check the wiring, the pull-up, and the
TSOP polarity (`TSOP_ACTIVE_LOW` in the source).

---

## Building / flashing

Identical to Phase 1 — same board, same libraries, same Arduino IDE
setup. Open `phase1b_paddle.ino` in Arduino IDE, select
**ESP32 Dev Module**, pick the correct USB serial port, click Upload.

---

## Sample output

Steady 2 Hz hand-wave, roughly 60 ms wide fingers, no "loaded" events:

```json
{"t_ms":8000,"events_seen_total":15,"n_in_window":15,"period_us":498000,"paddles_per_sec":2.01,"baseline_us":58200,"last_width_us":61300,"last_load_us":3100,"avg_load_us":4200.0,"beam_state":"CLEAR"}
```

Same wave with a deliberate slow "loaded" swipe on the last event:

```json
{"t_ms":9000,"events_seen_total":17,"n_in_window":17,"period_us":501000,"paddles_per_sec":2.00,"baseline_us":58900,"last_width_us":142000,"last_load_us":83100,"avg_load_us":9800.0,"beam_state":"CLEAR"}
```

You can see the baseline barely moves (it tracks the narrow quartile,
which the slow swipe doesn't affect) but `last_load_us` jumps from ~3 ms
to ~83 ms of "extra dwell beyond baseline."

---

## What Phase 1B is deliberately NOT doing

- **No calibration curve yet.** We're emitting `avg_load_us` in raw
  microseconds. Mapping that to pounds/sec or bushels/hour needs
  side-by-side grain-cart data from the field, which is a Phase 3 task.
- **No GPS.** That's Phase 2.
- **No Wi-Fi.** That's also Phase 2.
- **No persistent storage of the baseline.** Every power-cycle relearns
  from scratch. Good enough for now — the baseline stabilizes within
  ~4 seconds of harvest activity. If that turns out to be a nuisance
  we'll add SPIFFS persistence.

---

## Ranges the code is designed to handle

| Parameter | Design range | Notes |
|---|---|---|
| Paddle passing rate | 1–20 paddles/sec | Covers our ~10× RPM uncertainty. |
| Beam-break width | 1 ms – 5 s | Under 1 ms is rejected as noise. |
| Ring buffer depth | 128 events | ~6 seconds at worst-case 20 Hz. |
| Analysis window | 40 events | ~4 seconds at expected 10 Hz. |

Change the constants at the top of the sketch if reality forces our hand.

---

## Known limits (things to revisit after real elevator data)

- **Baseline drift under sustained heavy load.** If every paddle is
  packed for minutes on end, the narrow-quartile trick still works
  (the 25% narrowest events are still the narrowest — grain modulates
  all of them upward, so the floor moves up too, but that's actually
  fine because "baseline" always means "narrowest recent paddles").
  Worth confirming with real data.
- **Slug / clump events.** A big clump of grain hitting one paddle
  could saturate the width for that paddle. Firmware treats it as a
  huge single event, which is correct for total-throughput purposes
  but may be worth flagging separately in Phase 2.
- **Reverse-direction paddle sensing.** If someone unclogs the
  elevator by running it backward, we'll still count events. Not a
  correctness problem — just a "flag and ignore" situation for the
  Phase 2 downstream logic.

---

*Firmware author: Mike Otto + Vylor Chat (Claude). Written 22/08/2026.*
