[README (13).md](https://github.com/user-attachments/files/31326304/README.13.md)# OPiO Yield Monitor

DIY optical yield monitor for older combines — a FarmTRX-style architecture
built from an ESP32, a modulated IR emitter/receiver pair, and an RTK-GNSS
receiver, streaming live yield-map data to the OPiO / Diamond O Farms PWA
over Wi-Fi.

**Target combine:** Case IH 1480 (1980s-era rotary combine, 45-year-old
platform, no factory yield monitor).

**Target cost:** ~$100 in parts (vs. $2,250+ for FarmTRX or Loup Elite).

**Target accuracy goal:** Within 5% of grain-cart-weighed yield after
calibration — enough to make actionable yield maps for management-zone
decisions.

---

## System architecture

```
                    ┌──────────────────────────┐
                    │  Cab enclosure (in cab)  │
                    │  ┌────────────────────┐  │
                    │  │ DFRobot RTK rover  │──┼─── GNSS antenna (roof)
                    │  │ (Quectel LC29HDA)  │──┼─── LoRa antenna (roof)
                    │  └─────────┬──────────┘  │
                    │            │ UART NMEA   │
                    │            ▼             │
                    │  ┌────────────────────┐  │
                    │  │ ESP32-WROOM-32     │──┼─── Wi-Fi AP → Samsung tablet
                    │  │                    │  │       (OPiO PWA)
                    │  │                    │──┼─── USB brick from 12V→120V
                    │  └────────────────────┘  │       inverter
                    └────────────┬─────────────┘
                                 │ 2-wire cable
                                 ▼
                    ┌──────────────────────────┐
                    │ Clean grain elevator     │
                    │ ┌────────┐    ┌────────┐ │
                    │ │  IR    │───▶│  TSOP  │ │  ← paddles pass through
                    │ │ LED    │    │ 4838   │ │     the beam
                    │ │(38 kHz)│    │        │ │
                    │ └────────┘    └────────┘ │
                    └──────────────────────────┘
```

**Sensing principle:** IR emitter pulses at 38 kHz. TSOP4838 receiver
demodulates and outputs LOW when the modulated beam is detected, HIGH when
the beam is broken. Grain-loaded paddles crossing the beam produce
occlusion-time events; the ESP32 sums those over a rolling window and
correlates with GPS position + ground speed to produce yield data points.

---

## Project phases

| Phase | Deliverable | Status |
|---|---|---|
| **1** | Bench prototype — ESP32 + TSOP4838 + IR LED, prove occlusion detection on the workbench | 🟡 In progress |
| **2** | Add GPS + Wi-Fi AP — ESP32 hosts an AP, serves a live-data webpage, integrates DFRobot rover NMEA | ⚪ Not started |
| **3** | OPiO PWA integration — "Harvest" tab in the Diamond O Farms app connects to ESP32, renders live yield map, handles calibration workflow | ⚪ Not started |
| **4** | Mount on combine — SCAD sensor housings, elevator bracket reusing inspection cover fastener pattern, first calibration pass at harvest | ⚪ Not started |

---

## Repository layout

```
opio-yield-monitor/
├── README.md                    ← you are here
├── LICENSE                      ← MIT
├── .gitignore
├── docs/
│   ├── PROJECT_HANDOFF.md       ← full context for the next chat thread
│   ├── 1480_recon_checklist.md  ← physical measurements + photos checklist
│   ├── recon_results/           ← photos + filled-in measurements
│   └── wiring/
│       └── phase1_bench.svg     ← breadboard wiring for Phase 1
├── firmware/
│   ├── phase1_bench/
│   │   ├── phase1_bench.ino     ← Arduino sketch for bench occlusion detection
│   │   └── README.md            ← how to build + flash
│   ├── phase2_gps_wifi/         ← (Phase 2, TBD)
│   └── phase3_pwa_bridge/       ← (Phase 3, TBD)
├── hardware/
│   ├── cab_enclosure.scad       ← RTK + ESP32 box (v7 tub is the starting point)
│   ├── emitter_housing.scad     ← elevator-side IR LED housing (TBD after recon)
│   ├── receiver_housing.scad    ← elevator-side TSOP4838 housing (TBD after recon)
│   └── elevator_bracket.scad    ← mounts sensor housings to inspection cover (TBD)
└── pwa-module/
    └── harvest_tab/             ← the OPiO PWA integration (Phase 3)
```

---

## Hardware bill of materials (Phase 1 — bench prototype)

Ordered 21/08/2026.

| Part | Qty | Vendor | Notes |
|---|---|---|---|
| ESP32-WROOM-32 dev board | 1 (spares on hand) | — | already owned |
| Vishay TSOP4838 IR receiver, 38 kHz | 5 | Amazon (VoyageSupply) | genuine Vishay parts |
| CHANZON 5mm 940nm IR emitter LED | 100 | Amazon | for beam projection |
| ALLECIN 1/4W 1% metal film resistor kit | 1 | Amazon | 50 values |
| REXQualis Dupont jumper wire kit | 1 | Amazon | M-M, M-F, F-F |
| Solderless breadboard | 1 | — | already owned |
| FNIRSI DSO152 pocket oscilloscope (optional) | 1 | Amazon | for waveform verification |

**Phase 1 cost:** ~$76 shipped, plus the ESP32 boards on hand.

---

## Hardware bill of materials (Phases 2–4 — deferred)

| Part | Notes |
|---|---|
| DFRobot GNSS-RTK Kit w/ LoRa (KIT0198-US) | already owned; base station + rover, LC29HBS + LC29HDA, 1 Hz RTK |
| 12V→120V power inverter for cab | user-supplied |
| 5V/2A USB power brick | user-supplied |
| PG7 cable gland | already in cab enclosure design |
| Sensor cable, 4-conductor shielded, ~15 ft | for emitter/receiver ↔ cab |
| Elevator mount hardware | reuse existing inspection cover fasteners (TBD after recon) |
| PLA/PETG filament for sensor housings | user-supplied (Bambu Lab printer) |

---

## Related project

**Diamond O Farms LLC — OPiO Farming App.** The Progressive Web App
(`Otto-9092/Updated-farming-app`) that this yield monitor will integrate
with. The "Harvest" tab (Phase 3 of this project) will consume the ESP32's
data stream and render live yield maps alongside the existing P&L, fields,
and equipment features.

---

## Design decisions locked in

- **Optical, not impact plate.** Modeled after FarmTRX architecture and
  6th Gen Farmer's build (YouTube: "Modernizing this 45 Year Old John Deere
  Combine!!"). Simpler DIY reproducibility, no impact-plate wear.
- **38 kHz modulation + TSOP4838 demodulator.** Standard TV-remote IR
  approach. Ambient light rejection built in.
- **ESP32-WROOM-32 as controller.** Cheap, Wi-Fi AP capable, plenty of
  GPIOs, mature toolchain. NOT ESP32-S3/C3 — different pinout.
- **Wi-Fi AP for tablet connection, not BLE.** Samsung tablet in the cab
  (Android Chrome supports both, but Wi-Fi is more robust for continuous
  streaming and works on any tablet).
- **DFRobot RTK GNSS w/ LoRa correction link.** Base station on a tripod,
  moves field-to-field, 1.5 km range. LC29HDA is 1 Hz — we interpolate in
  software using NMEA VTG heading + speed to project position between
  fixes.
- **Header width: 30 ft.** For yield calculation (bu/ac).

---

## Ongoing / open questions

- Final NMEA sentence set from the DFRobot rover (need a dump — see the
  1480 recon checklist).
- Elevator interior geometry (need recon photos + measurements).
- Whether the DFRobot rover's UART is easily accessible for ESP32 wiring
  (check pin breakouts when the board is in hand).
- Base station survey-in workflow — first-time setup and re-survey when
  moving between fields.

---

*Owner: Mike Otto, Pioneer Hi-Bred Territory Manager, Nebraska panhandle
& SE Wyoming. Project started 21/08/2026.*
