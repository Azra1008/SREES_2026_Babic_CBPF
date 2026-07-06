#!/usr/bin/env python3
# Validacija formulacije strujnog balansa (polarno) na MATPOWER case9.
# 1) Riješi PF standardnim NR (balans snaga) -> referentno rješenje (V, delta).
# 2) Provjeri da su reziduali jednačina STRUJNOG BALANSA ~0 u tom rješenju.
# 3) Riješi sistem STRUJNOG BALANSA Newtonom iz flat starta -> isto rješenje.
import numpy as np

baseMVA = 100.0
# bus: id, type(1=PQ,2=PV,3=slack), Pd, Qd, Gs, Bs, Vm, Va
bus = [
    (1,3,0,0,0,0,1.0,0),(2,2,0,0,0,0,1.0,0),(3,2,0,0,0,0,1.0,0),
    (4,1,0,0,0,0,1.0,0),(5,1,90,30,0,0,1.0,0),(6,1,0,0,0,0,1.0,0),
    (7,1,100,35,0,0,1.0,0),(8,1,0,0,0,0,1.0,0),(9,1,125,50,0,0,1.0,0)]
# gen: bus, Pg, Qg, Vg
gen = [(1,72.3,27.03,1.04),(2,163,6.54,1.025),(3,85,-10.95,1.025)]
# branch: f,t,r,x,b
branch = [(1,4,0,0.0576,0),(4,5,0.017,0.092,0.158),(5,6,0.039,0.17,0.358),
          (3,6,0,0.0586,0),(6,7,0.0119,0.1008,0.209),(7,8,0.0085,0.072,0.149),
          (8,2,0,0.0625,0),(8,9,0.032,0.161,0.306),(9,4,0.01,0.085,0.176)]

n = len(bus)
id2i = {b[0]:k for k,b in enumerate(bus)}
typ = {id2i[b[0]]: b[1] for b in bus}

# Y-bus
Y = np.zeros((n,n), dtype=complex)
for f,t,r,x,b in branch:
    fi,ti = id2i[f], id2i[t]
    y = 1.0/complex(r,x); bsh = complex(0,b/2)
    Y[fi,fi]+= y+bsh; Y[ti,ti]+= y+bsh
    Y[fi,ti]-= y;     Y[ti,fi]-= y
for bb in bus:
    i=id2i[bb[0]]; Y[i,i]+= complex(bb[4],bb[5])/baseMVA

# injekcije
P = np.zeros(n); Q = np.zeros(n)
for gb,Pg,Qg,Vg in gen:
    i=id2i[gb]; P[i]+=Pg/baseMVA; Q[i]+=Qg/baseMVA
for bb in bus:
    i=id2i[bb[0]]; P[i]-=bb[2]/baseMVA; Q[i]-=bb[3]/baseMVA

Vg = {id2i[g[0]]: g[3] for g in gen}
slack = [i for i in range(n) if typ[i]==3][0]
pv = [i for i in range(n) if typ[i]==2]
pq = [i for i in range(n) if typ[i]==1]

Ymag = np.abs(Y); Yang = np.angle(Y)

def Icalc(V, d):
    # kompleksna proracunata struja I_i = sum_k Y_ik V_k
    Vc = V*np.exp(1j*d)
    return Y @ Vc

# ---------- 1) standardni NR (balans snaga, polarno) ----------
V = np.array([Vg.get(i, 1.0) for i in range(n)])
d = np.zeros(n)
def Smis(V,d):
    Vc=V*np.exp(1j*d); S=Vc*np.conj(Y@Vc)
    return S.real-P, S.imag-Q
for it in range(50):
    dP,dQ = Smis(V,d)
    mis = np.concatenate([dP[pv+pq], dQ[pq]])
    if np.max(np.abs(mis))<1e-12: break
    # numericki Jacobian
    xpv_pq = pv+pq
    nx=len(xpv_pq)+len(pq); J=np.zeros((nx,nx)); e=1e-7
    def F(d_,V_):
        a,b=Smis(V_,d_); return np.concatenate([a[pv+pq], b[pq]])
    base=F(d,V); col=0
    for i in (pv+pq):
        dd=d.copy(); dd[i]+=e; J[:,col]=(F(dd,V)-base)/e; col+=1
    for i in pq:
        VV=V.copy(); VV[i]+=e; J[:,col]=(F(d,VV)-base)/e; col+=1
    dx=np.linalg.solve(J,-base); col=0
    for i in (pv+pq): d[i]+=dx[col]; col+=1
    for i in pq: V[i]+=dx[col]; col+=1
print("== Referentno PF rjesenje (standardni NR, balans snaga) ==")
print("bus  |V|        delta[deg]")
for i in range(n):
    print(f" {bus[i][0]}  {V[i]:.6f}  {np.degrees(d[i]):+.4f}")
Vref, dref = V.copy(), d.copy()

# ---------- 2) reziduali STRUJNOG BALANSA u referentnom rjesenju ----------
# real:  sum_k Ymag_ik V_k cos(theta_ik + d_k)  - (P cos d + Q sin d)/V
# imag:  sum_k Ymag_ik V_k sin(theta_ik + d_k)  - (P sin d - Q cos d)/V
# za PV: Q = Q_i_g (= stvarno injektovani Q u rjesenju)
Ic = Icalc(Vref,dref)
Qeff = Q.copy()
for i in pv:  # stvarni Q na PV cvoru iz rjesenja
    S = (Vref[i]*np.exp(1j*dref[i]))*np.conj(Ic[i]); Qeff[i]=S.imag
res=[]
for i in range(n):
    if i==slack: continue
    Ire = np.sum(Ymag[i,:]*Vref*np.cos(Yang[i,:]+dref))
    Iim = np.sum(Ymag[i,:]*Vref*np.sin(Yang[i,:]+dref))
    rhsR = (P[i]*np.cos(dref[i]) + Qeff[i]*np.sin(dref[i]))/Vref[i]
    rhsI = (P[i]*np.sin(dref[i]) - Qeff[i]*np.cos(dref[i]))/Vref[i]
    res += [Ire-rhsR, Iim-rhsI]
print(f"\n== Max |rezidual strujnog balansa| u ref. rjesenju: {np.max(np.abs(res)):.3e} ==")
print("   (treba biti ~0 -> formulacija je korektna)")

# ---------- 3) NR direktno nad STRUJNIM BALANSOM iz flat starta ----------
# Nepoznate: za PQ -> d_i, V_i ; za PV -> d_i, Qg_i (V_i = Vg fiksno) ; slack fiksiran.
V2=np.ones(n); d2=np.zeros(n); V2[slack]=Vref[slack]; d2[slack]=dref[slack]
for i in pv: V2[i]=Vg[i]
Qv={i:Q[i] for i in pv}  # Qg kao nepoznata, init = pocetna injekcija
unk=[]  # lista (vrsta, cvor)
for i in (pv+pq): unk.append(('d',i))
for i in pq: unk.append(('V',i))
for i in pv: unk.append(('Q',i))
def getx():
    x=[]
    for kind,i in unk:
        x.append(d2[i] if kind=='d' else V2[i] if kind=='V' else Qv[i])
    return np.array(x)
def setx(x):
    for k,(kind,i) in enumerate(unk):
        if kind=='d': d2[i]=x[k]
        elif kind=='V': V2[i]=x[k]
        else: Qv[i]=x[k]
def Fcb():
    Vc=V2*np.exp(1j*d2); Ic=Y@Vc; f=[]
    for i in range(n):
        if i==slack: continue
        Qi = Qv[i] if i in pv else Q[i]
        rhsR=(P[i]*np.cos(d2[i])+Qi*np.sin(d2[i]))/V2[i]
        rhsI=(P[i]*np.sin(d2[i])-Qi*np.cos(d2[i]))/V2[i]
        f += [Ic[i].real-rhsR, Ic[i].imag-rhsI]
    return np.array(f)
for it in range(50):
    base=Fcb()
    if np.max(np.abs(base))<1e-12: break
    x=getx(); m=len(x); J=np.zeros((len(base),m)); e=1e-7
    for k in range(m):
        xx=x.copy(); xx[k]+=e; setx(xx); J[:,k]=(Fcb()-base)/e; setx(x)
    x=x+np.linalg.solve(J,-base); setx(x)
print(f"\n== NR nad strujnim balansom: konvergirao za {it} iteracija ==")
print("Max |V_cb - V_ref| =", f"{np.max(np.abs(V2-Vref)):.3e}",
      " Max |delta_cb - delta_ref| =", f"{np.max(np.abs(d2-dref)):.3e}")
print("   (treba biti ~0 -> strujni balans daje isto rjesenje kao standardni PF)")
