// generator_anc_teensy.ino -- engine-order ANC on a Teensy 4.0/4.1 + Audio Shield.
//
// Signal flow:
//   error mic  -> Audio Shield line/mic IN -> [AudioEOC] -> Audio Shield line OUT -> amp -> anti-noise speaker
//   spark wire -> inductive pickup -> conditioning -> TACH_PIN interrupt -> engine phase
//
// The controller's reference is phase-locked to the engine: on every audio block we
// interpolate the crank angle from the most recent spark timestamp and re-seat the
// oscillator (syncPhase). That coherence is what makes real cancellation work -- a
// free-running oscillator drifts out of phase with the (variable-RPM, inverter) engine.
//
// HARDWARE NOTES
//  * Teensy 4.x pins are 3.3V ONLY -- never feed raw spark/ignition voltage to a pin.
//    Condition the inductive pickup to a clean 0/3.3V pulse (clamp + comparator/transistor).
//  * Audio Shield = PJRC rev D (SGTL5000). Error mic on LINEIN (or MIC with bias).
//  * Secondary path S_hat MUST be calibrated for your speaker/mic geometry (see setup()).
//
// Build: Arduino IDE + Teensyduino. Copy engine_order_canceller.hpp into this folder.
// (Not compile-checked in the dev container -- it needs the Teensy core.)

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include "engine_order_canceller.hpp"

// ----------------------- config -----------------------
static constexpr int   TACH_PIN    = 2;        // conditioned spark pulse (0/3.3V), once per revolution
static constexpr int   NUM_ORDERS  = 6;        // single-cylinder: orders 1..6 carry the tonal energy
static constexpr double MU          = 0.05;    // normalized step (stable range ~0.01..0.06)
static constexpr double LEAK        = 1e-6;    // weight leakage
// RPM gate for the tach debounce (reject ignition ringing / dropouts):
static constexpr double RPM_MIN     = 1200.0;  // -> 20 Hz rev
static constexpr double RPM_MAX     = 4200.0;  // -> 70 Hz rev

// ----------------------- audio graph -----------------------
AudioInputI2S        i2sIn;
AudioOutputI2S       i2sOut;
AudioControlSGTL5000 codec;

// custom EOC node: in0 = error mic, out0 = anti-noise
class AudioEOC : public AudioStream {
public:
    AudioEOC() : AudioStream(1, inputQueueArray),
                 eoc(AUDIO_SAMPLE_RATE_EXACT, NUM_ORDERS, MU, LEAK) {}

    eoc::EngineOrderCanceller eoc;

    void update(void) override {
        audio_block_t* in = receiveReadOnly(0);
        if (!in) return;
        audio_block_t* out = allocate();
        if (!out) { release(in); return; }

        // phase-lock to the engine from the latest spark
        const uint32_t now = ARM_DWT_CYCCNT;
        noInterrupts();
        const uint32_t ls = lastSparkCyc;
        const uint32_t rp = revPeriodCyc;
        const bool valid  = tachValid;
        interrupts();
        if (valid && rp > 0) {
            const double theta0 = 6.283185307179586 * (double)(uint32_t)(now - ls) / (double)rp;
            const double f0     = (double)F_CPU_ACTUAL / (double)rp;
            eoc.setFrequency(f0);
            eoc.syncPhase(theta0);
        }

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            const float e = (float)in->data[i] * (1.0f / 32768.0f);   // error mic, int16 -> float
            float y = eoc.process(e);                                  // anti-noise
            if (y >  0.98f) y =  0.98f;
            if (y < -0.98f) y = -0.98f;
            out->data[i] = (int16_t)(y * 32767.0f);
        }
        transmit(out);
        release(out);
        release(in);
    }

    // tach state (written by ISR)
    static volatile uint32_t lastSparkCyc;
    static volatile uint32_t revPeriodCyc;
    static volatile bool     tachValid;

private:
    audio_block_t* inputQueueArray[1];
};
volatile uint32_t AudioEOC::lastSparkCyc = 0;
volatile uint32_t AudioEOC::revPeriodCyc = 0;
volatile bool     AudioEOC::tachValid    = false;

AudioEOC eocNode;
AudioConnection patchIn(i2sIn, 0, eocNode, 0);
AudioConnection patchOut(eocNode, 0, i2sOut, 0);
AudioConnection patchMon(eocNode, 0, i2sOut, 1);  // same anti-noise to both outputs

// ----------------------- tach ISR -----------------------
static uint32_t kMinPeriodCyc;  // RPM_MAX -> shortest period
static uint32_t kMaxPeriodCyc;  // RPM_MIN -> longest period

void tachISR() {
    const uint32_t now = ARM_DWT_CYCCNT;
    const uint32_t dt  = now - AudioEOC::lastSparkCyc;
    if (dt >= kMinPeriodCyc && dt <= kMaxPeriodCyc) {   // debounce / gate
        AudioEOC::revPeriodCyc = dt;
        AudioEOC::tachValid    = true;
    }
    AudioEOC::lastSparkCyc = now;
}

// ----------------------- setup / loop -----------------------
void setup() {
    Serial.begin(115200);

    // cycle counter for sub-microsecond tach timing
    ARM_DEMCR |= ARM_DEMCR_TRCENA;
    ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
    const double revHzMin = RPM_MIN / 60.0, revHzMax = RPM_MAX / 60.0;
    kMaxPeriodCyc = (uint32_t)((double)F_CPU_ACTUAL / revHzMin);
    kMinPeriodCyc = (uint32_t)((double)F_CPU_ACTUAL / revHzMax);

    AudioMemory(24);
    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);   // error mic on LINEIN (use AUDIO_INPUT_MIC for a bare mic)
    codec.volume(0.6);
    codec.lineInLevel(5);

    // Secondary-path estimate S_hat per order: magnitude + phase of speaker->mic at h*f0.
    // PLACEHOLDER -- you MUST calibrate this for your geometry, e.g.:
    //   1) engine off, emit a unit cosine at each order's frequency,
    //   2) correlate the mic signal to get magnitude + phase,
    //   3) eocNode.eoc.setSecondaryPath(h, mag, phase).
    // Until calibrated, unity/zero is only correct if speaker and mic are co-located.
    for (int h = 1; h <= NUM_ORDERS; ++h) {
        eocNode.eoc.setSecondaryPath(h, /*mag*/ 1.0, /*phaseRad*/ 0.0);
    }

    pinMode(TACH_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, RISING);

    Serial.println("generator-anc: engine-order ANC running. Calibrate S_hat for real cancellation.");
}

void loop() {
    static uint32_t t = 0;
    if (millis() - t >= 500) {
        t = millis();
        Serial.print("f0=");
        Serial.print(eocNode.eoc.frequency(), 1);
        Serial.print(" Hz  RPM=");
        Serial.print(eocNode.eoc.frequency() * 60.0, 0);
        Serial.print("  orders[");
        for (int h = 1; h <= NUM_ORDERS; ++h) {
            Serial.print(eocNode.eoc.orderAmplitude(h), 3);
            if (h < NUM_ORDERS) Serial.print(' ');
        }
        Serial.print("]  cpu=");
        Serial.print(AudioProcessorUsage(), 1);
        Serial.print("%  tach=");
        Serial.println(AudioEOC::tachValid ? "lock" : "--");
    }
}
