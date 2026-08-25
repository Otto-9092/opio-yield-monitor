// ============================================================================
// OPiO Yield Monitor — Phase 1B: Paddle-Aware Occlusion Detection
// ----------------------------------------------------------------------------
// Target board:  ESP32-WROOM-32 (classic dev kit, e.g. ESP32-DevKitC-V4).
//                NOT ESP32-S3, NOT ESP32-C3 — different pinout.
//
// What changed from Phase 1 (phase1_bench.ino):
//   Phase 1  = "count every beam-break" — good for hand-waves and kernel
//              drops on the bench, wrong model for the real elevator.
//   Phase 1B = "detect PADDLES, then measure grain-loading modulation on
//              each paddle" — models what the real clean grain elevator
//              actually presents to the sensor.
//
// Why the model change:
//   The 1480 clean grain elevator has 40 rubber paddles on a CA550 chain
//   at 6.520" pitch. At harvest engine speed the paddles pass the IR beam
//   at a periodic rate (estimated 1–20 Hz — see docs/recon_results/
//   1480_measurements.md for the derivation and the ~10× uncertainty
//   we're carrying until we measure shaft RPM on the machine).
//
//   Each paddle-passing is a beam-break, whether there's grain or not.
//   The "yield signal" lives in the *width* of each break — an empty
//   paddle blocks the beam for a baseline duration; a paddle loaded with
//   grain kernels blocks it longer (paddle occlusion + trailing kernel
//   fringe). So we can't just count events — we have to measure and
//   compare per-event widths against a rolling baseline of "empty
//   paddle" widths.
//
// What this sketch does:
//   1. Same emitter (38 kHz on GPIO 25) and TSOP input (GPIO 26) as
//      Phase 1. Same ISR-driven edge capture.
//   2. Adds a small ring buffer of the last N beam-break events, each
//      recorded as {timestamp_us, width_us}.
//   3. Every REPORT_INTERVAL_MS (default 1000 ms), the main loop:
//        a. Snapshots any new events out of the ring buffer.
//        b. Computes the MEDIAN interval between consecutive events over
//           the recent window. This is the observed paddle period.
//        c. Computes an "empty paddle" baseline width as the mean of the
//           narrowest 25% of recent widths (grain never makes a paddle
//           narrower — the narrow tail is your empty-paddle floor).
//        d. Emits a one-line JSON report with:
//             - observed paddle period + inferred paddles/sec
//             - baseline empty-paddle width
//             - per-paddle grain-loading estimate for the most recent
//               event (width - baseline, floored at 0)
//             - a rolling average grain-loading estimate over the window
//   4. Also emits a raw CSV line per beam-break event (behind a compile
//      flag) so you can log the raw stream for offline analysis.
//
// Bench validation checklist (once flashed):
//   [ ] Serial prints JSON reports every 1000 ms with all fields present.
//   [ ] With the beam clear, paddles_per_sec = 0, baseline_us = 0,
//       and last_load_us = 0.
//   [ ] Wave your finger through the beam at a roughly steady rate.
//       Watch paddles_per_sec settle to your hand-wave frequency and
//       baseline_us settle to your typical finger-width dwell.
//   [ ] Wave your finger through slowly (a "loaded paddle") and note
//       last_load_us goes positive. Wave normally again and it drops
//       back toward zero as the baseline catches up.
//   [ ] Drop a pen or ruler through the beam (a wider occluder) and
//       watch last_load_us spike, then decay as the ring buffer rolls
//       over.
//
// After Phase 1B works, Phase 2 adds: GPS parsing, Wi-Fi AP, HTTP/WS
// server, and eventually the calibration curve that maps "excess
// blocked microseconds per paddle" to "pounds of grain per second."
// ============================================================================

#include <Arduino.h>

// ----------------------------------------------------------------------------
// Pin assignments (same as Phase 1)
// ----------------------------------------------------------------------------
constexpr int PIN_IR_EMITTER   = 25;   // PWM output to IR LED (via 220Ω)
constexpr int PIN_TSOP_INPUT   = 26;   // Digital input from TSOP4838 OUT
constexpr int PIN_STATUS_LED   = 2;    // ESP32 dev kit built-in LED

// ----------------------------------------------------------------------------
// 38 kHz carrier config (LEDC / PWM)
// ----------------------------------------------------------------------------
// (LEDC_CHANNEL_EMITTER removed — ESP32 Arduino Core 3.x manages channels
//  internally via ledcAttach(). Kept as a comment for git-blame context.)
constexpr int    LEDC_TIMER_BITS      = 8;
constexpr double LEDC_FREQ_HZ         = 38000.0;
constexpr int    LEDC_DUTY_50PCT      = 128;

// ----------------------------------------------------------------------------
// TSOP4838 polarity
//   HIGH (idle) = beam is BROKEN or 38kHz not seen
//   LOW         = 38kHz beam DETECTED (clear)
// ----------------------------------------------------------------------------
constexpr bool TSOP_ACTIVE_LOW = true;

// ----------------------------------------------------------------------------
// Ring buffer of recent beam-break events
// ----------------------------------------------------------------------------
// Sizing rationale: at worst-case 20 paddles/sec × 5-sec window we need
// 100 slots. Round up to 128 for a power-of-2 index mask.
constexpr size_t EVENT_BUF_SIZE = 128;
constexpr size_t EVENT_BUF_MASK = EVENT_BUF_SIZE - 1;   // 0x7F

struct BreakEvent {
    uint32_t start_us;   // micros() when beam broke
    uint32_t width_us;   // duration beam stayed broken
};

// The ring buffer itself. Written from ISR, read from loop().
// head is the next slot to write. It wraps via & EVENT_BUF_MASK.
volatile BreakEvent g_events[EVENT_BUF_SIZE];
volatile uint32_t   g_events_head = 0;   // monotonic write counter
uint32_t            g_events_tail = 0;   // monotonic read counter (loop only)

// In-progress break state (ISR)
volatile uint32_t g_block_start_us       = 0;
volatile bool     g_beam_currently_blocked = false;

// ----------------------------------------------------------------------------
// Reporting cadence
// ----------------------------------------------------------------------------
constexpr uint32_t REPORT_INTERVAL_MS = 1000;
uint32_t g_last_report_ms = 0;

// Uncomment to also emit one CSV line per beam-break event.
// Handy for offline analysis in Python / spreadsheet.
//   Columns: t_us,width_us
// #define EMIT_CSV_PER_EVENT 1

// ----------------------------------------------------------------------------
// Analysis window (how many recent events feed the median/baseline calc)
// ----------------------------------------------------------------------------
// At 10 paddles/sec, 40 events ≈ 4 seconds of history. Plenty of statistics
// without smearing away real changes.
constexpr size_t ANALYSIS_WINDOW = 40;

// ----------------------------------------------------------------------------
// Interrupt Service Routine — TSOP edge change
// ----------------------------------------------------------------------------
void IRAM_ATTR onTsopChange() {
    int lvl = digitalRead(PIN_TSOP_INPUT);
    bool blocked = TSOP_ACTIVE_LOW ? (lvl == HIGH) : (lvl == LOW);

    uint32_t now_us = micros();

    if (blocked && !g_beam_currently_blocked) {
        // Beam JUST broke — mark the start
        g_block_start_us = now_us;
        g_beam_currently_blocked = true;
    } else if (!blocked && g_beam_currently_blocked) {
        // Beam JUST cleared — log a completed event
        if (g_block_start_us != 0) {
            uint32_t width = now_us - g_block_start_us;

            // Reject implausibly short events (< 1 ms). At 20 paddles/sec
            // an empty paddle is still tens of ms wide, so anything under
            // 1 ms is noise (mechanical bounce, EMI, dust flash).
            if (width >= 1000) {
                size_t slot = g_events_head & EVENT_BUF_MASK;
                g_events[slot].start_us = g_block_start_us;
                g_events[slot].width_us = width;
                g_events_head++;
            }
        }
        g_block_start_us = 0;
        g_beam_currently_blocked = false;
    }
}

// ----------------------------------------------------------------------------
// Utility: insertion-sort a small array of uint32_t in ascending order.
// N is small (≤ ANALYSIS_WINDOW = 40) so O(N²) is fine and simpler than
// dragging in qsort or std::sort.
// ----------------------------------------------------------------------------
static void sort_asc(uint32_t* arr, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        uint32_t v = arr[i];
        size_t j = i;
        while (j > 0 && arr[j - 1] > v) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = v;
    }
}

// ----------------------------------------------------------------------------
// setup()
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("=============================================");
    Serial.println("OPiO Yield Monitor — Phase 1B Paddle-Aware");
    Serial.println("=============================================");
    Serial.printf("IR emitter pin:   GPIO %d @ %d Hz\n",
                  PIN_IR_EMITTER, (int)LEDC_FREQ_HZ);
    Serial.printf("TSOP input pin:   GPIO %d (active %s)\n",
                  PIN_TSOP_INPUT, TSOP_ACTIVE_LOW ? "LOW" : "HIGH");
    Serial.printf("Status LED pin:   GPIO %d\n", PIN_STATUS_LED);
    Serial.printf("Event buffer:     %u slots\n",
                  (unsigned)EVENT_BUF_SIZE);
    Serial.printf("Analysis window:  %u events\n",
                  (unsigned)ANALYSIS_WINDOW);
    Serial.println("---------------------------------------------");
    Serial.println("Reports every 1000 ms. Wave rhythmically to simulate paddles.");
    Serial.println();

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    pinMode(PIN_TSOP_INPUT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_TSOP_INPUT),
                    onTsopChange, CHANGE);

    // NOTE: requires ESP32 Arduino Core 3.0 or later. Core 2.x used
    // ledcSetup() + ledcAttachPin() + ledcWrite(channel, duty); Core 3.x
    // replaced that with a single ledcAttach(pin, freq, bits) + a pin-
    // addressed ledcWrite(). See the migration guide:
    //   https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html
    ledcAttach(PIN_IR_EMITTER, LEDC_FREQ_HZ, LEDC_TIMER_BITS);
    ledcWrite(PIN_IR_EMITTER, LEDC_DUTY_50PCT);

    Serial.println("38 kHz carrier active. Setup complete.");
    Serial.println();
}

// ----------------------------------------------------------------------------
// Per-window analysis state (persists across loop iterations so we can
// report deltas and rolling averages).
// ----------------------------------------------------------------------------
uint32_t g_baseline_us_last     = 0;   // last computed empty-paddle baseline
uint32_t g_period_us_last       = 0;   // last computed paddle period
uint32_t g_last_event_width_us  = 0;   // width of most recent event
float    g_avg_load_us          = 0.0f; // rolling avg of (width - baseline)

// ----------------------------------------------------------------------------
// loop()
// ----------------------------------------------------------------------------
void loop() {
    uint32_t now_ms = millis();

    // Mirror beam state to the status LED (visual health check).
    digitalWrite(PIN_STATUS_LED, g_beam_currently_blocked ? HIGH : LOW);

    // ------------------------------------------------------------------------
    // Drain new events out of the ring buffer, oldest-first.
    // Optionally emit them as CSV for offline analysis.
    // ------------------------------------------------------------------------
    uint32_t head_snap;
    noInterrupts();
    head_snap = g_events_head;
    interrupts();

    while (g_events_tail != head_snap) {
        size_t slot = g_events_tail & EVENT_BUF_MASK;
        uint32_t t_us = g_events[slot].start_us;
        uint32_t w_us = g_events[slot].width_us;
        g_events_tail++;

        g_last_event_width_us = w_us;

#ifdef EMIT_CSV_PER_EVENT
        Serial.printf("EVT,%lu,%lu\n",
                      (unsigned long)t_us,
                      (unsigned long)w_us);
#else
        (void)t_us;   // silence unused-variable warning
#endif
    }

    // ------------------------------------------------------------------------
    // Every REPORT_INTERVAL_MS, compute + emit the summary line.
    // ------------------------------------------------------------------------
    if (now_ms - g_last_report_ms < REPORT_INTERVAL_MS) return;
    g_last_report_ms = now_ms;

    // Copy the most recent ANALYSIS_WINDOW events into a local scratch
    // array so we can sort them without holding interrupts off.
    size_t n_have = (head_snap >= ANALYSIS_WINDOW)
                        ? ANALYSIS_WINDOW
                        : (size_t)head_snap;

    uint32_t widths[ANALYSIS_WINDOW];
    uint32_t starts[ANALYSIS_WINDOW];

    for (size_t i = 0; i < n_have; ++i) {
        // Walk backward from head_snap - 1 down to head_snap - n_have.
        // Ring buffer indexing survives the wrap.
        uint32_t idx = head_snap - 1 - i;
        size_t slot = idx & EVENT_BUF_MASK;
        widths[i] = g_events[slot].width_us;
        starts[i] = g_events[slot].start_us;
    }

    // -- observed paddle period (median inter-event interval) ---------------
    // Compute intervals between consecutive events in chronological order.
    // starts[] is currently newest-first; we want oldest-first for
    // differencing, so index accordingly.
    uint32_t period_us = 0;
    if (n_have >= 2) {
        uint32_t intervals[ANALYSIS_WINDOW - 1];
        size_t n_int = n_have - 1;
        for (size_t i = 0; i < n_int; ++i) {
            // starts is newest-first: starts[i] is newer than starts[i+1]
            intervals[i] = starts[i] - starts[i + 1];
        }
        sort_asc(intervals, n_int);
        period_us = intervals[n_int / 2];   // median
    }
    g_period_us_last = period_us;

    // -- empty-paddle baseline (mean of narrowest 25% of widths) ------------
    uint32_t baseline_us = 0;
    if (n_have >= 4) {
        uint32_t sorted[ANALYSIS_WINDOW];
        for (size_t i = 0; i < n_have; ++i) sorted[i] = widths[i];
        sort_asc(sorted, n_have);
        size_t q = n_have / 4;              // narrowest 25%
        if (q < 1) q = 1;
        uint64_t sum = 0;
        for (size_t i = 0; i < q; ++i) sum += sorted[i];
        baseline_us = (uint32_t)(sum / q);
    }
    g_baseline_us_last = baseline_us;

    // -- last-event grain-loading estimate ----------------------------------
    // "Load" is how many microseconds wider this event was than the
    // empty-paddle baseline. Floor at 0 (a paddle narrower than baseline
    // isn't "negative grain" — it's just noise in the baseline estimator).
    uint32_t last_load_us = 0;
    if (g_last_event_width_us > baseline_us) {
        last_load_us = g_last_event_width_us - baseline_us;
    }

    // -- rolling-average load over the window -------------------------------
    float avg_load = 0.0f;
    if (n_have >= 4 && baseline_us > 0) {
        uint64_t load_sum = 0;
        for (size_t i = 0; i < n_have; ++i) {
            if (widths[i] > baseline_us) load_sum += (widths[i] - baseline_us);
        }
        avg_load = (float)load_sum / (float)n_have;
    }
    g_avg_load_us = avg_load;

    // -- paddles per second (derived from period) ---------------------------
    float paddles_per_sec = 0.0f;
    if (period_us > 0) {
        paddles_per_sec = 1e6f / (float)period_us;
    }

    // ------------------------------------------------------------------------
    // Emit one-line JSON report.
    // Keep the schema stable — Phase 2 will parse this and hand it off to
    // the OPiO PWA over Wi-Fi.
    // ------------------------------------------------------------------------
    Serial.printf(
        "{\"t_ms\":%lu,"
        "\"events_seen_total\":%lu,"
        "\"n_in_window\":%u,"
        "\"period_us\":%lu,"
        "\"paddles_per_sec\":%.2f,"
        "\"baseline_us\":%lu,"
        "\"last_width_us\":%lu,"
        "\"last_load_us\":%lu,"
        "\"avg_load_us\":%.1f,"
        "\"beam_state\":\"%s\"}\n",
        (unsigned long)now_ms,
        (unsigned long)head_snap,
        (unsigned)n_have,
        (unsigned long)period_us,
        paddles_per_sec,
        (unsigned long)baseline_us,
        (unsigned long)g_last_event_width_us,
        (unsigned long)last_load_us,
        avg_load,
        g_beam_currently_blocked ? "BLOCKED" : "CLEAR"
    );
}
