"""Animated demo of engine-order cancellation converging (synthetic, phase-coherent)."""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter

C_BG="#0d1b2a"; C_TXT="#e0e1dd"; C_D="#6a7a8a"; C_E="#4cc9f0"; C_ACC="#ffb703"

fs=2000; f0=30.0; H=6; T=5.0; N=int(T*fs); t=np.arange(N)/fs
rng=np.random.default_rng(3)
amp=np.array([0,1.0,0.7,0.5,0.6,0.4,0.55]); ph=0.4*np.arange(H+1)
d=sum(amp[h]*np.cos(2*np.pi*h*f0*t+ph[h]) for h in range(1,H+1)) + 0.05*rng.standard_normal(N)

# secondary path + realistic S_hat
M=32; k=np.arange(M); Sfir=np.where(k>=2,np.exp(-(k-2)/(0.002*fs)),0.0); Sfir/=np.abs(Sfir).sum()+1e-12
def Sresp(f):
    w=2*np.pi*f/fs; re=(Sfir*np.cos(w*k)).sum(); im=-(Sfir*np.sin(w*k)).sum()
    return np.hypot(re,im)*1.1, np.arctan2(im,re)+0.08
smag=np.zeros(H+1); sph=np.zeros(H+1)
for h in range(1,H+1): smag[h],sph[h]=Sresp(h*f0)

wc=np.zeros(H+1); ws=np.zeros(H+1); e=np.zeros(N); yh=np.zeros(M); mu=0.04
ordAmp=np.zeros((N,H+1))
for n in range(N):
    th=2*np.pi*f0*n/fs; y=0.0; cs=[(np.cos(h*th),np.sin(h*th)) for h in range(H+1)]
    for h in range(1,H+1): y+=wc[h]*cs[h][0]+ws[h]*cs[h][1]
    yh[1:]=yh[:-1]; yh[0]=y; e[n]=d[n]+Sfir@yh
    for h in range(1,H+1):
        c,s=cs[h]; xc=smag[h]*(c*np.cos(sph[h])-s*np.sin(sph[h])); xs=smag[h]*(s*np.cos(sph[h])+c*np.sin(sph[h]))
        me=mu/(0.5*smag[h]**2*H+1e-12); wc[h]=(1-0.000001)*wc[h]-me*e[n]*xc; ws[h]=(1-0.000001)*ws[h]-me*e[n]*xs
    ordAmp[n]=np.sqrt(wc**2+ws**2)

# per-order cancellation dB vs time (windowed)
win=int(0.3*fs)
def odb(n):
    a=max(0,n-win);
    if n-a<50: return np.zeros(H+1)
    out=np.zeros(H+1)
    for h in range(1,H+1):
        f=h*f0; w=2*np.pi*f/fs; nn=np.arange(a,n)
        Ib=np.sum(d[a:n]*np.cos(w*nn)); Qb=np.sum(d[a:n]*np.sin(w*nn))
        Ie=np.sum(e[a:n]*np.cos(w*nn)); Qe=np.sum(e[a:n]*np.sin(w*nn))
        out[h]=10*np.log10((Ib*Ib+Qb*Qb)/((Ie*Ie+Qe*Qe)+1e-9)+1e-9)
    return out

fig,(ax1,ax2)=plt.subplots(2,1,figsize=(11,7),height_ratios=[2,1])
fig.patch.set_facecolor(C_BG)
for a in (ax1,ax2):
    a.set_facecolor("#10202f")
    for sp in a.spines.values(): sp.set_color("#33444f")
    a.tick_params(colors=C_TXT)
fig.suptitle("generator-anc  —  engine-order cancellation converging",color=C_TXT,fontsize=14,fontweight="bold")

FR=70; idx=np.linspace(int(0.05*N),N-1,FR).astype(int)
def frame(i):
    n=idx[i]; ax1.clear(); ax2.clear()
    ax1.set_facecolor("#10202f"); ax2.set_facecolor("#10202f")
    ax1.plot(t[:n],d[:n],color=C_D,lw=0.7,label="generator drone (disturbance)")
    ax1.plot(t[:n],e[:n],color=C_E,lw=0.8,label="residual (controller ON)")
    ax1.set_xlim(0,T); ax1.set_ylim(-3.2,3.2)
    ax1.set_ylabel("amplitude",color=C_TXT); ax1.legend(loc="upper right",fontsize=8,facecolor="#10202f",labelcolor=C_TXT)
    rms_d=np.sqrt(np.mean(d[max(0,n-win):n]**2)+1e-9); rms_e=np.sqrt(np.mean(e[max(0,n-win):n]**2)+1e-9)
    ax1.set_title(f"t={t[n]:.1f}s   broadband {20*np.log10(rms_d/rms_e):+.0f} dB",color=C_ACC,fontsize=11)
    db=odb(n)
    ax2.bar(range(1,H+1),db[1:],color=C_E,edgecolor=C_ACC)
    ax2.set_xlim(0.3,H+0.7); ax2.set_ylim(0,55)
    ax2.set_xlabel("engine order",color=C_TXT); ax2.set_ylabel("cancel (dB)",color=C_TXT)
    ax2.set_xticks(range(1,H+1)); ax2.set_xticklabels([f"{h}\n{int(h*f0)}Hz" for h in range(1,H+1)],fontsize=7,color=C_TXT)
    for sp in (*ax1.spines.values(),*ax2.spines.values()): sp.set_color("#33444f")
    ax1.tick_params(colors=C_TXT); ax2.tick_params(colors=C_TXT)
    return []

an=FuncAnimation(fig,frame,frames=FR,blit=False)
an.save("docs/demo.gif",writer=PillowWriter(fps=12),dpi=90)
print("wrote docs/demo.gif")
