// genset_sim.cpp -- offline validation of EngineOrderCanceller on synthetic
// generator noise, until a real recording is supplied.
//
// Builds a periodic genset disturbance (firing fundamental + harmonics + broadband
// floor), a realistic secondary-path FIR, derives the per-order complex secondary
// response the controller needs, runs the controller, and reports per-order +
// broadband cancellation. Writes before/after WAV and a convergence CSV.
//
// Usage: genset_sim [f0Hz] [numOrders] [seconds] [ideal]
//   f0Hz       firing fundamental (default 120)
//   numOrders  harmonics to cancel (default 8)
//   seconds    duration (default 8)
//   ideal      if present, controller gets the EXACT secondary path (no ID error)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "eoc/engine_order_canceller.hpp"
#include "wav.hpp"

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

// Narrowband power at frequency f over x[a..b) via Goertzel. Returns |X(f)|^2.
double goertzel_power(const std::vector<double>& x, std::size_t a, std::size_t b, double f, double fs) {
    const double w = kTwoPi * f / fs;
    const double cw = std::cos(w), sw = std::sin(w), coeff = 2.0 * cw;
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t n = a; n < b; ++n) {
        const double s0 = x[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * cw;
    const double im = s2 * sw;
    return re * re + im * im;
}

}  // namespace

int main(int argc, char** argv) {
    const double fs = 8000.0;
    const double f0 = (argc > 1) ? std::atof(argv[1]) : 120.0;     // firing fundamental
    const int H = (argc > 2) ? std::atoi(argv[2]) : 8;             // harmonics
    const double seconds = (argc > 3) ? std::atof(argv[3]) : 8.0;
    const bool ideal = (argc > 4) && std::string(argv[4]) == "ideal";

    const std::size_t N = static_cast<std::size_t>(seconds * fs);

    // ---- disturbance: firing harmonics (1/h amplitude) + broadband floor ----
    std::mt19937 rng(12345);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::vector<double> d(N, 0.0);
    std::vector<double> amp(static_cast<std::size_t>(H + 1), 0.0);
    std::vector<double> phs(static_cast<std::size_t>(H + 1), 0.0);
    for (int h = 1; h <= H; ++h) {
        amp[static_cast<std::size_t>(h)] = 1.0 / static_cast<double>(h);
        phs[static_cast<std::size_t>(h)] = 0.37 * h;  // arbitrary fixed phases
    }
    for (std::size_t n = 0; n < N; ++n) {
        double v = 0.0;
        for (int h = 1; h <= H; ++h) {
            v += amp[static_cast<std::size_t>(h)] *
                 std::cos(kTwoPi * h * f0 * static_cast<double>(n) / fs + phs[static_cast<std::size_t>(h)]);
        }
        v += 0.05 * gauss(rng);  // broadband floor (engine-order ANC won't touch this)
        d[n] = v;
    }

    // ---- true secondary path FIR (speaker -> error mic): delay + decay ----
    const std::size_t L = 64;
    std::vector<double> Sfir(L, 0.0);
    {
        const int delay = 6;
        const double tau = 0.0025;  // s
        double sum = 0.0;
        for (std::size_t k = 0; k < L; ++k) {
            const double t = static_cast<double>(static_cast<int>(k) - delay) / fs;
            Sfir[k] = (static_cast<int>(k) >= delay) ? std::exp(-t / tau) : 0.0;
            sum += std::fabs(Sfir[k]);
        }
        for (auto& v : Sfir) v /= (sum + 1e-12);  // unit DC gain
    }

    // ---- controller; per-order Ŝ derived from the FIR's frequency response ----
    eoc::EngineOrderCanceller ctrl(fs, H, 0.05, 1e-6);
    ctrl.setFrequency(f0);
    for (int h = 1; h <= H; ++h) {
        const double w = kTwoPi * h * f0 / fs;
        double re = 0.0, im = 0.0;
        for (std::size_t k = 0; k < L; ++k) {
            re += Sfir[k] * std::cos(w * static_cast<double>(k));
            im -= Sfir[k] * std::sin(w * static_cast<double>(k));  // H(e^{jw}) = sum S[k] e^{-jwk}
        }
        double mag = std::sqrt(re * re + im * im);
        double ang = std::atan2(im, re);
        if (!ideal) {                  // realistic identification error
            mag *= 1.10;               // 10% magnitude error
            ang += 0.08;               // ~4.6 deg phase error
        }
        ctrl.setSecondaryPath(h, mag, ang);
    }

    // ---- run: e[n] = d[n] + (Sfir * y_history) ----
    std::vector<double> e(N, 0.0);
    std::vector<double> yhist(L, 0.0);
    std::size_t head = 0;
    double yPrev = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        yhist[head] = yPrev;                          // emit previous output
        double sy = 0.0;                              // anti-noise at mic = Sfir * yhist
        for (std::size_t k = 0; k < L; ++k) {
            const std::size_t idx = (head + L - k) % L;
            sy += Sfir[k] * yhist[idx];
        }
        const double mic = d[n] + sy;
        e[n] = mic;
        yPrev = static_cast<double>(ctrl.process(static_cast<float>(mic)));
        head = (head + 1) % L;
    }

    // ---- metrics over converged second half ----
    const std::size_t a = N / 2;
    double pd = 0.0, pe = 0.0;
    for (std::size_t n = a; n < N; ++n) {
        pd += d[n] * d[n];
        pe += e[n] * e[n];
    }
    const double broadband = 10.0 * std::log10(pd / (pe + 1e-300));

    std::printf("Generator ANC sim  fs=%.0f Hz  f0=%.1f Hz  orders=%d  %.0fs  %s\n",
                fs, f0, H, seconds, ideal ? "(ideal S_hat)" : "(realistic S_hat: +10% mag, +0.08 rad)");
    std::printf("%-6s %-10s %-12s\n", "order", "freq(Hz)", "cancellation");
    for (int h = 1; h <= H; ++h) {
        const double f = h * f0;
        const double bd = goertzel_power(d, a, N, f, fs);
        const double be = goertzel_power(e, a, N, f, fs);
        std::printf("%-6d %-10.1f %+8.1f dB\n", h, f, 10.0 * std::log10(bd / (be + 1e-300)));
    }
    std::printf("broadband (incl. noise floor): %+.1f dB\n", broadband);

    // ---- artifacts ----
    std::filesystem::create_directories("outputs");
    const double g = [&] {
        double mx = 1e-12;
        for (double v : d) mx = std::fmax(mx, std::fabs(v));
        return 0.9 / mx;
    }();
    std::vector<float> bf(N), af(N);
    for (std::size_t n = 0; n < N; ++n) {
        bf[n] = static_cast<float>(d[n] * g);
        af[n] = static_cast<float>(e[n] * g);
    }
    wav::write_mono16("outputs/genset_before.wav", bf, static_cast<int>(fs));
    wav::write_mono16("outputs/genset_after.wav", af, static_cast<int>(fs));

    if (std::FILE* csv = std::fopen("outputs/convergence.csv", "wb")) {
        std::fprintf(csv, "t,residual_dB\n");
        const std::size_t win = 400;
        double acc = 0.0;
        const double ref = pd / static_cast<double>(N - a);
        for (std::size_t n = 0; n < N; ++n) {
            acc += e[n] * e[n];
            if (n >= win) acc -= e[n - win] * e[n - win];
            if (n % 16 == 0 && n >= win) {
                const double p = acc / static_cast<double>(win);
                std::fprintf(csv, "%.4f,%.2f\n", static_cast<double>(n) / fs,
                             10.0 * std::log10(p / (ref + 1e-300)));
            }
        }
        std::fclose(csv);
    }
    std::printf("wrote outputs/genset_before.wav, genset_after.wav, convergence.csv\n");
    return 0;
}
