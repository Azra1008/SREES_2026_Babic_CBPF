# SREES_2026_Babic_CBPF

**Seminarski rad SREES 2026 — Azra Babić (19029)**
Tema: **Konvertor nelinearnog strujnog balansa: polarne koordinate**

C++ plugin za framework **dTwin** (natID SDK) koji iz MATPOWER `.m` datoteke generiše
dTwin `.dmodl` NL model tokova snaga u formulaciji **strujnog balansa** u polarnim
koordinatama. Model rješava ugrađeni dTwin rješavač (Newton–Raphson).

## Struktura repozitorija
```
docs/          Dokumentacija (LyX/LaTeX .tex, .md) + dijagrami (SVG/PNG)
src/           Izvorni kod (CMake + C++)
  CurrentBalancePlugin/   Glavni plugin (Converter.h — Y-bus u natID dense::CmplxMatrix)
  tools/cbconv/           Standalone konvertor + validacija (validate.py, checks.py, case9.m)
  tools/cbgen/            Headless pokretač konvertora (test jezgre s natID matricama)
  tools/pfsolve/          Alat koji dTwin modSolver-om rješava .dmodl (runtime test)
presentation/  Prezentacija (.pptx) + ReadMe.txt (trajanje izlaganja)
bin/           Prekompajlirani plugin (libcbpf.dylib, macOS x64)
```

## Kako se gradi (macOS, Xcode + CMake)
Preduslov: natID SDK (**main grana / v4.2.0+**, koja sadrži `arch/MemoryOut.h` i
`matrix/MatrixLib.h`) postavljen na `$HOME/natID.SDK` (+ `$HOME/natID.RAMDisk`), release
binarije u `$HOME/natID.SDK/bin`.
```bash
export HOME=$HOME
cmake -G Xcode -S src/CurrentBalancePlugin -B $HOME/natID.RAMDisk/build/CBPlugin
cmake --build $HOME/natID.RAMDisk/build/CBPlugin --config Debug
# rezultat: $HOME/natID.RAMDisk/Out/CBPluginSol/Debug/libcbpf.dylib
```

## Kako se koristi
1. Kopirati `libcbpf.dylib` u `$HOME/ba.natID/plugins`.
2. Pokrenuti dTwin, meni **Model → Import → Konvertor strujnog balansa (polarno)**.
3. Odabrati ulaznu `.m` i izlaznu `.dmodl`, kliknuti **Konvertuj** (progres-bar).
4. dTwin učita i riješi generisani model.

## Validacija (case9)
- Y-bus se poklapa sa referentnim `case9.dmodl` (10+ cifara).
- Reziduali strujnog balansa u tačnom rješenju: 2.4·10⁻¹³; NR konvergira za 4 iteracije.
- Generisani model riješen dTwin `modSolver`-om: **OK**; naponi tačni (npr. V₉=0.9956).

Za brzu (nezavisnu) provjeru bez GUI-ja:
```bash
python3 src/tools/cbconv/validate.py           # matematička provjera
```

## Napomena o natID matricama i headerima
Y-bus se gradi u **natID kompleksnoj dense matrici** (`dense::CmplxMatrix`), a plugin koristi
zvanični `arch/MemoryOut.h`. Ovi headeri (`arch/MemoryOut.h`, `matrix/MatrixLib.h`) su
nedostajali u ranijem release-u (v4.1.0), ali su prisutni u aktuelnoj **main grani natID-a** —
zato build zahtijeva tu (ili noviju) verziju SDK-a.

## Napomena o build-u na drugom sistemu
Zadatak traži i build na drugom sistemu (Windows/Linux) — nije urađeno u ovoj verziji.
CMake konfiguracija je cross-platform (isti `CMakeLists.txt`).
