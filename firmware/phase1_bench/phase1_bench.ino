// ============================================================================
// OPiO Yield Monitor — Phase 1: Bench Occlusion Detection
// ----------------------------------------------------------------------------
// Target board:  ESP32-WROOM-32 (classic dev kit, e.g. ESP32-DevKitC-V4).
//                NOT ESP32-S3, NOT ESP32-C3 — different pinout, this code
//                won't work on them without changes.
//
// Hardware under test (bench setup):
//   - 1x IR LED (940 nm, 5mm)                     pulsed at 38 kHz
//   - 1x Vishay TSOP4838 IR receiver              demodulates the 38 kHz beam
//   - 1x current-limit resistor (220 Ω)           in series with the IR LED
//
// Wiring (see docs/wiring/phase1_bench.svg for the diagram):
//
//   ESP32 GPIO 25  --[220Ω]--(+)IR LED(-)-- GND     (emitter, PWM 38 kHz)
//
//   TSOP4838:
//     Pin 1 (OUT) → ESP32 GPIO 26                    (digital input w/ interrupt)
//     Pin 2 (GND) → GND
//     Pin 3 (VS)  → 3.3V
//
//   ESP32 built-in LED (GPIO 2) is used as a status blink:
//     - solid off when beam is clear
//     - solid on when beam is broken
//     - blink at 1 Hz on the "reporting" cycle so you know the loop is alive
//
// What this sketch does:
//   1. Drives GPIO 25 as a 38 kHz square wave (50% duty) using the ESP32's
//      LEDC peripheral. This is the IR emitter carrier.
//   2. Attaches a pin-change interrupt to GPIO 26 (TSOP output).
//   3. When the beam is broken (TSOP OUT goes HIGH), starts a microsecond
//      timer. When the beam clears (TSOP OUT goes LOW), stops the timer,
//      accumulates the blocked-microseconds total, and increments a count.
//   4. Every 1000 ms, prints a report over Serial (115200 baud):
//        {
//          "t_ms": 12345,
//          "block_count": 7,
//          "total_blocked_us": 14523,
//          "beam_state": "CLEAR" | "BLOCKED"
//        }
//
// Bench validation checklist (once flashed):
//   [ ] Serial monitor opens and prints reports every second.
//   [ ] With the beam clear (nothing between LED and TSOP), block_count = 0
//       and total_blocked_us = 0 in each 1-second window.
//   [ ] Waving a finger between the LED and TSOP produces block_count > 0.
//   [ ] Sustained blocking (index finger held between) gives block_count = 1
//       and total_blocked_us close to the duration held (in microseconds).
//   [ ] Dropping a few corn kernels between the sensors registers as
//       multiple short blocks — each kernel is a countable event.
//
// After Phase 1 works, Phase 2 adds: GPS parsing, Wi-Fi AP, HTTP/WS server.
// ============================================================================

#include <Arduino.h>

// ----------------------------------------------------------------------------
// Pin assignments
// ----------------------------------------------------------------------------
constexpr int PIN_IR_EMITTER   = 25;   // PWM output to IR LED (via 220Ω)
constexpr int PIN_TSOP_INPUT   = 26;   // Digital input from TSOP4838 OUT
constexpr int PIN_STATUS_LED   = 2;    // ESP32 dev kit built-in LED

// ----------------------------------------------------------------------------
// 38 kHz carrier config (LEDC / PWM)
// ----------------------------------------------------------------------------
constexpr int    LEDC_CHANNEL_EMITTER = 0;
constexpr int    LEDC_TIMER_BITS      = 8;         // 8-bit resolution (0-255)
constexpr double LEDC_FREQ_HZ         = 38000.0;   // 38 kHz — matches TSOP4838
constexpr int    LEDC_DUTY_50PCT      = 128;       // 128/255 ≈ 50% duty

// ----------------------------------------------------------------------------
// Occlusion detection state (updated from ISR — must be volatile)
// ----------------------------------------------------------------------------
volatile uint32_t g_block_start_us       = 0;   // micros() when beam broke; 0 = clear
volatile uint32_t g_total_blocked_us     = 0;   // accumulated blocked time this window
volatile uint16_t g_block_count          = 0;   // number of block events this window
volatile bool     g_beam_currently_blocked = false;

// TSOP4838 OUT behavior:
//   - HIGH (idle) = beam is BROKEN or 38kHz not seen
//   - LOW         = 38kHz beam DETECTED (clear)
// (Some TSOP variants are inverted. If yours behaves opposite,
//  set TSOP_ACTIVE_LOW to false.)
constexpr bool TSOP_ACTIVE_LOW = true;

// ----------------------------------------------------------------------------
// Reporting cadence
// ----------------------------------------------------------------------------
constexpr uint32_t REPORT_INTERVAL_MS = 1000;
uint32_t g_last_report_ms = 0;

// ----------------------------------------------------------------------------
// Interrupt Service Routine — called on every edge of TSOP output
// ----------------------------------------------------------------------------
// IRAM_ATTR keeps this in fast IRAM so it can fire during flash reads.
void IRAM_ATTR onTsopChange() {
    // Read pin state
    int lvl = digitalRead(PIN_TSOP_INPUT);
    bool blocked = TSOP_ACTIVE_LOW ? (lvl == HIGH) : (lvl == LOW);

    uint32_t now_us = micros();

    if (blocked && !g_beam_currently_blocked) {
        // Beam JUST broke
        g_block_start_us = now_us;
        g_beam_currently_blocked = true;
    } else if (!blocked && g_beam_currently_blocked) {
        // Beam JUST cleared
        if (g_block_start_us != 0) {
            uint32_t duration = now_us - g_block_start_us;
            g_total_blocked_us += duration;
            g_block_count++;
        }
        g_block_start_us = 0;
        g_beam_currently_blocked = false;
    }
}

// ----------------------------------------------------------------------------
// setup()
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("========================================");
    Serial.println("OPiO Yield Monitor — Phase 1 Bench Test");
    Serial.println("========================================");
    Serial.printf("IR emitter pin:   GPIO %d @ %d Hz\n",
                  PIN_IR_EMITTER, (int)LEDC_FREQ_HZ);
    Serial.printf("TSOP input pin:   GPIO %d (active %s)\n",
                  PIN_TSOP_INPUT, TSOP_ACTIVE_LOW ? "LOW" : "HIGH");
    Serial.printf("Status LED pin:   GPIO %d\n", PIN_STATUS_LED);
    Serial.println("----------------------------------------");
    Serial.println("Reports every 1000 ms. Break the beam to see counts rise.");
    Serial.println();

    // Status LED
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    // TSOP input with pull-up (TSOP4838 is push-pull, so pull-up is
    // insurance for a disconnected input rather than strictly required).
    pinMode(PIN_TSOP_INPUT, INPUT_PULLUP);

    // Attach the change interrupt (both rising and falling edges).
    attachInterrupt(digitalPinToInterrupt(PIN_TSOP_INPUT),
                    onTsopChange, CHANGE);

    // Set up the 38 kHz PWM carrier on the emitter pin.
    ledcSetup(LEDC_CHANNEL_EMITTER, LEDC_FREQ_HZ, LEDC_TIMER_BITS);
    ledcAttachPin(PIN_IR_EMITTER, LEDC_CHANNEL_EMITTER);
    ledcWrite(LEDC_CHANNEL_EMITTER, LEDC_DUTY_50PCT);
    Serial.println("38 kHz carrier active on emitter pin. Setup complete.");
    Serial.println();
}

// ----------------------------------------------------------------------------
// loop()
// ----------------------------------------------------------------------------
void loop() {
    uint32_t now_ms = millis();

    // Mirror beam state to the status LED for quick visual feedback.
    digitalWrite(PIN_STATUS_LED, g_beam_currently_blocked ? HIGH : LOW);

    if (now_ms - g_last_report_ms >= REPORT_INTERVAL_MS) {
        g_last_report_ms = now_ms;

        // Snapshot the counters, then clear them atomically.
        // Disabling interrupts around a few instructions is fine on ESP32.
        noInterrupts();
        uint32_t total_blocked_us_snap = g_total_blocked_us;
        uint16_t block_count_snap      = g_block_count;
        bool     beam_blocked_snap     = g_beam_currently_blocked;
        g_total_blocked_us = 0;
        g_block_count      = 0;
        interrupts();

        // If the beam is currently blocked at the moment we sampled, add the
        // in-progress duration so the report reflects reality rather than
        // rolling that time entirely into the next window.
        if (beam_blocked_snap && g_block_start_us != 0) {
            uint32_t in_progress = micros() - g_block_start_us;
            total_blocked_us_snap += in_progress;
            // Reset the in-progress start so the NEXT window starts fresh
            // for this same ongoing block.
            noInterrupts();
            g_block_start_us = micros();
            interrupts();
        }

        // Report as one-line JSON — easy to grep, easy to parse in Phase 2.
        Serial.printf(
            "{\"t_ms\":%lu,\"block_count\":%u,\"total_blocked_us\":%lu,"
            "\"beam_state\":\"%s\"}\n",
            (unsigned long)now_ms,
            (unsigned)block_count_snap,
            (unsigned long)total_blocked_us_snap,
            beam_blocked_snap ? "BLOCKED" : "CLEAR"
        );
    }
}
