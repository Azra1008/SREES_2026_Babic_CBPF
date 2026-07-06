# Konvertor nelinearnog strujnog balansa: polarne koordinate

**Seminarski rad — predmet SREES**
**Student:** Azra Babić, index 19029
**Oblast:** Dinamički konvertori
**Okruženje:** natID SDK v4.1.0, dTwin 1.2.27 (C++20, macOS/Xcode)

---

## 1. Uvod

Proračun tokova snaga (engl. *power flow* / *load flow*) je osnovni proračun u
elektroenergetskim sistemima: za zadane injekcije snaga i topologiju mreže traže se
naponi (moduli i uglovi) u svim čvorovima. Klasično se sistem jednačina formira preko
**balansa snaga** (mismatch aktivne i reaktivne snage, ΔP i ΔQ). Ovaj rad obrađuje
alternativnu, ali ekvivalentnu formulaciju — **balans struja** (engl. *current
injection / current balance method*) u **polarnim koordinatama**.

Zadatak nije implementacija samog rješavača, nego **konvertor**: programski alat
(C++ plugin za framework **dTwin**) koji iz opisa mreže (MATPOWER `.m` datoteka)
generiše tekstualni **`.dmodl` NL model** sa jednačinama strujnog balansa. Taj model
zatim rješava ugrađeni nelinearni rješavač dTwin-a (Newton–Raphson). Konvertor radi u
zasebnoj radnoj niti, dok se napredak konverzije prikazuje u realnom vremenu na GUI
niti; isporučuje se kao dinamička biblioteka (dll/dylib) koja se učitava u meniju
**Model → Import**.

---

## 2. Teorijska osnova

### 2.1 Matrica admitansi (Y-bus)

Mreža se opisuje matricom admitansi čvorova **Y**, gdje je veza struja i napona:

$$\mathbf{I} = \mathbf{Y}\,\mathbf{V}, \qquad I_i = \sum_{k=1}^{n} Y_{ik} V_k .$$

Za granu (vod/transformator) između čvorova *f* i *t* sa serijskom admitansom
$y=1/(r+jx)$, otočnom susceptansom $b_{sh}=j\,b/2$ i kompleksnim odnosom transformatora
$a = t\,e^{j\varphi}$ (tap *t*, fazni pomak $\varphi$), doprinosi su:

$$Y_{ff}\!+\!=\!\frac{y+b_{sh}}{a\,a^*},\quad Y_{tt}\!+\!=\!y+b_{sh},\quad
Y_{ft}\!+\!=\!-\frac{y}{a^*},\quad Y_{tf}\!+\!=\!-\frac{y}{a}.$$

Otočni elementi čvora $(G_s+jB_s)$ dodaju se na dijagonalu. Matrica je rijetka
(*sparse*) — svaki čvor je povezan samo sa svojim susjedima.

U polarnom obliku zapisujemo $Y_{ik}=Y_{ik}\,e^{j\theta_{ik}}$ (modul i ugao) i napone
$V_k = V_k\,e^{j\delta_k}$.

### 2.2 Jednačine strujnog balansa

**Proračunata struja** u čvoru *i* iz mreže:

$$I_i^{calc} = \sum_k Y_{ik}V_k = \sum_k Y_{ik}V_k\,e^{j(\theta_{ik}+\delta_k)}.$$

$$\Re\{I_i^{calc}\} = \sum_k Y_{ik}V_k\cos(\theta_{ik}+\delta_k),\qquad
\Im\{I_i^{calc}\} = \sum_k Y_{ik}V_k\sin(\theta_{ik}+\delta_k).$$

**Zadana struja** iz injektovane snage $S_i = P_i + jQ_i = V_i I_i^{*}$:

$$I_i^{sp} = \left(\frac{S_i}{V_i}\right)^{\!*} = \frac{P_i - jQ_i}{V_i}\,e^{j\delta_i},$$

$$\Re\{I_i^{sp}\} = \frac{P_i\cos\delta_i + Q_i\sin\delta_i}{V_i},\qquad
\Im\{I_i^{sp}\} = \frac{P_i\sin\delta_i - Q_i\cos\delta_i}{V_i}.$$

**Uslov balansa** (mismatch struja jednak nuli) daje dvije jednačine po čvoru:

$$\boxed{\;\sum_k Y_{ik}V_k\cos(\theta_{ik}+\delta_k) = \frac{P_i\cos\delta_i + Q_i\sin\delta_i}{V_i}\;}$$
$$\boxed{\;\sum_k Y_{ik}V_k\sin(\theta_{ik}+\delta_k) = \frac{P_i\sin\delta_i - Q_i\cos\delta_i}{V_i}\;}$$

Razlika u odnosu na klasični **balans snaga**
$\big(V_i\sum_k Y_{ik}V_k\cos(\delta_i-\theta_{ik}-\delta_k)=P_i\big)$ je u tome što su
rezidual i Jakobijan izvedeni iz **struja**, a ne snaga (nema množenja s $V_i$ ispred
sume; desna strana je zadana struja).

### 2.3 Newton–Raphson i Jakobijan

Sistem je nelinearan, $\mathbf{f}(\mathbf{x}) = \mathbf{0}$, gdje je vektor stanja
$\mathbf{x}=[\delta,\;V]$ (polarno). Rješava se iterativno:

$$\mathbf{J}\,\Delta\mathbf{x} = -\mathbf{f}(\mathbf{x}^{(\nu)}), \qquad
\mathbf{x}^{(\nu+1)} = \mathbf{x}^{(\nu)} + \Delta\mathbf{x},$$

dok $\lVert\mathbf{f}\rVert_\infty > \varepsilon$. Jakobijan je
$\mathbf{J} = \partial(\Delta I^{\Re},\Delta I^{\Im})/\partial(\delta,V)$, rijedak i iste
strukture kao Y-bus, pa se faktorizacija radi *sparse* postupkom (LU). U dTwin-u se
Jakobijan i njegova faktorizacija generišu automatski iz simboličkog zapisa jednačina.

### 2.4 Tipovi čvorova

- **Slack** (referentni): $V_i$ i $\delta_i$ poznati (fiksni parametri) — bez jednačina.
- **PQ**: zadani $P_i, Q_i$; nepoznate $\delta_i, V_i$ — dvije jednačine balansa struja.
- **PV** (generatorski): zadani $P_i$ i $|V_i|=V_i^{sp}$; reaktivna snaga $Q_i$ nepoznata.
  U formulaciji strujnog balansa $V_i$ je fiksni parametar, a nepoznate su $\delta_i$ i
  $Q_{i}^{g}$ (reaktivna injekcija) — dvije jednačine balansa struja, s $Q_i^{g}$ na
  desnoj strani. (Broj jednačina = broj nepoznatih za svaki tip čvora.)

---

## 3. Implementacija

### 3.1 Arhitektura

Konvertor je realizovan kao **dTwin plugin** (`sc::IPlugin`) prema natID API-ju.
Plugin eksportuje `extern "C" sc::IPlugin* getPluginInterface()`, prijavljuje se u meni
`Model → Import` (`getMenuName`), tip modela je `ModelType::NL`. Tok rada:

```
MATPOWER .m  ──►  parsiranje  ──►  Y-bus (sparse)  ──►  generisanje .dmodl  ──►  dTwin NL solver
```

Plugin sam ne rješava — generiše tekstualni model koji rješava dTwin-ov rješavač.

### 3.2 Struktura izvornog koda

`natID.Examples/dTwin/Plugin/CurrentBalancePlugin/`:

| Fajl | Uloga |
|---|---|
| `src/Converter.h` | jezgra: parsiranje `.m`, Y-bus u natID `dense::CmplxMatrix`, generisanje `.dmodl` |
| `src/CBPlugin.cpp` | `sc::IPlugin` omotač + `getPluginInterface` |
| `src/WindowPlugin.h` | GUI prozor (`gui::Window`) |
| `src/ViewConv.h` | GUI prikaz + threading konverzije |
| `CMakeLists.txt`, `CBPlugin.cmake` | build (shared library `libcbpf.dylib`) |

### 3.3 Generisanje modela

Za svaki ne-slack čvor ispisuju se dvije NL jednačine (realni i imaginarni dio struje)
u dTwin DSL-u. Primjer (PQ čvor 5 iz `case9`):

```
Y_5_5*V_5*cos(θ_5_5+δ_5) + Y_5_4*V_4*cos(θ_5_4+δ_4) + Y_5_6*V_6*cos(θ_5_6+δ_6) = (P_5*cos(δ_5) + Q_5*sin(δ_5))/V_5
Y_5_5*V_5*sin(θ_5_5+δ_5) + Y_5_4*V_4*sin(θ_5_4+δ_4) + Y_5_6*V_6*sin(θ_5_6+δ_6) = (P_5*sin(δ_5) - Q_5*cos(δ_5))/V_5
```

PV čvorovi koriste promjenljivu `Q_<i>_g` i fiksni `V_<i>`; čvorovi bez injekcije
(*zero-injection*) imaju desnu stranu 0.

### 3.4 Višenitnost (threading)

Prema zahtjevu teme, konverzija se izvršava u **radnoj niti** (`std::thread`), tako da
GUI ostaje responzivan, a **progres-indikator** se ažurira u realnom vremenu na glavnoj
(GUI) niti. Korišten je natID mehanizam `gui::thread::asyncExecInMainThread(...)` (isti
obrazac kao u natID primjeru `ViewProgress2`): radna nit javlja napredak (0–100%, i po
čvoru za velike mreže), a lambda na glavnoj niti postavlja `gui::ProgressIndicator`.
Korištena je *shared* zastavica životnog vijeka radi zaštite od pristupa oslobođenoj
memoriji pri zatvaranju prozora. Izlazna datoteka se piše preko `std::ofstream`, a
sadržaj se ujedno upisuje u izlaznu arhivu (`arch::MemoryOut::put`).

---

## 4. Rezultati i validacija

Testni slučaj: standardni **WSCC 9-čvorni sistem** (MATPOWER `case9`, 3 generatora,
9 grana, baseMVA=100).

### 4.1 Y-bus

Generisane vrijednosti admitansi se poklapaju s referentnim dTwin modelom
`examples/PowerSystem/real/case9.dmodl` do 10+ značajnih cifara, npr.
$Y_{11}=17.3611\angle{-}90°$, $Y_{44}=39.4478\angle{-}85.2°$, $Y_{45}=10.6886$,
$Y_{99}=17.5252$.

### 4.2 Konvergencija i tačnost

Nezavisnom provjerom (Newton–Raphson) utvrđeno je:
- reziduali jednačina strujnog balansa u tačnom PF rješenju: **2.4·10⁻¹³** (≈ 0),
- NR nad strujnim balansom iz *flat start*-a konvergira za **4 iteracije** na isto
  rješenje kao klasični balans snaga (razlika napona ~10⁻¹⁴).

### 4.3 Rješavanje stvarnim dTwin rješavačem

Generisani `.dmodl` je učitan i riješen dTwin-ovim `modSolver`-om
(`createRealStaticModel(NLE) → initFromFile → solve`): **status = OK**. Rješenje:

| Čvor | Tip | V [p.u.] | δ [°] |
|---|---|---|---|
| 1 | slack | 1.0400 | 0.00 |
| 2 | PV | 1.0250 | 9.28 |
| 3 | PV | 1.0250 | 4.67 |
| 4 | PQ | 1.0258 | −2.22 |
| 5 | PQ | 1.0127 | −3.69 |
| 6 | PQ | 1.0324 | 1.97 |
| 7 | PQ | 1.0159 | 0.73 |
| 8 | PQ | 1.0258 | 3.72 |
| 9 | PQ | 0.9956 | −3.99 |

Rezultati odgovaraju poznatom rješenju WSCC 9-čvornog sistema; PV čvorovi su korektno
izračunali reaktivne injekcije ($Q_2^g=0.0665$, $Q_3^g=-0.1086$ p.u.).

---

## 5. Zaključak

Implementiran je i validiran C++ plugin-konvertor koji iz MATPOWER opisa mreže generiše
dTwin NL model tokova snaga u formulaciji **nelinearnog strujnog balansa u polarnim
koordinatama**. Pokazano je (analitički i kroz stvarni dTwin rješavač) da formulacija
daje fizički ispravno rješenje, identično klasičnom balansu snaga. Ispunjeni su svi
zahtjevi teme: C++, konverzija u zasebnoj niti, real-time progres-indikator na drugoj
(GUI) niti, grafički interfejs i isporuka u formi dinamičke biblioteke (plugin).

---

## 6. Literatura

1. P. Kundur, *Power System Stability and Control*, McGraw-Hill, 1994.
2. P. W. Sauer, M. A. Pai, *Power System Dynamics and Stability*, 2007.
3. F. Milano, *Power System Modelling and Scripting*, Springer, 2010.
4. V. M. da Costa, N. Martins, J. L. R. Pereira, „Developments in the Newton–Raphson
   Power Flow Formulation Based on Current Injections", *IEEE Trans. Power Systems*, 1999.
5. R. D. Zimmerman et al., *MATPOWER* — testni slučajevi (case9, case30, case118, case300).
6. I. Džafić, *natID* i *dTwin* dokumentacija (GitHub: idzafic/natID, idzafic/dTwin).

---

> **Napomena o okruženju:** Y-bus se gradi u natID kompleksnoj dense matrici
> (`dense::CmplxMatrix`), a plugin koristi zvanični `arch/MemoryOut.h`. Ovi headeri
> (`arch/MemoryOut.h`, `matrix/MatrixLib.h`) nedostajali su u ranijem release-u (v4.1.0),
> ali su prisutni u aktuelnoj **main grani** natID-a — build zahtijeva tu (ili noviju) verziju.
