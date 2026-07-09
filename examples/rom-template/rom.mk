# =============================================================================
# ZeroPoint ROM build template
# =============================================================================
# Copy this Makefile into your game/software project and edit the "PROJECT"
# section below. It drives the ZeroPoint devtools (cpuasm / ppuasm / apuasm /
# def88186cc) through one of two link paths:
#
#   modern:   cpuasm/... -> zplink (-> ELF) -> zpbuild (-> signed .zpb)
#   classic:  cpuasm/... -> rombuilder (-> .rom + .txt)
#
#   make               build + locally sign  -> build/<ROM>.zpb
#   make elf           dev link only         -> build/<ROM>.elf  (submit this to HQ)
#   make classic       legacy/DOS            -> build/<ROM>.rom  (rombuilder)
#   make PLATFORM=dos  build on MS-DOS via DJGPP (defaults to the classic path)
#   make clean
#   make help
#
# Expected project layout (all sources optional except CPU):
#   cpu/main.asm   (or main.c — compiled by def88186cc)
#   ppu/graphics.asm
#   apu/audio.asm
#
# SIGNING: zplink holds no key — it only emits the ELF. zpbuild turns that ELF
# into a signed .zpb, and it carries the bundled *development* key, so `make`
# produces a ROM the emulator loads for local testing. A production ROM is
# signed at HQ with the real key: ship `build/<ROM>.elf`, not a locally-signed
# .zpb. On real hardware only HQ-signed ROMs are trusted.
#
# NOTE ON MS-DOS: the devtools ship as real-mode DOS .exe's, so this Makefile
# runs on a 1990s box — but it needs *GNU* make (DJGPP's `make`), because it
# uses conditionals and functions Borland/Turbo MAKE don't understand. On DOS
# it defaults to `rombuilder` (the classic path).
# =============================================================================

# ---- PROJECT (edit these) ---------------------------------------------------
# NOTE: keep values free of trailing spaces — make does not trim them, and a
# stray space in ROM/paths would break the target names. That's why the
# comments here sit on their own lines instead of after the values.

# Output basename, embedded ROM title, developer/author tag.
ROM   := game
TITLE := My ZeroPoint Game
DEV   := ZP

# Component sources. CPU may be .asm or .c (def88186cc). PPU/APU: blank to omit.
CPU_SRC := cpu/main.asm
PPU_SRC := ppu/graphics.asm
APU_SRC := apu/audio.asm

# Load addresses (defaults match the console memory map).
ENTRY    := 0x8000
CPU_BASE := 0x8000
PPU_BASE := 0x100000
APU_BASE := 0x200000

# ---- Toolchain location (override on the command line if needed) ------------
# Points at ZPdevtools/executables. def88186cc usually lives in c_compiler/.
ZPTOOLS ?= ../../../ZPdevtools/executables
ZPCC    ?= ../../../ZPdevtools/c_compiler/def88186cc

# ---- Platform ---------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  PLATFORM ?= windows
else
  PLATFORM ?= unix
endif
ifeq ($(filter dos windows,$(PLATFORM)),)
  EXE :=
else
  # DOS/Windows tool binaries end in .exe (no trailing space — see note above).
  EXE := .exe
endif

# On DOS default to the classic rombuilder path; elsewhere use zplink.
ifeq ($(PLATFORM),dos)
  LINKER ?= rombuilder
else
  LINKER ?= zplink
endif

# ---- Resolved tools ---------------------------------------------------------
CPUASM     := $(ZPTOOLS)/cpuasm$(EXE)
PPUASM     := $(ZPTOOLS)/ppuasm$(EXE)
APUASM     := $(ZPTOOLS)/apuasm$(EXE)
ROMBUILDER := $(ZPTOOLS)/rombuilder$(EXE)
ZPLINK     := $(ZPTOOLS)/zplink$(EXE)
ZPBUILD    := $(ZPTOOLS)/zpbuild$(EXE)
CC88       := $(ZPCC)$(EXE)

BUILD := build
CPU_BIN := $(BUILD)/cpu.bin
PPU_BIN := $(BUILD)/ppu.bin
APU_BIN := $(BUILD)/apu.bin

# =============================================================================
# Component object lists + per-linker argument strings
# =============================================================================
COMPONENTS := $(CPU_BIN)
ZL_ARGS    := --cpu $(CPU_BIN)@$(CPU_BASE)
RB_ARGS    := -cpu $(CPU_BIN)

ifneq ($(strip $(PPU_SRC)),)
  COMPONENTS += $(PPU_BIN)
  ZL_ARGS    += --ppu $(PPU_BIN)@$(PPU_BASE)
  RB_ARGS    += -ppu $(PPU_BIN)
endif
ifneq ($(strip $(APU_SRC)),)
  COMPONENTS += $(APU_BIN)
  ZL_ARGS    += --apu $(APU_BIN)@$(APU_BASE)
  RB_ARGS    += -apu $(APU_BIN)
endif

# =============================================================================
# Default goal
# =============================================================================
.PHONY: all elf sign classic clean help

ifeq ($(LINKER),rombuilder)
all: classic
else
all: sign
endif

elf:     $(BUILD)/$(ROM).elf
sign:    $(BUILD)/$(ROM).zpb
classic: $(BUILD)/$(ROM).rom

# =============================================================================
# Assemble / compile the components
# =============================================================================
$(BUILD):
	mkdir -p $(BUILD)

# --- CPU: .c is compiled to .asm first, then assembled; .asm is direct. ------
ifeq ($(suffix $(CPU_SRC)),.c)
$(BUILD)/cpu.asm: $(CPU_SRC) | $(BUILD)
	$(CC88) $< -o $@
$(CPU_BIN): $(BUILD)/cpu.asm
	$(CPUASM) $< $@
else
$(CPU_BIN): $(CPU_SRC) | $(BUILD)
	$(CPUASM) $< $@
endif

# --- PPU: note ppuasm takes -o for the output. -------------------------------
$(PPU_BIN): $(PPU_SRC) | $(BUILD)
	$(PPUASM) $< -o $@

# --- APU: positional in/out. -------------------------------------------------
$(APU_BIN): $(APU_SRC) | $(BUILD)
	$(APUASM) $< $@

# =============================================================================
# Link + sign
# =============================================================================
# zplink: place the flat segments at their load addresses and emit an ELF.
# This is the dev-side deliverable (no signing key). Submit it to HQ to ship.
$(BUILD)/$(ROM).elf: $(COMPONENTS)
	$(ZPLINK) -o $@ $(ZL_ARGS) --entry $(ENTRY)

# zpbuild: sign the ELF into a .zpb ROM. Locally this uses the bundled dev key
# (emulator-loadable); at HQ the same tool is built against the production key.
$(BUILD)/$(ROM).zpb: $(BUILD)/$(ROM).elf
	$(ZPBUILD) $< -o $@ --title "$(TITLE)" --dev "$(DEV)"

# rombuilder: classic .rom + .txt metadata. Requires all three components.
$(BUILD)/$(ROM).rom: $(COMPONENTS)
	$(ROMBUILDER) $(RB_ARGS) -entry $(ENTRY) -o $@ -v

# =============================================================================
# Housekeeping
# =============================================================================
clean:
	rm -rf $(BUILD)

help:
	@echo "ZeroPoint ROM build template"
	@echo
	@echo "Targets:"
	@echo "  all (default)  signed .zpb on Unix (zplink+zpbuild), classic .rom on DOS"
	@echo "  elf            build/$(ROM).elf  - zplink output; submit this to HQ to ship"
	@echo "  sign           build/$(ROM).zpb  - zpbuild-signed ROM (dev key, emulator-loadable)"
	@echo "  classic        build/$(ROM).rom  - rombuilder + .txt metadata"
	@echo "  clean          remove build/"
	@echo
	@echo "Options (VAR=value):"
	@echo "  PLATFORM   unix (default) | dos | windows   (dos => classic path, .exe tools)"
	@echo "  LINKER     zplink (default) | rombuilder"
	@echo "  ROM TITLE DEV                                output name + metadata"
	@echo "  CPU_SRC PPU_SRC APU_SRC                      component sources (.asm/.c)"
	@echo "  ENTRY CPU_BASE PPU_BASE APU_BASE             load addresses"
	@echo "  ZPTOOLS    path to ZPdevtools/executables"
	@echo "  ZPCC       path to def88186cc (C compiler)"
