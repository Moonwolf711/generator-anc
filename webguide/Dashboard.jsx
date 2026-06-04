/* Live Bench dashboard: serial control, animated engine-order spectrum, meters. */
const { useState: useS, useEffect: useE, useRef: useR } = React;

const ORDERS = [50, 100, 150, 200, 250, 300]; // engine-order harmonics (Hz)
const ORDER_AMP = [1.0, 0.72, 0.85, 0.5, 0.34, 0.22];

function Spectrum({ stateRef }) {
  const cvs = useR(null);
  useE(() => {
    const c = cvs.current;
    const ctx = c.getContext("2d");
    let raf, t = 0;
    const draw = () => {
      const W = c.clientWidth, H = c.clientHeight;
      if (c.width !== W * 2) { c.width = W * 2; c.height = H * 2; }
      ctx.setTransform(2, 0, 0, 2, 0, 0);
      ctx.clearRect(0, 0, W, H);
      const s = stateRef.current;
      const padL = 6, padB = 18, padT = 8;
      const x = (f) => padL + (f / 360) * (W - padL - 6);
      const y = (a) => padT + (1 - a) * (H - padT - padB); // a in 0..1

      // grid + freq ticks
      ctx.strokeStyle = "#16273a"; ctx.lineWidth = 1;
      ctx.fillStyle = "#4a5d72"; ctx.font = "10px 'JetBrains Mono', monospace";
      [50,100,150,200,250,300].forEach((f) => {
        ctx.beginPath(); ctx.moveTo(x(f), padT); ctx.lineTo(x(f), H - padB); ctx.stroke();
        ctx.fillText(f, x(f) - 8, H - 5);
      });

      // amplitude model
      const reduction = s.reduction;          // 0..1 cancellation of peaks
      const eng = s.engineOn ? 1 : 0;
      const amp = (f) => {
        let a = 0.06 + 0.02 * Math.abs(Math.sin(f * 0.13 + t * 0.05)); // noise floor
        if (eng) ORDERS.forEach((o, i) => {
          const w = 7;
          a += ORDER_AMP[i] * 0.82 * Math.exp(-((f - o) ** 2) / (2 * w * w))
               * (1 + 0.04 * Math.sin(t * 0.3 + i));
        });
        // calibration sweep bump
        if (s.mode === "calibrating") {
          const w = 9;
          a += 0.7 * Math.exp(-((f - s.sweepHz) ** 2) / (2 * w * w));
        }
        return Math.min(1, a);
      };
      const ampCancelled = (f) => {
        let a = amp(f);
        if (s.mode === "running" && eng) {
          let peak = 0;
          ORDERS.forEach((o, i) => { const w = 7;
            peak += ORDER_AMP[i] * 0.82 * Math.exp(-((f - o) ** 2) / (2 * w * w)); });
          a -= peak * reduction;
        }
        return Math.max(0.04, a);
      };

      const N = 240;
      // ghost (uncancelled) outline when running
      if (s.mode === "running" && eng) {
        ctx.beginPath();
        for (let i = 0; i <= N; i++) { const f = (i / N) * 360; const xi = x(f);
          i ? ctx.lineTo(xi, y(amp(f))) : ctx.moveTo(xi, y(amp(f))); }
        ctx.strokeStyle = "#fb560744"; ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]); ctx.stroke();
        ctx.setLineDash([]);
      }

      // main filled spectrum
      const running = s.mode === "running" && eng;
      const col = running ? "#2dd4a7" : s.mode === "calibrating" ? "#ffb703" : (eng ? "#fb5607" : "#3f5468");
      ctx.beginPath(); ctx.moveTo(x(0), y(0));
      for (let i = 0; i <= N; i++) { const f = (i / N) * 360; ctx.lineTo(x(f), y(ampCancelled(f))); }
      ctx.lineTo(x(360), y(0)); ctx.closePath();
      const grad = ctx.createLinearGradient(0, padT, 0, H - padB);
      grad.addColorStop(0, col + "cc"); grad.addColorStop(1, col + "10");
      ctx.fillStyle = grad; ctx.fill();
      ctx.beginPath();
      for (let i = 0; i <= N; i++) { const f = (i / N) * 360; const xi = x(f);
        i ? ctx.lineTo(xi, y(ampCancelled(f))) : ctx.moveTo(xi, y(ampCancelled(f))); }
      ctx.strokeStyle = col; ctx.lineWidth = 1.6; ctx.shadowColor = col; ctx.shadowBlur = 8;
      ctx.stroke(); ctx.shadowBlur = 0;

      // sweep marker line
      if (s.mode === "calibrating") {
        ctx.beginPath(); ctx.moveTo(x(s.sweepHz), padT); ctx.lineTo(x(s.sweepHz), H - padB);
        ctx.strokeStyle = "#ffb703"; ctx.lineWidth = 1.5; ctx.stroke();
        ctx.fillStyle = "#ffb703"; ctx.font = "bold 11px 'JetBrains Mono', monospace";
        ctx.fillText(Math.round(s.sweepHz) + "Hz", x(s.sweepHz) + 4, padT + 12);
      }
      t += 1; raf = requestAnimationFrame(draw);
    };
    draw();
    return () => cancelAnimationFrame(raf);
  }, []);
  return <canvas ref={cvs} className="spectrum" />;
}

function CmdButton({ cmd, enabled, onSend }) {
  return (
    <button className="cmd-btn" disabled={!enabled} onClick={() => onSend(cmd.key)}
      style={enabled ? { borderColor: cmd.color + "66" } : null}>
      <span className="cmd-key" style={{ color: cmd.color, borderColor: cmd.color + "66" }}>{cmd.key}</span>
      <span className="cmd-meta">
        <span className="cmd-name">{cmd.name}</span>
        <span className="cmd-blurb">{cmd.blurb}</span>
      </span>
    </button>
  );
}

function Dashboard({ connections, onBack }) {
  const A = window.ANC;
  const wired = A.WIRES.every((w) => connections[w.id]);
  const [mode, setMode] = useS("idle");        // idle | calibrating | calibrated | running
  const [engineOn, setEngineOn] = useS(false);
  const [m, setM] = useS({ reduction: 0, residual: -46, conf: 0, gain: 0, mic: 0.2 });
  const [log, setLog] = useS([
    { k: "sys", t: "Teensy 4.0 · ANC firmware ready · fs=44.1kHz" },
  ]);
  const sweepRef = useR(0);
  const stateRef = useR({ mode: "idle", engineOn: false, reduction: 0, sweepHz: 30 });
  const logRef = useR(null);

  const push = (k, t) => setLog((L) => [...L.slice(-40), { k, t }]);

  // keep stateRef synced for the canvas
  useE(() => { stateRef.current.mode = mode; }, [mode]);
  useE(() => { stateRef.current.engineOn = engineOn; }, [engineOn]);
  useE(() => { stateRef.current.reduction = m.reduction / 18; }, [m.reduction]);
  useE(() => { if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight; }, [log]);

  // metric easing loop
  useE(() => {
    const id = setInterval(() => {
      setM((p) => {
        const running = mode === "running" && engineOn;
        const tgtRed = running ? 15 + Math.sin(Date.now() / 700) * 1.5 : 0;
        const tgtRes = engineOn ? (running ? -34 : -14) : -46;
        const tgtConf = mode === "calibrating" ? p.conf : (mode === "calibrated" || mode === "running" ? 96 : 0);
        const tgtGain = mode === "running" ? 58 + Math.sin(Date.now() / 500) * 4 : 0;
        const tgtMic = (engineOn ? (running ? 0.34 : 0.78) : 0.16) + Math.random() * 0.06;
        const ease = (a, b, k) => a + (b - a) * k;
        return {
          reduction: ease(p.reduction, tgtRed, 0.12),
          residual: ease(p.residual, tgtRes, 0.1),
          conf: ease(p.conf, tgtConf, 0.15),
          gain: ease(p.gain, tgtGain, 0.15),
          mic: ease(p.mic, tgtMic, 0.3),
        };
      });
    }, 90);
    return () => clearInterval(id);
  }, [mode, engineOn]);

  const calibrate = () => {
    if (engineOn) { push("warn", "! turn the engine OFF before calibrating"); return; }
    setMode("calibrating");
    push("tx", "> c");
    push("rx", "calibrating secondary path Ŝ — sweep 30→300 Hz");
    let hz = 30; sweepRef.current = 30; stateRef.current.sweepHz = 30;
    const step = setInterval(() => {
      hz += 7; sweepRef.current = hz; stateRef.current.sweepHz = hz;
      setM((p) => ({ ...p, conf: Math.min(96, p.conf + 2.4) }));
      if (hz % 60 < 7) push("rx", `  Ŝ[${Math.round((hz - 30) / 6)}]  ${hz} Hz  mag=${(0.4 + Math.random() * 0.5).toFixed(2)}`);
      if (hz >= 300) {
        clearInterval(step);
        push("ok", "Ŝ estimated · 48 taps · confidence 96%");
        push("rx", "ready — send  r  to run ANC");
        setMode("calibrated");
      }
    }, 130);
  };

  const send = (key) => {
    if (key === "c") return calibrate();
    if (key === "r") {
      if (mode !== "calibrated" && mode !== "running") { push("warn", "! calibrate first (send c)"); return; }
      setMode("running"); push("tx", "> r");
      push("ok", "controller running · engine-order ANC active");
      if (!engineOn) push("rx", "  (no engine noise yet — toggle the generator)");
      return;
    }
    if (key === "s") { setMode(wired ? "calibrated" : "idle"); push("tx", "> s"); push("rx", "output muted"); return; }
    if (key === "?") {
      push("tx", "> ?");
      push("rx", `  state=${mode} fs=44100 taps=48 Ŝ_conf=${Math.round(m.conf)}%`);
      push("rx", `  engine=${engineOn ? "ON" : "off"} reduction=${m.reduction.toFixed(1)}dB`);
      return;
    }
  };

  const canCal = wired && mode !== "calibrating";
  const canRun = (mode === "calibrated") && mode !== "running";
  const reductionPct = Math.round((m.reduction / 18) * 100);

  return (
    <div className="screen dash">
      <div className="dash-head">
        <button className="back-btn" onClick={onBack}>← Wiring</button>
        <div className="dash-title">
          <h2>Live Bench</h2>
          <span className={"link-pill " + (wired ? "ok" : "bad")}>
            <span className="dot" /> {wired ? "Teensy linked · output wired" : "output not wired"}
          </span>
        </div>
        <label className={"engine-tgl" + (engineOn ? " on" : "")}>
          <input type="checkbox" checked={engineOn} onChange={(e) => setEngineOn(e.target.checked)} />
          <span className="tgl-track"><span className="tgl-knob" /></span>
          <span className="tgl-text">Generator<br /><b>{engineOn ? "RUNNING" : "off"}</b></span>
        </label>
      </div>

      {!wired && (
        <div className="warn-banner">Finish the 5 output wires first — the amp won't make sound until it's powered + clocked.</div>
      )}

      <div className="metrics">
        <Meter label="Noise reduction" value={m.reduction} max={18} unit="dB" color="#2dd4a7"
          sub={mode === "running" && engineOn ? `${reductionPct}% of engine orders` : "ANC idle"} />
        <Meter label="Residual @ mic" value={m.residual} max={0} unit="dB" color="#4cc9f0"
          sub="SM58 → Komplete A2" />
        <Meter label="Ŝ confidence" value={m.conf} max={100} unit="%" color="#ffb703"
          sub={mode === "idle" ? "not calibrated" : "secondary path"} />
        <Meter label="Output drive" value={m.gain} max={100} unit="%" color="#fb5607"
          sub="MAX98357A → speaker" />
      </div>

      <div className="scope-card">
        <div className="scope-top">
          <span className="scope-label">SPECTRUM · mic input</span>
          <span className="scope-legend">
            <i style={{ background: "#fb5607" }} /> engine orders
            <i style={{ background: "#2dd4a7" }} /> after ANC
          </span>
        </div>
        <Spectrum stateRef={stateRef} />
        <div className="freq-axis">frequency (Hz) — generator firing harmonics</div>
      </div>

      <div className="control-row">
        <div className="cmd-list">
          <CmdButton cmd={A.COMMANDS[0]} enabled={canCal} onSend={send} />
          <CmdButton cmd={A.COMMANDS[1]} enabled={canRun} onSend={send} />
          <CmdButton cmd={A.COMMANDS[2]} enabled={wired} onSend={send} />
          <CmdButton cmd={A.COMMANDS[3]} enabled={wired} onSend={send} />
        </div>
        <div className="console">
          <div className="console-top"><span>SERIAL · 115200</span><span className="blink">● live</span></div>
          <div className="console-body" ref={logRef}>
            {log.map((l, i) => <div key={i} className={"line line-" + l.k}>{l.t}</div>)}
          </div>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Dashboard });
