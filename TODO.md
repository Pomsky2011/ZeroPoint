# ZeroPoint TODO

## Critical Bugs

### 🔴 Loop Counters Get Stuck (2025-10-19)
**Status**: BLOCKING - Loops fundamentally broken
**Description**: Even simple increment/compare/jump loops get stuck with counters at value 1
**Test Case**:
```asm
R10 = COUNTER
start:
    CLR COUNTER
loop:
    INC COUNTER
    TARREG 2, LSB, R15
    SETBYTE 2, 5
    CMP COUNTER, R15
    JNG loop
    HLT
```
**Expected**: Counter increments 0→1→2→3→4→5, then exits loop
**Actual**: Counter increments to 1 and gets stuck in infinite loop at PC cycling through same addresses

**Possible Causes**:
1. zpasm label encoding might be incorrect (jumping to wrong address)
2. JNG instruction might not be evaluating flags correctly
3. JNG might not be jumping to the address in PC correctly
4. Label resolution might be placing loop: at wrong offset

**Impact**: All demos using loops (color_bars, gradients, etc.) cannot complete execution

**Next Steps**:
1. Disassemble loop_test.bin and verify label addresses
2. Add PC and flag tracing to PPU
3. Test JNG instruction in isolation
4. Check if manual PC loading works better than JNG shorthand

## Recent Implementations (2025-10-20)

### ✅ APU Stack Operations and Function Calls
**Added**: Complete stack system and redesigned function call mechanism
**Changes**:
- Added 16-bit Stack Pointer (SP) register
- Implemented stack operations (opcode 0x17): BSP, PUX, PUY, POX, POY, RET
- JMP and JNZ now push return addresses onto stack before jumping
- Redesigned function system: CFS→CFN, CFE→STACK, CCF updated
- Three ways to call subroutines: JMP, JNZ, CFN/CCF

**New APU Instructions** (41→47 total):
- CFN (0x16): Define callable function at PC + offset
- BSP: Build Stack Pointer from X (LSB) and Y (MSB)
- PUX/PUY: Push X/Y register onto stack
- POX/POY: Pop from stack into X/Y register
- RET: Pop 16-bit return address from stack to PC

**Stack Behavior**:
- JMP: Always pushes return address before jumping
- JNZ: Pushes return address only if condition is true
- CCF: Pushes return address when calling function
- All require RET to return to caller
- Stack must be initialized with BSP before use

### ✅ PPU MOVXP/NOP Instruction
**Added**: Opcode 0x1 redesigned from ENDDEFCALL to dual-purpose instruction
**Changes**:
- `0x1000-0x103F`: MOVXP X - copies execution pointer + 2 to register X
- `0x1800-0x1FFF`: NOP - does nothing
- Enables manual function calls and position-independent code

## Recent Fixes (2025-10-19)

### ✅ Interrupt Stack Push Order
**Fixed**: Interrupt handlers were writing return address before decrementing SP
**Solution**: Changed to decrement SP first, then write to [SP] and [SP+1]
```cpp
// Before (WRONG):
memory[registers[REG_SP]] = returnAddr & 0xFF;
memory[registers[REG_SP] + 1] = (returnAddr >> 8) & 0xFF;
registers[REG_SP] -= 2;

// After (CORRECT):
registers[REG_SP] -= 2;
memory[registers[REG_SP]] = returnAddr & 0xFF;
memory[registers[REG_SP] + 1] = (returnAddr >> 8) & 0xFF;
```

### ✅ HLT Detection
**Added**: Automatic detection of HLT pattern in run_demo and test_demo
**Details**:
- HLT shorthand expands to a 6-instruction loop (not 5 as originally assumed)
- Track PC history over 20 cycles
- Detect repeating patterns of 5-8 instructions
- Exit cleanly when HLT detected instead of running until MAX_CYCLES

**Files Modified**:
- `tools/run_demo.cpp` - Added HLT detection loop
- `tools/test_demo.cpp` - Added HLT detection loop
- `include/ppu.h` - Added `getExecutionPointer()` method

## Known Issues

1. **No Palette System**: Palette instructions (PALETTE16, PALETTE256) defined but not implemented
2. **Grayscale Tiles**: Tile system treats each byte as grayscale (R=G=B) instead of using palette indices
3. **No Immediate Values**: Constants must be built step-by-step using INC, ADD, MUL
4. **HLT is not HALT**: HLT is a macro that creates an infinite loop, not a real CPU halt state

## Future Enhancements

### High Priority - PPU
- [ ] **Fix loop bug** (CRITICAL - blocking all PPU development)
- [ ] Add proper HALT opcode (0xE is available)
- [ ] Add PPU debugger with PC tracing and flag inspection
- [ ] Add register/memory watch points

### High Priority - APU
- [ ] Test and verify stack operations with real programs
- [ ] Implement MMP audio mixing
- [ ] Add APU debugger with register/stack inspection
- [ ] Test function call/return mechanisms

### Medium Priority - PPU
- [ ] Implement palette system
- [ ] Add indexed color tile format
- [ ] DMA-based tile copying
- [ ] Hardware scrolling
- [ ] Window scaling options (2×, 4×, 8×)

### Medium Priority - APU
- [ ] Implement SST sample storage
- [ ] Add I/O operations (IOO/IOI)
- [ ] Test banking system (RBC/IBC)
- [ ] Add audio output to SDL/Qt frontends

### Low Priority
- [ ] Better assemblers with macros (both PPU and APU)
- [ ] Immediate value support in assemblers
- [ ] Sprite system (optional layer over DIY tiles)
- [ ] Disassemblers for both PPU and APU

## Testing Status

### PPU - Working
- ✅ HLT detection
- ✅ Interrupts (R59/R60) with automatic return address push
- ✅ Bank-based rolling framebuffer
- ✅ SETRENDMOD (color mode switching)
- ✅ Pixel I/O (0x0100-0x010B)
- ✅ Simple programs (no loops)
- ✅ MOVXP/NOP instruction

### PPU - Broken
- ❌ All loop-based programs (counters stuck at 1)
- ❌ color_bars_clean.asm
- ❌ gradient demos
- ❌ Any program using INC + CMP + JNG pattern

### PPU - Not Tested
- ⚠️ Tile drawing
- ⚠️ V-Blank/H-Blank interrupt handlers
- ⚠️ Complex programs with subroutines
- ⚠️ DMA operations

### APU - Working
- ✅ Basic instruction execution
- ✅ Stack operations (BSP, PUX, PUY, POX, POY, RET)
- ✅ JMP/JNZ with return address pushing
- ✅ CFN/CCF function system
- ✅ Memory operations
- ✅ Register operations

### APU - Not Tested
- ⚠️ Full function call/return cycle
- ⚠️ Nested function calls
- ⚠️ Hardware loops (LST/LFN)
- ⚠️ MMP audio mixing
- ⚠️ SST sample storage
- ⚠️ I/O operations
- ⚠️ Banking (ROM/IO)
