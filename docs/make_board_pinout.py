"""Annotate the actual BurgessWorld Teensy4+ESP-12E photo with the generator-anc connections."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch
from PIL import Image

IMG = r"C:\Users\Owner\OneDrive\Desktop\teensy.jpg"
im = Image.open(IMG); W, H = im.size

fig, ax = plt.subplots(figsize=(15, 11))
ax.imshow(im); ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis("off")

# (function, silkscreen label, target frac (x,y), callout frac (x,y), color)
PINS = [
    ("ANTI-NOISE OUT", "header  MISO 12  (top row)\n-> 1k + 10nF + 1uF -> amp RCA",
     (0.355, 0.135), (0.16, 0.015), "#ffb703"),
    ("TACH IN", "header  2  (top row, in 6 5 4 3 2)\n<- spark clamp, 3.3V conditioned",
     (0.575, 0.135), (0.74, 0.015), "#ff006e"),
    ("MIC IN", "header  A2 SCL1 RX4  (bottom row)\n<- MAX4466 preamp output",
     (0.570, 0.90), (0.60, 0.99), "#4cc9f0"),
    ("3V3  (mic power)", "header  3V3  (bottom row)\n-> MAX4466 VCC",
     (0.355, 0.90), (0.13, 0.99), "#fb5607"),
    ("GND  (common)", "header  GND  (top OR bottom row)",
     (0.285, 0.135), (0.02, 0.20), "#90e0ef"),
    ("ESP-12E WiFi", "telemetry bridge on Serial1 (pins 0/1)\nalready wired -> live dashboard",
     (0.80, 0.42), (0.90, 0.30), "#8ecae6"),
]

for fn, lab, (tx, ty), (cx, cy), col in PINS:
    txd, tyd = tx*W, ty*H
    cxd, cyd = cx*W, cy*H
    ax.add_patch(FancyArrowPatch((cxd, cyd), (txd, tyd), arrowstyle="-|>",
                                 mutation_scale=22, lw=2.6, color=col,
                                 connectionstyle="arc3,rad=0.15"))
    ax.text(cxd, cyd, f" {fn}\n {lab} ", ha="center",
            va=("bottom" if cy < 0.5 else "top"),
            color="#0d1b2a", fontsize=9.5, fontweight="bold",
            family="monospace",
            bbox=dict(boxstyle="round,pad=0.4", fc=col, ec="white", lw=1.5))

ax.text(W*0.5, H*0.50, "generator-anc  wiring  ·  BurgessWorld Teensy4 + ESP-12E",
        ha="center", va="center", color="white", fontsize=13, fontweight="bold",
        bbox=dict(boxstyle="round,pad=0.5", fc="#0d1b2acc", ec="#4cc9f0", lw=2))

plt.tight_layout()
plt.savefig("docs/board_pinout.png", dpi=120, bbox_inches="tight", facecolor="white")
print(f"wrote docs/board_pinout.png  (source {W}x{H})")
