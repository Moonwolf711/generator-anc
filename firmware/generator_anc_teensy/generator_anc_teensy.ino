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

// Audio front-end:
//   0 = NO SHIELD: Teensy MQS output (pins 10 & 12 -> RC low-pass -> amp) + analog ADC mic in (A2).
//       For sub-bass anti-noise (30-360 Hz) MQS is plenty; its noise is ultrasonic (amp/sub filter it).
//   1 = PJRC Audio Adaptor Rev D (SGTL5000): clean line-in mic + line-out.
#define USE_AUDIO_SHIELD 0

// ----------------------- config -----------------------
static constexpr int   TACH_PIN    = 2;        // conditioned spark pulse (0/3.3V), once per revolution
static constexpr int   NUM_ORDERS  = 6;        // single-cylinder: orders 1..6 carry the tonal energy
static constexpr double MU          = 0.05;    // normalized step (stable range ~0.01..0.06)
static constexpr double LEAK        = 1e-6;    // weight leakage
// RPM gate for the tach debounce (reject ignition ringing / dropouts):
static constexpr double RPM_MIN     = 1200.0;  // -> 20 Hz rev
static constexpr double RPM_MAX     = 4200.0;  // -> 70 Hz rev
static constexpr double NOMINAL_CAL_RPM = 3000.0;  // engine-off S_hat cal point (mid-range)
// ESP-12E telemetry link. CONFIRMED Serial1 (Rev2 back silkscreen: "ESP8266: RX1/TX1",
// jumpers SJ2 ESP-TX->RX1, SJ3 ESP-RX->TX1). Pins 0/1 are reserved for the ESP -- do not reuse.
#define TELEM Serial1
static constexpr long TELEM_BAUD = 115200;

// ----------------------- audio graph -----------------------
#if USE_AUDIO_SHIELD
AudioInputI2S        i2sIn;
AudioOutputI2S       i2sOut;
AudioControlSGTL5000 codec;
#else
AudioInputAnalog     adcIn(A2);   // error mic -> preamp -> bias ~1.65V -> pin A2 (pin 16)
AudioOutputMQS       mqsOut;       // anti-noise on pins 10 & 12 -> RC low-pass -> amp RCA
#endif

// custom EOC node: in0 = error mic, out0 = anti-noise
class AudioEOC : public AudioStream {
public:
    AudioEOC() : AudioStream(1, inputQueueArray),
                 eoc(AUDIO_SAMPLE_RATE_EXACT, NUM_ORDERS, MU, LEAK) {}

    eoc::EngineOrderCanceller eoc;

    // Start engine-off secondary-path calibration at nominal fundamental f0nom (Hz).
    // Emits a probe tone per order from the anti-noise speaker and correlates the mic
    // (quadrature) to recover |S| and angle S -> setSecondaryPath. Verified by the
    // desktop unit test `secondary_path_calibration_recovers_response`.
    void startCalibration(double f0nom) {
        calF0 = f0nom;
        calOrder = 1;
        calMode = true;
        Serial.print("calibrating S_hat at nominal f0=");
        Serial.print(f0nom, 1);
        Serial.println(" Hz -- engine OFF, speaker+mic in final position");
        startOrder();
    }

    bool calibrating() const { return calMode; }

    void update(void) override {
        audio_block_t* in = receiveReadOnly(0);
        if (!in) return;
        audio_block_t* out = allocate();
        if (!out) { release(in); return; }
        if (calMode) calProcess(in, out);
        else         runProcess(in, out);
        transmit(out);
        release(out);
        release(in);
    }

    // tach state (written by ISR)
    static volatile uint32_t lastSparkCyc;
    static volatile uint32_t revPeriodCyc;
    static volatile bool     tachValid;

private:
    static constexpr double kTwoPi    = 6.283185307179586;
    static constexpr double CAL_AMP   = 0.30;    // probe amplitude
    static constexpr uint32_t CAL_SETTLE = 2048; // samples to ignore (path warm-up)
    static constexpr uint32_t CAL_WIN    = 8192; // correlation window

    void runProcess(audio_block_t* in, audio_block_t* out) {
        const uint32_t now = ARM_DWT_CYCCNT;
        noInterrupts();
        const uint32_t ls = lastSparkCyc;
        const uint32_t rp = revPeriodCyc;
        const bool valid  = tachValid;
        interrupts();

        // No engine sync (no tach, or stale = engine stopped): emit silence, do NOT adapt.
        // Without a phase reference there is nothing to cancel; adapting would wind up on noise.
        const uint32_t staleCyc = (uint32_t)(2.0 * (double)F_CPU_ACTUAL / (RPM_MIN / 60.0));
        if (!valid || rp == 0 || (uint32_t)(now - ls) > staleCyc) {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) out->data[i] = 0;
            return;
        }

        const double theta0 = kTwoPi * (double)(uint32_t)(now - ls) / (double)rp;
        const double f0     = (double)F_CPU_ACTUAL / (double)rp;
        eoc.setFrequency(f0);
        eoc.syncPhase(theta0);
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            const float e = (float)in->data[i] * (1.0f / 32768.0f);
            float y = eoc.process(e);
            if (y >  0.98f) y =  0.98f;
            if (y < -0.98f) y = -0.98f;
            out->data[i] = (int16_t)(y * 32767.0f);
        }
    }

    void calProcess(audio_block_t* in, audio_block_t* out) {
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            const double c = cos(calPhase), sn = sin(calPhase);
            out->data[i] = (int16_t)(CAL_AMP * c * 32767.0);   // emit probe
            if (calCount >= CAL_SETTLE) {
                const double mic = (double)in->data[i] * (1.0 / 32768.0);
                calI += mic * c;
                calQ += mic * sn;
            }
            calPhase += calOmega;
            if (calPhase > kTwoPi) calPhase -= kTwoPi;
            if (++calCount >= CAL_SETTLE + CAL_WIN) { finishOrder(); break; }
        }
    }

    void startOrder() {
        calI = calQ = 0.0;
        calCount = 0;
        calPhase = 0.0;
        calOmega = kTwoPi * (double)calOrder * calF0 / AUDIO_SAMPLE_RATE_EXACT;
    }

    void finishOrder() {
        const double mag = 2.0 / (double)CAL_WIN * sqrt(calI * calI + calQ * calQ);
        const double ph  = atan2(-calQ, calI);          // mic = mag*cos(theta + ph)
        eoc.setSecondaryPath(calOrder, mag, ph);
        Serial.print("  S_hat order "); Serial.print(calOrder);
        Serial.print(": mag="); Serial.print(mag, 4);
        Serial.print("  phase="); Serial.print(ph, 3); Serial.println(" rad");
        if (++calOrder > NUM_ORDERS) {
            calMode = false;
            eoc.reset();                                 // clear weights before live run
            Serial.println("calibration done -> RUN");
        } else {
            startOrder();
        }
    }

    volatile bool calMode = false;
    int calOrder = 1;
    double calF0 = 50.0, calOmega = 0.0, calPhase = 0.0, calI = 0.0, calQ = 0.0;
    uint32_t calCount = 0;

    audio_block_t* inputQueueArray[1];
};
volatile uint32_t AudioEOC::lastSparkCyc = 0;
volatile uint32_t AudioEOC::revPeriodCyc = 0;
volatile bool     AudioEOC::tachValid    = false;

AudioEOC eocNode;
#if USE_AUDIO_SHIELD
AudioConnection patchIn(i2sIn, 0, eocNode, 0);
AudioConnection patchOut(eocNode, 0, i2sOut, 0);
AudioConnection patchMon(eocNode, 0, i2sOut, 1);  // same anti-noise to both outputs
#else
AudioConnection patchIn(adcIn, 0, eocNode, 0);
AudioConnection patchOutL(eocNode, 0, mqsOut, 0); // pin 12
AudioConnection patchOutR(eocNode, 0, mqsOut, 1); // pin 10
#endif

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
#if USE_AUDIO_SHIELD
    codec.enable();
    codec.inputSelect(AUDIO_INPUT_LINEIN);   // error mic on LINEIN (use AUDIO_INPUT_MIC for a bare mic)
    codec.volume(0.6);
    codec.lineInLevel(5);
#endif

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

    TELEM.begin(TELEM_BAUD);  // ESP-12E telemetry link
    Serial.println("generator-anc ready. Send 'c' (engine OFF) to calibrate S_hat, then run.");
}

void loop() {
    if (Serial.available()) {
        const char ch = Serial.read();
        if (ch == 'c') eocNode.startCalibration(NOMINAL_CAL_RPM / 60.0);
    }
    if (eocNode.calibrating()) return;   // calibration prints its own progress

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

        // compact CSV to the ESP-12E:  ANC,f0,rpm,o1..oN,cpu,lock
        TELEM.print("ANC,");
        TELEM.print(eocNode.eoc.frequency(), 1);
        TELEM.print(',');
        TELEM.print(eocNode.eoc.frequency() * 60.0, 0);
        for (int h = 1; h <= NUM_ORDERS; ++h) { TELEM.print(','); TELEM.print(eocNode.eoc.orderAmplitude(h), 3); }
        TELEM.print(',');
        TELEM.print(AudioProcessorUsage(), 1);
        TELEM.print(',');
        TELEM.println(AudioEOC::tachValid ? 1 : 0);
    }
}
