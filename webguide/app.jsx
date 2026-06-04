/* App shell: top bar, two-step nav, shared wiring state. */
const { useState: useSA, useEffect: useEA } = React;

function App() {
  const A = window.ANC;
  const [connections, setConnections] = useSA(() => {
    try { return JSON.parse(localStorage.getItem("anc_conn") || "{}"); } catch { return {}; }
  });
  const [screen, setScreen] = useSA(() => localStorage.getItem("anc_screen") || "wiring");

  useEA(() => { localStorage.setItem("anc_conn", JSON.stringify(connections)); }, [connections]);
  useEA(() => { localStorage.setItem("anc_screen", screen); }, [screen]);

  const wired = A.WIRES.every((w) => connections[w.id]);
  const doneCount = A.WIRES.filter((w) => connections[w.id]).length;

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="brand-mark">ANC</span>
          <div className="brand-txt">
            <b>Generator Active Noise Cancellation</b>
            <span>Teensy 4.0 · engine-order controller · bench rig</span>
          </div>
        </div>
        <nav className="steps">
          <button className={"step" + (screen === "wiring" ? " active" : "")} onClick={() => setScreen("wiring")}>
            <span className="step-n">1</span>
            <span className="step-l">Wiring<small>{doneCount}/5 wires</small></span>
          </button>
          <span className="step-sep" />
          <button className={"step" + (screen === "bench" ? " active" : "") + (wired ? "" : " locked")}
            onClick={() => setScreen("bench")}>
            <span className="step-n">2</span>
            <span className="step-l">Live Bench<small>{wired ? "ready" : "wire first"}</small></span>
          </button>
        </nav>
      </header>

      <main className="stage">
        {screen === "wiring"
          ? <WiringGuide connections={connections} setConnections={setConnections}
              onComplete={() => setScreen("bench")} />
          : <Dashboard connections={connections} onBack={() => setScreen("wiring")} />}
      </main>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(<App />);
