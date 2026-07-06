# SREES_2026_Babic_CBPF

**SREES 2026 seminar paper — Azra Babić (19029)**
Topic: **Nonlinear current balance converter: polar coordinates**

A C++ plugin for [dTwin](https://github.com/idzafic/dTwin) (built on the [natID](https://github.com/idzafic/natID) framework) that takes a MATPOWER `.m` case file and generates a dTwin `.dmodl` nonlinear power flow model formulated as a **current balance** in polar coordinates. The model is then solved by dTwin's built-in Newton–Raphson solver.

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
  CurrentBalancePlugin/   The main plugin (Converter.h — Y-bus in natID dense::CmplxMatrix)
  tools/cbconv/           Standalone converter + validation (validate.py, checks.py, case9.m)
  tools/cbgen/            Headless converter runner (tests the core with natID matrices)
  tools/pfsolve/          Solves a .dmodl with dTwin's modSolver (runtime test)
external/      Git submodules: natID (SDK), dTwin (models and examples)
presentation/  Presentation (.pptx) + ReadMe.txt (talk duration)
```

## Using the plugin in dTwin

1. Copy the built plugin (`cbpf.so` / `cbpf.dylib` / `cbpf.dll`) into `$HOME/ba.natID/plugins`.
2. Start dTwin and open **Model → Import → Konvertor strujnog balansa (polarno)** (the menu entry is labeled in Bosnian).
3. Pick the input `.m` and output `.dmodl` file, click **Konvertuj** (a progress bar tracks the conversion).
4. dTwin loads and solves the generated model.

The fastest way to see the result is `make run-dtwin` — it downloads the dTwin desktop app from the `SelectedSetups_<platform>.7z` archive ([natID releases](https://github.com/idzafic/natID/releases)) into `build/dTwin`, generates `case9_cb.dmodl` if it's missing, and opens it directly in dTwin. On Linux, dTwin needs OpenAL (Ubuntu: `sudo apt install libopenal1`, Arch: `sudo pacman -S openal`) — the target checks for missing system libraries and tells you exactly what to install.

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
