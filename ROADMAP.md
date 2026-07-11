# ZeroPoint Roadmap to Final Release

## Current Status (2025-10-29)

### ✅ Complete & Production Ready (~85% Done!)
- **CPU (DEF88186)**: All 256 opcodes, hardware multiply/divide, loops, interrupts ✅
- **PPU**: Display system, tiles, palettes, interrupts, VOC, JIT (experimental) ✅
- **APU**: All 47 instructions, MMP audio (16 stereo channels), SST system ✅
- **DMA**: All 4 transfer modes, 16 channels, interrupt-aware ✅
- **C Compiler**: ~95% complete! Pointers, structs, unions, enums, typedef, arrays, all operators, all control flow ✅
- **Development Tools**: Assemblers (CPU/PPU/APU), ROM builder, disassemblers ✅

### 🔧 Remaining Core Work
- **Boot ROM**: System initialization and program loader (Phase 1) — WIP in the separate `ZPbootROM` repo; `init.def` (register/stack/hardware init + RAM-clear DMA) is written, `main.def`/`copy.def`/`rsa.def` are still empty
- **System Integration**: CPU ↔ PPU ↔ APU communication (Phase 1)
- **Debuggers**: Interactive debugging for CPU/PPU/APU (Phase 3)
- **Documentation**: User manual, tutorials, examples (Phase 5)

**The hardware emulation is essentially complete! Most remaining work is integration, debugging tools, and documentation.**

---

## Phase 1: Core System Integration (2-3 weeks)

### 1.1 System Boot & Initialization ⚡ CRITICAL
- [ ] **Boot ROM implementation**
  - System initialization sequence
  - Hardware detection and setup
  - Default interrupt vectors
  - Basic diagnostics
  - User program loader
- [ ] **Memory mapping finalization**
  - CPU memory map validation ($00-$FF banks)
  - APU window ($A0xxxx) integration
  - PPU window ($B0xxxx) integration
  - I/O registers ($D80000-$D80047) finalization
- [ ] **Interrupt vector system**
  - Proper IRQ/NMI handling in CPU
  - Interrupt priority and masking
  - Vector table at $FFFA-$FFFF
  - CPU interrupt integration with PPU/APU

**Deliverable**: Bootable system that initializes all hardware and loads user programs

### 1.2 Inter-Component Communication ⚡ CRITICAL
- [ ] **CPU ↔ PPU interface**
  - CPU writes to PPU window ($B0xxxx)
  - CPU reads from PPU VOC registers
  - CPU triggers PPU operations via I/O registers
  - Test: CPU-controlled graphics demo
- [ ] **CPU ↔ APU interface**
  - CPU writes to APU window ($A0xxxx)
  - CPU controls MMP channels from main program
  - APU status register reads
  - Test: CPU-triggered audio playback
- [ ] **DMA integration testing**
  - CPU-initiated DMA transfers to PPU
  - CPU-initiated DMA transfers to APU
  - Multi-channel concurrent transfers
  - Performance benchmarking

**Deliverable**: Unified system where CPU can control all components

### 1.3 System-Level Testing
- [ ] **Integration test suite**
  - CPU + PPU rendering test
  - CPU + APU audio test
  - CPU + DMA transfer test
  - Full system test (all components active)
- [ ] **Performance validation**
  - Clock synchronization verification
  - Timing accuracy measurement
  - Frame rate stability (60 Hz target)
  - Audio sync validation (48 kHz)

**Deliverable**: Comprehensive test suite proving system stability

---

## Phase 2: Toolchain Completion (2-3 weeks)

### 2.1 C Compiler Polish (OPTIONAL)
- [ ] **Multi-dimensional array subscripting**
  - Direct `matrix[i][j]` syntax (currently requires workaround)
  - Code generation for nested subscripts
  - Test: Matrix operations
- [ ] **Struct initialization**
  - Complete struct initializer lists
  - Nested struct initialization
  - Test: Complex data structure init
- [ ] **Function pointers** (optional)
  - Function pointer declaration
  - Function pointer calls
  - Callbacks and dispatch tables
  - Test: Event system
- [ ] **Preprocessor integration** (optional)
  - #include, #define, #ifdef support
  - Or document using cpp separately

**Deliverable**: C compiler supporting 100% of practical C89 features

**Note**: C compiler is already ~95% complete! Pointers, structs, unions, enums, typedef, arrays, all operators, and all control flow are DONE.

### 2.2 Development Tools Enhancement
- [ ] **Disassembler improvements**
  - Better formatting and comments
  - Symbol table support
  - Cross-references
- [ ] **Assembler macros**
  - Macro definition and expansion
  - Conditional assembly
  - Include file support
  - Better error messages
- [ ] **ROM builder enhancements**
  - Metadata embedding
  - Compression support
  - Multiple program segments
  - Debug symbol export

**Deliverable**: Professional-grade development toolchain

---

## Phase 3: Developer Experience (1-2 weeks)

### 3.1 Debugging Tools ⚡ HIGH PRIORITY
- [ ] **CPU debugger**
  - Register inspection
  - Memory viewer/editor
  - Breakpoints
  - Single-step execution
  - Call stack trace
- [ ] **PPU debugger**
  - PC tracing
  - Register watch points
  - VRAM viewer
  - Tile/palette inspector
  - Frame capture
- [ ] **APU debugger**
  - Register/stack inspection
  - Audio channel viewer
  - Waveform display
  - Sample inspector

**Deliverable**: Interactive debugging environment

### 3.2 Emulator Features
- [x] **Window scaling options** — configurable scale in Qt settings dialog (`qt/mainwindow.cpp`)
  - Fullscreen mode
  - Aspect ratio options
  - Scanline filters (optional)
- [ ] **Save states**
  - Full system state serialization
  - Quick save/load hotkeys
  - Multiple save slots
- [ ] **Input handling**
  - Keyboard mapping configuration
  - Gamepad support
  - Input recording/playback

**Deliverable**: User-friendly emulator with modern features

---

## Phase 4: Performance & Polish (1-2 weeks)

### 4.1 Performance Optimization
- [ ] **JIT compiler stabilization**
  - Debug and fix remaining ARM64 issues
  - Add safety checks and error handling
  - Performance benchmarking vs interpreter
  - Make JIT default when stable
- [ ] **Interpreter optimization**
  - Instruction dispatch optimization
  - Memory access caching
  - Hot path optimization
- [ ] **Display optimization**
  - Hardware scrolling implementation
  - Display caching strategies
  - Reduce render overhead

**Deliverable**: Stable 60 FPS performance on all platforms

### 4.2 Code Quality & Testing
- [ ] **Extended test suites**
  - CPU: All instruction categories
  - PPU: All rendering modes
  - APU: All audio features
  - System: Edge cases and stress tests
- [ ] **Code cleanup**
  - Remove debug output (or gate behind flags)
  - Consistent error handling
  - Code documentation
  - Memory leak checking
- [ ] **CI/CD enhancement**
  - Automated testing on all platforms
  - Performance regression detection
  - Binary artifact generation

**Deliverable**: High-quality, well-tested codebase

---

## Phase 5: Documentation & Examples (1-2 weeks)

### 5.1 Documentation Completion
- [ ] **User manual**
  - Getting started guide
  - System architecture overview
  - Programming tutorials
  - API reference
  - Troubleshooting guide
- [ ] **Developer guide**
  - Building from source
  - Contributing guidelines
  - Architecture deep-dive
  - Porting guide
- [ ] **Specification documents**
  - Complete hardware specification
  - Instruction set reference
  - Memory map reference
  - Timing diagrams

**Deliverable**: Comprehensive documentation package

### 5.2 Example Programs
- [ ] **PPU demo collection**
  - Hello World (simple pixel)
  - Color bars and gradients
  - Tile-based graphics
  - Sprite animation
  - Scrolling background
  - Particle effects
- [ ] **APU demo collection**
  - Tone generation
  - Music playback
  - Sound effects
  - Multi-channel mixing
- [ ] **Full game demo**
  - Simple game using all components
  - C source code included
  - Build instructions
  - Educational comments

**Deliverable**: 10+ example programs demonstrating all features

---

## Phase 6: Final Release Preparation (1 week)

### 6.1 Release Engineering
- [ ] **Version 1.0 feature freeze**
- [ ] **Final testing pass**
  - All platforms (Windows, Linux, macOS)
  - All architectures (x64, ARM64)
  - All example programs
- [ ] **Release notes**
  - Feature list
  - Known limitations
  - Future roadmap
  - Credits and acknowledgments
- [ ] **Licensing**
  - Choose license (MIT, GPL, Apache, etc.)
  - Add license headers to all files
  - Third-party attribution
- [ ] **Distribution packages**
  - Binary releases for all platforms
  - Source code archive
  - Documentation bundle
  - Example programs collection

**Deliverable**: ZeroPoint 1.0 Release

---

## Post-Release: Optional Enhancements

### Graphics Extensions
- [ ] Hardware scrolling
- [x] ~~Sprite system~~ — by design, not planned as a dedicated layer; done manually on top of the tile system
- [ ] Additional blending modes
- [ ] Transparency effects

### Audio Extensions
- [ ] Reverb/echo effects implementation
- [x] Cubic (Catmull-Rom) sample interpolation — replaces linear; true Gaussian windowing not pursued
- [ ] Advanced sample looping
- [ ] Real-time audio effects

### JIT Extensions
- [ ] ARMv8/ARMv7/ARMv6 (Switch, PS Vita, 3DS)
- [ ] PowerPC 32-bit (Wii, Wii U)
- [ ] PowerPC 64-bit (Xbox 360, PS3)
- [ ] RISC-V support

### Community Features
- [ ] Online program repository
- [ ] Program sharing and rating
- [ ] Collaborative development tools
- [ ] Discord/forum community

---

## Timeline Summary

| Phase | Duration | Priority | Deliverable |
|-------|----------|----------|-------------|
| 1. Core System Integration | 2-3 weeks | ⚡ Critical | Bootable unified system |
| 2. Toolchain Polish | 1 week | Low | C compiler 100% (optional) |
| 3. Developer Experience | 1-2 weeks | ⚡ High | Debugging tools |
| 4. Performance & Polish | 1-2 weeks | Medium | Stable 60 FPS |
| 5. Documentation & Examples | 1-2 weeks | ⚡ High | Complete docs |
| 6. Final Release Prep | 1 week | ⚡ Critical | v1.0 Release |
| **Total** | **7-11 weeks** | | **ZeroPoint 1.0** |

**Note**: C compiler is essentially complete (~95%), so Phase 2 is mostly optional polish!

---

## Success Criteria for v1.0

### Functional Requirements ✅
- [ ] Boot ROM initializes all hardware
- [ ] CPU can control PPU and APU via memory windows
- [ ] DMA transfers work reliably
- [ ] C compiler can build non-trivial programs (with pointers and structs)
- [ ] Debugger allows inspection of all components
- [ ] 60 FPS sustained performance on modern hardware
- [ ] Clean audio at 48 kHz with no glitches
- [ ] All test suites pass (CPU, PPU, APU, DMA, System)

### User Experience ✅
- [ ] Installation takes < 5 minutes
- [ ] "Hello World" program works in < 10 minutes
- [ ] Documentation covers all features
- [ ] Example programs demonstrate all capabilities
- [ ] Error messages are clear and helpful

### Quality Standards ✅
- [ ] No crashes on valid programs
- [ ] Memory leaks < 1 KB/hour
- [ ] Code coverage > 80% (where applicable)
- [ ] Works on Windows, Linux, macOS (x64 + ARM64)
- [ ] Professional documentation

---

## Notes

- **Critical Path**: Phases 1, 5, and 6 are on the critical path to release
- **Parallelization**: Phases 2 and 3 can be developed concurrently
- **Optional Features**: Post-release items are nice-to-have, not blockers
- **Community Input**: Consider beta release after Phase 5 for feedback

**Target Release Date**: 3 months from start of Phase 1
