// replay_recording.cpp -- run the EngineOrderCanceller against a REAL recording.
//
// Offline replay: reads a WAV of generator noise, tracks the firing fundamental
// per block (a stand-in for a hardware tach), recomputes the per-order secondary
// path at the current frequency, runs the controller, and writes before/after WAV.
//
// On real hardware the fundamental comes from a tach pulse, not from the audio;
// estimating it here keeps the offline tool to a single self-contained binary.
//
// Usage: replay_recording <path.wav> [numOrders] [f0lo] [f0hi]
//   numOrders  harmonics to cancel (default 10)
//   f0lo,f0hi  fundamental search band in Hz (default 26 40)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "eoc/engine_order_canceller.hpp"
#include "wav.hpp"

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

double goertzel(const std::vector<double>& x, std::size_t a, std::size_t b, double f, double fs) {
    const double w = kTwoPi * f / fs;
    const double cw = std::cos(w), sw = std::sin(w), coeff = 2.0 * cw;
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t n = a; n < b; ++n) {
        const double s0 = x[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * cw, im = s2 * sw;
    return re * re + im * im;
}

// Estimate the firing fundamental over x[a..b) by maximizing summed harmonic energy.
double estimate_f0(const std::vector<double>& x, std::size_t a, std::size_t b,
                   double fs, double lo, double hi) {
    double best = -1.0, bestF = lo;
    for (double c = lo; c <= hi; c += 0.1) {
        double s = 0.0;
        for (int h = 1; h <= 5; ++h) s += goertzel(x, a, b, h * c, fs);
        if (s > best) { best = s; bestF = c; }
    }
    return bestF;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: replay_recording <path.wav> [orders] [f0lo] [f0hi]\n"); return 2; }
    const std::string path = argv[1];
    const int H = (argc > 2) ? std::atoi(argv[2]) : 10;
    const double f0lo = (argc > 3) ? std::atof(argv[3]) : 26.0;
    const double f0hi = (argc > 4) ? std::atof(argv[4]) : 40.0;
    const double mu = (argc > 5) ? std::atof(argv[5]) : 0.02;

    int fsI = 0;
    const std::vector<float> wavf = wav::read_mono16(path, fsI);
    if (wavf.empty()) { std::printf("failed to read %s\n", path.c_str()); return 1; }
    const double fs = static_cast<double>(fsI);
    const std::size_t N = wavf.size();
    std::vector<double> d(N);
    double mx = 1e-12;
    for (std::size_t n = 0; n < N; ++n) { d[n] = wavf[n]; mx = std::fmax(mx, std::fabs(d[n])); }
    for (auto& v : d) v /= mx;  // normalize

    std::printf("recording: %s  %.0f Hz  %.1f s  orders=%d  f0 band [%.0f,%.0f] Hz\n",
                path.c_str(), fs, N / fs, H, f0lo, f0hi);

    // modeled secondary path (speaker -> error mic): short delay + decay FIR
    const std::size_t L = static_cast<std::size_t>(fs * 0.004);  // ~4 ms
    std::vector<double> Sfir(L, 0.0);
    {
        const int delay = static_cast<int>(fs * 0.0007);
        const double tau = 0.0015;
        double s = 0.0;
        for (std::size_t k = 0; k < L; ++k) {
            const double t = static_cast<double>(static_cast<int>(k) - delay) / fs;
            Sfir[k] = (static_cast<int>(k) >= delay) ? std::exp(-t / tau) : 0.0;
            s += std::fabs(Sfir[k]);
        }
        for (auto& v : Sfir) v /= (s + 1e-12);
    }
    auto setSecondaryForF0 = [&](eoc::EngineOrderCanceller& c, double f0) {
        for (int h = 1; h <= H; ++h) {
            const double w = kTwoPi * h * f0 / fs;
            double re = 0.0, im = 0.0;
            for (std::size_t k = 0; k < L; ++k) {
                re += Sfir[k] * std::cos(w * static_cast<double>(k));
                im -= Sfir[k] * std::sin(w * static_cast<double>(k));
            }
            c.setSecondaryPath(h, std::sqrt(re * re + im * im) * 1.10, std::atan2(im, re) + 0.08);
        }
    };

    eoc::EngineOrderCanceller ctrl(fs, H, mu, 1e-6);
    const std::size_t BLK = static_cast<std::size_t>(fs * 0.043);   // ~43 ms blocks
    const std::size_t WIN = static_cast<std::size_t>(fs * 0.17);    // f0 window ~170 ms

    std::vector<double> e(N, 0.0);
    std::vector<double> yhist(L, 0.0);
    std::size_t head = 0;
    double yPrev = 0.0;
    double f0 = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        if (n % BLK == 0) {                                  // update "tach" estimate
            const std::size_t a = (n >= WIN) ? n - WIN : 0;
            f0 = estimate_f0(d, a, n + 1 < N ? n + 1 : N, fs, f0lo, f0hi);
            ctrl.setFrequency(f0);
            setSecondaryForF0(ctrl, f0);
        }
        yhist[head] = yPrev;
        double sy = 0.0;
        for (std::size_t k = 0; k < L; ++k) sy += Sfir[k] * yhist[(head + L - k) % L];
        const double mic = d[n] + sy;
        e[n] = mic;
        yPrev = static_cast<double>(ctrl.process(static_cast<float>(mic)));
        head = (head + 1) % L;
    }

    // broadband reduction: steady window (5..min(30,end)) and whole file
    auto band_red = [&](double t0, double t1) {
        const std::size_t a = static_cast<std::size_t>(t0 * fs);
        const std::size_t b = std::min(N, static_cast<std::size_t>(t1 * fs));
        double pd = 0.0, pe = 0.0;
        for (std::size_t n = a; n < b; ++n) { pd += d[n] * d[n]; pe += e[n] * e[n]; }
        return 10.0 * std::log10(pd / (pe + 1e-300));
    };
    const double steadyEnd = std::fmin(30.0, N / fs);
    std::printf("broadband reduction  steady 5-%.0fs: %+.1f dB | whole file: %+.1f dB\n",
                steadyEnd, band_red(5.0, steadyEnd), band_red(0.0, N / fs));

    std::filesystem::create_directories("outputs");
    std::vector<float> bf(N), af(N);
    const double g = 0.9;
    for (std::size_t n = 0; n < N; ++n) { bf[n] = static_cast<float>(d[n] * g); af[n] = static_cast<float>(e[n] * g); }
    wav::write_mono16("outputs/rec_before.wav", bf, fsI);
    wav::write_mono16("outputs/rec_after.wav", af, fsI);
    std::printf("wrote outputs/rec_before.wav, rec_after.wav\n");
    return 0;
}
