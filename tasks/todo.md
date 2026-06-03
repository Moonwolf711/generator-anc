# Generator ANC — Engine-Order Cancellation (real-time C++17)

## Decisions (locked with user)
- **Target:** real-time C++17 controller, hardware-bound (mic + anti-noise speaker + MCU/DSP). Portable core, no OS audio deps.
- **Reference:** tach / RPM-synced engine-order cancellation (narrowband FxLMS on firing harmonics).
- **Tuning data:** user will provide a generator recording; until then, validate on synthesized genset noise.
- **Toolchain:** MinGW g++ 15.2, CMake 4.1. `-Wall -Wextra -Wpedantic` clean. No UB. ASAN/UBSAN in debug.
- **IP:** core algorithm is original. Only dependency is a vendored ~40-line test harness (own).

## Why engine-order, not broadband FxLMS
Genset noise is periodic: firing fundamental f0 + harmonics. With a tach we synthesize the exact
reference internally, so:
- the "secondary path estimate" collapses from a full FIR to ONE complex coefficient per order
  (magnitude + phase at h·f0) — trivially identifiable, robust;
- per-order 2-tap (cos/sin) adaptive filter → a handful of MACs/sample, MCU-friendly;
- cancellation at the harmonics is far deeper than broadband FxLMS achieves.

## Plan
- [x] 1. Core `EngineOrderCanceller` (header-only) — phasor-recurrence oscillators (trig-free hot loop),
        per-order cos/sin weights, per-order complex secondary path, leaky normalized FxLMS update.
- [x] 2. Zero-dep test harness `minitest.hpp`.
- [x] 3. Unit tests (5/5 pass): single tone → +30 dB null; multi-harmonic; Ŝ 180° → no false convergence;
        frequency tracking; idle stays bounded.
- [x] 4. Tiny WAV writer + reader (narrow paths only — MinGW wchar fstream is unsafe).
- [x] 5. Sim harness `genset_sim`: synth genset, real secondary-path FIR, derive per-order Ŝ,
        run controller, report per-order + broadband dB, write before/after wav + convergence csv.
- [x] 6. CMake build (static, Release + Debug/sanitizers), `-Wall -Wextra -Wpedantic` clean.
- [x] 7. Run tests + sim — proven: +56..+86 dB per order on synthetic genset with realistic Ŝ.
- [x] 8. `replay_recording` tool + analysis of the user's real recording.
- [ ] 9. git init + push.

## Verify (success criteria) — MET for the controller
- [x] Tests pass; warning-clean build.
- [x] Sim ≥20 dB at each tracked harmonic (got +56..+86 dB) with realistic (non-perfect) Ŝ.
- [x] Hot loop calls no transcendental functions.

## KEY FINDING from the real recording (20260603 153404mix.wav)
- It IS the generator: ~1822 RPM (30.4 Hz fundamental) harmonic ladder, dual-mono field recording,
  steady to ~35 s then load-driven RPM swings (1600–2050 RPM).
- **Engine-order ANC needs a clean PHASE reference (a tach). A single noise recording does not contain one.**
  - Free-running oscillator at an *estimated* f0 decorrelates from the real (jittering) engine phase → no cancellation.
  - A virtual tach (Hilbert phase of the fundamental) is too noisy: ×h amplifies jitter, harmonics decorrelate.
  - The synthetic sim cancels deeply precisely because its reference is phase-coherent by construction.
- => Hardware requirement: tach pickup / crank-or-cam sensor / block accelerometer for engine angle,
  plus a co-located error mic. The recording is still valuable for: order count (H), f0 band, harmonic
  amplitudes, and secondary-path tuning targets.

## Next (to cancel the REAL unit)
- Capture: tach pulse (engine angle) + simultaneous error-mic audio on the genset.
- Then: tune per-order Ŝ to the measured speaker→mic path; run the tach-driven controller live.
- Port: tach-edge ISR advances the phasor (already trig-free); fixed-point for MCU; frequency-table Ŝ for RPM sweeps.
- Optional offline demo: resynthesize this machine's signature (measured f0(t)+amplitudes, coherent phase)
  and show the tach-driven controller cancelling it.
