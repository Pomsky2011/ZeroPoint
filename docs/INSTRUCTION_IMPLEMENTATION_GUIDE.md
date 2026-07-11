# ZeroPoint Instruction Implementation Guide

This guide explains how to implement instructions for the CPU, PPU, and APU architectures in the ZeroPoint emulator.

## Table of Contents

1. [DEF88186 CPU Instructions](#cpu-instructions)
2. [PPU Instructions](#ppu-instructions)
3. [APU Instructions](#apu-instructions)
4. [Common Patterns](#common-patterns)

---

## CPU Instructions

### Architecture Overview

- **256 opcodes** (0x00-0xFF)
- **24-bit addressing** (16 MB memory space)
- **Little-endian** byte order
- **Variable instruction size** (1-4 bytes)
- **Hybrid 65816/8086** instruction set

### Registers

- **A**: Accumulator (16-bit or 8-bit based on M flag)
- **X, Y**: Index registers (16-bit or 8-bit based on X flag)
- **SP**: Stack pointer (16-bit, grows downward)
- **D**: Direct page base (16-bit)
- **PC**: Program counter (16-bit)
- **PB**: Program bank (8-bit)
- **DB**: Data bank (8-bit)
- **P**: Processor status (8 flags: NVMXDIZC)

### Implementing a CPU Instruction

**File**: `src/cpu_instructions.cpp`

**Template**:
```cpp
void cpu_inst_0xXX(CPU* cpu) {
    // 1. Fetch operands (if any)
    uint8_t operand8 = cpu->fetch();
    uint16_t operand16 = cpu->fetch16();
    uint32_t operand24 = cpu->fetch24();

    // 2. Calculate effective address
    uint32_t addr = cpu->addrAbsolute();  // Or other addressing mode

    // 3. Implement instruction logic
    cpu->opLDA(addr);  // Use existing opXXX() method
    // OR implement directly:
    uint8_t value = cpu->readByte(addr);
    cpu->setA(value);
    cpu->setNZ8(value);

    // 4. Update cycle count
    cpu->cycleCount += 3;
}
```

### Available Addressing Modes

```cpp
cpu->addrImmediate()                    // #$12 or #$1234
cpu->addrAbsolute()                     // $1234
cpu->addrAbsoluteLong()                 // $123456
cpu->addrAbsoluteIndexedX()             // $1234,X
cpu->addrAbsoluteIndexedY()             // $1234,Y
cpu->addrAbsoluteLongIndexedX()         // $123456,X
cpu->addrDirectPage()                   // $12
cpu->addrDirectPageIndexedX()           // $12,X
cpu->addrDirectPageIndexedY()           // $12,Y
cpu->addrDirectPageIndirect()           // ($12)
cpu->addrDirectPageIndirectLong()       // [$12]
cpu->addrDirectPageIndexedIndirectX()   // ($12,X)
cpu->addrDirectPageIndirectIndexedY()   // ($12),Y
cpu->addrDirectPageIndirectLongIndexedY() // [$12],Y
cpu->addrStackRelative()                // $12,S
cpu->addrStackRelativeIndirectIndexedY() // ($12,S),Y
```

### Available Helper Functions

```cpp
// Memory access
cpu->readByte(addr)
cpu->writeByte(addr, value)
cpu->readWord(addr)
cpu->writeWord(addr, value)
cpu->readLong(addr)
cpu->writeLong(addr, value)

// Register access
cpu->getA() / cpu->setA(value)
cpu->getX() / cpu->setX(value)
cpu->getY() / cpu->setY(value)
cpu->getSP() / cpu->setSP(value)
cpu->getPC() / cpu->setPC(value)
cpu->getFlags()  // Returns CPUFlags struct

// Flag operations
cpu->setNZ8(value)   // Set N and Z flags for 8-bit value
cpu->setNZ16(value)  // Set N and Z flags for 16-bit value

// Stack operations
cpu->push8(value)
cpu->push16(value)
cpu->push24(value)
cpu->pull8()
cpu->pull16()
cpu->pull24()

// Existing instruction implementations (use these!)
cpu->opLDA(addr)
cpu->opSTA(addr)
cpu->opADC(addr)
cpu->opSBC(addr)
cpu->opAND(addr)
cpu->opORA(addr)
cpu->opXOR(addr)
// ... and many more (see cpu.h)
```

### Example: Implementing LDA #const

```cpp
void cpu_inst_0x40(CPU* cpu) {  // LDA #const
    uint32_t addr = cpu->addrImmediate();
    cpu->opLDA(addr);
    cpu->cycleCount += 2;
}
```

### Example: Implementing ADC with Decimal Mode

```cpp
void cpu_inst_0x97(CPU* cpu) {  // ADC #const
    uint32_t addr = cpu->addrImmediate();

    CPUFlags flags = cpu->getFlags();
    if (flags.M) {  // 8-bit mode
        uint8_t operand = cpu->readByte(addr);
        uint8_t a = cpu->getA() & 0xFF;

        if (flags.D) {  // Decimal mode
            // Implement BCD addition
            // ... your logic here ...
        } else {
            uint16_t result = a + operand + (flags.C ? 1 : 0);
            cpu->setA(result & 0xFF);
            flags.C = result > 0xFF;
            flags.V = ((a ^ result) & (operand ^ result) & 0x80) != 0;
        }
        cpu->setNZ8(cpu->getA() & 0xFF);
    } else {  // 16-bit mode
        // Similar but for 16-bit
    }

    cpu->cycleCount += 2;
}
```

---

## PPU Instructions

### Architecture Overview

- **16 opcodes** (0x0-0xF)
- **16-bit big-endian** instructions
- **Format**: `[OOOO PPPP PPPP PPPP]` (4-bit opcode, 12-bit operand)
- **64 x 16-bit registers** (R0-R63)
- **Special registers**: R59 (VBlank INT), R60 (HBlank INT), R61 (SP), R62 (PC), R63 (DP)

### Implementing a PPU Instruction

**File**: `src/ppu.cpp` — `include/ppu_instructions.h`/`src/ppu_instructions.cpp`
are now empty stubs (dispatch was consolidated directly into `ppu.cpp`); do
not add new opcodes there.

Basic opcodes (0x0-0xD) are `case` blocks in the switch inside
`PPU::executeInstruction()`. Preset E sub-opcodes go in
`PPU::executePresetE()`, Preset F sub-opcodes in `PPU::executePresetF()` —
there is no per-opcode free function, just inline cases operating on `this`.

**Template** (adding a case to `PPU::executeInstruction()`'s switch):
```cpp
case PPUOpcode::YOUR_OPCODE: {
    // regX/regY are already pre-extracted from the operand's upper/lower
    // 6 bits at the top of executeInstruction() — reuse them where they fit
    uint16_t value = registers[regX];
    registers[regY] = value;

    // Override instrCycles here if this isn't a single base-cycle op
    // (see the CYC_* constants and how MUL/DIV/branches set it)
    break;
}
```

### Available Helper Functions

```cpp
// Register access
ppu->getRegister(n)              // Read register 0-63
ppu->setRegister(n, value)       // Write register 0-63

// Memory access
ppu->readMemory(addr)            // Read PPU memory (0x0000-0xFFFF)
ppu->writeMemory(addr, value)    // Write PPU memory

// Execution pointer (read-only via getExecutionPointer())
// For jumps, modify R62 (PC) instead
```

### Common Operand Patterns

```cpp
// Two 6-bit register operands
uint8_t regX = (operand >> 6) & 0x3F;
uint8_t regY = operand & 0x3F;

// Single 12-bit address
uint16_t address = operand & 0x0FFF;

// Flag bit + address
bool flag = (operand & 0x800) != 0;  // Bit 11
uint16_t addr = operand & 0x7FF;     // Lower 11 bits

// PRESET_E sub-opcodes (opcode 0xE)
uint8_t subop = (operand >> 10) & 0x03;  // Bits 11-10
uint16_t suboperand = operand & 0x3FF;    // Bits 9-0

// PRESET_F sub-opcodes (opcode 0xF)
uint8_t subop = (operand >> 8) & 0x0F;  // Bits 11-8
uint8_t suboperand = operand & 0xFF;     // Bits 7-0
```

### Example: ADD Instruction (real code, `src/ppu.cpp`)

```cpp
case PPUOpcode::ADD: {
    // add X Y - X = X + Y (regX, regY pre-extracted)
    registers[regX] += registers[regY];
    break;
}
```

### Example: Jump If Zero/Greater (real code, `src/ppu.cpp`)

PPU flags (`zero`/`greater`) are fully implemented (`PPUFlags` in
`include/ppu.h`, set by `CMP` via `updateFlags()`) — this is not a
placeholder:

```cpp
case PPUOpcode::JUMP_ZG: {
    // Bit 11: 0 = jmz, 1 = jmg
    bool isGreater = (operand & 0x800) != 0;
    bool taken = isGreater ? flags.greater : flags.zero;
    if (taken) {
        executionPointer = registers[REG_PC];
    }
    instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
    break;
}
```

---

## APU Instructions

### Architecture Overview

- **5-bit opcode field** (0x00-0x1F, 32 encodable values); `switch(opcode)`
  in `APU::executeInstruction()` only dispatches 0x00-0x1B (28 values) —
  0x1C-0x1F fall through to `default: halted = true`. Several of those 28
  cases further sub-decode into 2-4 mnemonics off the operand bits (~47
  total mnemonics — see CLAUDE.md's APU section)
- **16-bit big-endian** instructions
- **Format**: `[OOOOO PPPPPPPPPPP]` (5-bit opcode, 11-bit operand)
- **256 x 8-bit registers exist, but only R0-R127 are reachable** — ALU
  source operands are hardwired to a single bit selecting R0(X)/R1(Y) only,
  and destination fields are 7 bits wide (max R127)
- **4 MHz clock, 4 cycles per instruction**

### Implementing an APU Instruction

**File**: `src/apu.cpp` — `include/apu_instructions.h`/`src/apu_instructions.cpp`
are unused; there are no `apu_exec_0xXX` free functions. Dispatch is a
single inline `switch(opcode)` in `APU::executeInstruction()`; add your case
there.

**Template** (adding a case to `APU::executeInstruction()`'s switch):
```cpp
case 0xXX: {
    // Decode operand
    uint8_t regX = (operand >> 4) & 0xFF;  // Example
    uint8_t regY = operand & 0x0F;

    // Implement logic directly on member state (registers, pc, sp, ...)
    uint8_t value = registers[regX];
    registers[regY] = value;

    // Cycles are automatically added by APU::step()
    break;
}
```

### Available Helper Functions

```cpp
// Register access
apu->getRegister(n)           // Read register 0-255
apu->setRegister(n, value)    // Write register 0-255

// Memory access
apu->readByte(addr)           // Read APU memory
apu->writeByte(addr, value)   // Write APU memory
apu->readWord(addr)           // Read 16-bit word
apu->writeWord(addr, value)   // Write 16-bit word

// PC/SP access
apu->getPC()
apu->setPC(value)
apu->getSP()
apu->setSP(value)
```

### Example: JMP Instruction

```cpp
void apu_exec_0x01(APU* apu, uint16_t operand) {  // JMP
    // operand contains 11-bit address
    apu->setPC(operand & 0x7FF);
}
```

### Example: ADD Instruction

```cpp
void apu_exec_0x06(APU* apu, uint16_t operand) {  // ADD
    // Decode register fields from operand
    // Exact encoding depends on your ISA design
    uint8_t regX = (operand >> 5) & 0x1F;  // Example: 5-bit reg
    uint8_t regY = operand & 0x1F;

    uint8_t result = apu->getRegister(regX) + apu->getRegister(regY);
    apu->setRegister(regX, result);

    // Update flags if needed
}
```

---

## Common Patterns

Note: PPU and APU no longer use a free function per opcode (the sections
above cover this) — the snippets below show the *logic* pattern, not the
literal function signature; put the body in the appropriate `case` inside
`ppu.cpp`'s/`apu.cpp`'s switch statements instead of a standalone function.

### Pattern 1: Simple Register Operation

```cpp
// CPU
void cpu_inst_0xXX(CPU* cpu) {
    cpu->opINX();  // Use existing implementation
    cpu->cycleCount += 2;
}

// PPU (inside a case in PPU::executeInstruction, member context - "this" implied)
uint8_t reg = operand & 0x3F;
registers[reg]++;

// APU (inside a case in APU::executeInstruction)
uint8_t reg = operand & 0xFF;
registers[reg]++;
```

### Pattern 2: Memory Access

```cpp
// CPU
void cpu_inst_0xXX(CPU* cpu) {
    uint32_t addr = cpu->addrAbsolute();
    uint8_t value = cpu->readByte(addr);
    cpu->setA(value);
    cpu->setNZ8(value);
    cpu->cycleCount += 3;
}

// PPU (using DP register, inside a case in PPU::executeInstruction)
uint16_t dp = registers[REG_DP];  // R63 = DP
uint8_t value = readMemory(dp);
uint8_t reg = operand & 0x3F;
registers[reg] = value;

// APU (inside a case in APU::executeInstruction)
uint16_t addr = operand & 0x7FF;
uint8_t value = readByte(addr);
registers[0] = value;  // Store in register 0
```

### Pattern 3: Conditional Branches

```cpp
// CPU
void cpu_inst_0xXX(CPU* cpu) {
    int8_t offset = (int8_t)cpu->fetch();
    CPUFlags flags = cpu->getFlags();

    if (flags.Z) {  // Branch if zero
        cpu->setPC(cpu->getPC() + offset);
        cpu->cycleCount += 1;  // Extra cycle for branch taken
    }
    cpu->cycleCount += 2;
}

// PPU
void ppu_exec_0xX(PPU* ppu, uint16_t operand) {
    // Check zero flag (you'll need access to PPU flags)
    uint16_t pc = ppu->getRegister(62);
    // if (zeroFlag) ppu->setRegister(62, pc + offset);
}

// APU
void apu_exec_0xXX(APU* apu, uint16_t operand) {
    uint16_t target = operand & 0x7FF;
    // if (condition) apu->setPC(target);
}
```

---

## Quick Reference

### CPU Opcode Map
- 0x00-0x0B: Branches and special
- 0x0C-0x12: Jumps/Calls
- 0x13-0x1B: Control and registers
- 0x1C-0x3F: Stack and LDA variants
- 0x40-0x73: Load/Store variants
- 0x74-0x96: Compare, Inc/Dec, ALU
- 0x97-0xFE: ADC, SBC, Logical, Shifts
- 0xFF: HLT

### PPU Opcode Map
- 0x0: DEFCALL
- 0x1: MOVXP/NOP
- 0x2-0x5: SWAPREG, CLR, CMP, CLRF
- 0x6-0x7: Conditional jumps
- 0x8-0xD: INC, DEC, ADD, SUB, MUL, INTDIV
- 0xE: PRESET_E (immediate ops)
- 0xF: PRESET_F (extended ops)

### APU Opcode Map
- 0x00-0x02: NOP, JMP, JNZ
- 0x03: SRP/SDP
- 0x04-0x07: NOR, AND, ADD, SUB
- 0x08-0x0E: Store, flags, I/O ops
- 0x0F-0x14: Load, branch ops
- 0x15-0x18: BEQ, BNE, BLT, BGT
- 0x19-0x1F: SDB, writes, calls, compares

---

## Building and Testing

After implementing instructions:

1. **Build**:
   ```bash
   cd build_qt
   cmake .. && make -j4
   ```

2. **Test CPU**:
   ```bash
   ./bin/test_cpu
   ```

3. **Test PPU**:
   ```bash
   ./bin/test_ppu
   ```

4. **Test APU**:
   ```bash
   ./bin/test_apu
   ```

---

## Tips

1. **Start simple**: Implement basic instructions (NOP, simple loads/stores) first
2. **Use existing code**: Many CPU instructions have `opXXX()` implementations - reuse them!
3. **Check the docs**: See `ZPdevtools/docs/` (sibling repo) for ISA specs
4. **Test incrementally**: Build and test after implementing each instruction
5. **Watch cycles**: Accurate cycle counts matter for timing-sensitive code

---

Good luck implementing your instruction set!
