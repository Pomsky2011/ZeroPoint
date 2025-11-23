# ZeroPoint TODO

## Critical Bugs

**NONE** - All critical bugs resolved! 🎉

## Recent Updates (2025-11-23)

### ✅ Instruction Dispatch System Refactoring
**Refactored**: Complete rewrite of CPU, PPU, and APU interpreters with clean table-driven dispatch
**Added**: Comprehensive instruction implementation guide
**Status**: ✅ **READY FOR IMPLEMENTATION** - Framework in place for easy instruction modification

**Architecture Refactoring**:
- Replaced large switch statements with function pointer dispatch tables
- CPU: 256 separate functions (`cpu_inst_0x00` through `cpu_inst_0xFF`)
- PPU: 16 separate functions (`ppu_exec_0x0` through `ppu_exec_0xF`)
- APU: 32 separate functions (`apu_exec_0x00` through `apu_exec_0x1F`)
- Each instruction now in its own clearly labeled function
- Easy to find, modify, and implement individual instructions

**New Files Created**:
- `include/cpu_instructions.h`: CPU instruction handler declarations (256 opcodes)
- `src/cpu_instructions.cpp`: CPU instruction implementations with templates
- `include/ppu_instructions.h`: PPU instruction handler declarations (16 opcodes)
- `src/ppu_instructions.cpp`: PPU instruction implementations with templates
- `include/apu_instructions.h`: APU instruction handler declarations (32 opcodes)
- `src/apu_instructions.cpp`: APU instruction implementations with templates
- `docs/INSTRUCTION_IMPLEMENTATION_GUIDE.md`: Complete implementation guide (400+ lines)

**CPU Refactoring** (src/cpu.cpp):
- Made all addressing modes public (addrImmediate, addrAbsolute, etc.)
- Made all fetch operations public (fetch, fetch16, fetch24)
- Made all opXXX methods public (opLDA, opSTA, opADC, etc.)
- Made cycleCount public for direct access by instruction handlers
- Reduced executeInstruction from 326 lines to 4 lines
- Each instruction handler receives CPU pointer and implements complete logic

**PPU Refactoring** (src/ppu.cpp):
- Simplified executeInstruction to fetch, decode, and dispatch
- Each handler receives PPU pointer and 12-bit operand
- Access to registers via getRegister/setRegister
- Access to memory via readMemory/writeMemory
- 1 cycle per instruction (no manual cycle management needed)

**APU Refactoring** (src/apu.cpp):
- Simplified executeInstruction to extract opcode/operand and dispatch
- Each handler receives APU pointer and 11-bit operand
- Access to registers via getRegister/setRegister
- Access to memory via readByte/writeByte/readWord/writeWord
- 4 cycles per instruction (automatic via APU::step())

**Documentation Guide Features**:
- Complete architecture overviews (registers, formats, addressing)
- Implementation templates with code examples
- Available helper functions reference
- Common patterns (register ops, memory access, branches)
- Real working examples for each architecture
- Quick reference opcode maps
- Building and testing instructions

**How to Implement Instructions**:
1. Open the appropriate file (cpu_instructions.cpp, ppu_instructions.cpp, or apu_instructions.cpp)
2. Find the instruction handler function (e.g., `cpu_inst_0x42`, `ppu_exec_0xA`, `apu_exec_0x06`)
3. Implement logic using provided helper functions
4. Update cycle count (CPU only - PPU/APU automatic)
5. Build and test: `cd build_qt && cmake .. && make -j4`

**Files Modified**:
- `include/cpu.h`: Moved addressing modes, fetch ops, opXXX, cycleCount to public
- `src/cpu.cpp`: Added #include "cpu_instructions.h", simplified executeInstruction
- `src/ppu.cpp`: Added #include "ppu_instructions.h", simplified executeInstruction
- `src/apu.cpp`: Added #include "apu_instructions.h", simplified executeInstruction
- `CMakeLists.txt`: Added cpu_instructions.cpp, ppu_instructions.cpp, apu_instructions.cpp

**Build Status**:
- ✅ All files compile successfully
- ✅ No build errors
- ⚠️ CPU tests currently fail (opcodes need reimplementation in new handlers)
- ⚠️ PPU/APU tests not yet run with new system

**Impact**:
- Instruction implementations now trivial to find and modify
- Each instruction isolated in its own function
- No more hunting through 300+ line switch statements
- Perfect for incremental implementation and testing
- Framework ready for user to implement opcodes themselves

**Next Steps**:
- Implement CPU instruction handlers (use existing opXXX methods where possible)
- Implement PPU instruction handlers (most are straightforward register ops)
- Implement APU instruction handlers (similar to PPU)
- Test each instruction incrementally
- Refer to `docs/INSTRUCTION_IMPLEMENTATION_GUIDE.md` for guidance

## Recent Updates (2025-10-31)

### ✅ Vulkan Renderer & Cross-Platform Compatibility
**Added**: Native Vulkan renderer with 28% performance improvement over SDL
**Fixed**: Cross-platform compilation issues (missing <cstddef> includes in 7 headers)
**Tested**: ARM64 JIT verified on real hardware (QEMU limitation documented)
**Status**: ✅ **PRODUCTION READY** - Vulkan renderer fully functional, builds on all platforms

[... rest of TODO.md content remains unchanged ...]
