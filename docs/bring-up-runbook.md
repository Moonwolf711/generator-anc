# Generator-ANC bench bring-up runbook

One ordered procedure from parts-in-hand to cancelling. Each stage has a **pass check** —
don't move on until it's green. Detail lives in the linked docs; this is the thread.

```
flash firmware → wire output → wire mic → wire tach → cockpit → tach lock → calibrate Ŝ → run → tune
```

---

## 0. Parts & tools

| Part | Role | Doc / link |
|---|---|---|
| Teensy 4.0 (+ ESP-12E carrier) | controller + WiFi cockpit | — |
| PC817 opto + 1N4148 + 10 kΩ + hookup wire | spark tach front-end | [spark-tach.md](spark-tach.md) |
| MAX4466 electret board | error mic → A2 (fixed gain, 1.65 V bias) | — |
| MQS RC parts: 2.2 kΩ, 22 nF, 1 µF | anti-noise line filter | [output-stage.md](output-stage.md) |
| Alpine amp + 12″ **sealed** sub | anti-noise SPL (orders 1–6, 30–183 Hz) | [output-stage.md](output-stage.md) |
| (bench-only) MAX98357A + small speaker | low-SPL desk testing | — |
| Whirlwind IMP 2 DI | ground-lift if mains hum | [output-stage.md](output-stage.md) |

Tools: arduino-cli (teensy:avr + esp8266:esp8266 cores), a phone, a multimeter, foam windscreen.

---

## 1. Flash the firmware

Two sketches. The Teensy auto-flashes; the ESP sits behind the Teensy on Serial1 and needs the
bridge dance.

**Teensy:**
```
arduino-cli compile --fqbn teensy:avr:teensy40 firmware/generator_anc_teensy
arduino-cli upload  -b teensy:avr:teensy40 firmware/generator_anc_teensy
```
Pick the output path in `generator_anc_teensy.ino`: `#define USE_MAX98357 1` for the bench speaker,
`#define USE_MAX98357 0` for the real MQS→Alpine→sub path.

**ESP-12E** (no direct USB — flash *through* the Teensy):
```
# 1. turn the Teensy into a USB<->Serial1 bridge
arduino-cli upload -b teensy:avr:teensy40 firmware/esp_passthrough
# 2. put the ESP in its bootloader: HOLD ESP-PGM, TAP reset, RELEASE ESP-PGM
# 3. flash it (it's already in the bootloader, so skip esptool's own reset):
arduino-cli compile --fqbn esp8266:esp8266:generic --output-dir /tmp/espbuild firmware/esp12e_dashboard
PYTHONNOUSERSITE=1 python -m esptool --chip esp8266 --port COM6 --before no_reset --after hard_reset \
    write_flash 0x0 /tmp/espbuild/esp12e_dashboard.ino.bin
# 4. restore the real Teensy firmware
arduino-cli upload -b teensy:avr:teensy40 firmware/generator_anc_teensy
# 5. POWER-CYCLE the board (unplug/replug USB) — the ESP needs a real reset to boot the new firmware
```
> **Pass:** phone sees WiFi **`generator-anc`** (not `ESP_xxxx`). Use the phone's WiFi list — the PC's
> `netsh` scan is unreliable/cached.

---

## 2. Wire the output stage  ·  [output-stage.md](output-stage.md)

Real path (`USE_MAX98357 0`): Teensy **pin 12 (MQS)** → `[2.2 kΩ]` → `[1 µF series]` → Alpine RCA center;
`[22 nF]` from the RC node to GND; Teensy GND → RCA shield. Amp → 12″ sealed sub.

- Amp gain knob **LOW** to start.
- Speaker polarity does **not** matter — the Ŝ calibration measures and absorbs it.

> **Pass:** continuity Teensy-GND ↔ amp-GND; no short on the RCA; amp powers up.

---

## 3. Wire the error mic

MAX4466: `VCC→3.3V`, `GND→GND`, `OUT→A2`. It biases at ~1.65 V (what `AudioInputAnalog(A2)` expects)
and has fixed (manual-pot) gain — **not** AGC. Put the **sub + mic together at the listening spot**
(the quiet zone is ~head-sized at these frequencies). Foam windscreen on the mic outdoors.

> **Pass:** `A2` idles near 1.65 V on the meter; tapping the mic wiggles it.

---

## 4. Wire the spark tach  ·  [spark-tach.md](spark-tach.md)

5–10 turns of wire around the plug HT lead → `[10 kΩ]` → PC817 LED (1N4148 anti-parallel) → opto
transistor pulls **TACH_PIN (pin 2)** low per spark. Firmware: `INPUT_PULLUP` + `FALLING`.

- Keep the low-voltage side away from the HT lead; twist the pickup pair.

> **Pass (bench, engine off):** shorting the pickup leads or sparking near the coil makes the cockpit
> RPM twitch and `tach` flip to lock.

---

## 5. Power on + connect the cockpit

Phone → WiFi `generator-anc` → **http://192.168.4.1** (tap *Use Without Internet* if iOS nags).

> **Pass:** cockpit shows **RPM 0 · NO TACH · idle**, the order labels (31–183 Hz), and the
> Calibrate/Run/Stop buttons + orders/μ/gain sliders. (This already proves phone↔ESP↔Teensy.)

---

## 6. Tach bring-up (engine ON)

Start the generator. Watch the cockpit.

> **Pass:** `TACH LOCK`, RPM tracks the engine (~1830 idle, swinging under load), `f0 ≈ 30.5 Hz`.
> If it won't lock, widen `RPM_MIN/RPM_MAX` in the firmware or check the opto edge.

---

## 7. Calibrate Ŝ (engine OFF)

Engine **off**, speaker + mic in their final positions. Tap **Calibrate** (`c`). The firmware probes
each order (defaults to 1830 RPM → 30.5–183 Hz; override with `SET cal_rpm <v>` to match your idle)
and measures the speaker→mic magnitude + phase.

> **Pass:** the **Ŝ · secondary path** card shows **`calibrated ✓`** and six non-trivial `|S|` bars.
> **`S-FAIL — probe not heard`** (red) → the amp/speaker/mic isn't delivering: raise amp gain, check
> wiring, re-run `c`.

---

## 8. Run + ramp (engine ON)

Set cockpit **Output gain ≈ 0.1**, tap **Run** (`r`), then ramp Output gain 0.1 → 0.5 → 1.0 while
watching the anti-noise effort bars and listening.

> **Pass:** residual **drops** as gain rises. Residual **rises / howls** → Ŝ phase is off (re-calibrate)
> or gain too high (back off — the gain slider is your kill switch).

---

## 9. Tune

All live from the phone, no reflash:
- **Active orders** — how many harmonics to cancel (6 fits the sub's band; raise only with a mid driver).
- **Step size μ** — convergence speed vs stability (0.01–0.06; lower if it hunts).
- **Output gain** — master anti-noise level / safe ramp.
- `SET cal_rpm <v>` then re-`c` if your idle differs from 1830.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| No `generator-anc` WiFi | ESP didn't boot new firmware | power-cycle the board; re-flash via §1 bridge |
| Cockpit `stale Ns` | Teensy not streaming (USB wedged after many flashes) | unplug/replug the board |
| `NO TACH` with engine running | opto edge / RPM gate | check FALLING edge; widen `RPM_MIN/MAX` |
| `S-FAIL` on calibrate | probe not heard | raise amp gain; check speaker + mic; re-`c` |
| Hum that isn't an engine order | ground loop | IMP 2 DI **ground-lift**, or share the 12 V ground |
| Howl / residual grows on run | wrong Ŝ phase or μ too hot | re-calibrate; lower μ; lower output gain |

## Safety
- Teensy pins are **3.3 V only** — the tach opto isolation is mandatory; never feed raw ignition to a pin.
- Calibrate and ramp with the gain low; the Output-gain slider is the kill switch.
- Keep the pickup and wiring clear of the muffler and moving parts.
