/* Capture Tuner: loads the REAL generator spectrum (window.GENSET) and lets you
   adjust the engine-order ANC parameters live, redrawing the predicted residual.
   This is a PLANNING preview (what a tach + calibrated S_hat would achieve), not
   the live adaptive result -- labeled as such. */
const { useState: useT, useEffect: useTE, useRef: useTR } = React;

const DBMIN = -92, DBMAX = -12;
const denorm = (v) => DBMIN + v * (DBMAX - DBMIN);   // 0..1 -> dB
const renorm = (d) => Math.max(0, Math.min(1, (d - DBMIN) / (DBMAX - DBMIN)));

/* module-scope so dragging a slider doesn't remount the <input> mid-drag */
function Slider({ label, val, set, min, max, step, fmt }) {
  return (
    <label className="ctl">
      <span className="ctl-top"><span>{label}</span><b>{fmt(val)}</b></span>
      <input type="range" min={min} max={max} step={step} value={val}
        onChange={(e) => set(parseFloat(e.target.value))} />
    </label>
  );
}

function CaptureTuner() {
  const G = window.GENSET;
  const cvs = useTR(null);
  const [f0, setF0]       = useT(G ? G.f0 : 30.5);  // firing fundamental (Hz)
  const [orders, setOrd]  = useT(6);                // orders the controller targets
  const [redDb, setRed]   = useT(18);               // per-order cancellation depth (dB)
  const [cutoff, setCut]  = useT(200);              // speaker usable top (Hz)

  const raw = G ? G.raw : [], n = raw.length, fmax = G ? G.fmax : 360;
  const fAt = (i) => (i / (n - 1)) * fmax;

  // targeted order frequencies that the speaker can actually reproduce
  const targets = [];
  for (let h = 1; h <= orders; h++) { const hz = h * f0; if (hz <= cutoff && hz <= fmax) targets.push(hz); }
  const ordersInBand = (() => { let c = 0; for (let h = 1; h <= 16; h++) if (h * f0 <= Math.min(cutoff, fmax)) c++; return c; })();

  // predicted residual: knock each targeted in-band order down by redDb
  const resid = raw.map((v, i) => {
    const f = fAt(i), rd = denorm(v);
    const hit = targets.some((hz) => Math.abs(f - hz) <= 2.2);
    return renorm(hit ? rd - redDb : rd);
  });

  // tonal-energy reduction = sum at the harmonic peaks (what engine-order ANC targets;
  // summing the whole band would drown the win in broadband floor and mislead).
  let tb = 0, ta = 0;
  for (let h = 1; h <= 16; h++) {
    const hz = h * f0; if (hz > fmax) break;
    const i = Math.round((hz / fmax) * (n - 1));
    tb += Math.pow(10, denorm(raw[i]) / 10);
    ta += Math.pow(10, denorm(resid[i]) / 10);
  }
  const tonalDb = 10 * Math.log10(ta / tb);

  useTE(() => {
    const c = cvs.current;
    if (!G || !c) return;
    const draw = () => {
    const ctx = c.getContext("2d");
    const W = c.clientWidth, H = c.clientHeight, dpr = window.devicePixelRatio || 1;
    if (c.width !== W * dpr) { c.width = W * dpr; c.height = H * dpr; }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    const padL = 8, padB = 18, padT = 8;
    const x = (f) => padL + (f / fmax) * (W - padL - 8);
    const y = (a) => padT + (1 - a) * (H - padT - padB);

    // grid + freq ticks every 60 Hz
    ctx.strokeStyle = "#16273a"; ctx.lineWidth = 1; ctx.fillStyle = "#4a5d72";
    ctx.font = "10px 'JetBrains Mono', monospace";
    for (let f = 60; f <= fmax; f += 60) { ctx.beginPath(); ctx.moveTo(x(f), padT); ctx.lineTo(x(f), H - padB); ctx.stroke(); ctx.fillText(f, x(f) - 8, H - 5); }

    // order tick marks
    for (let h = 1; h <= 16; h++) {
      const hz = h * f0; if (hz > fmax) break;
      const inBand = hz <= cutoff, targeted = h <= orders && inBand;
      ctx.strokeStyle = targeted ? "#2dd4a766" : inBand ? "#ffb70355" : "#3f546833";
      ctx.setLineDash(targeted ? [] : [3, 3]);
      ctx.beginPath(); ctx.moveTo(x(hz), padT); ctx.lineTo(x(hz), H - padB); ctx.stroke();
    }
    ctx.setLineDash([]);

    // speaker cutoff marker
    ctx.strokeStyle = "#fb5607"; ctx.lineWidth = 1.4; ctx.setLineDash([5, 4]);
    ctx.beginPath(); ctx.moveTo(x(cutoff), padT); ctx.lineTo(x(cutoff), H - padB); ctx.stroke();
    ctx.setLineDash([]); ctx.fillStyle = "#fb5607"; ctx.fillText("sub limit", x(cutoff) + 4, padT + 10);

    // raw comb (orange)
    ctx.beginPath();
    raw.forEach((v, i) => { const xi = x(fAt(i)); i ? ctx.lineTo(xi, y(v)) : ctx.moveTo(xi, y(v)); });
    ctx.strokeStyle = "#fb5607"; ctx.lineWidth = 1.4; ctx.stroke();

    // residual (teal fill + line)
    ctx.beginPath(); ctx.moveTo(x(0), y(0));
    resid.forEach((v, i) => ctx.lineTo(x(fAt(i)), y(v)));
    ctx.lineTo(x(fmax), y(0)); ctx.closePath();
    const g = ctx.createLinearGradient(0, padT, 0, H - padB);
    g.addColorStop(0, "#2dd4a7cc"); g.addColorStop(1, "#2dd4a710");
    ctx.fillStyle = g; ctx.fill();
    ctx.beginPath();
    resid.forEach((v, i) => { const xi = x(fAt(i)); i ? ctx.lineTo(xi, y(v)) : ctx.moveTo(xi, y(v)); });
    ctx.strokeStyle = "#2dd4a7"; ctx.lineWidth = 1.6; ctx.shadowColor = "#2dd4a7"; ctx.shadowBlur = 6; ctx.stroke(); ctx.shadowBlur = 0;
    };
    draw();
    const ro = new ResizeObserver(draw); ro.observe(c);   // redraw on rotate/resize
    return () => ro.disconnect();
  }, [f0, orders, redDb, cutoff]);

  if (!G) return <div className="tuner-missing">genset_spectrum.js not loaded — run the capture export.</div>;

  return (
    <div className="tuner">
      <div className="tuner-head">
        <span className="scope-label">REAL CAPTURE · {G.label}</span>
        <span className="plan-tag">PLANNING preview · tach + calibrated Ŝ</span>
      </div>
      <canvas ref={cvs} className="tuner-canvas" />
      <div className="freq-axis">frequency (Hz) — <span style={{ color: "#fb5607" }}>raw comb</span> vs <span style={{ color: "#2dd4a7" }}>predicted after ANC</span></div>

      <div className="tuner-readout">
        <div className="read-stat"><span>Tonal energy ↓</span><b style={{ color: "#2dd4a7" }}>{tonalDb.toFixed(1)} dB</b></div>
        <div className="read-stat"><span>Orders targeted</span><b>{targets.length} / {ordersInBand} in band</b></div>
        <div className="read-stat"><span>Firmware</span><b className="mono-b">NUM_ORDERS={orders} · {Math.round(f0 * 60)} RPM</b></div>
      </div>

      <div className="tuner-controls">
        <Slider label="Firing RPM (f0)" val={f0} set={setF0} min={28} max={33} step={0.1} fmt={(v) => `${Math.round(v * 60)} rpm · ${v.toFixed(1)} Hz`} />
        <Slider label="Orders cancelled" val={orders} set={setOrd} min={1} max={16} step={1} fmt={(v) => `${v}`} />
        <Slider label="Cancellation depth" val={redDb} set={setRed} min={0} max={30} step={1} fmt={(v) => `${v} dB`} />
        <Slider label="Speaker top (sub)" val={cutoff} set={setCut} min={60} max={360} step={10} fmt={(v) => `${v} Hz`} />
      </div>
      <p className="tuner-note">Drag <b>Orders</b> up until the <b>sub limit</b> stops them — that's why <b>NUM_ORDERS=6</b> fits a 12″ sub (orders 1–6 ≈ 30–183 Hz). Push the <b>Speaker top</b> higher only if you add a mid driver. <b>Depth</b> is what a calibrated Ŝ + tach buys you per order.</p>
    </div>
  );
}

Object.assign(window, { CaptureTuner });
