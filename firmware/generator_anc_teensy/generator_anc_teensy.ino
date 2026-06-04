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
//    Condition the inductive pickup to a clean 0/3.3V pulse. Recommended: opto-isolated
//    (PC817) open-collector pulling TACH_PIN low per spark. Full build: docs/spark-tach.md.
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

// MAX98357A I2S DAC+amp on the OUTPUT (you have this board). Clean I2S audio out ->
// MAX98357A -> a small 4-8 ohm speaker (3W, near-field ANC / bench test). Mic still ADC on A2.
// Set 0 to fall back to MQS output (pins 10/12 -> RC -> Alpine -> 12in sub) for big SPL.
// Wiring: BCLK->Teensy 21, LRC->20, DIN->7, Vin->5V, GND->GND, SD+GAIN floating (default).
#define USE_MAX98357 1

// ----------------------- config -----------------------
static constexpr int   TACH_PIN    = 2;        // conditioned spark pulse (0/3.3V), once per revolution
static constexpr int   NUM_ORDERS  = 6;        // single-cylinder: orders 1..6 carry the tonal energy
static constexpr double MU          = 0.05;    // normalized step (stable range ~0.01..0.06)
static constexpr double LEAK        = 1e-6;    // weight leakage
// RPM gate for the tach debounce (reject ignition ringing / dropouts):
static constexpr double RPM_MIN     = 1200.0;  // -> 20 Hz rev
static constexpr double RPM_MAX     = 4200.0;  // -> 70 Hz rev
// Engine-off S_hat calibration RPM. Default = this genset's MEASURED idle (~1830 RPM,
// f0=30.5 Hz, from the field capture) so S_hat is probed at the REAL order frequencies
// (30.5..183 Hz), not a nominal 50 Hz. Override live with "SET cal_rpm <v>".
static constexpr double NOMINAL_CAL_RPM = 1830.0;
static double g_calRpm = NOMINAL_CAL_RPM;
// ESP-12E telemetry link. CONFIRMED Serial1 (Rev2 back silkscreen: "ESP8266: RX1/TX1",
// jumpers SJ2 ESP-TX->RX1, SJ3 ESP-RX->TX1). Pins 0/1 are reserved for the ESP -- do not reuse.
#define TELEM Serial1
static constexpr long TELEM_BAUD = 115200;

// ----------------------- audio graph -----------------------
#if USE_AUDIO_SHIELD
AudioInputI2S        i2sIn;
AudioOutputI2S       i2sOut;
AudioControlSGTL5000 codec;
#elif USE_MAX98357
AudioInputAnalog     adcIn(A2);   // error mic -> preamp -> bias ~1.65V -> pin A2 (pin 16)
AudioOutputI2S       i2sOut;       // -> MAX98357A: BCLK->21, LRC->20, DIN->7 -> small 4-8ohm speaker
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
        calMaxMag = 0.0;
        calFailedFlag_ = false;
        Serial.print("calibrating S_hat at nominal f0=");
        Serial.print(f0nom, 1);
        Serial.println(" Hz -- engine OFF, speaker+mic in final position");
        startOrder();
    }

    bool calibrating() const { return calMode; }
    bool calFailed() const { return calFailedFlag_; }   // true if the last cal never heard the probe

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
    static constexpr double S_FLOOR   = 0.01;    // floor stored |S| so the FxLMS norm stays sane
    static constexpr double S_HEARD   = 0.02;    // min peak |S| that means the probe was actually heard

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
        // |S| = (mic amplitude)/(probe amplitude). The lock-in gives mic amplitude
        // 2/N*sqrt(I^2+Q^2); divide by the probe amplitude CAL_AMP to get the true ratio.
        const double mag = 2.0 / ((double)CAL_WIN * CAL_AMP) * sqrt(calI * calI + calQ * calQ);
        const double ph  = atan2(-calQ, calI);          // mic = mag*cos(theta + ph)
        if (mag > calMaxMag) calMaxMag = mag;
        const double sStore = mag < S_FLOOR ? S_FLOOR : mag;   // keep the FxLMS norm finite on weak orders
        eoc.setSecondaryPath(calOrder, sStore, ph);
        Serial.print("  S_hat order "); Serial.print(calOrder);
        Serial.print(" @ "); Serial.print(calOrder * calF0, 1); Serial.print(" Hz");
        Serial.print(": |S|="); Serial.print(mag, 4);
        Serial.print("  phase="); Serial.print(ph, 3); Serial.println(" rad");
        if (++calOrder > NUM_ORDERS) {
            calMode = false;
            eoc.reset();                                 // clear weights before live run
            if (calMaxMag < S_HEARD) {                   // never heard the probe -> bad cal
                calFailedFlag_ = true;
                eoc.setOutputGain(0.0);                  // stay muted until a good cal
                Serial.println("** S_hat FAIL: probe not heard. Check amp gain, speaker, and error-mic wiring, then re-run 'c'. **");
            } else {
                Serial.println("calibration done -> RUN");
            }
        } else {
            startOrder();
        }
    }

    volatile bool calMode = false;
    bool calFailedFlag_ = false;
    int calOrder = 1;
    double calF0 = 50.0, calOmega = 0.0, calPhase = 0.0, calI = 0.0, calQ = 0.0;
    double calMaxMag = 0.0;
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
#elif USE_MAX98357
AudioConnection patchIn(adcIn, 0, eocNode, 0);
AudioConnection patchOutL(eocNode, 0, i2sOut, 0); // -> MAX98357A
AudioConnection patchOutR(eocNode, 0, i2sOut, 1);
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

    // Conditioner is opto-isolated open-collector (PC817): idle HIGH via the internal
    // pull-up, the phototransistor pulls the pin LOW for each spark -> timestamp the
    // FALLING edge (= the spark onset; avoids pulse-width jitter at the tail).
    // If you instead build a push-pull comparator conditioner, use INPUT + RISING.
    pinMode(TACH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, FALLING);

    TELEM.begin(TELEM_BAUD);  // ESP-12E telemetry link
    Serial.println("generator-anc ready. Send 'c' (engine OFF) to calibrate S_hat, then run.");
}

// ---- command handling (from USB Serial OR the phone cockpit via the ESP/TELEM) ----
// Accepts single chars  c r s ?  and  "SET <mu|orders|gain> <value>"  lines.
static void applyCommand(const char* s) {
    if (strncmp(s, "SET ", 4) == 0) {
        const char* p = s + 4;
        char name[12] = {0};
        int i = 0; while (p[i] && p[i] != ' ' && i < 11) { name[i] = p[i]; ++i; }
        const char* sp = strchr(p, ' ');
        const double val = sp ? atof(sp + 1) : 0.0;
        if      (strcmp(name, "mu")      == 0) eocNode.eoc.setMu(val);
        else if (strcmp(name, "orders")  == 0) eocNode.eoc.setActiveOrders((int)val);
        else if (strcmp(name, "gain")    == 0) eocNode.eoc.setOutputGain(val);
        else if (strcmp(name, "cal_rpm") == 0) g_calRpm = val;
    } else if (s[0] == 'c') {
        eocNode.startCalibration(g_calRpm / 60.0);  // engine OFF, probe at the real orders
    } else if (s[0] == 'r') {
        eocNode.eoc.setOutputGain(1.0);                    // run: unmute anti-noise
    } else if (s[0] == 's') {
        eocNode.eoc.setOutputGain(0.0);                    // stop: mute output, keep weights
    }
}

// Line-buffered reader shared by the USB port and the ESP link.
static void readPort(Stream& port, char* buf, int& len, int cap) {
    while (port.available()) {
        const char c = (char)port.read();
        if (c == '\n' || len >= cap - 1) { buf[len] = 0; if (len) applyCommand(buf); len = 0; }
        else if (c != '\r') buf[len++] = c;
    }
}

// mode for telemetry: 0 idle(no tach) · 1 calibrating · 2 stopped/ready · 3 running · 4 S_hat fail
static int modeCode() {
    if (eocNode.calibrating())           return 1;
    if (eocNode.calFailed())             return 4;   // probe not heard -> recalibrate
    if (!AudioEOC::tachValid)            return 0;
    if (eocNode.eoc.outputGain() <= 0.0) return 2;
    return 3;
}

void loop() {
    static char usbBuf[40]; static int usbLen = 0;
    static char espBuf[40]; static int espLen = 0;
    readPort(Serial, usbBuf, usbLen, (int)sizeof(usbBuf));
    readPort(TELEM,  espBuf, espLen, (int)sizeof(espBuf));

    static uint32_t t = 0;
    if (millis() - t < 500) return;
    t = millis();

    if (!eocNode.calibrating()) {   // human-readable line on USB (calibration prints its own)
        Serial.print("f0="); Serial.print(eocNode.eoc.frequency(), 1);
        Serial.print(" Hz  RPM="); Serial.print(eocNode.eoc.frequency() * 60.0, 0);
        Serial.print("  cpu="); Serial.print(AudioProcessorUsage(), 1);
        Serial.print("%  tach="); Serial.println(AudioEOC::tachValid ? "lock" : "--");
    }

    // CSV to the ESP-12E:  ANC,f0,rpm,o1..oN,cpu,lock,mu,orders,gain,mode
    TELEM.print("ANC,");
    TELEM.print(eocNode.eoc.frequency(), 1);
    TELEM.print(','); TELEM.print(eocNode.eoc.frequency() * 60.0, 0);
    for (int h = 1; h <= NUM_ORDERS; ++h) { TELEM.print(','); TELEM.print(eocNode.eoc.orderAmplitude(h), 3); }
    TELEM.print(','); TELEM.print(AudioProcessorUsage(), 1);
    TELEM.print(','); TELEM.print(AudioEOC::tachValid ? 1 : 0);
    TELEM.print(','); TELEM.print(eocNode.eoc.mu(), 4);
    TELEM.print(','); TELEM.print(eocNode.eoc.activeOrders());
    TELEM.print(','); TELEM.print(eocNode.eoc.outputGain(), 2);
    TELEM.print(','); TELEM.println(modeCode());
}
