"""Annotate the user's ACTUAL Teensy + MAX98357A photos, zoomed to each pin header."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import ConnectionPatch, Circle
from PIL import Image

TEENSY = r"C:\Users\Owner\OneDrive\Desktop\teensy.jpg"
MAXP   = r"C:\Users\Owner\Downloads\IMG_0523.jpeg"
C_BG="#0d1b2a"; C_TXT="#e0e1dd"
C_PWR="#fb5607"; C_GND="#90e0ef"; C_CLK="#ffb703"; C_DAT="#4cc9f0"

tim = Image.open(TEENSY); tw, th = tim.size
mim = Image.open(MAXP);   mw, mh = mim.size

fig = plt.figure(figsize=(17, 9.5)); fig.patch.set_facecolor(C_BG)
ax1 = fig.add_axes([0.02, 0.06, 0.52, 0.84]); ax1.imshow(tim); ax1.axis("off")
ax2 = fig.add_axes([0.58, 0.06, 0.40, 0.84]); ax2.imshow(mim); ax2.axis("off")
# zoom each to its header region
ax1.set_xlim(0.02*tw, 0.40*tw); ax1.set_ylim(0.74*th, 0.22*th)   # Teensy green module
ax2.set_xlim(0.22*mw, 0.78*mw); ax2.set_ylim(0.52*mh, 0.20*mh)   # MAX board + pins
ax1.set_title("YOUR TEENSY 4.0  (green chip pins)", color=C_DAT, fontsize=15, fontweight="bold")
ax2.set_title("YOUR MAX98357A", color=C_CLK, fontsize=15, fontweight="bold")
fig.text(0.5, 0.955, "exact wiring — match the PRINTED pin label on each board",
         ha="center", color=C_TXT, fontsize=18, fontweight="bold")

# Teensy green-module pin coords (fraction of full image)
T = {"5V":(0.062,0.335,C_PWR,"left"), "GND":(0.062,0.650,C_GND,"left"),
     "21":(0.131,0.345,C_CLK,"up"),   "20":(0.156,0.345,C_CLK,"up"),
     "7":(0.247,0.650,C_DAT,"down")}
# MAX header pin coords (fraction of full image)
M = {"Vin":(0.315,0.305,C_PWR), "GND":(0.380,0.305,C_GND),
     "DIN":(0.575,0.305,C_DAT), "BCLK":(0.640,0.305,C_CLK), "LRC":(0.705,0.305,C_CLK)}
WIRES = [("5V","Vin",C_PWR),("GND","GND",C_GND),("21","BCLK",C_CLK),("20","LRC",C_CLK),("7","DIN",C_DAT)]

def dot(ax, X, Y, c, W, H, r=0.010):
    ax.add_patch(Circle((X*W, Y*H), max(W,H)*r, fc=c, ec="white", lw=2.5, zorder=6))

off = {"left":(-0.055,0.0,"right"), "up":(0.0,-0.075,"center"),
       "down":(0.0,0.085,"center"), "right":(0.055,0.0,"left")}
for k,(x,y,c,d) in T.items():
    dot(ax1, x, y, c, tw, th)
    dx,dy,ha = off[d]
    ax1.annotate(k, (x*tw,y*th), xytext=((x+dx)*tw,(y+dy)*th), color=c, fontsize=17,
                 fontweight="bold", family="monospace", ha=ha, va="center",
                 arrowprops=dict(arrowstyle="-", color=c, lw=2.5),
                 bbox=dict(boxstyle="round,pad=0.3", fc=C_BG, ec=c, lw=2.5), zorder=7)
for k,(x,y,c) in M.items():
    dot(ax2, x, y, c, mw, mh)
    ax2.annotate(k, (x*mw,y*mh), xytext=(x*mw,(y-0.085)*mh), color=c, fontsize=17,
                 fontweight="bold", family="monospace", ha="center", va="center",
                 arrowprops=dict(arrowstyle="-", color=c, lw=2.5),
                 bbox=dict(boxstyle="round,pad=0.3", fc=C_BG, ec=c, lw=2.5), zorder=7)
for tp,mp,c in WIRES:
    tx,ty,_,_=T[tp]; mx,my,_=M[mp]
    fig.add_artist(ConnectionPatch(xyA=(tx*tw,ty*th), coordsA=ax1.transData,
                                   xyB=(mx*mw,my*mh), coordsB=ax2.transData,
                                   color=c, lw=2.5, alpha=0.7))

fig.text(0.5, 0.018, "GAIN + SD on MAX: leave EMPTY.   speaker -> green screw terminal.   "
         "Teensy is 3.3V (MAX Vin from 5V is fine).", ha="center", color=C_CLK, fontsize=11, fontstyle="italic")
fig.savefig("docs/annotated_boards.png", dpi=120, facecolor=C_BG)
print("wrote docs/annotated_boards.png")
