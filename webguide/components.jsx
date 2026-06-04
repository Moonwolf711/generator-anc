/* Shared visual components for the Generator-ANC bench app.
   Exported to window at the bottom. */
const { useState, useEffect, useRef } = React;

/* ---------- a single pin pad on a board ---------- */
function Pad({ label, role, active, connected }) {
  const color = role ? window.ANC.ROLE[role].color : null;
  const lit = role && (active || connected);
  return (
    <div className="pad-wrap">
      <div
        className={"pad" + (role ? " pad-live" : "") + (lit ? " pad-lit" : "")}
        style={lit ? { borderColor: color, boxShadow: `0 0 0 2px ${color}, 0 0 14px ${color}aa` }
                   : role ? { borderColor: color + "88" } : null}
      >
        <span className="pad-hole" style={lit ? { background: color } : null} />
      </div>
      <span className="pad-label" style={lit ? { color } : null}>{label}</span>
    </div>
  );
}

/* ---------- Teensy 4.0 green module ---------- */
function TeensyBoard({ connections }) {
  const A = window.ANC;
  const isConn = (pin) => {
    const role = A.TEENSY_USED[pin];
    if (!role) return false;
    // a teensy pin counts as connected if any wire using it is connected
    return A.WIRES.some((w) => w.from === pin && connections[w.id]);
  };
  return (
    <div className="board board-teensy">
      <div className="board-head">
        <span className="board-name">TEENSY 4.0</span>
        <span className="board-sub">controller · 3.3V logic</span>
      </div>
      <div className="usb-notch" />
      <div className="pin-row">
        {A.TEENSY_TOP.map((p) => (
          <Pad key={"t" + p} label={p} role={A.TEENSY_USED[p]} connected={isConn(p)} />
        ))}
      </div>
      <div className="board-core">
        <span className="core-chip">IMXRT1062</span>
      </div>
      <div className="pin-row">
        {A.TEENSY_BOT.map((p) => (
          <Pad key={"b" + p} label={p === "G" ? "G" : p} role={A.TEENSY_USED[p]} connected={isConn(p)} />
        ))}
      </div>
    </div>
  );
}

/* ---------- MAX98357A purple amp ---------- */
function MaxBoard({ connections }) {
  const A = window.ANC;
  const isConn = (pin) => {
    const role = A.MAX_USED[pin];
    if (!role) return false;
    return A.WIRES.some((w) => w.to === pin && connections[w.id]);
  };
  return (
    <div className="board board-max">
      <div className="board-head">
        <span className="board-name">MAX98357A</span>
        <span className="board-sub">I²S class-D amp</span>
      </div>
      <div className="pin-row max-row">
        {A.MAX_PINS.map((p) => {
          const empty = A.MAX_LEAVE_EMPTY.includes(p);
          return (
            <Pad key={"m" + p} label={p} role={A.MAX_USED[p]} connected={isConn(p)}
                 active={false} />
          );
        })}
      </div>
      <div className="max-mid">
        <span className="max-chip" />
        <span className="empty-note">SD + GAIN → leave empty</span>
      </div>
      <div className="screw-term">
        <span className="screw" /><span className="screw" />
        <span className="screw-label">SPEAKER</span>
      </div>
    </div>
  );
}

/* ---------- a wire role legend dot ---------- */
function RoleDot({ role }) {
  const r = window.ANC.ROLE[role];
  return <span className="role-dot" style={{ background: r.color }} title={r.label} />;
}

/* ---------- horizontal meter bar ---------- */
function Meter({ label, value, max, unit, color, sub, invert }) {
  const pct = Math.max(0, Math.min(100, (value / max) * 100));
  return (
    <div className="meter">
      <div className="meter-top">
        <span className="meter-label">{label}</span>
        <span className="meter-val" style={{ color }}>
          {value.toFixed(value < 10 && value > -10 ? 1 : 0)}<small>{unit}</small>
        </span>
      </div>
      <div className="meter-track">
        <div className="meter-fill" style={{ width: pct + "%", background: color, boxShadow: `0 0 10px ${color}99` }} />
      </div>
      {sub && <span className="meter-sub">{sub}</span>}
    </div>
  );
}

Object.assign(window, { Pad, TeensyBoard, MaxBoard, RoleDot, Meter });
