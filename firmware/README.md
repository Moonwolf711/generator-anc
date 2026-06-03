# generator-anc firmware (Teensy 4.0/4.1)

Real-time engine-order ANC for a **Champion 4250** (224cc single-cylinder inverter genset)
on a Teensy 4.x + PJRC Audio Shield. The controller is `../include/eoc/engine_order_canceller.hpp`
(a synced copy lives in `generator_anc_teensy/` so the Arduino sketch can include it).

## What it does

- Reads the **error mic** through the Audio Shield codec.
- Reads the **spark tach** (one pulse/revolution) on an interrupt; interpolates crank angle and
  **phase-locks** the controller every audio block — the coherence that makes real cancellation work.
- Adapts a per-harmonic (cos/sin) anti-noise signal and plays it out the codec to a power amp/speaker.
- Tracks RPM live (essential — this is an inverter genset; engine speed swings with load).

## Bill of materials

| part | note |
|---|---|
| Teensy 4.0 or 4.1 | 600 MHz M7, the compute |
| PJRC Audio Shield (rev D, SGTL5000) | clean mic-in + line-out codec |
| Inductive spark pickup | clamps on the plug wire (timing-light style); no engine mod |
| Tach conditioning | clamp + comparator/transistor → **0/3.3 V** pulse (see below) |
| Electret/measurement mic | the error mic, into LINEIN |
| Class-D power amp + woofer | must reproduce ~30–360 Hz cleanly (a small subwoofer) |

## Wiring

```
spark plug wire ──[inductive clamp]──[diode+RC clamp]──[comparator → 0/3.3V]── Teensy pin 2 (TACH_PIN)
error mic ── Audio Shield LINEIN (L)
Audio Shield LINEOUT (L) ── power amp ── anti-noise speaker (near the error mic)
Teensy + Audio Shield: stack per PJRC pinout (I2S + I2C)
IMP 2 DI with GROUND LIFT on the line-level run to the amp -> kills the genset's 60 Hz ground-loop hum
```

**SAFETY — read this:**
- Teensy 4.x pins are **3.3 V only and NOT 5 V tolerant**. Never connect raw ignition voltage (kV)
  to a pin. The inductive pickup + clamp + comparator must deliver a clean 0–3.3 V pulse.
- A running generator emits **carbon monoxide** — bench/calibrate outdoors or with the engine off.
- The genset ground is dirty/possibly bonded-neutral; isolate line-level gear (the IMP 2 ground-lift)
  and don't create a shock path between the genset and your bench supply.

## Build

1. Install **Teensyduino** (Arduino IDE add-on).
2. Open `generator_anc_teensy/generator_anc_teensy.ino`. The controller header is already beside it.
   (To re-sync after editing the core: `cp ../../include/eoc/engine_order_canceller.hpp generator_anc_teensy/`.)
3. Board: Teensy 4.0/4.1, USB Type: Serial, CPU 600 MHz. Upload.
4. Open Serial Monitor (115200) — you'll see `f0`, RPM, per-order amplitudes, CPU%, and tach lock.

## Calibration (REQUIRED for actual cancellation)

The controller needs the **secondary path** `Ŝ` — the speaker→error-mic magnitude and phase at each
harmonic. Without it, it won't cancel (and a 180°-wrong `Ŝ` makes it worse — proven in the unit tests).

One-time procedure (engine off, rig in final geometry):
1. For each order frequency, emit a unit cosine from the anti-noise speaker.
2. Correlate the mic signal against cos/sin to get magnitude and phase.
3. Call `eoc.setSecondaryPath(h, mag, phase)` with those values.

`setup()` currently loads unity/zero placeholders — correct only if the speaker and mic are co-located.

## How well it works / limits

- ANC creates a **local quiet zone** at the error mic (head-sized), not silence everywhere. Place the
  mic where the ears are.
- It cancels the **tones** (engine orders) — the drone — not broadband exhaust roar or wind.
- Expected: deep nulls at orders 1–6 once `Ŝ` is calibrated (the desktop sim hits 56–86 dB/order with a
  *realistic* `Ŝ`; real-world will be less, limited by `Ŝ` accuracy, speaker headroom, and mic placement).
- Sub-block tach interpolation is block-granular (~2.9 ms); fine for these RPMs. For very fast transients,
  refine to sample-accurate spark timestamping.
