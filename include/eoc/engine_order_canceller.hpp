// engine_order_canceller.hpp -- real-time engine-order active noise cancellation.
//
// Tach-synced narrowband FxLMS for periodic machinery noise (generators, engines).
// Given the firing fundamental f0 (from a tach/RPM signal) and the error-mic sample,
// it adapts a 2-weight (cosine/sine) oscillator per harmonic to drive an anti-noise
// output that cancels the disturbance at each engine order.
//
// Key idea: because the reference is synthesized internally from the tach, the
// secondary path (anti-noise speaker -> error mic) reduces, at each harmonic, to a
// single complex coefficient -- magnitude |S(h*f0)| and phase angle(S(h*f0)).
// That is the "Filtered-x" rotation applied to the reference before the LMS update.
//
// Design constraints (real-time / embedded):
//   * The per-sample hot loop calls NO transcendental functions. Each harmonic
//     oscillator is advanced by a complex rotation (two muls, one add). cos/sin are
//     only evaluated in setFrequency()/setSecondaryPath(), i.e. on parameter change.
//   * No heap traffic in process(). Allocation happens once at construction.
//   * Header-only, C++17, freestanding-friendly (only <cmath>, <vector>, <cstddef>).
//
// Sign convention: the error mic measures  e = d + S{y}, where d is the disturbance
// and y is this controller's output. The leaky-LMS update descends e^2:
//     w <- (1 - leak) * w  -  mu_eff * e * x'      (x' = filtered reference)
// so the controller drives S{y} toward -d.

#ifndef EOC_ENGINE_ORDER_CANCELLER_HPP
#define EOC_ENGINE_ORDER_CANCELLER_HPP

#include <cmath>
#include <cstddef>
#include <vector>

namespace eoc {

class EngineOrderCanceller {
public:
    // fs        : sample rate (Hz)
    // numOrders : number of engine harmonics to cancel (orders 1..numOrders of f0)
    // mu        : normalized step size in (0, 2). ~0.05 is a safe start.
    // leak      : per-sample weight leakage in [0,1). Small values (1e-6..1e-4)
    //             keep weights from drifting when a harmonic is momentarily absent.
    EngineOrderCanceller(double fs, int numOrders, double mu = 0.05, double leak = 1e-6)
        : fs_(fs),
          H_(numOrders),
          mu_(mu),
          leak_(leak),
          active_(numOrders),
          wc_(static_cast<std::size_t>(numOrders), 0.0),
          ws_(static_cast<std::size_t>(numOrders), 0.0),
          cos_(static_cast<std::size_t>(numOrders), 1.0),
          sin_(static_cast<std::size_t>(numOrders), 0.0),
          dc_(static_cast<std::size_t>(numOrders), 1.0),
          ds_(static_cast<std::size_t>(numOrders), 0.0),
          sMag_(static_cast<std::size_t>(numOrders), 1.0),
          sCos_(static_cast<std::size_t>(numOrders), 1.0),
          sSin_(static_cast<std::size_t>(numOrders), 0.0) {}

    // Set the firing fundamental (Hz). Recomputes the per-harmonic rotation steps.
    // Call whenever the tach reports a new RPM. Trig happens here, not in process().
    void setFrequency(double f0Hz) {
        f0_ = f0Hz;
        for (int h = 0; h < H_; ++h) {
            const double order = static_cast<double>(h + 1);
            const double w = 2.0 * kPi * order * f0Hz / fs_;  // rad/sample
            dc_[static_cast<std::size_t>(h)] = std::cos(w);
            ds_[static_cast<std::size_t>(h)] = std::sin(w);
        }
    }

    // Set the secondary-path response at engine `order` (1-based): magnitude and phase
    // (radians) of S evaluated at order*f0. Identify these once per machine/mount.
    void setSecondaryPath(int order, double magnitude, double phaseRad) {
        const std::size_t i = static_cast<std::size_t>(order - 1);
        if (i >= static_cast<std::size_t>(H_)) return;
        sMag_[i] = magnitude < kSFloor ? kSFloor : magnitude;   // never 0: keeps the NLMS norm sane
        sCos_[i] = std::cos(phaseRad);
        sSin_[i] = std::sin(phaseRad);
    }

    // --- runtime tuning (e.g. from the phone cockpit, forwarded via the ESP link) ---
    void setMu(double mu) { mu_ = mu; }                       // adaptation step
    void setOutputGain(double g) { outGain_ = g; }            // master anti-noise scale (0..1 ramp)
    void setActiveOrders(int n) {                             // cancel only orders 1..n
        if (n < 1) n = 1;
        if (n > H_) n = H_;
        for (int h = n; h < active_; ++h) {                  // zero+freeze the dropped orders
            wc_[static_cast<std::size_t>(h)] = 0.0;
            ws_[static_cast<std::size_t>(h)] = 0.0;
        }
        active_ = n;
    }
    double mu() const { return mu_; }
    double outputGain() const { return outGain_; }
    int activeOrders() const { return active_; }

    // Process one sample. `errorSample` is the latest error-mic reading; returns the
    // anti-noise sample to emit. Hot path: no trig, no allocation.
    float process(float errorSample) {
        const double e = static_cast<double>(errorSample);
        double y = 0.0;

        for (int h = 0; h < active_; ++h) {
            const std::size_t i = static_cast<std::size_t>(h);
            const double c = cos_[i];
            const double s = sin_[i];

            // anti-noise contribution of this order
            y += wc_[i] * c + ws_[i] * s;

            // filtered reference x' = S{reference}: rotate (c,s) by S phase, scale by |S|
            const double xc = sMag_[i] * (c * sCos_[i] - s * sSin_[i]);
            const double xs = sMag_[i] * (s * sCos_[i] + c * sSin_[i]);

            // per-order normalized step: divide by filtered-ref power so |S| and the
            // number of orders don't destabilize the update. E[xc^2] = 0.5*|S|^2.
            const double norm = 0.5 * sMag_[i] * sMag_[i] * static_cast<double>(H_) + kEps;
            const double muEff = mu_ / norm;

            // leaky LMS descent of e^2
            wc_[i] = (1.0 - leak_) * wc_[i] - muEff * e * xc;
            ws_[i] = (1.0 - leak_) * ws_[i] - muEff * e * xs;

            // advance this harmonic's phasor by complex rotation (trig-free)
            const double nc = c * dc_[i] - s * ds_[i];
            const double ns = s * dc_[i] + c * ds_[i];
            cos_[i] = nc;
            sin_[i] = ns;
        }

        // periodically renormalize phasors to unit magnitude (fights slow drift)
        if (++renormCtr_ >= kRenormEvery) {
            renormCtr_ = 0;
            for (int h = 0; h < H_; ++h) {
                const std::size_t i = static_cast<std::size_t>(h);
                const double m = std::sqrt(cos_[i] * cos_[i] + sin_[i] * sin_[i]);
                if (m > kEps) {
                    cos_[i] /= m;
                    sin_[i] /= m;
                }
            }
        }
        return static_cast<float>(y * outGain_);
    }

    // Zero the adaptive weights and reset phasors (keeps fs/mu/secondary path).
    void reset() {
        for (int h = 0; h < H_; ++h) {
            const std::size_t i = static_cast<std::size_t>(h);
            wc_[i] = ws_[i] = 0.0;
            cos_[i] = 1.0;
            sin_[i] = 0.0;
        }
        renormCtr_ = 0;
    }

    // Re-seat every harmonic oscillator to a known engine angle theta0 (radians).
    // Call this on each tach edge (e.g. spark pulse = once per revolution) to lock the
    // internal reference phase to the real engine phase. This is what makes cancellation
    // work on a real machine: a free-running oscillator drifts out of phase with the
    // jittering engine; the tach edge pins it back. Trig runs here (once per rev), never
    // in the per-sample hot loop.
    void syncPhase(double theta0 = 0.0) {
        for (int h = 0; h < H_; ++h) {
            const double a = static_cast<double>(h + 1) * theta0;
            cos_[static_cast<std::size_t>(h)] = std::cos(a);
            sin_[static_cast<std::size_t>(h)] = std::sin(a);
        }
        renormCtr_ = 0;
    }

    // Current adapted amplitude at engine `order` (1-based): sqrt(wc^2 + ws^2).
    double orderAmplitude(int order) const {
        const std::size_t i = static_cast<std::size_t>(order - 1);
        if (i >= static_cast<std::size_t>(H_)) return 0.0;
        return std::sqrt(wc_[i] * wc_[i] + ws_[i] * ws_[i]);
    }

    // Current oscillator phasor for `order` (1-based) -- for tests/firmware introspection.
    double referenceCos(int order) const {
        const std::size_t i = static_cast<std::size_t>(order - 1);
        return i < static_cast<std::size_t>(H_) ? cos_[i] : 0.0;
    }
    double referenceSin(int order) const {
        const std::size_t i = static_cast<std::size_t>(order - 1);
        return i < static_cast<std::size_t>(H_) ? sin_[i] : 0.0;
    }

    int numOrders() const { return H_; }
    double frequency() const { return f0_; }

    // Calibrated secondary-path magnitude |S| at engine `order` (1-based) -- for the
    // cockpit to display what the S_hat calibration measured.
    double secondaryMag(int order) const {
        const std::size_t i = static_cast<std::size_t>(order - 1);
        if (i >= static_cast<std::size_t>(H_)) return 0.0;
        return sMag_[i];
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kEps = 1e-12;
    static constexpr double kSFloor = 0.01;   // floor |S| so a zero can't blow up the NLMS norm
    static constexpr int kRenormEvery = 1024;

    double fs_;
    int H_;
    double mu_;
    double leak_;
    int active_;             // orders currently adapted/output (<= H_)
    double outGain_ = 1.0;   // master anti-noise output scale
    double f0_ = 0.0;
    int renormCtr_ = 0;

    std::vector<double> wc_, ws_;    // adaptive cosine/sine weights per order
    std::vector<double> cos_, sin_;  // current oscillator phasor per order
    std::vector<double> dc_, ds_;    // per-order rotation step (cos/sin of w)
    std::vector<double> sMag_, sCos_, sSin_;  // secondary-path |S|, cos/sin(angle S)
};

}  // namespace eoc

#endif  // EOC_ENGINE_ORDER_CANCELLER_HPP
