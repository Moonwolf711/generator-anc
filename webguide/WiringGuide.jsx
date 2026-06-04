/* Wiring Guide screen: the 5 wires, check-off flow, live board diagrams. */
const { useState: useStateW } = React;

function WireRow({ wire, connected, onToggle }) {
  const role = window.ANC.ROLE[wire.role];
  return (
    <button
      className={"wire-row" + (connected ? " wire-done" : "")}
      onClick={onToggle}
      style={connected ? { borderColor: role.color + "66" } : null}
    >
      <span className="wire-check" style={connected ? { background: role.color, borderColor: role.color } : null}>
        {connected ? "\u2713" : ""}
      </span>
      <span className="wire-pins">
        <span className="pin-chip" style={{ borderColor: role.color, color: role.color }}>{wire.from}</span>
        <span className="wire-line" style={{ background: role.color }} />
        <span className="pin-chip" style={{ borderColor: role.color, color: role.color }}>{wire.to}</span>
      </span>
      <span className="wire-text">
        <span className="wire-role" style={{ color: role.color }}>{role.label}</span>
        <span className="wire-desc">{wire.desc}</span>
      </span>
    </button>
  );
}

function WiringGuide({ connections, setConnections, onComplete }) {
  const A = window.ANC;
  const [showPhotos, setShowPhotos] = useStateW(false);
  const toggle = (id) => setConnections((c) => ({ ...c, [id]: !c[id] }));
  const doneCount = A.WIRES.filter((w) => connections[w.id]).length;
  const allDone = doneCount === A.WIRES.length;
  const signal = A.WIRES.filter((w) => w.group === "signal");
  const power = A.WIRES.filter((w) => w.group === "power");

  return (
    <div className="screen wiring">
      <div className="wire-head">
        <div>
          <h2>Wire the output stage</h2>
          <p className="lede">
            Five small wires from the Teensy to the MAX98357A amp.
            <strong style={{ color: "#ffb703" }}> 3 signal</strong> +
            <strong style={{ color: "#fb5607" }}> 2 power</strong>. Tap each as you connect it.
          </p>
        </div>
        <div className="wire-progress">
          <ProgressRing done={doneCount} total={A.WIRES.length} />
        </div>
      </div>

      <div className="boards">
        <TeensyBoard connections={connections} />
        <div className="boards-flow">
          <span className="flow-label">I²S + PWR</span>
          <div className="flow-wires">
            {A.WIRES.map((w) => (
              <span key={w.id} className="flow-wire"
                style={{ background: connections[w.id] ? A.ROLE[w.role].color : "#26374a",
                         boxShadow: connections[w.id] ? `0 0 8px ${A.ROLE[w.role].color}` : "none" }} />
            ))}
          </div>
        </div>
        <MaxBoard connections={connections} />
      </div>

      <button className="photo-toggle" onClick={() => setShowPhotos((s) => !s)}>
        {showPhotos ? "\u2715 Hide" : "\u25B8 Show"} my real boards
      </button>
      {showPhotos && (
        <div className="photos">
          <figure>
            <img src="assets/teensy_photo.jpg" alt="Teensy 4.0" />
            <figcaption>Your Teensy — find <b>5V 21 20 7 G</b> on the green chip</figcaption>
          </figure>
          <figure>
            <img src="assets/max_photo.jpg" alt="MAX98357A" className="rot180" />
            <figcaption>Your MAX — reads <b>Vin GND SD GAIN DIN BCLK LRC</b></figcaption>
          </figure>
        </div>
      )}

      <div className="wire-groups">
        <div className="wire-group">
          <h3><span className="grp-tag" style={{ color: "#ffb703" }}>SIGNAL</span> I²S audio — 3 wires</h3>
          {signal.map((w) => (
            <WireRow key={w.id} wire={w} connected={!!connections[w.id]} onToggle={() => toggle(w.id)} />
          ))}
        </div>
        <div className="wire-group">
          <h3><span className="grp-tag" style={{ color: "#fb5607" }}>POWER</span> the amp needs juice — 2 wires</h3>
          {power.map((w) => (
            <WireRow key={w.id} wire={w} connected={!!connections[w.id]} onToggle={() => toggle(w.id)} />
          ))}
        </div>
      </div>

      <div className="wire-foot">
        <p className="foot-note">
          <b style={{ color: "#2dd4a7" }}>Speaker</b> → the green screw terminal on the MAX.
          Leave <b>SD</b> and <b>GAIN</b> unconnected.
        </p>
        <button className={"big-btn" + (allDone ? " ready" : "")} disabled={!allDone} onClick={onComplete}>
          {allDone ? "Output wired \u2014 go to Live Bench \u2192" : `Connect all 5 wires (${doneCount}/5)`}
        </button>
      </div>
    </div>
  );
}

function ProgressRing({ done, total }) {
  const pct = done / total;
  const R = 30, C = 2 * Math.PI * R;
  const allDone = done === total;
  const col = allDone ? "#2dd4a7" : "#4cc9f0";
  return (
    <div className="ring-wrap">
      <svg width="76" height="76" viewBox="0 0 76 76">
        <circle cx="38" cy="38" r={R} fill="none" stroke="#1b2a3a" strokeWidth="7" />
        <circle cx="38" cy="38" r={R} fill="none" stroke={col} strokeWidth="7"
          strokeLinecap="round" strokeDasharray={C} strokeDashoffset={C * (1 - pct)}
          transform="rotate(-90 38 38)" style={{ transition: "stroke-dashoffset .4s ease, stroke .3s" }} />
        <text x="38" y="35" textAnchor="middle" className="ring-num" fill={col}>{done}</text>
        <text x="38" y="50" textAnchor="middle" className="ring-den">of {total}</text>
      </svg>
    </div>
  );
}

Object.assign(window, { WiringGuide });
