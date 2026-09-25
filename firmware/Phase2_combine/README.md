[README (15).md](https://github.com/user-attachments/files/32666398/README.15.md)
# Phase 2 Combine Install - Firmware v0.5

**File:** `yield_monitor_bench_v5.ino`
**Target:** ESP32-WROOM-32 (classic DevKit, NOT S3/C3)
**Machine:** Case IH 1480 combine, clean grain elevator (CGE)
**Status:** First real yield-signal firmware. Adds per-event occlusion width capture on top of the working Phase 1B paddle counter.

---

## What is this?

This is the firmware that turns the yield monitor from a **paddle counter** into a **yield sensor**.

Phase 1 / 1B / v0.4 all answered the question: "did a paddle just go past?" That's counting events. Useful for diagnostics (is the elevator turning, at what speed) but it is **not the yield signal**.

Phase 2 / v0.5 answers a different question: "how *long* did that paddle block the beam?" That width, in microseconds, is proportional to how much grain the paddle is carrying. An empty paddle blocks the beam for a baseline duration (just the paddle body). A loaded paddle blocks it longer, because the grain sitting on top of the paddle keeps the beam broken past the trailing edge of the paddle itself.

The math for grain flow is:

```text
excess_us = last_paddle_width_us - empty_paddle_baseline_us
grain_flow (lbs/s) = excess_us * calibration_factor
```

The calibration factor is TBD - that's what field calibration against grain-cart weigh tickets gives us.

---

## What changed from v0.4

v0.4 already had the correct polarity, correct newlines, and clean paddle counting on the actual machine (see `docs/recon_results/2026-09-25_cge_empty_2800rpm.md`). v0.5 keeps all of that and adds:

| Feature | Purpose |
|---|---|
| **Per-event width capture (us)** | ISR records `t_start_us` on falling edge (beam BLOCKED) and computes `width_us = t_end - t_start` on rising edge (beam CLEAR). This is the actual yield signal. |
| **64-slot event ring buffer** | Rolling window of `{t_start_us, width_us}` for the most recent 64 paddles. At ~7 Hz that's ~9 seconds of history. |
| **Rolling baseline (empty-paddle floor)** | `baseline_us` = mean of the narrowest 25% of widths in the buffer. Rationale: grain never makes a paddle occlude *less*, so the narrow tail is the empty-paddle floor even during active loading. |
| **Excess width per event** | `excess_us = last_width_us - baseline_us`, floored at 0. This is what Phase 2 calibration will map to lbs/s. |
| **CSV event stream (optional)** | With `#define CSV_STREAM`, every completed event emits a line: `EVT,<count>,<t_us>,<width_us>`. Capture to disk for offline analysis in Python or Excel. |
| **Updated header** | Reflects observed on-machine 7 Hz rate at 2800 engine RPM (empty CGE). Retires the 17 Hz geometric prediction from v0.3. |

---

## Hardware wiring

Same as v0.4. No hardware changes needed.

| ESP32 GPIO | Direction | Connection |
|---|---|---|
| GPIO 25 | OUT | IR LED anode, via ~150 ohm resistor to +3.3V. LEDC drives 38 kHz carrier here. |
| GPIO 26 | IN (pull-up) | TSOP4838 OUT pin. Active LOW when carrier detected (beam clear). |
| GPIO 2  | OUT | Onboard LED, 1 Hz heartbeat |
| +3.3V   | -- | TSOP4838 VCC (add 100 uF cap and 100 ohm series R per datasheet) |
| GND     | -- | Common ground |

Cable: shielded CAT5e (SFTP), with the drain wire tied to GND at the ESP32 end only.

---

## How to flash

1. Arduino IDE 2.x with the ESP32 board package (Core 3.x).
2. Board: **ESP32 Dev Module** (or your specific WROOM-32 variant).
3. Baud: 115200.
4. Compile and upload. Open the Serial Monitor.

**Note on the header comment:** the block comment starts with `/*` on line 1 (NOT `// /*` -- that typo bit us in v0.4 and would break compile with cryptic "invalid octal constant" errors).

---

## Compile-time flags

At the top of the file:

```cpp
#define CSV_STREAM    // Comment out to disable per-event CSV lines
```

**With `CSV_STREAM` enabled (default):**
- Every completed paddle event emits a single CSV line to serial.
- Format: `EVT,<paddle_count>,<t_start_us>,<width_us>`
- At 7 Hz that's 7 CSV lines per second plus the 1 Hz status line.
- **Use this for calibration runs** where you want to capture every event for offline analysis.

**With `CSV_STREAM` disabled:**
- Only the 1 Hz status line is emitted.
- Cleaner interactive output when you're just watching the sensor work.

---

## Expected serial output

### Status line (once per second)

```text
[t=42s] paddles=245  rate=7.0 Hz  beam=BLOCKED  ms_since_edge=15  baseline_us=62150  last_width_us=64280  excess_us=2130
```

| Field | Meaning |
|---|---|
| `t=42s` | Seconds since boot |
| `paddles=245` | Total paddles counted since boot |
| `rate=7.0 Hz` | Paddles per second over the last 1000 ms |
| `beam=BLOCKED` | Current instantaneous state (LOW=CLEAR, HIGH=BLOCKED per TSOP4838 datasheet) |
| `ms_since_edge=15` | Time since last state change, in ms |
| `baseline_us=62150` | Rolling empty-paddle baseline width (~62 ms in this example) |
| `last_width_us=64280` | Width of the most recent completed paddle (~64 ms) |
| `excess_us=2130` | Excess over baseline. Zero or near-zero = empty paddle. Larger = grain-loaded paddle. |

### CSV event lines (when `CSV_STREAM` enabled, one per paddle)

```text
EVT,245,1234567890,64280
```

| Field | Meaning |
|---|---|
| `EVT` | Literal tag - grep-friendly for post-processing |
| `245` | Cumulative event number |
| `1234567890` | `micros()` timestamp of the falling edge (beam went BLOCKED) |
| `64280` | Width of the occlusion in microseconds |

`micros()` wraps around every ~71 minutes. For sessions longer than that, either handle the rollover in post-processing (differences still work because unsigned arithmetic) or add a compile-time flag to include `millis() / 1000` in the event line.

---

## What "good" looks like

Once flashed and running with the sensor mounted on the CGE, engine at 2800 RPM, empty (no grain flow):

- **`rate`** should sit near 7 Hz sustained (0.59 Hz stdev per prior test).
- **`baseline_us`** should stabilize somewhere in the range of ~50,000 to ~70,000 us (50-70 ms) within the first few seconds. Actual number is TBD -- that's what the first v0.5 test tells us.
- **`last_width_us`** should hover around `baseline_us` +/- a few percent when empty.
- **`excess_us`** should be near zero (say <5,000 us) with no grain flowing.

When grain starts flowing:

- **`rate`** should stay near 7 Hz -- paddle passing rate is set by engine speed, not by load.
- **`last_width_us`** should climb.
- **`excess_us`** should climb, and its magnitude across many paddles should correlate with total grain flow.

---

## Bench test procedure

Same as v0.4, plus a width-capture check:

1. Flash, open Serial Monitor at 115200.
2. Point LED at TSOP (~8" gap), status should show `beam=CLEAR`, `rate=0.0 Hz`, `baseline_us=0` (no events yet).
3. Wave your finger through the beam at a roughly steady rate.
   - `rate` should track your hand-wave frequency (~1-3 Hz typically).
   - After a few waves, `baseline_us` should populate.
   - Fast flicks -> lower `last_width_us`. Slow drags -> higher `last_width_us`.
4. Compare finger vs. pen vs. ruler through the beam. Wider occluders should show up as higher `last_width_us` and higher `excess_us`.
5. Verify CSV event stream. Every wave should produce one `EVT,...` line.

---

## On-machine test procedure (calibration run)

1. Mount sensor on CGE per bracket design.
2. Enable `CSV_STREAM` compile flag.
3. Start Serial Monitor capture to a file (in Arduino IDE 2.x: right-click serial output -> Save to file, or use `screen` / `PuTTY` / `tio` on the command line for reliability).
4. Fire up the combine, let it stabilize at 2800 RPM.
5. **Empty run first** -- 2-3 minutes of running with no grain flow. This locks in the empty-paddle baseline.
6. **Loaded run** -- engage header, harvest a known area with a grain cart under the auger to weigh output.
7. Save the log to `docs/recon_results/YYYY-MM-DD_cge_<condition>_widths.log`.

Post-process in Python: histogram of `width_us` in the empty run gives baseline distribution. Histogram in the loaded run shows the shift. Weigh-ticket lbs / total excess_us gives the calibration factor.

---

## Known limitations / TBD

- **Serial buffer under EMI:** v0.4 test showed 2 garbled serial lines in ~13 minutes. Not a big deal at 1 Hz status rate, but at 7+ lines/sec with CSV_STREAM on, an occasional bad line is expected. Post-processing should skip lines that don't match the CSV regex.
- **micros() wraparound at ~71 min:** noted above. Not currently handled in-firmware; if calibration runs exceed 60 min, either restart the ESP32 or add wraparound handling.
- **Baseline is naive.** Mean of narrowest 25% works for detecting *any* grain loading vs. empty, but assumes at least some empty paddles pass during the window. If the elevator is 100% loaded continuously, the baseline will drift up and understate excess. For now this is fine because we always start empty. Future improvement: freeze baseline once locked in.
- **No SD card logging yet.** CSV_STREAM goes to serial only. Long calibration runs need laptop tethered to combine, OR the Phase 3 upgrade to add SPIFFS/SD storage.
- **No GPS or timestamp sync yet.** That's Phase 3.

---

## File tree suggestion

```text
firmware/
  phase1_bench/            <- v0.3 original
    phase1_bench.ino
  phase1b_paddle/          <- Phase 1B paddle-aware sketch
    phase1b_paddle.ino
  phase2_combine_v5/       <- THIS FILE (recommended location)
    yield_monitor_bench_v5.ino
    README.md              <- this file
```

---

## Change log

- **v0.5** (25/09/2026): Added per-event width capture, ring buffer, baseline computation, CSV event stream. Header updated with measured on-machine rates.
- **v0.4** (24/09/2026): Fixed missing newline, inverted polarity labels in serial output, and initial state. First version to run correctly on-machine.
- **v0.3** (17/09/2026): Bench-only. Had polarity and newline bugs discovered during first on-machine test.

---

## References

- On-machine test writeup: `docs/recon_results/2026-09-25_cge_empty_2800rpm.md`
- Raw log from that test: `docs/recon_results/2026-09-25_cge_empty_2800rpm.log`
- Recon measurements (elevator geometry): `docs/recon_results/1480_measurements.md`
- TSOP4838 datasheet: Vishay, active-low IR receiver tuned to 38 kHz
