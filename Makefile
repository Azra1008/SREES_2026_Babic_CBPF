# SREES 2026 — Konvertor strujnog balansa (CBPF)
#
# Radi na Linuxu (Ubuntu, Arch), macOS-u (Intel i Apple Silicon) i
# Windowsu (Git Bash ili MSYS2 s GNU make; kompajler Visual Studio 2022).
#
#   make deps      — povuče git submodule (natID, dTwin) i natID release binarije
#                    (poziva se automatski iz build targeta ako fali)
#   make           — sve: plugin (cbpf) + cbgen + pfsolve
#   make test      — cbgen konvertuje case9.m, pfsolve ga riješi
#   make run-dtwin — otvori generisani .dmodl u dTwin aplikaciji
#                    (povuče dTwin i generiše model ako fale)
#   make validate  — python validacija (sam napravi .venv + instalira requirements)
#   make cbconv    — standalone konvertor (bez natID-a, samo C++ kompajler)
#   make clean     — obriše build/ folder
#
#   make CONFIG=Debug     — debug build (default je Release)

CONFIG      ?= Release
BUILD_DIR   ?= build

NATID_TAG      := v4.2.0
NATID_BIN_DATE := 20260517
NATID_SDK      := external/natID/natID.SDK

# ---- platforma -------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  PLATFORM   := win_x64
  EXE        := .exe
  PLUGIN_LIB := cbpf.dll
  PYTHON     ?= python
  VENV_BIN   := .venv/Scripts
  RUNENV     := PATH="$(CURDIR)/$(NATID_SDK)/bin:$(CURDIR)/$(NATID_SDK)/bin/lib:$$PATH"
else
  UNAME_S := $(shell uname -s)
  UNAME_M := $(shell uname -m)
  EXE     :=
  PYTHON  ?= python3
  VENV_BIN := .venv/bin
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(UNAME_M),arm64)
      PLATFORM := macOS_arm64
    else
      PLATFORM := macOS_x64
    endif
    PLUGIN_LIB := cbpf.dylib
    RUNENV     := DYLD_LIBRARY_PATH="$(CURDIR)/$(NATID_SDK)/bin/lib"
  else
    PLATFORM   := linux_x64
    PLUGIN_LIB := cbpf.so
    RUNENV     := LD_LIBRARY_PATH="$(CURDIR)/$(NATID_SDK)/bin/lib"
  endif
endif

NATID_URL     := https://github.com/idzafic/natID
DTWIN_URL     := https://github.com/idzafic/dTwin
RELEASE_URL   := $(NATID_URL)/releases/download/$(NATID_TAG)
NATID_BIN_URL := $(RELEASE_URL)/bin_$(PLATFORM)_$(NATID_BIN_DATE).7z
SETUPS_URL    := $(RELEASE_URL)/SelectedSetups_$(PLATFORM)_$(NATID_BIN_DATE).7z
DEPS_STAMP    := $(NATID_SDK)/bin/lib/.stamp-$(PLATFORM)-$(NATID_BIN_DATE)

CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(CONFIG)

MODEL      := $(BUILD_DIR)/case9_cb.dmodl
DTWIN_DIR  := $(BUILD_DIR)/dTwin
DTWIN_STAMP := $(DTWIN_DIR)/.ready

# preuzimanje: curl (svuda default), fallback wget
define FETCH
	curl -fL -o $(1) $(2) || wget -O $(1) $(2)
endef

.PHONY: all deps submodules plugin cbgen pfsolve cbconv test run-dtwin dtwin-app venv validate clean

all: plugin cbgen pfsolve

# ---- zavisnosti: submoduli + prekompajlirane natID biblioteke ---------------
NATID_MARKER := $(NATID_SDK)/DevEnv/Common.cmake
DTWIN_MARKER := external/dTwin/README.md

deps: $(DEPS_STAMP)

submodules: $(NATID_MARKER) $(DTWIN_MARKER)

# submodule update radi u normalnom repou; ako je git stanje polomljeno ili
# ovo uopste nije git repo (zip download sa GitHuba), folder se brise i
# kloniramo direktno — build mora raditi u oba slucaja
$(NATID_MARKER):
	-git submodule update --init --depth 1 external/natID
	@test -e $@ || { \
		echo "== submodule nedostupan — kloniram $(NATID_URL) direktno"; \
		rm -rf external/natID; \
		git clone --depth 1 $(NATID_URL) external/natID; \
	}
	@test -e $@ || { echo "GRESKA: external/natID nekompletan ($@ ne postoji)"; exit 1; }

$(DTWIN_MARKER):
	-git submodule update --init --depth 1 external/dTwin
	@test -e $@ || { \
		echo "== submodule nedostupan — kloniram $(DTWIN_URL) direktno"; \
		rm -rf external/dTwin; \
		git clone --depth 1 $(DTWIN_URL) external/dTwin; \
	}
	@test -e $@ || { echo "GRESKA: external/dTwin nekompletan ($@ ne postoji)"; exit 1; }

$(DEPS_STAMP): $(NATID_MARKER) $(DTWIN_MARKER)
	@echo "== natID binarije: $(NATID_BIN_URL)"
	@mkdir -p $(BUILD_DIR)/natID-bin
	$(call FETCH,$(BUILD_DIR)/natID-bin.7z,$(NATID_BIN_URL))
	cd $(BUILD_DIR)/natID-bin && cmake -E tar xf ../natID-bin.7z
	rm -f $(BUILD_DIR)/natID-bin/ReadMe.txt
	mkdir -p $(NATID_SDK)/bin/lib
	cp -R $(BUILD_DIR)/natID-bin/. $(NATID_SDK)/bin/
	rm -rf $(BUILD_DIR)/natID-bin $(BUILD_DIR)/natID-bin.7z
	touch $@

# ---- CMake targeti ----------------------------------------------------------
plugin: $(DEPS_STAMP)
	cmake -S src/CurrentBalancePlugin -B $(BUILD_DIR)/CBPlugin $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)/CBPlugin --config $(CONFIG) -j
	@echo "== plugin: $(BUILD_DIR)/CBPlugin/out/$(PLUGIN_LIB)"

cbgen: $(DEPS_STAMP)
	cmake -S src/tools/cbgen -B $(BUILD_DIR)/cbgen $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)/cbgen --config $(CONFIG) -j

pfsolve: $(DEPS_STAMP)
	cmake -S src/tools/pfsolve -B $(BUILD_DIR)/pfsolve $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR)/pfsolve --config $(CONFIG) -j

# ---- standalone konvertor (bez natID-a) -------------------------------------
cbconv:
	@mkdir -p $(BUILD_DIR)/cbconv
	$(CXX) -std=c++20 -O2 -o $(BUILD_DIR)/cbconv/cbconv$(EXE) src/tools/cbconv/cbconv.cpp
	@echo "== cbconv: $(BUILD_DIR)/cbconv/cbconv$(EXE)"

# ---- generisani model (cbgen nad case9.m) ------------------------------------
$(MODEL): src/tools/cbconv/cases/case9.m | cbgen
	$(RUNENV) $(BUILD_DIR)/cbgen/out/cbgen$(EXE) src/tools/cbconv/cases/case9.m $(MODEL)

# ---- test: konverzija + rješavanje case9 ------------------------------------
test: pfsolve $(MODEL)
	$(RUNENV) $(BUILD_DIR)/pfsolve/out/pfsolve$(EXE) $(MODEL)

# ---- dTwin aplikacija (SelectedSetups release arhiv) --------------------------
dtwin-app: $(DTWIN_STAMP)

$(DTWIN_STAMP):
	@echo "== dTwin aplikacija: $(SETUPS_URL)"
	@mkdir -p $(DTWIN_DIR)/setups
	$(call FETCH,$(DTWIN_DIR)/setups.7z,$(SETUPS_URL))
	cd $(DTWIN_DIR)/setups && cmake -E tar xf ../setups.7z
ifeq ($(OS),Windows_NT)
	# MSI se raspakuje bez instalacije (administrativna slika)
	MSYS2_ARG_CONV_EXCL='*' msiexec /a "$(subst /,\,$(CURDIR)/$(DTWIN_DIR)/setups/Digital Twin.msi)" /qn TARGETDIR="$(subst /,\,$(CURDIR)/$(DTWIN_DIR)/app)"
else ifeq ($(UNAME_S),Darwin)
	mv "$(DTWIN_DIR)/setups/Digital Twin.app" $(DTWIN_DIR)/
else
	cd $(DTWIN_DIR) && cmake -E tar xf setups/dTwin.deb && cmake -E tar xf data.tar.xz
	rm -f $(DTWIN_DIR)/data.tar.* $(DTWIN_DIR)/control.tar.* $(DTWIN_DIR)/debian-binary
endif
	rm -rf $(DTWIN_DIR)/setups $(DTWIN_DIR)/setups.7z
	touch $@

# ---- run-dtwin: otvori generisani model u dTwin-u -----------------------------
run-dtwin: $(MODEL) $(DTWIN_STAMP)
ifeq ($(OS),Windows_NT)
	DTWIN_EXE=$$(find "$(DTWIN_DIR)/app" -name dTwin.exe | head -1); \
	if [ -z "$$DTWIN_EXE" ]; then echo "GRESKA: dTwin.exe nije nadjen u $(DTWIN_DIR)/app"; exit 1; fi; \
	"$$DTWIN_EXE" "$(CURDIR)/$(MODEL)"
else ifeq ($(UNAME_S),Darwin)
	"$(DTWIN_DIR)/Digital Twin.app/Contents/MacOS/dTwin" "$(CURDIR)/$(MODEL)"
else
	@missing=$$(LD_LIBRARY_PATH="$(CURDIR)/$(DTWIN_DIR)/usr/lib/x86_64-linux-gnu/dTwin" \
		ldd "$(DTWIN_DIR)/usr/bin/dTwin" | grep 'not found' || true); \
	if [ -n "$$missing" ]; then \
		echo "GRESKA — nedostaju sistemske biblioteke:"; echo "$$missing"; \
		echo "  Ubuntu: sudo apt install libopenal1"; \
		echo "  Arch:   sudo pacman -S openal"; \
		exit 1; \
	fi
	LD_LIBRARY_PATH="$(CURDIR)/$(DTWIN_DIR)/usr/lib/x86_64-linux-gnu/dTwin" \
		"$(DTWIN_DIR)/usr/bin/dTwin" "$(CURDIR)/$(MODEL)"
endif

# ---- python: venv + validacija -------------------------------------------------
REQS := src/tools/cbconv/requirements.txt

venv: .venv/.stamp

.venv/.stamp: $(REQS)
	$(PYTHON) -m venv .venv
	$(VENV_BIN)/pip install -r $(REQS)
	touch $@

validate: .venv/.stamp
	$(VENV_BIN)/python src/tools/cbconv/validate.py
	$(VENV_BIN)/python src/tools/cbconv/checks.py

clean:
	rm -rf $(BUILD_DIR)
