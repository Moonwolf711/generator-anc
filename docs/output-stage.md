# Anti-noise output stage — Alpine amp + 12″ sealed sub

The controller computes anti-noise; this stage turns it into enough SPL at 30–183 Hz
to actually null the generator's first six orders. Two firmware-selectable paths:

| `firmware/.../generator_anc_teensy.ino` | Path | Use |
|---|---|---|
| `#define USE_MAX98357 1` (current) | I²S → MAX98357A → small 4–8 Ω speaker, ~3 W | **bench / near-field** only — not enough SPL for the genset |
| `#define USE_MAX98357 0` | MQS (pins 10/12) → RC low-pass → **Alpine amp → 12″ sealed sub** | **the real deployment** — big SPL where the orders live |

Flip to `USE_MAX98357 0` for the generator. The 12″ sub in its sealed box covers
30–183 Hz with real output, which is exactly orders 1–6 (`NUM_ORDERS = 6`) and exactly
where the genset capture showed the biggest energy.

## Signal chain: Teensy MQS → Alpine line input

MQS is the Teensy's PWM audio out (pin 12 = left, pin 10 = right). It needs an RC
low-pass to recover the waveform, and AC-coupling to drop its DC bias before the amp.

```
 Teensy pin 12 (MQS) ──[ R 2.2k ]──┬──[ C 1µF ]──→ Alpine RCA center (signal +)
                                    │
                               [ C 22nF ]
                                    │
   Teensy GND ──────────────────────┴───────────→ Alpine RCA shield (signal −)
```

- **RC low-pass** R=2.2 kΩ, C=22 nF → fc ≈ 3.3 kHz. Kills the MQS carrier (hundreds
  of kHz), passes the 30–360 Hz band untouched. (10 nF → 7 kHz also fine; lower C =
  cleaner for a sub-only feed.)
- **Series C** 1 µF (film or electrolytic) blocks the MQS ~1.65 V DC bias. With the
  amp's input impedance (~10–47 kΩ) it high-passes at ~3–16 Hz — below the band.
- **Level:** MQS is ~3.3 Vpp after the RC. Most Alpine RCA inputs are happy with that
  if you keep the amp gain knob LOW. If it distorts, add a divider (10 kΩ series + 3.3 kΩ
  to GND ≈ 0.8 Vpp). Use one MQS pin (12); leave pin 10 unused for a single-ended amp.

## Polarity — auto-handled by Ŝ (don't sweat the speaker leads)

For cancellation the anti-noise must hit the error mic 180° out of phase with the
engine noise. You do **not** set speaker polarity by hand. The secondary-path
calibration (`c`, engine OFF) measures the *whole* speaker→amp→air→mic response —
magnitude **and** phase, including however the sub is wired. Reversed leads just appear
as a π term in Ŝ, which the adaptation absorbs. Wire it either way; calibrate Ŝ; the
controller finds the null.

The one hard requirement: **calibrate Ŝ for the real rig** before running. The firmware
ships with unity-Ŝ placeholders (`setSecondaryPath(h, 1.0, 0.0)`), correct only if the
speaker and mic are co-located. Replace them via the `c` routine.

## Why a sealed box (you have the right one)

Sealed enclosures have a tight, well-behaved phase response. Ported boxes add group
delay and a phase twist near the port tuning that makes Ŝ harder to estimate and track.
Sealed is the correct choice for ANC — good that yours is sealed.

## The quiet zone is local

Low-frequency ANC is **zonal** — the null forms around the error mic, roughly λ/10
across. At 100 Hz (λ ≈ 3.4 m) that's a ~30 cm, head-sized bubble. Place the **sub +
error mic together at the listening position** (the window/campsite side of the
generator), not at the engine. You're making one spot quiet, not the whole yard.

## Safe bring-up (use the cockpit Output-gain slider)

1. Amp gain knob LOW. Cockpit **Output gain** ≈ 0.1.
2. Engine OFF → `c` (Calibrate). The sub sweeps; the error mic should hear it; Ŝ
   confidence climbs.
3. Engine ON → `r` (Run). Ramp Output gain 0.1 → 0.5 → 1.0 while watching the
   residual on the cockpit.
4. Residual **drops** → working. Residual **rises / howls** → Ŝ phase is off
   (recalibrate) or gain too high (back off). The leaky NLMS + per-order normalization
   keep it stable inside the safe range; the gain slider is your ramp + kill switch.

## Hum / grounding (where the IMP 2 DI fits)

The Alpine is 12 V automotive ground; the Teensy is 5 V/USB. A ground loop between them
injects 60/120 Hz hum into the anti-noise. Two fixes:

- Run the MQS→amp line through the **Whirlwind IMP 2 DI with the ground-lift engaged** —
  isolates the signal run from the amp/genset ground.
- Or power the Teensy from the same 12 V rail (buck to 5 V) so they share one ground.

Use the DI lift if you hear mains-harmonic hum that isn't an engine order.

## Cleaner DAC option

If MQS quantization noise ever bothers you, the PJRC Audio Shield (SGTL5000) line-out
(`USE_AUDIO_SHIELD 1`) is a true DAC. But MQS noise is ultrasonic and the sub + amp
filter it out, so MQS is perfectly adequate for sub-bass anti-noise — don't add the
shield unless you measure a problem.
