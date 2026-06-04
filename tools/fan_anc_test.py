#!/usr/bin/env python3
"""Fan/generator ANC bench test: record a mic, find the tonal (engine-order)
content, run an offline engine-order canceller, report dB reduction.

The fan has no tach, so the per-tone phase reference is derived from the
recording itself (FFT-locked). On the real generator that reference comes
from the tach pickup -- the algorithm is identical, only the ref source
differs. Engine-order ANC cancels TONES, not broadband hiss; this measures
exactly how much of the captured noise is tonal/cancellable.
"""
import sys, argparse
import numpy as np
import sounddevice as sd
import soundfile as sf
from scipy.signal import welch, find_peaks

FS = 44100

def record(device, secs, ch=1):
    print(f"[rec] {secs}s @ {FS} Hz from device {device} ...", flush=True)
    x = sd.rec(int(secs*FS), samplerate=FS, channels=ch, device=device, dtype='float32')
    sd.wait()
    return x[:,0]

def psd_db(x):
    f, p = welch(x, FS, nperseg=8192, noverlap=4096)
    return f, 10*np.log10(p + 1e-12)

def find_tones(x, fmax=600, n=6):
    """Dominant narrowband peaks below fmax = the engine orders."""
    f, p = welch(x, FS, nperseg=16384, noverlap=8192)
    band = f <= fmax
    fb, pb = f[band], p[band]
    # peak prominence relative to local broadband floor
    pk,_ = find_peaks(10*np.log10(pb+1e-12), prominence=4, distance=8)
    if len(pk)==0: return []
    order = pk[np.argsort(pb[pk])[::-1]][:n]
    return sorted(float(fb[i]) for i in order if fb[i] > 25)

def engine_order_cancel(x, tones, mu=0.02, leak=1e-4):
    """Offline narrowband FxLMS: one 2-tap adaptive oscillator per tone.
    Returns residual signal. Secondary path ~ unity here (offline, mic==error)."""
    N = len(x); n = np.arange(N)
    y = np.zeros(N, dtype=np.float64)
    w = {f: np.zeros(2) for f in tones}          # [cos, sin] weights per order
    ref = {f: (np.cos(2*np.pi*f*n/FS), np.sin(2*np.pi*f*n/FS)) for f in tones}
    e = x.astype(np.float64).copy()
    # block-LMS for speed
    B = 64
    for s in range(0, N, B):
        sl = slice(s, min(s+B, N))
        anti = np.zeros(sl.stop-sl.start)
        for f in tones:
            c, si = ref[f][0][sl], ref[f][1][sl]
            anti += w[f][0]*c + w[f][1]*si
        err = x[sl] - anti
        e[sl] = err
        for f in tones:
            c, si = ref[f][0][sl], ref[f][1][sl]
            nrm = (c@c + si@si) + 1e-6
            w[f][0] = (1-leak)*w[f][0] + mu*(c@err)/nrm
            w[f][1] = (1-leak)*w[f][1] + mu*(si@err)/nrm
    return e

def tone_energy_db(x, tones, bw=4):
    f, p = welch(x, FS, nperseg=16384, noverlap=8192)
    out = {}
    for t in tones:
        m = np.abs(f - t) <= bw
        out[t] = 10*np.log10(p[m].sum() + 1e-12)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--device', type=int, default=1)
    ap.add_argument('--secs', type=float, default=4.0)
    ap.add_argument('--label', default='cap')
    ap.add_argument('--cancel', action='store_true')
    a = ap.parse_args()

    x = record(a.device, a.secs)
    rms = float(np.sqrt(np.mean(x**2)))
    peak = float(np.max(np.abs(x)))
    wav = f"captures/{a.label}.wav"
    sf.write(wav, x, FS)
    print(f"[lvl] rms={rms:.4f}  peak={peak:.3f}  dBFS_rms={20*np.log10(rms+1e-9):.1f}  -> {wav}")
    if peak < 0.002:
        print("[warn] almost silent -- is the Yeti the source / unmuted / gain up?")

    tones = find_tones(x)
    if not tones:
        print("[tones] no clear narrowband tone found (mostly broadband).")
    else:
        print(f"[tones] engine-order tones (Hz): " + ", ".join(f"{t:.1f}" for t in tones))

    if a.cancel and tones:
        before = tone_energy_db(x, tones)
        e = engine_order_cancel(x, tones)
        sf.write(f"captures/{a.label}_anc.wav", e.astype('float32'), FS)
        after = tone_energy_db(e, tones)
        print("\n[ANC] per-order tonal reduction:")
        tot_b = tot_a = 0.0
        for t in tones:
            d = before[t]-after[t]
            tot_b += 10**(before[t]/10); tot_a += 10**(after[t]/10)
            print(f"   {t:6.1f} Hz   {before[t]:6.1f} -> {after[t]:6.1f} dB   ({d:+.1f} dB)")
        print(f"\n[ANC] total tonal energy: {10*np.log10(tot_b):.1f} -> {10*np.log10(tot_a):.1f} dB "
              f"({10*np.log10(tot_a/tot_b):+.1f} dB on the whine)")
        print("[note] broadband hiss is unchanged -- ANC only kills the tones.")

        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fb, pb = psd_db(x); fa, pa = psd_db(e)
            fig, ax = plt.subplots(figsize=(10, 4.6), dpi=120)
            fig.patch.set_facecolor("#0a131e"); ax.set_facecolor("#0e1b28")
            ax.plot(fb, pb, color="#fb5607", lw=1.3, label="fan noise (mic)")
            ax.plot(fa, pa, color="#2dd4a7", lw=1.3, label="after engine-order ANC")
            for t in tones:
                ax.axvline(t, color="#ffb703", ls=":", lw=0.8, alpha=0.6)
            ax.set_xlim(0, 800); ax.set_xlabel("frequency (Hz)"); ax.set_ylabel("PSD (dB)")
            ax.set_title("Engine-order ANC on real fan noise (Yeti capture)", color="#e9eef4")
            for s in ax.spines.values(): s.set_color("#233a50")
            ax.tick_params(colors="#9fb4c8"); ax.xaxis.label.set_color("#9fb4c8"); ax.yaxis.label.set_color("#9fb4c8")
            ax.grid(color="#16273a", lw=0.6)
            ax.legend(facecolor="#0e1b28", edgecolor="#233a50", labelcolor="#c6d3e0")
            png = f"captures/{a.label}_anc.png"; fig.tight_layout(); fig.savefig(png)
            print(f"[plot] {png}")
        except Exception as ex:
            print(f"[plot] skipped ({ex})")

if __name__ == '__main__':
    main()
