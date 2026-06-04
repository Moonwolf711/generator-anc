"""Generate the generator-anc wiring blueprint (no-shield: Teensy MQS out + ADC mic in)."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

C_BG = "#0d1b2a"; C_BOX = "#1b2a3a"; C_EDGE = "#4cc9f0"; C_TXT = "#e0e1dd"
C_ACC = "#ffb703"; C_SIG = "#8ecae6"; C_PWR = "#fb5607"; C_TACH = "#ff006e"

fig, ax = plt.subplots(figsize=(15, 9))
fig.patch.set_facecolor(C_BG); ax.set_facecolor(C_BG)
ax.set_xlim(0, 100); ax.set_ylim(0, 62); ax.axis("off")

def box(x, y, w, h, title, lines, edge=C_EDGE, tcol=C_ACC):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.3,rounding_size=0.8",
                                fc=C_BOX, ec=edge, lw=2.2))
    ax.text(x + w/2, y + h - 1.4, title, ha="center", va="top", color=tcol,
            fontsize=11, fontweight="bold")
    for i, ln in enumerate(lines):
        ax.text(x + 1.2, y + h - 3.6 - i*2.0, ln, ha="left", va="top",
                color=C_TXT, fontsize=8.6, family="monospace")

def wire(p1, p2, color=C_SIG, label="", ls="-", rad=0.0):
    a = FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=16, lw=2.0,
                        color=color, ls=ls, connectionstyle=f"arc3,rad={rad}")
    ax.add_patch(a)
    if label:
        mx, my = (p1[0]+p2[0])/2, (p1[1]+p2[1])/2
        ax.text(mx, my+1.1, label, ha="center", va="bottom", color=color,
                fontsize=8, family="monospace", fontweight="bold")

ax.text(50, 60.4, "generator-anc  —  engine-order ANC wiring blueprint", ha="center",
        color=C_TXT, fontsize=16, fontweight="bold")
ax.text(50, 57.8, "Champion 4250 inverter genset  ·  Teensy 4.0 (no Audio Shield: MQS out + ADC in)",
        ha="center", color=C_SIG, fontsize=10)

# --- components ---
box(2, 40, 22, 15, "CHAMPION 4250 GENSET",
    ["224cc single-cyl, inverter", "engine RPM varies w/ load", "(eco-throttle ~1800-3600)",
     "", "NOISE: orders 1-6", "  30 / 60 / 90 ... Hz", "SPARK: 1 pulse / revolution"], edge=C_PWR)

box(2, 22, 22, 13, "SPARK TACH PICKUP",
    ["inductive clamp on plug wire", "(timing-light style, no cut)", "  -> diode + RC clamp",
     "  -> comparator 0/3.3V", "** 3.3V MAX to Teensy **"], edge=C_TACH, tcol=C_TACH)

box(2, 4, 22, 14, "ERROR MIC + PREAMP",
    ["analog electret mic", "MAX4466 / MAX9814 preamp", "(NOT USB/Bluetooth!)",
     "output biased ~1.65V", "  -> Teensy A2 (pin 16)"], edge=C_SIG)

box(39, 24, 24, 22, "TEENSY 4.0  (EOC)",
    ["running engine_order_canceller", "", "IN  : A2  <- mic preamp",
     "TACH: pin 2 <- spark pulse", "OUT : pin 12 -> RC -> amp",
     "GND : common ground", "3V3 : sensor power", "",
     "phase-locks to each spark,", "tracks RPM, cancels orders 1-6",
     "send 'c' = calibrate S_hat"], edge=C_EDGE, tcol="#90e0ef")

box(72, 40, 26, 14, "RC RECONSTRUCTION FILTER",
    ["pin 12 (MQS) --[1k]--+--[1uF]--> amp RCA tip", "                     |",
     "                  [10nF]", "                     |",
     "                    GND  --> RCA sleeve", "(passes 30-360Hz, kills MQS hash)"], edge=C_ACC)

box(72, 22, 26, 14, "ALPINE AMP + 12\" SUB",
    ["sealed-box 12in subwoofer", "Alpine amp -> RCA in", "",
     "SET: LPF = MAX", "     bass-boost = OFF", "     subsonic = OFF, gain LOW"], edge=C_SIG)

box(72, 4, 26, 13, "POWER",
    ["Teensy : USB (PC) 5V", "Amp    : SEPARATE 12V battery", "         (NOT off the genset)",
     "IMP2 DI ground-lift on the", "line run = kills 60Hz hum"], edge=C_PWR, tcol=C_PWR)

# --- wires ---
wire((24, 47), (39, 40), C_PWR, "acoustic noise", rad=-0.1)      # genset -> teensy area (noise)
wire((24, 28), (39, 36), C_TACH, "spark pulse")                   # tach -> teensy pin2
wire((24, 11), (39, 30), C_SIG, "mic -> A2", rad=0.1)             # mic -> teensy A2
wire((63, 40), (72, 45), C_ACC, "pin 12 MQS")                     # teensy -> RC
wire((85, 40), (85, 36), C_ACC, "")                               # RC -> amp
wire((85, 22), (63, 30), C_SIG, "anti-noise (air)", ls="--", rad=0.15)  # sub -> back to mic zone

ax.text(50, 19.5, "QUIET ZONE: place sub + error mic where the ears are (local null, head-sized)",
        ha="center", color=C_ACC, fontsize=9.5, fontstyle="italic")
ax.text(50, 1.6, "github.com/Moonwolf711/generator-anc   ·   firmware flashed + verified on Teensy 4.0",
        ha="center", color="#5a6a7a", fontsize=8)

plt.tight_layout()
plt.savefig("docs/blueprint.png", dpi=130, facecolor=C_BG)
print("wrote docs/blueprint.png")
