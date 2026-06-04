// test_eoc.cpp -- unit tests for EngineOrderCanceller.
#include <cmath>
#include <vector>

#include "eoc/engine_order_canceller.hpp"
#include "minitest.hpp"

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

// Simulate the controller against an IDENTITY secondary path: the error mic reads
// e[n] = d[n] + y[n-1]. The controller's default secondary path is unit gain / zero
// phase, so Ŝ matches the true path here. Returns broadband reduction (dB) over the
// final 1/`frac` of the run.
double run_identity(eoc::EngineOrderCanceller& c, const std::vector<double>& d, int frac = 4) {
    const std::size_t N = d.size();
    std::vector<double> e(N, 0.0);
    double yPrev = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        const double cur = d[n] + yPrev;            // mic = disturbance + anti-noise (identity S)
        e[n] = cur;
        yPrev = static_cast<double>(c.process(static_cast<float>(cur)));
    }
    const std::size_t h = N * static_cast<std::size_t>(frac - 1) / static_cast<std::size_t>(frac);
    double pd = 0.0, pe = 0.0;
    std::size_t cnt = 0;
    for (std::size_t n = h; n < N; ++n) {
        pd += d[n] * d[n];
        pe += e[n] * e[n];
        ++cnt;
    }
    pd /= static_cast<double>(cnt);
    pe /= static_cast<double>(cnt);
    if (!std::isfinite(pe)) return -300.0;  // diverged -> treat as "no cancellation"
    if (pe <= 0.0) return 300.0;            // exact null
    return 10.0 * std::log10(pd / pe);
}

std::vector<double> tone(double amp, int order, double f0, double fs, std::size_t N, double phase = 0.0) {
    std::vector<double> d(N);
    for (std::size_t n = 0; n < N; ++n) {
        d[n] = amp * std::cos(kTwoPi * order * f0 * static_cast<double>(n) / fs + phase);
    }
    return d;
}

}  // namespace

TEST(single_tone_deep_null) {
    const double fs = 8000.0, f0 = 120.0;
    const std::size_t N = 16000;  // 2 s
    eoc::EngineOrderCanceller c(fs, 1, 0.05);
    c.setFrequency(f0);
    const auto d = tone(1.0, 1, f0, fs, N);
    const double red = run_identity(c, d);
    CHECK_GE(red, 30.0);  // single tone, perfect Ŝ -> very deep null
}

TEST(multi_harmonic_cancellation) {
    const double fs = 8000.0, f0 = 100.0;
    const std::size_t N = 32000;  // 4 s
    eoc::EngineOrderCanceller c(fs, 4, 0.05);
    c.setFrequency(f0);
    std::vector<double> d(N, 0.0);
    for (int h = 1; h <= 4; ++h) {
        const auto t = tone(1.0 / h, h, f0, fs, N, 0.3 * h);
        for (std::size_t n = 0; n < N; ++n) d[n] += t[n];
    }
    const double red = run_identity(c, d);
    CHECK_GE(red, 25.0);
    // each order's adapted amplitude should be non-trivial (it found all four)
    for (int h = 1; h <= 4; ++h) CHECK(c.orderAmplitude(h) > 1e-3);
}

TEST(wrong_secondary_phase_does_not_cancel) {
    // True path is identity, but we LIE to the controller: Ŝ phase = 180 deg.
    // The filtered reference is negated -> the update ascends -> no cancellation.
    const double fs = 8000.0, f0 = 120.0;
    const std::size_t N = 16000;
    eoc::EngineOrderCanceller c(fs, 1, 0.02);
    c.setFrequency(f0);
    c.setSecondaryPath(1, 1.0, 3.14159265358979323846);  // 180 deg error
    const auto d = tone(1.0, 1, f0, fs, N);
    const double red = run_identity(c, d);
    CHECK_LT(red, 3.0);  // must NOT achieve meaningful cancellation
}

TEST(frequency_change_is_tracked) {
    const double fs = 8000.0;
    const std::size_t seg = 16000;
    eoc::EngineOrderCanceller c(fs, 1, 0.05);

    // first frequency
    c.setFrequency(100.0);
    auto d1 = tone(1.0, 1, 100.0, fs, seg);
    const double r1 = run_identity(c, d1);
    CHECK_GE(r1, 25.0);

    // RPM changes: new firing frequency. Controller is told the new f0; it must re-null.
    c.setFrequency(160.0);
    auto d2 = tone(1.0, 1, 160.0, fs, seg);
    const double r2 = run_identity(c, d2);
    CHECK_GE(r2, 25.0);
}

TEST(silent_input_stays_bounded) {
    const double fs = 8000.0;
    const std::size_t N = 8000;
    eoc::EngineOrderCanceller c(fs, 6, 0.05, 1e-4);
    c.setFrequency(120.0);
    double yPrev = 0.0;
    bool finite = true;
    for (std::size_t n = 0; n < N; ++n) {
        const double e = 0.0 + yPrev;  // no disturbance
        yPrev = static_cast<double>(c.process(static_cast<float>(e)));
        if (!std::isfinite(yPrev)) finite = false;
    }
    CHECK(finite);
    CHECK_LT(std::fabs(yPrev), 1e-6);  // output stays at zero with no disturbance
}

TEST(sync_phase_reseats_oscillator) {
    eoc::EngineOrderCanceller c(8000.0, 3, 0.05);
    c.setFrequency(120.0);
    c.syncPhase(0.0);                       // spark at top-dead-center reference
    CHECK_NEAR(c.referenceCos(1), 1.0, 1e-9);
    CHECK_NEAR(c.referenceSin(1), 0.0, 1e-9);
    const double pi = 3.14159265358979323846;
    c.syncPhase(pi);                        // half a revolution later
    CHECK_NEAR(c.referenceCos(1), -1.0, 1e-9);
    CHECK_NEAR(c.referenceCos(2), 1.0, 1e-9);  // order 2 advances 2*pi -> back to 1
}

TEST(secondary_path_calibration_recovers_response) {
    // The firmware's engine-off calibration in miniature: emit a unit cosine through a
    // KNOWN secondary path (FIR), correlate the "mic" with quadrature references, and
    // recover the path's magnitude + phase at that frequency. Must match the FIR's true
    // response -- this is the exact math the Teensy CAL mode uses to fill setSecondaryPath().
    const double fs = 8000.0, f = 180.0;
    const double pi = 3.14159265358979323846;
    const int M = 48;
    std::vector<double> S(static_cast<std::size_t>(M), 0.0);
    double s = 0.0;
    for (int k = 0; k < M; ++k) {
        S[static_cast<std::size_t>(k)] = (k >= 3) ? std::exp(-(k - 3) / (0.002 * fs)) : 0.0;
        s += std::fabs(S[static_cast<std::size_t>(k)]);
    }
    for (auto& v : S) v /= s;

    const double w = 2.0 * pi * f / fs;
    double re = 0.0, im = 0.0;
    for (int k = 0; k < M; ++k) { re += S[(std::size_t)k] * std::cos(w * k); im -= S[(std::size_t)k] * std::sin(w * k); }
    const double trueMag = std::hypot(re, im), truePh = std::atan2(im, re);

    const int N = 4000;
    std::vector<double> hist(static_cast<std::size_t>(M), 0.0);
    double I = 0.0, Q = 0.0;
    for (int n = 0; n < N; ++n) {
        const double drive = std::cos(w * n);
        for (int k = M - 1; k > 0; --k) hist[(std::size_t)k] = hist[(std::size_t)(k - 1)];
        hist[0] = drive;
        double mic = 0.0;
        for (int k = 0; k < M; ++k) mic += S[(std::size_t)k] * hist[(std::size_t)k];
        if (n >= M) { I += mic * std::cos(w * n); Q += mic * std::sin(w * n); }  // skip startup
    }
    const double Nc = static_cast<double>(N - M);
    const double mag = 2.0 / Nc * std::hypot(I, Q);
    double ph = std::atan2(-Q, I);
    double dph = ph - truePh;
    while (dph > pi) dph -= 2.0 * pi;
    while (dph < -pi) dph += 2.0 * pi;
    CHECK_NEAR(mag, trueMag, 0.02 * trueMag + 1e-3);
    CHECK_NEAR(dph, 0.0, 0.05);
}

int main() { return minitest::run(); }
