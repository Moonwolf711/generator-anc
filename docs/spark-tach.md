# Spark Tach — Champion 4250 phase reference

The engine-order controller is only as good as its phase reference. This is the
build that gives the Teensy a clean **once-per-revolution** pulse locked to the
engine, so `syncPhase()` can re-seat the anti-noise oscillators on every spark.
Without it you get the desk-fan result (≈ −4 dB); with it you get the
tach-locked regime (20 dB+ per order). See the fan experiment in
`tools/fan_anc_test.py` for the empirical proof.

## Why one pulse per rev

The Champion 4250 is a single-cylinder 4-stroke with a magneto ignition. The
magneto fires the plug **once per crankshaft revolution** ("wasted spark" — once
on compression, once on exhaust). So spark rate (Hz) = RPM / 60 = the 1st-order
(rotation) frequency — ~30 Hz at 1800 RPM, the fundamental the controller
cancels. Each spark is a hard timing fiducial: that's all we need.

## Circuit (opto-isolated, 3.3 V safe)

**Pickup** = 5–10 turns of insulated hookup wire wrapped around the spark-plug
HT lead (the thick rubber boot wire). It's a floating coil — no electrical
contact, purely inductive. The fast kV spark edge induces a sharp pulse on it.

The pulse is bipolar, ringy, and can be tens of volts — so it never touches the
Teensy directly. It drives an **optocoupler LED**; the phototransistor on the
isolated side pulls the Teensy pin LOW. The opto is the galvanic barrier that
keeps brutal ignition transients off the 3.3 V logic.

```
   spark plug HT lead
        ||  (5-10 turns wrapped around it = pickup coil)
        ||
   pickupA o------[ 10k ]------+----------------+
                               |                |        PC817
                            [1N4148]         |>|  LED  (input side, FLOATING
                            (cathode            |        near the engine)
                             to +)              |
   pickupB o-------------------+----------------+
                                  - - - - - - - - - - -  isolation barrier
                                                |C
   Teensy 3.3V o---[ 10k ]---+----------------- | phototransistor
                             |                   |E
   Teensy pin 2 o------------+                   |
   (INPUT_PULLUP)                                |
   Teensy GND  o---------------------------------+
```

- `10k` series (LED side) limits peak LED current (~3 mA on a 30 V pulse) —
  raise to 22k if the pickup is hot, lower to 4.7k if it won't trigger.
- `1N4148` anti-parallel across the LED clamps the reverse half-cycle (the
  PC817 LED only tolerates ~6 V reverse).
- The `10k` on the output side is optional — the firmware uses the Teensy's
  **internal** pull-up (`INPUT_PULLUP`), so you can omit the external one.
- Idle = pin HIGH. Spark → LED lights → transistor conducts → pin → LOW.
  The firmware triggers on the **FALLING** edge = the spark onset.

### Using a pre-made PC817 module instead of a bare chip

A 1- or 2-channel PC817 board saves you breadboarding. Feed the pickup through
the `10k` + `1N4148` into the module's input terminals (IN+/IN−), take its
output to Teensy pin 2, power its logic side from Teensy 3.3 V + GND. Same
behavior: output pulls low on each spark.

## Firmware

Already wired in `firmware/generator_anc_teensy/generator_anc_teensy.ino`:

```c
static constexpr int TACH_PIN = 2;                 // conditioned pulse in
pinMode(TACH_PIN, INPUT_PULLUP);                   // idle HIGH
attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, FALLING);
```

The ISR timestamps each falling edge with the CPU cycle counter and **period-
gates** it (rejects anything faster than `RPM_MAX`=4200 or slower than
`RPM_MIN`=1200) so ignition ringing and dropouts can't inject false sparks.
Widen `RPM_MIN/MAX` in the config block if your idle/load RPM falls outside.

> If you build a **push-pull comparator** conditioner instead of the opto
> (drives the line both HIGH and LOW), switch back to `INPUT` + `RISING`.

## Bring-up test (do this BEFORE going near the running engine wired in)

1. Bench-prove with no engine: tap the pickup leads together, or wave the coil
   near any sparking source — the serial monitor should show `tach=lock` and a
   plausible RPM, or the Live Bench dashboard's Ŝ/RPM should react.
2. On the generator: clip the coil around the plug boot, route the low-voltage
   pair away from the HT lead, start the engine. Serial `?` → RPM should track
   the engine (≈1800 idle → ~3000+ under load) and `tach=lock`.
3. Only once the tach reads stable RPM do you calibrate Ŝ (`c`, engine OFF) and
   run (`r`).

## Safety

- Teensy 4.x pins are **3.3 V only**. The opto is mandatory isolation — never
  shortcut the pickup to a pin.
- Keep the conditioner's low-voltage wiring physically away from the HT lead;
  twist the pickup pair to reject common-mode ignition noise.
- The pickup is non-invasive (no fuel-system contact). Mount the coil so it
  can't drift into the muffler or moving parts.

## Bill of materials

| Part | Role | Note |
|---|---|---|
| Insulated hookup wire | pickup coil | 5–10 turns around the HT lead — likely already on hand |
| PC817 optocoupler (chip or 1-ch board) | isolation | see purchase links in the session / README |
| 1N4148 diode | reverse clamp | jellybean |
| 10 kΩ resistor ×1–2 | LED current limit | jellybean |
| MAX4466 electret board | **error mic** → Teensy A2 | fixed manual gain, biases at 1.65 V (what A2 expects) — NOT an AGC mic |
| Dayton EMM-6 (optional) | flat validation mic → Komplete | honest before/after dB; you already have SM58+Komplete as a rougher stand-in |
