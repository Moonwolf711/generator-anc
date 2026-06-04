"""Photo wiring diagram: real device images + the exact pin mapping between them."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
from PIL import Image

C_BG="#0d1b2a"; C_TXT="#e0e1dd"; C_MIC="#4cc9f0"; C_OUT="#ffb703"; C_PWR="#fb5607"; C_SPK="#90e0ef"

fig, ax = plt.subplots(figsize=(17, 10))
fig.patch.set_facecolor(C_BG); ax.set_facecolor(C_BG)
ax.set_xlim(0, 100); ax.set_ylim(0, 64); ax.axis("off")
ax.text(50, 62, "generator-anc  —  real-parts wiring (MAX98357A output build)",
        ha="center", color=C_TXT, fontsize=16, fontweight="bold")

def place(path, cx, cy, w, label, lcol):
    im = Image.open(path); iw, ih = im.size
    h = w * ih / iw
    ax.imshow(im, extent=[cx-w/2, cx+w/2, cy-h/2, cy+h/2], zorder=2, aspect="auto")
    ax.add_patch(FancyBboxPatch((cx-w/2, cy-h/2), w, h, boxstyle="round,pad=0.1",
                                fill=False, ec=lcol, lw=2.5, zorder=3))
    ax.text(cx, cy-h/2-1.4, label, ha="center", va="top", color=lcol,
            fontsize=11, fontweight="bold", zorder=4)
    return dict(l=cx-w/2, r=cx+w/2, t=cy+h/2, b=cy-h/2, cx=cx, cy=cy)

T  = place(r"C:\Users\Owner\OneDrive\Desktop\teensy.jpg", 50, 36, 46, "YOUR TEENSY4 + ESP-12E", C_TXT)
MA = place("docs/parts/max98357a.jpg", 86, 44, 17, "MAX98357A  (I2S amp)", C_OUT)
SP = place("docs/parts/speaker.jpg", 86, 17, 15, "small 4-8Ω speaker", C_SPK)
MI = place("docs/parts/sm58.jpg", 8, 42, 11, "SM58 (dynamic mic)", C_MIC)

def maplabel(x, y, title, rows, col, w=17):
    ax.add_patch(FancyBboxPatch((x, y), w, 2.0+len(rows)*1.9, boxstyle="round,pad=0.3",
                                fc="#10202f", ec=col, lw=2, zorder=5))
    ax.text(x+w/2, y+1.4+len(rows)*1.9, title, ha="center", va="top", color=col,
            fontsize=9.5, fontweight="bold", zorder=6)
    for i, r in enumerate(rows):
        ax.text(x+1, y+0.8+(len(rows)-1-i)*1.9, r, ha="left", va="bottom", color=C_TXT,
                fontsize=8, family="monospace", zorder=6)

def wire(p1, p2, col):
    ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=16, lw=2.4,
                                 color=col, connectionstyle="arc3,rad=0.12", zorder=4))

# SM58 -> Komplete -> A2
wire((MI["r"], MI["cy"]), (T["l"], T["cy"]+4), C_MIC)
maplabel(16, 41, "SM58 -> KOMPLETE A6 -> A2",
         ["SM58 XLR -> Komplete ch1", "48V OFF, DIRECT-MONITOR on",
          "LINE OUT -> 1uF -> A2", "A2 node: 10k->3V3 + 10k->GND",
          "GND -> GND   (start level LOW)"], C_MIC, w=23)

# Teensy -> MAX98357A (I2S)
wire((T["r"], T["cy"]+3), (MA["l"], MA["cy"]), C_OUT)
maplabel(60, 47, "TEENSY -> MAX98357A", ["21 -> BCLK", "20 -> LRC", "7  -> DIN",
                                          "5V -> Vin", "GND-> GND"], C_OUT)

# MAX98357A -> speaker
wire((MA["cx"], MA["b"]), (SP["cx"], SP["t"]), C_SPK)
maplabel(60, 20, "MAX98357A -> SPKR", ["+ -> speaker +", "-  -> speaker -"], C_SPK)

ax.text(50, 3.2, "go by the printed PIN NAMES (silkscreen) — arrows are approximate.   "
        "Tach: spark clamp (3.3V) -> pin 2.   3.3V ONLY.",
        ha="center", color=C_OUT, fontsize=9.5, fontstyle="italic")
ax.text(50, 0.6, "github.com/Moonwolf711/generator-anc", ha="center", color="#5a6a7a", fontsize=8)

plt.tight_layout()
plt.savefig("docs/photo_wiring.png", dpi=120, facecolor=C_BG)
print("wrote docs/photo_wiring.png")
