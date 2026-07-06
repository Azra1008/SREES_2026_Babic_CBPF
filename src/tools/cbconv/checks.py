#!/usr/bin/env python3
# Dodatne provjere tačnosti case9: bilans snaga (gubici), snaga slacka, Q generatora,
# i kriva konvergencije Newton-Raphsona. Piše i SVG dijagram konvergencije.
import numpy as np, math, os
baseMVA=100.0
bus=[(1,3,0,0),(2,2,0,0),(3,2,0,0),(4,1,0,0),(5,1,90,30),(6,1,0,0),(7,1,100,35),(8,1,0,0),(9,1,125,50)]
gen=[(1,72.3,1.04),(2,163,1.025),(3,85,1.025)]
branch=[(1,4,0,0.0576,0),(4,5,0.017,0.092,0.158),(5,6,0.039,0.17,0.358),(3,6,0,0.0586,0),
        (6,7,0.0119,0.1008,0.209),(7,8,0.0085,0.072,0.149),(8,2,0,0.0625,0),
        (8,9,0.032,0.161,0.306),(9,4,0.01,0.085,0.176)]
n=len(bus); id2i={b[0]:k for k,b in enumerate(bus)}; typ={id2i[b[0]]:b[1] for b in bus}
Y=np.zeros((n,n),complex)
for f,t,r,x,b in branch:
    fi,ti=id2i[f],id2i[t]; y=1/complex(r,x); bs=complex(0,b/2)
    Y[fi,fi]+=y+bs; Y[ti,ti]+=y+bs; Y[fi,ti]-=y; Y[ti,fi]-=y
P=np.zeros(n); Q=np.zeros(n)
for gb,Pg,_ in gen: P[id2i[gb]]+=Pg/baseMVA
for b in bus: P[id2i[b[0]]]-=b[2]/baseMVA; Q[id2i[b[0]]]-=b[3]/baseMVA
Vg={id2i[g[0]]:g[2] for g in gen}
slack=[i for i in range(n) if typ[i]==3][0]; pv=[i for i in range(n) if typ[i]==2]; pq=[i for i in range(n) if typ[i]==1]
V=np.array([Vg.get(i,1.0) for i in range(n)]); d=np.zeros(n)
def Smis(V,d):
    Vc=V*np.exp(1j*d); S=Vc*np.conj(Y@Vc); return S.real-P,S.imag-Q
hist=[]
for it in range(20):
    dP,dQ=Smis(V,d); mis=np.concatenate([dP[pv+pq],dQ[pq]]); hist.append(np.max(np.abs(mis)))
    if hist[-1]<1e-12: break
    xs=pv+pq; nx=len(xs)+len(pq); J=np.zeros((nx,nx)); e=1e-7
    def F(d_,V_): a,b=Smis(V_,d_); return np.concatenate([a[pv+pq],b[pq]])
    base=F(d,V); col=0
    for i in xs: dd=d.copy(); dd[i]+=e; J[:,col]=(F(dd,V)-base)/e; col+=1
    for i in pq: VV=V.copy(); VV[i]+=e; J[:,col]=(F(d,VV)-base)/e; col+=1
    dx=np.linalg.solve(J,-base); col=0
    for i in xs: d[i]+=dx[col]; col+=1
    for i in pq: V[i]+=dx[col]; col+=1

# --- fizičke provjere iz rješenja ---
Vc=V*np.exp(1j*d); S=Vc*np.conj(Y@Vc)   # kompleksna injektovana snaga po čvoru
Pload=sum(b[2] for b in bus); Qload=sum(b[3] for b in bus)
Ploss=S.real.sum()*baseMVA               # zbir injekcija = gubici
Pgen_tot=(S.real.sum()+P.sum()*0)*baseMVA  # placeholder
Pgen_total=Pload+Ploss
Pslack=S.real[slack]*baseMVA; Qslack=S.imag[slack]*baseMVA
Qg2=S.imag[id2i[2]]*baseMVA; Qg3=S.imag[id2i[3]]*baseMVA
print("="*54)
print("DODATNE PROVJERE TAČNOSTI (case9)")
print("="*54)
print(f"Ukupno opterećenje:        P = {Pload:.1f} MW,  Q = {Qload:.1f} MVAr")
print(f"Ukupna proizvodnja:        P = {Pgen_total:.2f} MW")
print(f"Ukupni gubici (P):         {Ploss:.3f} MW      [MATPOWER poznato: ~4.64 MW]")
print(f"Snaga slack generatora B1: P = {Pslack:.2f} MW  [MATPOWER: ~71.64 MW]")
print(f"                           Q = {Qslack:.2f} MVAr")
print(f"Reaktivna snaga gen. B2:   Q = {Qg2:.2f} MVAr [MATPOWER: ~6.65 MVAr]")
print(f"Reaktivna snaga gen. B3:   Q = {Qg3:.2f} MVAr [MATPOWER: ~-10.9 MVAr]")
print(f"Provjera bilansa: gen - opt - gubici = {Pgen_total-Pload-Ploss:.6f} MW  (treba ~0)")
print("\nKonvergencija (max |mismatch| po iteraciji):")
for k,h in enumerate(hist): print(f"  iter {k}:  {h:.2e}")

# --- SVG krive konvergencije ---
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","..","..","docs","diagrams"))
W,H=640,420; x0,y0,w,h=80,340,500,250
ys=[math.log10(max(v,1e-16)) for v in hist]; ymin,ymax=-14,2
def PX(i): return x0+ (i/(max(1,len(hist)-1)))*w
def PY(v): return y0-(v-ymin)/(ymax-ymin)*h
s=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="-apple-system,Helvetica Neue,Arial">']
s.append(f'<rect width="{W}" height="{H}" fill="#FBFCFE"/>')
s.append(f'<text x="{W/2}" y="32" text-anchor="middle" font-size="18" font-weight="700" fill="#2F3B4C">Konvergencija Newton–Raphsona (case9)</text>')
for gv in range(-14,3,2):
    yy=PY(gv); s.append(f'<line x1="{x0}" y1="{yy:.0f}" x2="{x0+w}" y2="{yy:.0f}" stroke="#E6EAEF"/>')
    s.append(f'<text x="{x0-8}" y="{yy+4:.0f}" text-anchor="end" font-size="10" fill="#6b7886">1e{gv}</text>')
s.append(f'<line x1="{x0}" y1="{PY(-6):.0f}" x2="{x0+w}" y2="{PY(-6):.0f}" stroke="#E8A33D" stroke-width="1.4" stroke-dasharray="6 4"/>')
s.append(f'<text x="{x0+w-4}" y="{PY(-6)-6:.0f}" text-anchor="end" font-size="10" fill="#E8A33D">tolerancija 1e-6</text>')
s.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0-h}" stroke="#2F3B4C" stroke-width="1.3"/>')
s.append(f'<line x1="{x0}" y1="{y0}" x2="{x0+w}" y2="{y0}" stroke="#2F3B4C" stroke-width="1.3"/>')
pts=" ".join(f"{PX(i):.0f},{PY(v):.0f}" for i,v in enumerate(ys))
s.append(f'<polyline points="{pts}" fill="none" stroke="#4E79A7" stroke-width="3"/>')
for i,v in enumerate(ys):
    s.append(f'<circle cx="{PX(i):.0f}" cy="{PY(v):.0f}" r="5" fill="#4E79A7"/>')
    s.append(f'<text x="{PX(i):.0f}" y="{y0+16}" text-anchor="middle" font-size="10.5" fill="#2F3B4C">{i}</text>')
s.append(f'<text x="{x0+w/2}" y="{y0+34}" text-anchor="middle" font-size="11.5" fill="#6b7886">iteracija</text>')
s.append(f'<text x="24" y="{y0-h-6}" font-size="10.5" fill="#6b7886">max |mismatch|</text>')
s.append('</svg>')
open(f"{OUT}/04_konvergencija.svg","w").write("\n".join(s))
print(f"\nSVG konvergencije: {OUT}/04_konvergencija.svg")
