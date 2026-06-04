"""Full connection diagram: each Teensy pin -> the exact component terminal it wires to."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

C_BG="#0d1b2a"; C_BOX="#1b2a3a"; C_TXT="#e0e1dd"
C_MIC="#4cc9f0"; C_PWR="#fb5607"; C_GND="#90e0ef"; C_OUT="#ffb703"; C_TACH="#ff006e"

fig, ax = plt.subplots(figsize=(16, 10))
fig.patch.set_facecolor(C_BG); ax.set_facecolor(C_BG)
ax.set_xlim(0, 100); ax.set_ylim(0, 66); ax.axis("off")
ax.text(50, 64, "generator-anc  —  every connection, end to end", ha="center",
        color=C_TXT, fontsize=17, fontweight="bold")
ax.text(50, 61, "BurgessWorld Teensy4 + ESP-12E   ·   wire each board pin to the terminal shown",
        ha="center", color=C_MIC, fontsize=10)

def box(x,y,w,h,title,rows,edge):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle="round,pad=0.3,rounding_size=0.8",
                                fc=C_BOX,ec=edge,lw=2.4))
    ax.text(x+w/2,y+h-1.3,title,ha="center",va="top",color=edge,fontsize=11,fontweight="bold")
    pts={}
    for i,(name,desc) in enumerate(rows):
        yy=y+h-3.6-i*2.3
        ax.text(x+1.2,yy,name,ha="left",va="center",color=C_TXT,fontsize=9,
                family="monospace",fontweight="bold")
        ax.text(x+w-1.2,yy,desc,ha="right",va="center",color="#9fb3c8",fontsize=7.5,family="monospace")
        parts=name.split()
        if parts: pts[parts[0]]=(x, yy)
    return pts

def wire(p1,p2,color,label="",rad=0.0):
    ax.add_patch(FancyArrowPatch(p1,p2,arrowstyle="-|>",mutation_scale=14,lw=2.2,
                                 color=color,connectionstyle=f"arc3,rad={rad}"))
    if label:
        ax.text((p1[0]+p2[0])/2,(p1[1]+p2[1])/2+0.9,label,ha="center",color=color,
                fontsize=7.5,family="monospace",fontweight="bold")

# ---- CENTER: Teensy pins ----
tb=box(40,20,20,30,"TEENSY  (carrier)",
    [("A2","mic in (ADC)"),("3V3","sensor pwr"),("GND","common"),
     ("12","MQS audio out"),("2","tach in"),("0/1","ESP (do not use)")],C_MIC)
def tp(pin,side="L"):
    x,y=tb[pin]; return (40 if side=="L" else 60, y)

# ---- LEFT: inputs ----
mb=box(4,38,24,12,"MAX4466 MIC PREAMP",
    [("OUT","-> A2"),("VCC","-> 3V3"),("GND","-> GND")],C_MIC)
sb=box(4,20,24,14,"SPARK TACH PICKUP",
    [("PULSE","-> pin 2 (0/3.3V)"),("GND","-> GND"),
     ("clamp","on plug wire"),("cond.","diode+RC+comparator")],C_TACH)

wire((28,mb["OUT"][1]),(40,tb["A2"][1]),C_MIC,"mic")
wire((28,mb["VCC"][1]),(40,tb["3V3"][1]),C_PWR,"3V3")
wire((28,mb["GND"][1]),(40,tb["GND"][1]),C_GND,"gnd",rad=0.05)
wire((28,sb["PULSE"][1]),(40,tb["2"][1]),C_TACH,"spark")
wire((28,sb["GND"][1]),(40,tb["GND"][1]),C_GND,"",rad=-0.1)

# ---- RIGHT: output chain ----
rc=box(64,40,32,10,"RC FILTER  (3 parts)",
    [("IN","pin 12 -[1k]-+-> OUT"),("","         +-[10nF]-> GND"),("OUT","-[1uF]-> amp RCA tip")],C_OUT)
am=box(64,24,32,13,"ALPINE AMP",
    [("RCA tip","<- RC OUT"),("RCA gnd","-> Teensy GND"),
     ("SPKR +/-","-> sub +/-"),("12V +/-","<- battery"),("set","LPF max, boost OFF")],C_OUT)
su=box(64,12,15,9,"12in SUB",
    [("+","amp SPKR+"),("-","amp SPKR-")],C_MIC)
ba=box(82,12,14,9,"12V BATTERY",
    [("+","amp 12V+"),("-","amp 12V-")],C_PWR)

wire((60,tb["12"][1]),(64,rc["IN"][1]),C_OUT,"pin12")
wire((80,40),(80,37),C_OUT,"")               # RC -> amp
wire((64,am["RCA"][1]),(60,tb["GND"][1]),C_GND,"amp gnd",rad=0.2)
wire((72,24),(72,21),C_OUT,"spkr")            # amp -> sub
wire((89,24),(89,21),C_PWR,"12V")             # battery -> amp

ax.text(50,8.5,"QUIET ZONE: sub + mic together, where the ears are.   ESP-12E (pins 0/1) = WiFi telemetry, pre-wired.",
        ha="center",color=C_OUT,fontsize=9.5,fontstyle="italic")
ax.text(50,5.2,"Teensy 3.3V ONLY — never feed raw spark/line voltage to a pin.",
        ha="center",color=C_TACH,fontsize=9,fontweight="bold")
ax.text(50,2.2,"github.com/Moonwolf711/generator-anc",ha="center",color="#5a6a7a",fontsize=8)

plt.tight_layout()
plt.savefig("docs/connections.png",dpi=125,facecolor=C_BG)
print("wrote docs/connections.png")
