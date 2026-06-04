"""Focused close-up: exact MAX98357A <-> Teensy 4.0 wiring (5 wires)."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

C_BG="#0d1b2a"; C_TXT="#e0e1dd"; C_BOX="#16263a"
C_CLK="#ffb703"; C_DAT="#4cc9f0"; C_PWR="#fb5607"; C_GND="#90e0ef"; C_NC="#5a6a7a"

fig, ax = plt.subplots(figsize=(12, 8))
fig.patch.set_facecolor(C_BG); ax.set_facecolor(C_BG)
ax.set_xlim(0, 100); ax.set_ylim(0, 60); ax.axis("off")
ax.text(50, 57, "MAX98357A  <->  Teensy 4.0   —   exact wiring", ha="center",
        color=C_TXT, fontsize=19, fontweight="bold")
ax.text(50, 53, "5 wires.  GAIN + SD left floating.", ha="center", color=C_NC, fontsize=12)

# rows: (max pin, teensy pin, color, note)
rows = [
    ("Vin",  "5V",  C_PWR, ""),
    ("GND",  "GND", C_GND, ""),
    ("BCLK", "21",  C_CLK, "bit clock"),
    ("LRC",  "20",  C_CLK, "word clock"),
    ("DIN",  "7",   C_DAT, "audio data"),
    ("GAIN", "n/c", C_NC,  "= 9 dB (default)"),
    ("SD",   "n/c", C_NC,  "if silent -> 3V3"),
]

y0, dy = 44, 5.6
xL, xR, w = 14, 64, 22

# headers
ax.add_patch(FancyBboxPatch((xL, y0+3), w, 3, boxstyle="round,pad=0.2", fc=C_BOX, ec=C_TXT, lw=2))
ax.text(xL+w/2, y0+4.5, "MAX98357A", ha="center", va="center", color="#ffb703", fontsize=13, fontweight="bold")
ax.add_patch(FancyBboxPatch((xR, y0+3), w, 3, boxstyle="round,pad=0.2", fc=C_BOX, ec=C_TXT, lw=2))
ax.text(xR+w/2, y0+4.5, "TEENSY 4.0", ha="center", va="center", color="#4cc9f0", fontsize=13, fontweight="bold")

for i, (mp, tp, col, note) in enumerate(rows):
    y = y0 - i*dy
    # left pad (MAX pin)
    ax.add_patch(FancyBboxPatch((xL, y-1.6), w, 3.2, boxstyle="round,pad=0.2", fc=C_BOX, ec=col, lw=2))
    ax.text(xL+w-1.5, y, mp, ha="right", va="center", color=C_TXT, fontsize=14, fontweight="bold", family="monospace")
    # right pad (Teensy pin)
    ax.add_patch(FancyBboxPatch((xR, y-1.6), w, 3.2, boxstyle="round,pad=0.2", fc=C_BOX, ec=col, lw=2))
    ax.text(xR+1.5, y, tp, ha="left", va="center", color=C_TXT, fontsize=14, fontweight="bold", family="monospace")
    if tp != "n/c":
        ax.add_patch(FancyArrowPatch((xL+w, y), (xR, y), arrowstyle="-", lw=3.5, color=col))
    else:
        ax.plot([xL+w, xL+w+6], [y, y], lw=2, ls=":", color=col)
    if note:
        ax.text(50, y, note, ha="center", va="center", color=col, fontsize=10, fontstyle="italic")

ax.text(50, 3, "Teensy is 3.3V — MAX98357A Vin from 5V is fine (it has its own regulator).",
        ha="center", color=C_OUT if (C_OUT:='#ffb703') else C_TXT, fontsize=10, fontstyle="italic")

plt.tight_layout()
plt.savefig("docs/teensy_max98357a.png", dpi=130, facecolor=C_BG)
print("wrote docs/teensy_max98357a.png")
