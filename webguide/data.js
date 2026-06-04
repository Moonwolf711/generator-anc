/* Generator-ANC bench data: pin map, wires, serial commands.
   Plain JS — attaches to window.ANC. */
(function () {
  // Wire roles -> colors (carried through the whole UI)
  const ROLE = {
    power: { key: "power", label: "Power", color: "#fb5607" },
    gnd:   { key: "gnd",   label: "Ground", color: "#7fdfff" },
    clock: { key: "clock", label: "Clock", color: "#ffb703" },
    data:  { key: "data",  label: "Data",  color: "#4cc9f0" },
    spkr:  { key: "spkr",  label: "Speaker", color: "#2dd4a7" },
  };

  // The 5 wires: Teensy 4.0 -> MAX98357A
  const WIRES = [
    { id: "w1", role: "clock", from: "21",  to: "BCLK", group: "signal",
      desc: "I\u00B2S bit clock \u2014 ticks every audio bit." },
    { id: "w2", role: "clock", from: "20",  to: "LRC",  group: "signal",
      desc: "I\u00B2S word-select \u2014 left/right frame clock." },
    { id: "w3", role: "data",  from: "7",   to: "DIN",  group: "signal",
      desc: "I\u00B2S data \u2014 the actual anti-noise samples." },
    { id: "w4", role: "power", from: "5V",  to: "Vin",  group: "power",
      desc: "Powers the amp. 5V is fine even though the Teensy logic is 3.3V." },
    { id: "w5", role: "gnd",   from: "GND", to: "GND",  group: "power",
      desc: "Common ground \u2014 nothing works without it." },
  ];

  // Teensy 4.0 green-module pin headers (as printed on the board)
  const TEENSY_TOP = ["5V","3V","23","22","21","20","19","18","17","16","15","14"];
  const TEENSY_BOT = ["G","0","1","2","3","4","5","6","7","8","9","10","11"];
  // which printed labels are "live" in this build, mapped to a wire role
  const TEENSY_USED = {
    "5V":  "power",
    "G":   "gnd",   // GND
    "21":  "clock",
    "20":  "clock",
    "7":   "data",
  };

  // MAX98357A header, left -> right as printed on the user's board
  const MAX_PINS = ["Vin","GND","SD","GAIN","DIN","BCLK","LRC"];
  const MAX_USED = {
    "Vin":  "power",
    "GND":  "gnd",
    "DIN":  "data",
    "BCLK": "clock",
    "LRC":  "clock",
  };
  const MAX_LEAVE_EMPTY = ["SD","GAIN"]; // intentionally unconnected

  // Serial commands the firmware understands (engine-order ANC controller)
  const COMMANDS = [
    { key: "c", name: "Calibrate", color: "#ffb703",
      blurb: "Sweep the speaker 30\u2192300 Hz with the engine OFF and estimate the secondary path \u015C (speaker\u2192mic).",
      need: "wired" },
    { key: "r", name: "Run ANC", color: "#2dd4a7",
      blurb: "Start the engine-order controller. It generates anti-noise locked to the firing frequency.",
      need: "calibrated" },
    { key: "s", name: "Stop", color: "#fb5607",
      blurb: "Mute the output and hold the controller.", need: "wired" },
    { key: "?", name: "Status", color: "#8aa0b6",
      blurb: "Print firmware state, sample rate, and current \u015C confidence.", need: "wired" },
  ];

  // Bench-test checklist that confirms the full loop before touching the generator
  const BENCH_STEPS = [
    "Engine OFF, speaker on the green screw terminal",
    "Send  c  \u2014 speaker sweeps 30\u2192300 Hz",
    "SM58 hears the sweep, \u015C prints with rising confidence",
    "Send  r  \u2014 controller starts, residual drops",
  ];

  // Project status (from the build log)
  const STATUS = [
    { label: "Firmware", state: "done",   note: "Compiled & flashed \u2014 MAX98357A output mode" },
    { label: "Output side", state: "active", note: "MAX98357A \u2192 speaker (5 wires)" },
    { label: "Input side", state: "active", note: "SM58 \u2192 Komplete Audio 6 \u2192 bias \u2192 A2" },
    { label: "Bench loop", state: "todo",  note: "Send c, confirm sweep + \u015C" },
  ];

  window.ANC = { ROLE, WIRES, TEENSY_TOP, TEENSY_BOT, TEENSY_USED,
    MAX_PINS, MAX_USED, MAX_LEAVE_EMPTY, COMMANDS, BENCH_STEPS, STATUS };
})();
