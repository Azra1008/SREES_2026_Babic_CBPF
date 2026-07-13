# SREES_2026_Babic_CBPF

**SREES 2026 seminar paper — Azra Babić (19029)**
Topic: **Nonlinear current balance converter: polar coordinates**

A C++ plugin for [dTwin](https://github.com/idzafic/dTwin) (built on the [natID](https://github.com/idzafic/natID) framework) that takes a MATPOWER `.m` case file and generates a dTwin `.dmodl` nonlinear power flow model formulated as a **current balance** in polar coordinates. The model is then solved by dTwin's built-in Newton–Raphson solver.

> ### 🔶 Update — dynamic mode (disturbances + graphs)
>
> Added after the initial submission, on the supervisor's request. Alongside the original
> **static** current-balance power flow, the plugin now has a **dynamic (DAE) mode**: the user
> picks a disturbance in the GUI (*load step/disconnect* or *short circuit*) and the converter
> generates a **transient-stability model + graphs** that dTwin plots as the time response — so
> the converter now produces a graphical output, not just a solved operating point.
> Details in [**Dynamic mode**](#dynamic-mode-disturbances--graphs) below.

## Quick start

```bash
git clone <repo-url>
cd SREES_2026_Babic_CBPF
make run-dtwin
```

That single target fetches everything it needs (submodules, natID binaries, the dTwin app), builds the converter, generates the case9 model and opens it directly in dTwin — which solves it on load.

## Prerequisites

- git, CMake ≥ 3.18, GNU make
- a C++20 compiler:
  - **Linux** — GCC or Clang, plus the GTK4 runtime (Ubuntu: `sudo apt install libgtk-4-1`, Arch: `sudo pacman -S gtk4`)
  - **macOS** — Xcode Command Line Tools (Intel and Apple Silicon both supported)
  - **Windows** — Visual Studio 2022, run make from Git Bash or MSYS2
- Python 3 for the optional validation scripts (`make validate` sets up its own venv)

## Building

```bash
make deps       # one-time: initializes submodules + downloads natID release binaries
make            # builds the plugin (cbpf) + cbgen + pfsolve
make test       # cbgen converts case9.m, pfsolve solves the resulting .dmodl
make run-dtwin  # opens the generated .dmodl directly in the dTwin app
make validate   # independent math check (creates .venv, installs numpy)
make cbconv     # standalone converter — no natID needed, just a C++ compiler
make dynsolve   # builds the headless DAE simulator
make test-dyn   # generates a dynamic model (with a disturbance) and simulates it
make clean      # removes the build/ folder
```

Every target fetches its own dependencies when they're missing, so plain `make` after a plain `git clone` just works — no `--recurse-submodules` needed. The platform and matching release archives are detected automatically; the same Makefile covers Linux (tested on Arch, works on Ubuntu), macOS and Windows. Use `make CONFIG=Debug` for a debug build.

Build outputs:

- plugin: `build/CBPlugin/out/cbpf.so` (macOS: `.dylib`, Windows: `.dll`)
- tools: `build/cbgen/out/cbgen`, `build/pfsolve/out/pfsolve`

If you already have the natID SDK installed system-wide, it can be used instead of the submodule: the `NATID_SDK_ROOT` environment variable takes precedence, then the submodule, then `$HOME/natID.SDK`.

## Dependencies

Both dependencies live as git submodules in `external/`:

| Library | Version | Used for |
|---|---|---|
| [natID](https://github.com/idzafic/natID) | main branch (v4.2.0+), binaries [v4.2.0](https://github.com/idzafic/natID/releases/tag/v4.2.0) | SDK — headers, `DevEnv/*.cmake`, natGUI, MatrixLib (`dense::CmplxMatrix`), modSolver |
| [dTwin](https://github.com/idzafic/dTwin) | main branch ([1.2.53](https://github.com/idzafic/dTwin/tags)) | reference models, converter examples, the `.dmodl` format |

natID headers and CMake come from the submodule; the precompiled libraries (`mainUtils`, `Matrix`, `natGUI`, `modSolver`, …) are downloaded by `make deps` from the [natID v4.2.0 release](https://github.com/idzafic/natID/releases/tag/v4.2.0) (`bin_<platform>_20260517.7z`) into `external/natID/natID.SDK/bin/lib`.

## Repository layout
```
docs/          Documentation (LyX/LaTeX .tex, .md) + diagrams (SVG/PNG)
src/           Source code (CMake + C++)
  CurrentBalancePlugin/   The main plugin (Converter.h — Y-bus in natID dense::CmplxMatrix;
                          DynEmit.h — dynamic DAE model + .vmodl graphs)
  tools/cbconv/           Standalone converters + validation (cbconv.cpp, cbdyn.cpp, case9.m)
  tools/cbgen/            Headless converter runner (tests the core with natID matrices)
  tools/pfsolve/          Solves a static .dmodl with dTwin's modSolver (NLE runtime test)
  tools/dynsolve/         Simulates a dynamic .dmodl with dTwin's modSolver (DAE: reset+step)
external/      Git submodules: natID (SDK), dTwin (models and examples)
presentation/  Presentation (.pptx) + ReadMe.txt (talk duration)
```

## Using the plugin in dTwin

1. Copy the built plugin (`cbpf.so` / `cbpf.dylib` / `cbpf.dll`) into `$HOME/ba.natID/plugins`.
2. Start dTwin and open **Model → Import → Konvertor strujnog balansa (polarno)** (the menu entry is labeled in Bosnian).
3. Pick the input `.m` and output `.dmodl` file, click **Konvertuj** (a progress bar tracks the conversion).
4. dTwin loads and solves the generated model.

The fastest way to see the result is `make run-dtwin` — it downloads the dTwin desktop app from the `SelectedSetups_<platform>.7z` archive ([natID releases](https://github.com/idzafic/natID/releases)) into `build/dTwin`, generates `case9_cb.dmodl` if it's missing, and opens it directly in dTwin. On Linux, dTwin needs OpenAL (Ubuntu: `sudo apt install libopenal1`, Arch: `sudo pacman -S openal`) — the target checks for missing system libraries and tells you exactly what to install.

## Dynamic mode (disturbances + graphs)

> **This is an update to the original seminar.** The first submission covered only the static
> current-balance power flow; this section documents the later addition of time-domain simulation
> with user-selected disturbances and graphical output, requested by the supervisor.

Ticking **"Dinamicki model (smetnja + grafici)"** in the plugin GUI switches the converter from
the static power flow to a **dynamic (DAE)** transient-stability model: a detailed generator
(4th-order machine + AVR + governor) over the same Y-bus / current balance, plus a companion
**`.vmodl`** that dTwin draws as graphs (the output). The model mirrors the reference
`external/dTwin/examples/PowerSystem/Dynamics/case9_dyn.dmodl`.

The user selects, in the GUI:

- **Disturbance type** — *load step/disconnect* or *short circuit (voltage dip)*.
- **Bus** (0 = auto, first load bus), **t start / end [s]**, and **magnitude** — for a load it is
  a factor (0 = disconnect); for a short circuit it is the added shunt conductance `G` [p.u.]
  (0 = default 5; keep ≲10 or the algebraic solve can oscillate at fault instant).

After conversion dTwin receives the DAE model plus the graphs (rotor speed/frequency ω, rotor
angles δ, bus voltages V, mechanical/electrical powers P) and plots the time response to the
chosen disturbance.

Headless validation (no GUI — runs the initialization and the time simulation through dTwin's solver):
```bash
make test-dyn   # cbdyn generates case9_cb_dyn.dmodl, dynsolve runs reset()+step()
```
Both disturbances simulate stably; the load response matches the reference `case9_dyn`. Ready
reference outputs live in `src/tools/cbconv/out/case9_cb_dyn.*` and `case9_cb_short.*` — a `.dmodl`
opened in dTwin next to its sibling `.vmodl` is drawn as graphs immediately.

## Validation (case9)

- The Y-bus matches the reference `case9.dmodl` to 10+ digits.
- Current balance residuals at the exact solution: 2.4·10⁻¹³; Newton–Raphson converges in 4 iterations.
- The generated model solved by dTwin's `modSolver` (`make test`): **SOLVE OK**, voltages correct (e.g. V₉ = 0.9956).

For a quick independent check without natID:
```bash
make validate   # validate.py + checks.py — creates .venv and installs
                # src/tools/cbconv/requirements.txt (numpy) automatically
make cbconv     # standalone converter, needs only a C++ compiler
```
