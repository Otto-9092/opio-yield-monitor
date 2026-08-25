[phase1_bench_README_v2.md](https://github.com/user-attachments/files/31430045/phase1_bench_README_v2.md)
# Phase 1 firmware — bench occlusion detection

Proves the optical sensing works on the bench, before any GPS, Wi-Fi, or combine mounting.

## What you need on the bench

- ESP32-WROOM-32 dev board (classic ESP32, NOT S3/C3)
- 1x IR LED (940 nm, 5mm)
- 1x TSOP4838 IR receiver (Vishay, genuine)
- 1x 220 Ω resistor (1/4W)
- Breadboard + jumper wires
- USB cable

> **Firmware requirement:** this sketch uses the ESP32 Arduino Core 3.x
> LEDC API (`ledcAttach()` / `ledcWrite(pin, duty)`). It will **NOT** compile
> against Core 2.x, which used the older `ledcSetup()` / `ledcAttachPin()`
> API. Install **esp32 by Espressif Systems, version 3.0.0 or later** from
> Boards Manager. See
> [the migration guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html)
> for background.

## Wiring

See `docs/wiring/phase1_bench_wiring.png` for the diagram. Summary:

| From | To | Notes |
|---|---|---|
| ESP32 GPIO 25 | 220 Ω resistor → IR LED anode (+) | The emitter drive |
| IR LED cathode (-) | ESP32 GND | LED return path |
| TSOP4838 pin 1 (OUT) | ESP32 GPIO 26 | Signal in |
| TSOP4838 pin 2 (GND) | ESP32 GND | |
| TSOP4838 pin 3 (VS) | ESP32 3.3V | Power for the receiver |

TSOP4838 pinout, viewed from the FRONT (dome side facing you):

```text
   ┌───────┐
   │ ◉ ◉ ◉ │     Pin 1 (left):  OUT
   │  ( )  │     Pin 2 (mid):   GND
   │       │     Pin 3 (right): VS  (3.3V)
   └─┬─┬─┬─┘
     1 2 3
```

**Point the IR LED directly at the TSOP4838's dome, ~2–6 inches apart, unobstructed.**

## Build environment setup

If you don't already have Arduino IDE with ESP32 support:

1. Install **Arduino IDE 2.x** — https://www.arduino.cc/en/software
2. Open Preferences → "Additional boards manager URLs" and add:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → Boards Manager → search "esp32" → install
   **"esp32 by Espressif Systems"**, **version 3.0.0 or later**
4. Tools → Board → ESP32 Arduino → **"ESP32 Dev Module"**
5. Tools → Port → pick the USB serial port that appeared when you plugged in the ESP32
   (on Windows: some ESP32 boards need the CP210x or CH340 USB driver — search for it if the port doesn't show up)

## Flashing

1. Open `firmware/phase1_bench/phase1_bench.ino` in Arduino IDE
2. Verify (checkmark button) — should compile cleanly
3. Upload (right-arrow button) — takes ~30 seconds
4. Open Serial Monitor (Tools → Serial Monitor), set baud rate to **115200**
5. You should see the startup banner and then one JSON line per second

## Expected serial output

Beam clear, nothing between LED and TSOP:

```json
{"t_ms":15000,"block_count":0,"total_blocked_us":0,"beam_state":"CLEAR"}
{"t_ms":16000,"block_count":0,"total_blocked_us":0,"beam_state":"CLEAR"}
```

Wave a finger through the beam:

```json
{"t_ms":17000,"block_count":2,"total_blocked_us":85302,"beam_state":"CLEAR"}
```

Hold something between the sensors:

```json
{"t_ms":18000,"block_count":1,"total_blocked_us":998300,"beam_state":"BLOCKED"}
```

Drop a few corn kernels through the beam:

```json
{"t_ms":19000,"block_count":6,"total_blocked_us":4210,"beam_state":"CLEAR"}
```

## Troubleshooting

**Compile error: `'ledcSetup' was not declared in this scope`**
- You're on ESP32 Arduino Core 2.x. Upgrade to 3.0.0 or later in
  Boards Manager. Alternatively (not recommended), pin to Core 2.0.17
  and use the old-API version of this sketch from git history.

**No serial output at all**
- Wrong port selected. Check Tools → Port.
- USB cable is charge-only, not data. Try a different cable.
- ESP32 needs the boot button held during flash — some cheap dev boards do.

**block_count is always 0 even when I wave my hand**
- Emitter isn't working. Point your phone camera at the IR LED — you should see a faint purple/white glow on the camera preview (phone cameras see IR).
- TSOP pinout is wrong — double-check pin 1 = OUT, pin 3 = VS.
- Distance too far — start with the LED touching the TSOP dome to guarantee signal, then back off.

**block_count is huge and total_blocked_us is huge even when the beam is clear**
- Wiring backwards — try setting `TSOP_ACTIVE_LOW = false` in the sketch and re-flash.
- Emitter is off — confirm 38 kHz is actually being generated (oscilloscope on GPIO 25 should show a 38 kHz square wave).
- Ambient IR interference — sunlight or halogen lighting can saturate the TSOP. Move to a room with LED bulbs or blinds closed.

**block_count fluctuates randomly when beam is clear**
- TSOP AGC has decided the continuous 38 kHz is "noise" and is filtering it out.
  Workaround for Phase 2: burst-modulate the carrier (e.g., 600 µs on / 600 µs off) instead of continuous 38 kHz.
- Grounding issue — the TSOP and IR LED need a common ground with the ESP32.

**Beam is clear but beam_state says BLOCKED**
- Emitter and TSOP not aligned — the TSOP has a ~±25° acceptance cone, but the IR LED can be more directional. Rotate the LED slowly and watch beam_state flip.

## What to send me once it's working

- **A screenshot or paste** of ~10 seconds of serial output showing clear-beam, hand-wave, and sustained-block behavior
- **A photo of the breadboard** so I can sanity-check the wiring
- **Confirmation of the TSOP polarity** — did `TSOP_ACTIVE_LOW = true` work, or did you have to flip it to false?

Once Phase 1 is verified working, we move to Phase 1B (paddle-aware
signal processing — see `firmware/phase1b_paddle/`) and then Phase 2:
add the DFRobot RTK rover's NMEA input, spin up a Wi-Fi AP, and serve a
live-data webpage that your Samsung tablet can hit.
