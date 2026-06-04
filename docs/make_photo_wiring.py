"""Photo wiring diagram: real device images on top, big readable connection tables below."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
from PIL import Image

C_BG="#0d1b2a"; C_TXT="#e0e1dd"; C_MIC="#4cc9f0"; C_OUT="#ffb703"; C_SPK="#90e0ef"; C_BOX="#10202f"

fig, ax = plt.subplots(figsize=(18, 11))
fig.patch.set_facecolor(C_BG); ax.set_facecolor(C_BG)
ax.set_xlim(0, 100); ax.set_ylim(0, 70); ax.axis("off")
ax.text(50, 68, "generator-anc  —  bench wiring (your parts)", ha="center",
        color=C_TXT, fontsize=20, fontweight="bold")

def photo(path, cx, cy, w, cap, col):
    im = Image.open(path); iw, ih = im.size; h = w*ih/iw
    ax.imshow(im, extent=[cx-w/2, cx+w/2, cy-h/2, cy+h/2], zorder=2, aspect="auto")
    ax.add_patch(FancyBboxPatch((cx-w/2, cy-h/2), w, h, boxstyle="round,pad=0.1",
                                fill=False, ec=col, lw=2.5, zorder=3))
    ax.text(cx, cy-h/2-1.0, cap, ha="center", va="top", color=col, fontsize=11, fontweight="bold")

def flowbox(cx, cy, w, h, text, col):
    ax.add_patch(FancyBboxPatch((cx-w/2, cy-h/2), w, h, boxstyle="round,pad=0.3",
                                fc=C_BOX, ec=col, lw=2.5))
    ax.text(cx, cy, text, ha="center", va="center", color=col, fontsize=12, fontweight="bold")

def arrow(x1, x2, y, col):
    ax.add_patch(FancyArrowPatch((x1, y), (x2, y), arrowstyle="-|>", mutation_scale=22, lw=3, color=col))

# ---- top: signal-flow photo row ----
photo("docs/parts/sm58.jpg", 9, 54, 8, "SM58", C_MIC)
flowbox(24, 54, 12, 8, "KOMPLETE\nAUDIO 6\n(preamp)", C_MIC)
photo(r"C:\Users\Owner\OneDrive\Desktop\teensy.jpg", 50, 54, 26, "YOUR TEENSY4 + ESP-12E", C_TXT)
photo("docs/parts/max98357a.jpg", 78, 56, 12, "MAX98357A", C_OUT)
photo("docs/parts/speaker.jpg", 92, 50, 10, "speaker", C_SPK)
for x1, x2, col in [(13,18,C_MIC),(30,37,C_MIC),(63,72,C_OUT),(84,87,C_SPK)]:
    arrow(x1, x2, 54, col)

# ---- bottom: three big readable connection tables ----
def table(x, w, title, rows, col):
    h = 3.2 + len(rows)*2.6
    ax.add_patch(FancyBboxPatch((x, 38-h), w, h, boxstyle="round,pad=0.4",
                                fc=C_BOX, ec=col, lw=3))
    ax.text(x+w/2, 38-2.0, title, ha="center", va="top", color=col, fontsize=14, fontweight="bold")
    for i, r in enumerate(rows):
        ax.text(x+1.5, 38-5.2-i*2.6, r, ha="left", va="center", color=C_TXT,
                fontsize=12, family="monospace")

table(2, 31, "1 · MIC IN  (SM58 -> A2)", [
    "SM58  --XLR-->  Komplete ch.1",
    "  48V phantom OFF",
    "  DIRECT-MONITOR on (analog)",
    "Komplete LINE OUT --1uF--> A2",
    "A2 node: 10k->3V3 + 10k->GND",
    "Komplete GND ----------> GND",
    ">> start output level LOW <<",
], C_MIC)

table(35, 30, "2 · AUDIO OUT  (Teensy -> MAX98357A)", [
    "pin 21  ----->  BCLK",
    "pin 20  ----->  LRC",
    "pin 7   ----->  DIN",
    "5V      ----->  Vin",
    "GND     ----->  GND",
    "(SD + GAIN: leave floating)",
], C_OUT)

table(67, 31, "3 · SPEAKER + TACH", [
    "MAX98357A + --> speaker +",
    "MAX98357A - --> speaker -",
    "  (small 4-8 ohm speaker)",
    "",
    "spark clamp --> pin 2",
    "  (3.3V conditioned ONLY)",
    "tach GND ----> GND",
], C_SPK)

ax.text(50, 1.3, "Teensy is 3.3V ONLY.   Engine off -> send 'c' -> speaker sweeps 30-300Hz, SM58 hears it, S_hat prints.",
        ha="center", color=C_OUT, fontsize=11, fontstyle="italic")

plt.tight_layout()
plt.savefig("docs/photo_wiring.png", dpi=125, facecolor=C_BG)
print("wrote docs/photo_wiring.png")
