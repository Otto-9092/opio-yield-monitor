/*
 * Yield Monitor - Combine Firmware v1
 * ESP32 + Chanzon 5mm 940nm IR LED + TSOP4838 receiver
 *
 * CHANGES FROM v0.4 (2026-09-25):
 *   1. Added per-event occlusion WIDTH capture (in microseconds) via ISR-side
 *      pulse timing. Each falling edge (beam becomes BLOCKED, TSOP OUT goes HIGH)
 *      records t_start_us. Each rising edge (beam becomes CLEAR) records
 *      t_end_us and computes width_us = t_end - t_start.
 *   2. Added a ring buffer of the last N=64 completed events, each storing
 *      {timestamp_us, width_us}.
 *   3. Main loop now computes and reports:
 *        - baseline_us: mean of the narrowest 25% of recent widths
 *          (empty-paddle floor - grain never makes a paddle narrower)
 *        - last_width_us: width of the most recent completed event
 *        - excess_us: last_width_us - baseline_us, floored at 0
 *          (this is the grain-loading signal for Phase 2 calibration)
 *   4. Added a compile-time CSV_STREAM flag. When defined, each completed
 *      event is also emitted as a single CSV line:
 *        EVT,<paddle_count>,<t_us>,<width_us>
 *      Turn on for offline analysis in Python. Turn off for cleaner status
 *      output during interactive use.
 *   5. Header pulse-rate expectation updated to reflect measured on-machine
 *      data from 2026-09-25:
 *        - Empty CGE at 2800 engine RPM = ~7 Hz sustained (stdev 0.59 Hz)
 *      Previous v0.3/v0.4 header cited 17 Hz predicted from a 450 RPM head
 *      shaft measurement that did not reconcile with observed rate. Observed
 *      rate now supersedes the geometric prediction.
 *
 * BASIS FOR CHANGES:
 *   Real machine data from 2026-09-25 (empty CGE, Case IH 1480, 2800 engine RPM)
 *   showed 7 Hz paddle passing rate. See
 *   docs/recon_results/2026-09-25_cge_empty_2800rpm.md for full test writeup.
 *
 * Target: Case IH 1480 clean grain elevator paddle counter
 * Elevator: 8" throat, glass-window mount, paddles every 6.52" (CA550 chain, 4 links)
 * Observed paddle rate (empty, 2800 engine RPM): 7 Hz sustained, 9 Hz peak
 *
 * Hardware wiring (bench + on-machine):
 *   GPIO 25 -> IR LED anode (through ~150 ohm resistor to +3.3V)
 *              (LEDC PWM drives 38 kHz carrier on GPIO 25)
 *   GPIO 26 <- TSOP4838 OUT pin (has internal pull-up, active LOW when IR detected)
 *   GPIO 2  -> Onboard LED (heartbeat)
 *
 * Author: Mike Otto's 1480 yield monitor project
 * Date: 2026-09-25 (v0.5: per-event width capture, on-machine calibration)
 */

#include <Arduino.h>

// ---- Compile-time options ----
// Uncomment to emit one CSV line per completed event (verbose but great for
// offline analysis). Comment out for cleaner status-only output.
#define CSV_STREAM

// ---- Pin assignments ----
constexpr int PIN_IR_LED    = 25;   // 38 kHz PWM carrier out
constexpr int PIN_TSOP_IN   = 26;   // TSOP4838 signal in (active LOW when clear)
constexpr int PIN_HEARTBEAT = 2;    // Onboard LED

// ---- LEDC (PWM) config for 38 kHz carrier (ESP32 Core 3.x API) ----
constexpr int LEDC_FREQ_HZ    = 38000;
constexpr int LEDC_RESOLUTION = 8;
constexpr int LEDC_DUTY       = 128;   // 50% duty

// ---- Debounce ----
// At 7 Hz observed / 9 Hz peak, full cycle is 111-143 ms. Half-cycle is 55-71 ms.
// 10 ms debounce leaves ~5x margin against the shortest legit half-cycle.
constexpr unsigned long DEBOUNCE_US = 10000UL;   // 10 ms in microseconds

// ---- Event ring buffer ----
constexpr int EVENT_BUF_SIZE = 64;   // Rolling window of recent events

struct PaddleEvent {
  uint32_t t_start_us;    // micros() at falling edge (beam went BLOCKED)
  uint32_t width_us;      // duration BLOCKED, in microseconds
};

volatile PaddleEvent g_events[EVENT_BUF_SIZE];
volatile int          g_eventHead = 0;   // next write slot
volatile int          g_eventCount = 0;  // total completed events written

// ---- ISR state ----
// TSOP4838: OUT is LOW when carrier detected (beam CLEAR).
//           OUT is HIGH when no carrier (beam BLOCKED).
volatile int      g_lastStableState  = LOW;      // LOW = CLEAR, HIGH = BLOCKED
volatile uint32_t g_lastEdgeUs       = 0;        // For debounce
volatile uint32_t g_blockStartUs     = 0;        // Set when beam becomes BLOCKED
volatile unsigned long g_paddleCount = 0;        // Total paddles observed

// ---- Main-loop reporting state ----
unsigned long g_lastReportMs   = 0;
unsigned long g_lastReportCount = 0;
int           g_lastEventReported = 0;   // How many events we've CSV'd so far

// ---- ISR: TSOP edge detection with debounce + width capture ----
void IRAM_ATTR tsopIsr() {
  uint32_t now_us = micros();
  if (now_us - g_lastEdgeUs < DEBOUNCE_US) return;

  int state = digitalRead(PIN_TSOP_IN);
  if (state == g_lastStableState) return;

  g_lastStableState = state;
  g_lastEdgeUs = now_us;

  if (state == HIGH) {
    // Beam JUST BLOCKED - paddle leading edge entering beam
    g_blockStartUs = now_us;
    g_paddleCount++;
  } else {
    // Beam JUST CLEARED - paddle trailing edge leaving beam
    // Compute width and write event to ring buffer
    if (g_blockStartUs != 0) {
      uint32_t width = now_us - g_blockStartUs;
      int slot = g_eventHead;
      g_events[slot].t_start_us = g_blockStartUs;
      g_events[slot].width_us   = width;
      g_eventHead = (g_eventHead + 1) % EVENT_BUF_SIZE;
      g_eventCount++;
    }
  }
}

// ---- Compute empty-paddle baseline as mean of narrowest 25% of buffer ----
uint32_t computeBaselineUs() {
  // Snapshot buffer
  noInterrupts();
  int nAvail = (g_eventCount < EVENT_BUF_SIZE) ? g_eventCount : EVENT_BUF_SIZE;
  uint32_t widths[EVENT_BUF_SIZE];
  for (int i = 0; i < nAvail; i++) {
    widths[i] = g_events[i].width_us;
  }
  interrupts();

  if (nAvail < 4) return 0;

  // Sort ascending (simple insertion sort - small array)
  for (int i = 1; i < nAvail; i++) {
    uint32_t key = widths[i];
    int j = i - 1;
    while (j >= 0 && widths[j] > key) {
      widths[j+1] = widths[j];
      j--;
    }
    widths[j+1] = key;
  }

  // Mean of narrowest 25%
  int nUse = nAvail / 4;
  if (nUse < 1) nUse = 1;
  uint64_t sum = 0;
  for (int i = 0; i < nUse; i++) sum += widths[i];
  return (uint32_t)(sum / nUse);
}

uint32_t lastEventWidthUs() {
  noInterrupts();
  int last = (g_eventHead - 1 + EVENT_BUF_SIZE) % EVENT_BUF_SIZE;
  uint32_t w = (g_eventCount > 0) ? g_events[last].width_us : 0;
  interrupts();
  return w;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("==============================================="));
  Serial.println(F("Yield Monitor Bench v0.5 - Width Capture"));
  Serial.println(F("==============================================="));

  // 38 kHz carrier
  ledcAttach(PIN_IR_LED, LEDC_FREQ_HZ, LEDC_RESOLUTION);
  ledcWrite(PIN_IR_LED, LEDC_DUTY);
  Serial.print(F("IR carrier: "));
  Serial.print(LEDC_FREQ_HZ);
  Serial.print(F(" Hz on GPIO "));
  Serial.println(PIN_IR_LED);

  // TSOP input
  pinMode(PIN_TSOP_IN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TSOP_IN), tsopIsr, CHANGE);
  Serial.print(F("TSOP input: GPIO "));
  Serial.println(PIN_TSOP_IN);

  // Heartbeat
  pinMode(PIN_HEARTBEAT, OUTPUT);

  Serial.print(F("Debounce: "));
  Serial.print(DEBOUNCE_US / 1000);
  Serial.println(F(" ms"));
  Serial.print(F("Event buffer: "));
  Serial.print(EVENT_BUF_SIZE);
  Serial.println(F(" slots"));

#ifdef CSV_STREAM
  Serial.println(F("CSV_STREAM enabled - per-event lines emitted as EVT,count,t_us,width_us"));
#endif

  Serial.println(F("Ready."));
  Serial.println();

  g_lastReportMs = millis();
}

void loop() {
  unsigned long now = millis();

  // Heartbeat
  digitalWrite(PIN_HEARTBEAT, (now / 500) % 2);

#ifdef CSV_STREAM
  // Emit CSV line for each newly completed event
  noInterrupts();
  int currentEvents = g_eventCount;
  interrupts();
  while (g_lastEventReported < currentEvents) {
    // Find the event N-back in the ring buffer
    int howFarBack = currentEvents - g_lastEventReported;
    if (howFarBack > EVENT_BUF_SIZE) {
      // We fell behind - skip ahead to the oldest still-in-buffer
      g_lastEventReported = currentEvents - EVENT_BUF_SIZE;
      howFarBack = EVENT_BUF_SIZE;
    }
    int slot = (g_eventHead - howFarBack + EVENT_BUF_SIZE) % EVENT_BUF_SIZE;
    noInterrupts();
    uint32_t t_us = g_events[slot].t_start_us;
    uint32_t w_us = g_events[slot].width_us;
    interrupts();
    Serial.print(F("EVT,"));
    Serial.print(g_lastEventReported + 1);
    Serial.print(F(","));
    Serial.print(t_us);
    Serial.print(F(","));
    Serial.println(w_us);
    g_lastEventReported++;
  }
#endif

  // Status report every 1000 ms
  if (now - g_lastReportMs >= 1000) {
    noInterrupts();
    unsigned long count = g_paddleCount;
    uint32_t lastEdgeUs = g_lastEdgeUs;
    int beamState = g_lastStableState;
    interrupts();

    unsigned long deltaCount = count - g_lastReportCount;
    float hz = (float)deltaCount * 1000.0f / (float)(now - g_lastReportMs);
    uint32_t msSinceEdge = (micros() - lastEdgeUs) / 1000;

    uint32_t baseline = computeBaselineUs();
    uint32_t lastWidth = lastEventWidthUs();
    int32_t excess = (int32_t)lastWidth - (int32_t)baseline;
    if (excess < 0) excess = 0;

    Serial.print(F("[t="));
    Serial.print(now / 1000);
    Serial.print(F("s] paddles="));
    Serial.print(count);
    Serial.print(F("  rate="));
    Serial.print(hz, 1);
    Serial.print(F(" Hz  beam="));
    Serial.print(beamState == HIGH ? F("BLOCKED") : F("CLEAR  "));
    Serial.print(F("  ms_since_edge="));
    Serial.print(msSinceEdge);
    Serial.print(F("  baseline_us="));
    Serial.print(baseline);
    Serial.print(F("  last_width_us="));
    Serial.print(lastWidth);
    Serial.print(F("  excess_us="));
    Serial.print(excess);
    Serial.println();

    g_lastReportMs = now;
    g_lastReportCount = count;
  }
}
