# Case IH 1480 — Clean Grain Elevator Recon Checklist
## For DIY Optical Yield Monitor (OPiO Yield Monitor Project)

**Combine:** Case IH 1480
**Purpose:** Gather everything needed to design sensor mounting brackets and housings for a FarmTRX-style optical yield monitor (IR emitter + TSOP receiver across the elevator shaft).
**Time to complete:** ~45–90 minutes on the combine, once you're there.

---

## ⚠️ Safety First — Read Before Approaching

- **Engine OFF, key OUT, wait for all motion to stop.** The clean grain elevator chain will take a finger.
- **Chock the wheels** if the combine is on any grade.
- Bring: **flashlight**, **tape measure** (imperial), **calipers** (for fastener sizes), **phone** (photos), **notebook and pen** (backup for numbers), and this checklist printed or on-screen.
- If the inspection cover is stuck, don't force it with sharp tools — use a rubber mallet and a soft pry.

---

## Section 1 — Overview Shots (before touching anything)

Take these BEFORE you remove any covers. If you have to walk away, you want to know what "before" looked like.

- [ ] **Photo 1a:** Full clean grain elevator from the side, ground-level, showing the whole tube from bottom to top sprocket
- [ ] **Photo 1b:** Same shot from the opposite side of the combine
- [ ] **Photo 1c:** Top of the elevator, where it dumps into the grain tank. Include the top sprocket area.
- [ ] **Photo 1d:** The inspection cover(s) as-installed, showing fastener heads and any labels
- [ ] **Photo 1e:** Wide shot showing what's adjacent to the elevator on both sides — hydraulic lines, wiring, framework, engine components. We need to know what we can't hit when running sensor cables.

**Note:** If the 1480 has more than one inspection cover on the clean grain elevator, photograph ALL of them and pick the one on the **up-side** (grain-moving-upward side) for the sensor location.

---

## Section 2 — Inspection Cover, Cover REMOVED

Once the engine is off and you've confirmed no motion, remove the inspection cover on the **up-side** of the elevator.

- [ ] **Photo 2a:** Cover removed, looking straight into the elevator housing. Get the whole opening in frame.
- [ ] **Photo 2b:** Angled shot into the housing showing where the chain runs and where the paddles are relative to the opening.
- [ ] **Photo 2c:** The **wear/polish marks** inside the housing. This is critical — shiny/polished zones show where grain has been impacting or scraping. **This zone tells us where the grain stream actually flows,** which is where we want the optical beam to cross.
- [ ] **Photo 2d:** Photo of one paddle (rotate the chain BY HAND with the engine off if you can). Show the paddle profile — is it flat, cupped, angled? Are there gaps at the edges?
- [ ] **Photo 2e:** The back of the inspection cover, showing any gasket, seal, or wear pattern on its inside surface.

---

## Section 3 — Measurements (this is the SCAD-critical part)

Have a helper if you can — some of these are two-hand jobs.

### 3.1 Elevator housing external dimensions

| # | Measurement | Value | Notes |
|---|---|---|---|
| M1 | **Housing outer width** (across the elevator, perpendicular to grain flow) | ______ in | The dimension the IR beam has to cross |
| M2 | **Housing outer depth** (front-to-back, in the direction the elevator faces) | ______ in | |
| M3 | **Elevator wall thickness** (if you can see a cut edge or feel it at the inspection opening) | ______ in | Steel is usually 3/16" or 1/4" on this era of combine |
| M4 | **Height from ground to inspection cover center** | ______ in | So we can plan a ladder / access when we install |

### 3.2 Inspection cover dimensions

The inspection cover is our **mounting real estate.** We'll probably attach the sensor bracket to the existing cover fastener pattern.

| # | Measurement | Value | Notes |
|---|---|---|---|
| M5 | **Cover length** (longest dimension) | ______ in | |
| M6 | **Cover width** (shortest dimension) | ______ in | |
| M7 | **Cover thickness** | ______ in | Calipers if you have them |
| M8 | **Bolt/screw pattern** — number of fasteners on the cover | ______ | |
| M9 | **Bolt spacing** — measure center-to-center for each pair | ______ in × ______ in | Sketch on the back of this page if pattern isn't a simple rectangle |
| M10 | **Fastener bolt size** — diameter × thread pitch, or a photo next to a ruler | ______ | e.g. "1/4-20" or "M8×1.25" |
| M11 | **Fastener head type** — hex bolt, capscrew, sheet-metal screw, other | ______ | Photo helps |
| M12 | **Fastener length** | ______ in | Unthread one and measure |

### 3.3 Interior geometry (measured through the open inspection port)

**⚠️ Hands OUT of the elevator when the engine could conceivably start. Ideally, disconnect the battery.**

| # | Measurement | Value | Notes |
|---|---|---|---|
| M13 | **Interior width** (chain side to chain side, across the paddle) | ______ in | This is the beam-crossing distance |
| M14 | **Paddle width** (the flat/cupped face that pushes grain) | ______ in | Measure the widest part |
| M15 | **Paddle height** (dimension along the direction of motion) | ______ in | |
| M16 | **Paddle spacing** (center of one paddle to center of the next, along the chain) | ______ in | Rotate by hand and measure — critical for firmware timing |
| M17 | **Distance from inspection port opening to the paddles** (depth into the housing) | ______ in | Important: the sensor bracket has to stand off from the housing so it doesn't foul the paddles |
| M18 | **Clearance between paddle edge and opposite wall** (where the beam would live) | ______ in | If a paddle is a "shelf" that spans full width, note that too |

### 3.4 Fastener + surface prep observations

- [ ] **Is the cover flat?** (I.e., can we use it as a mounting plane, or is it curved/dished?)
- [ ] **Is the housing wall flat around the cover?** (Alternative mounting surface if the cover itself isn't flat.)
- [ ] **What condition are the existing fastener threads?** (Reusing them is preferred over drilling.)
- [ ] **Is there room BEHIND the cover for a bracket to stand off toward the paddles?** (Distance measured in M17.)
- [ ] **Any sensors or wiring already on the elevator?** (Factory yield monitor, moisture sensor, elevator-full switch — these are all mounted in similar places and we need to not conflict.)

---

## Section 4 — Environmental Notes

These affect the enclosure and cable-routing design, not the sensor itself.

- [ ] **Cable routing path from elevator to cab.** Photograph the route you'd run wiring. Note any pinch points, sharp edges, or existing looms we could zip-tie to.
- [ ] **Nearest 12V switched power source.** In the cab, usually behind the radio or headliner. Photograph its location if you already know it.
- [ ] **GPS/LoRa antenna mounting locations.** Where on the cab roof do you plan to mount the RTK antenna and the LoRa antenna? Photo the roof from outside if you're near a ladder.
- [ ] **Is there existing cab wiring conduit or a gland plate** you could pass the sensor cable through, or does it have to go through a rubber grommet you install?

---

## Section 5 — Two Elevator Sprockets (Confirm)

The **top sprocket** is where paddles round the corner and dump into the tank. We want to mount sensors just **below** the top sprocket, in the straight-up section, so the paddles are moving at constant speed and are still fully loaded.

- [ ] **Photo 5a:** Top sprocket area, with dimensions if possible — distance from top of housing to the sensor location.
- [ ] Confirm: **the inspection cover you're planning to use is on the straight-up run**, not around a curve. If it's around the sprocket curve, note that and we'll pick a different location.

---

## Section 6 — What NOT to Skip (I know it's tempting)

- **All measurements in inches, decimal or fractional both fine.** (Metric fine too — just label it clearly.)
- **Photograph EVERYTHING with a ruler or tape in the frame.** A photo with no scale reference is worth about 30% of a photo with one.
- **When in doubt, take extra photos.** Data is cheap. A second trip is not.
- **If a measurement is a range** (e.g., housing wall thickness varies), note the range.
- **If something doesn't fit this checklist**, write it down anyway. Things I didn't think to ask about are often the most valuable observations.

---

## Section 7 — Post-Recon Deliverable Back to Me

When you're done, upload to the chat:

1. **All photos** (in whatever order — I'll sort them)
2. **This checklist filled in** — either mark it up on-screen and upload, or scan a paper copy
3. **A short "gotchas" note** — anything surprising, e.g., "the cover is welded on both sides, I couldn't remove it" or "there's already a moisture sensor there"
4. **Confirmation of the DFRobot NMEA dump** — 30 seconds of NMEA output from the rover, pasted as text (see below)

### Bonus while you're near the combine

- [ ] **DFRobot rover NMEA dump.** If you have the DFRobot kit assembled and can power it up (either bench or in the combine cab if that's easier), hook up the rover to your laptop via USB-C, open a serial terminal at 115200 baud, and copy 30 seconds of NMEA output. Paste as text — I need to see which sentences your rover emits (GGA/RMC/VTG/GST/GSA/GSV) so I can write the parser against real data.
- [ ] **Photos of the DFRobot rover board's connectors** — I want to see how the UART, power, and antenna connectors are laid out so I can spec the cab enclosure layout properly.

---

## What Happens Next

Once I have your photos, measurements, and NMEA dump:

1. I'll process the elevator geometry into a **parametric SCAD file** for the sensor bracket + emitter housing + receiver housing.
2. I'll design the bracket to **reuse your existing inspection cover fasteners** wherever possible (no new holes in the housing).
3. Firmware for Phase 1 (bench prototype) will already be written and ready to flash by the time your parts arrive.

**One-trip recon is the goal.** Come back with everything on this list and we'll be locked and loaded for Phase 4 (mounting on the combine) as soon as the bench prototype (Phase 1-3) is proven.

---

*Generated 21/08/2026 for Mike Otto — Pioneer Hi-Bred TM, Scottsbluff NE*
