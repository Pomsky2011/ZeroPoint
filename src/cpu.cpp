#include "cpu.h"
#include <cstring>
#include <stdexcept>

namespace ZeroPoint {

CPU::CPU()
    : A(0), X(0), Y(0), SP(0x01FF), D(0), PC(0), PB(0), DB(0),
      loopCounter(0), loopStart(0),
      memory(nullptr), memorySize(0),
      state(CPUState::Running),
      cycleCount(0), instructionCount(0)
{
    // Initialize with 8-bit mode, interrupts disabled
    P.M = true;
    P.X = true;
    P.I = true;
}

CPU::~CPU() {
}

void CPU::reset() {
    A = 0;
    X = 0;
    Y = 0;
    SP = 0x01FF;
    D = 0;
    PC = 0;
    PB = 0;
    DB = 0;
    P = CPUFlags();
    P.M = true;  // 8-bit accumulator
    P.X = true;  // 8-bit index registers
    P.I = true;  // Interrupts disabled
    loopCounter = 0;
    loopStart = 0;
    state = CPUState::Running;
    cycleCount = 0;
    instructionCount = 0;
}

void CPU::setMemory(uint8_t* mem, size_t size) {
    memory = mem;
    memorySize = size;
}

// Memory access with 24-bit addressing
uint8_t CPU::readByte(uint32_t address) {
    if (!memory || address >= memorySize) {
        return 0xFF;
    }
    return memory[address & (memorySize - 1)];
}

void CPU::writeByte(uint32_t address, uint8_t value) {
    if (!memory || address >= memorySize) {
        return;
    }
    memory[address & (memorySize - 1)] = value;
}

uint16_t CPU::readWord(uint32_t address) {
    // Little-endian
    uint8_t lo = readByte(address);
    uint8_t hi = readByte(address + 1);
    return (hi << 8) | lo;
}

void CPU::writeWord(uint32_t address, uint16_t value) {
    // Little-endian
    writeByte(address, value & 0xFF);
    writeByte(address + 1, (value >> 8) & 0xFF);
}

uint32_t CPU::readLong(uint32_t address) {
    // Little-endian, 24-bit
    uint8_t lo = readByte(address);
    uint8_t mid = readByte(address + 1);
    uint8_t hi = readByte(address + 2);
    return (hi << 16) | (mid << 8) | lo;
}

void CPU::writeLong(uint32_t address, uint32_t value) {
    // Little-endian, 24-bit
    writeByte(address, value & 0xFF);
    writeByte(address + 1, (value >> 8) & 0xFF);
    writeByte(address + 2, (value >> 16) & 0xFF);
}

// Fetch operations
uint8_t CPU::fetch() {
    uint32_t addr = (PB << 16) | PC;
    uint8_t value = readByte(addr);
    PC++;
    return value;
}

uint16_t CPU::fetch16() {
    uint8_t lo = fetch();
    uint8_t hi = fetch();
    return (hi << 8) | lo;
}

uint32_t CPU::fetch24() {
    uint8_t lo = fetch();
    uint8_t mid = fetch();
    uint8_t hi = fetch();
    return (hi << 16) | (mid << 8) | lo;
}

// Stack operations
void CPU::push8(uint8_t value) {
    writeByte(SP, value);
    SP--;
}

void CPU::push16(uint16_t value) {
    push8((value >> 8) & 0xFF);
    push8(value & 0xFF);
}

void CPU::push24(uint32_t value) {
    push8((value >> 16) & 0xFF);
    push8((value >> 8) & 0xFF);
    push8(value & 0xFF);
}

uint8_t CPU::pull8() {
    SP++;
    return readByte(SP);
}

uint16_t CPU::pull16() {
    uint8_t lo = pull8();
    uint8_t hi = pull8();
    return (hi << 8) | lo;
}

uint32_t CPU::pull24() {
    uint8_t lo = pull8();
    uint8_t mid = pull8();
    uint8_t hi = pull8();
    return (hi << 16) | (mid << 8) | lo;
}

// Flag operations
void CPU::setNZ8(uint8_t value) {
    P.Z = (value == 0);
    P.N = (value & 0x80) != 0;
}

void CPU::setNZ16(uint16_t value) {
    P.Z = (value == 0);
    P.N = (value & 0x8000) != 0;
}

void CPU::setNZC8(uint8_t value) {
    setNZ8(value);
}

void CPU::setNZC16(uint16_t value) {
    setNZ16(value);
}

// Main execution
void CPU::step() {
    if (state != CPUState::Running) {
        return;
    }

    executeInstruction();
    instructionCount++;
}

void CPU::run(uint64_t cycles) {
    uint64_t targetCycle = cycleCount + cycles;
    while (cycleCount < targetCycle && state == CPUState::Running) {
        step();
    }
}

// Addressing modes implementation
uint32_t CPU::addrImmediate() {
    uint32_t addr = (PB << 16) | PC;
    PC++;
    if (!P.M) PC++;  // 16-bit mode reads 2 bytes
    return addr;
}

uint32_t CPU::addrAbsolute() {
    uint16_t offset = fetch16();
    return (DB << 16) | offset;
}

uint32_t CPU::addrAbsoluteLong() {
    return fetch24();
}

uint32_t CPU::addrAbsoluteIndexedX() {
    uint16_t offset = fetch16();
    uint16_t effectiveOffset = offset + (P.X ? (X & 0xFF) : X);
    return (DB << 16) | effectiveOffset;
}

uint32_t CPU::addrAbsoluteIndexedY() {
    uint16_t offset = fetch16();
    uint16_t effectiveOffset = offset + (P.X ? (Y & 0xFF) : Y);
    return (DB << 16) | effectiveOffset;
}

uint32_t CPU::addrAbsoluteLongIndexedX() {
    uint32_t addr = fetch24();
    addr += (P.X ? (X & 0xFF) : X);
    return addr & 0xFFFFFF;
}

uint32_t CPU::addrDirectPage() {
    uint8_t offset = fetch();
    return (D + offset) & 0xFFFF;
}

uint32_t CPU::addrDirectPageIndexedX() {
    uint8_t offset = fetch();
    uint16_t effectiveOffset = offset + (P.X ? (X & 0xFF) : X);
    return (D + effectiveOffset) & 0xFFFF;
}

uint32_t CPU::addrDirectPageIndexedY() {
    uint8_t offset = fetch();
    uint16_t effectiveOffset = offset + (P.X ? (Y & 0xFF) : Y);
    return (D + effectiveOffset) & 0xFFFF;
}

uint32_t CPU::addrDirectPageIndirect() {
    uint8_t offset = fetch();
    uint16_t dpAddr = (D + offset) & 0xFFFF;
    uint16_t pointer = readWord(dpAddr);
    return (DB << 16) | pointer;
}

uint32_t CPU::addrDirectPageIndirectLong() {
    uint8_t offset = fetch();
    uint16_t dpAddr = (D + offset) & 0xFFFF;
    return readLong(dpAddr);
}

uint32_t CPU::addrDirectPageIndexedIndirectX() {
    uint8_t offset = fetch();
    uint16_t effectiveOffset = offset + (P.X ? (X & 0xFF) : X);
    uint16_t dpAddr = (D + effectiveOffset) & 0xFFFF;
    uint16_t pointer = readWord(dpAddr);
    return (DB << 16) | pointer;
}

uint32_t CPU::addrDirectPageIndirectIndexedY() {
    uint8_t offset = fetch();
    uint16_t dpAddr = (D + offset) & 0xFFFF;
    uint16_t pointer = readWord(dpAddr);
    uint16_t effectiveAddr = pointer + (P.X ? (Y & 0xFF) : Y);
    return (DB << 16) | effectiveAddr;
}

uint32_t CPU::addrDirectPageIndirectLongIndexedY() {
    uint8_t offset = fetch();
    uint16_t dpAddr = (D + offset) & 0xFFFF;
    uint32_t pointer = readLong(dpAddr);
    pointer += (P.X ? (Y & 0xFF) : Y);
    return pointer & 0xFFFFFF;
}

uint32_t CPU::addrStackRelative() {
    uint8_t offset = fetch();
    return (SP + offset) & 0xFFFF;
}

uint32_t CPU::addrStackRelativeIndirectIndexedY() {
    uint8_t offset = fetch();
    uint16_t spAddr = (SP + offset) & 0xFFFF;
    uint16_t pointer = readWord(spAddr);
    uint16_t effectiveAddr = pointer + (P.X ? (Y & 0xFF) : Y);
    return (DB << 16) | effectiveAddr;
}

// Instruction implementations

// Data Transfer - LDA
void CPU::opLDA(uint32_t addr) {
    if (P.M) {
        // 8-bit mode
        uint8_t value = readByte(addr);
        A = (A & 0xFF00) | value;
        setNZ8(value);
    } else {
        // 16-bit mode
        uint16_t value = readWord(addr);
        A = value;
        setNZ16(value);
    }
}

// Data Transfer - LDX
void CPU::opLDX(uint32_t addr) {
    if (P.X) {
        // 8-bit mode
        uint8_t value = readByte(addr);
        X = value;
        setNZ8(value);
    } else {
        // 16-bit mode
        uint16_t value = readWord(addr);
        X = value;
        setNZ16(value);
    }
}

// Data Transfer - LDY
void CPU::opLDY(uint32_t addr) {
    if (P.X) {
        // 8-bit mode
        uint8_t value = readByte(addr);
        Y = value;
        setNZ8(value);
    } else {
        // 16-bit mode
        uint16_t value = readWord(addr);
        Y = value;
        setNZ16(value);
    }
}

// Data Transfer - STA
void CPU::opSTA(uint32_t addr) {
    if (P.M) {
        // 8-bit mode
        writeByte(addr, A & 0xFF);
    } else {
        // 16-bit mode
        writeWord(addr, A);
    }
}

// Data Transfer - STX
void CPU::opSTX(uint32_t addr) {
    if (P.X) {
        // 8-bit mode
        writeByte(addr, X & 0xFF);
    } else {
        // 16-bit mode
        writeWord(addr, X);
    }
}

// Data Transfer - STY
void CPU::opSTY(uint32_t addr) {
    if (P.X) {
        // 8-bit mode
        writeByte(addr, Y & 0xFF);
    } else {
        // 16-bit mode
        writeWord(addr, Y);
    }
}

// Data Transfer - STZ
void CPU::opSTZ(uint32_t addr) {
    if (P.M) {
        // 8-bit mode
        writeByte(addr, 0);
    } else {
        // 16-bit mode
        writeWord(addr, 0);
    }
}

// Arithmetic - ADC
void CPU::opADC(uint32_t addr) {
    if (P.M) {
        // 8-bit mode
        uint8_t value = readByte(addr);
        uint8_t acc = A & 0xFF;
        uint16_t result = acc + value + (P.C ? 1 : 0);

        if (P.D) {
            // BCD mode
            uint16_t tempResult = (acc & 0x0F) + (value & 0x0F) + (P.C ? 1 : 0);
            if (tempResult > 0x09) tempResult += 0x06;
            tempResult = (acc & 0xF0) + (value & 0xF0) + (tempResult > 0x0F ? 0x10 : 0) + (tempResult & 0x0F);
            if (tempResult > 0x9F) tempResult += 0x60;
            result = tempResult;
        }

        P.C = result > 0xFF;
        P.V = ((acc ^ result) & (value ^ result) & 0x80) != 0;
        A = (A & 0xFF00) | (result & 0xFF);
        setNZ8(result & 0xFF);
    } else {
        // 16-bit mode
        uint16_t value = readWord(addr);
        uint32_t result = A + value + (P.C ? 1 : 0);

        P.C = result > 0xFFFF;
        P.V = ((A ^ result) & (value ^ result) & 0x8000) != 0;
        A = result & 0xFFFF;
        setNZ16(A);
    }
}

// Arithmetic - SBC
void CPU::opSBC(uint32_t addr) {
    if (P.M) {
        // 8-bit mode
        uint8_t value = readByte(addr);
        uint8_t acc = A & 0xFF;
        uint16_t result = acc - value - (P.C ? 0 : 1);

        if (P.D) {
            // BCD mode
            uint16_t tempResult = (acc & 0x0F) - (value & 0x0F) - (P.C ? 0 : 1);
            if (tempResult & 0x10) tempResult = ((tempResult - 0x06) & 0x0F) | ((acc & 0xF0) - (value & 0xF0) - 0x10);
            else tempResult = (tempResult & 0x0F) | ((acc & 0xF0) - (value & 0xF0));
            if (tempResult & 0x100) tempResult -= 0x60;
            result = tempResult;
        }

        P.C = result < 0x100;
        P.V = ((acc ^ value) & (acc ^ result) & 0x80) != 0;
        A = (A & 0xFF00) | (result & 0xFF);
        setNZ8(result & 0xFF);
    } else {
        // 16-bit mode
        uint16_t value = readWord(addr);
        uint32_t result = A - value - (P.C ? 0 : 1);

        P.C = result < 0x10000;
        P.V = ((A ^ value) & (A ^ result) & 0x8000) != 0;
        A = result & 0xFFFF;
        setNZ16(A);
    }
}

// Arithmetic - MUL (Hardware multiply)
void CPU::opMUL(uint32_t addr) {
    if (P.M) {
        // 8-bit mode
        uint8_t value = readByte(addr);
        uint8_t acc = A & 0xFF;
        uint16_t result = acc * value;
        A = result;  // Store full 16-bit result
        setNZ16(result);
        P.C = (result > 0xFF);  // Carry if overflow
        cycleCount += 8;
    } else {
        // 16-bit mode
        uint16_t value = readWord(addr);
        uint32_t result = A * value;
        A = result & 0xFFFF;  // Store low 16 bits
        setNZ16(A);
        P.C = (result > 0xFFFF);  // Carry if overflow
        cycleCount += 8;
    }
}

// Arithmetic - DIV (Hardware divide)
void CPU::opDIV() {
    // DIV has special addressing - will be implemented in executeInstruction
    cycleCount += 8;
}

// Arithmetic - INC
void CPU::opINC(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        value++;
        writeByte(addr, value);
        setNZ8(value);
    } else {
        uint16_t value = readWord(addr);
        value++;
        writeWord(addr, value);
        setNZ16(value);
    }
}

// Arithmetic - DEC
void CPU::opDEC(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        value--;
        writeByte(addr, value);
        setNZ8(value);
    } else {
        uint16_t value = readWord(addr);
        value--;
        writeWord(addr, value);
        setNZ16(value);
    }
}

void CPU::opINX() {
    if (P.X) {
        X = (X + 1) & 0xFF;
        setNZ8(X & 0xFF);
    } else {
        X = (X + 1) & 0xFFFF;
        setNZ16(X);
    }
}

void CPU::opINY() {
    if (P.X) {
        Y = (Y + 1) & 0xFF;
        setNZ8(Y & 0xFF);
    } else {
        Y = (Y + 1) & 0xFFFF;
        setNZ16(Y);
    }
}

void CPU::opDEX() {
    if (P.X) {
        X = (X - 1) & 0xFF;
        setNZ8(X & 0xFF);
    } else {
        X = (X - 1) & 0xFFFF;
        setNZ16(X);
    }
}

void CPU::opDEY() {
    if (P.X) {
        Y = (Y - 1) & 0xFF;
        setNZ8(Y & 0xFF);
    } else {
        Y = (Y - 1) & 0xFFFF;
        setNZ16(Y);
    }
}

// Logical - AND
void CPU::opAND(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        A = (A & 0xFF00) | ((A & 0xFF) & value);
        setNZ8(A & 0xFF);
    } else {
        uint16_t value = readWord(addr);
        A = A & value;
        setNZ16(A);
    }
}

// Logical - ORA
void CPU::opORA(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        A = (A & 0xFF00) | ((A & 0xFF) | value);
        setNZ8(A & 0xFF);
    } else {
        uint16_t value = readWord(addr);
        A = A | value;
        setNZ16(A);
    }
}

// Logical - XOR (EOR)
void CPU::opXOR(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        A = (A & 0xFF00) | ((A & 0xFF) ^ value);
        setNZ8(A & 0xFF);
    } else {
        uint16_t value = readWord(addr);
        A = A ^ value;
        setNZ16(A);
    }
}

// Logical - BIT
void CPU::opBIT(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        uint8_t result = (A & 0xFF) & value;
        P.Z = (result == 0);
        // BIT sets N and V from memory value
        P.N = (value & 0x80) != 0;
        P.V = (value & 0x40) != 0;
    } else {
        uint16_t value = readWord(addr);
        uint16_t result = A & value;
        P.Z = (result == 0);
        P.N = (value & 0x8000) != 0;
        P.V = (value & 0x4000) != 0;
    }
}

// Control - NOP
void CPU::opNOP() {
    cycleCount += 2;
}

// Control - HLT
void CPU::opHLT() {
    state = CPUState::Halted;
    cycleCount += 0;  // HLT consumes 0 cycles
}

// Control - LOOP
void CPU::opLOOP() {
    loopCounter = fetch16();
    loopStart = PC;
    cycleCount += 2;
}

// Control - LPEND
void CPU::opLPEND() {
    if (loopCounter > 0) {
        loopCounter--;
        if (loopCounter > 0) {
            PC = loopStart;
        }
    }
    cycleCount += 1;
}

// Control - SDB (Set Data Bank)
void CPU::opSDB() {
    DB = fetch();
    cycleCount += 2;
}

// Execute instruction (main decoder - COMPLETE 256 opcodes)
void CPU::executeInstruction() {
    uint8_t opcode = fetch();

    // Complete opcode dispatch table (all 256 opcodes)
    switch (opcode) {
        // 0x00: NOP
        case 0x00: opNOP(); break;

        // 0x01: BIT #const
        case 0x01: { uint32_t addr = addrImmediate(); P.Z = (P.M ? ((A & 0xFF) & readByte(addr)) : (A & readWord(addr))) == 0; cycleCount += 2; } break;

        // 0x02-0x05: BIT variants
        case 0x02: opBIT(addrAbsolute()); cycleCount += 3; break;
        case 0x03: opBIT(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x04: opBIT(addrDirectPage()); cycleCount += 3; break;
        case 0x05: opBIT(addrDirectPageIndexedX()); cycleCount += 4; break;

        // 0x06-0x0B: Branches
        case 0x06: opBMI(); break;
        case 0x07: opBRA(); break;
        case 0x08: opBRL(); break;
        case 0x09: opBVS(); break;
        case 0x0A: opBCS(); break;  // BGE is alias
        case 0x0B: opBEQ(); break;

        // 0x0C-0x12: JMP/JSR variants
        case 0x0C: { uint16_t base = fetch16(); uint16_t offset = base + (P.X ? (X & 0xFF) : X); opJMP(readWord(offset)); cycleCount += 4; } break;  // JMP (addr,X)
        case 0x0D: opJMP(readWord(fetch16())); cycleCount += 4; break;  // JMP (addr)
        case 0x0E: opJMP(readLong(fetch16())); cycleCount += 4; break;  // JMP [addr]
        case 0x0F: opJMP((DB << 16) | fetch16()); cycleCount += 4; break;  // JMP addr
        case 0x10: opJMP(fetch24()); cycleCount += 4; break;  // JMP long
        case 0x11: { uint16_t base = fetch16(); uint16_t offset = base + (P.X ? (X & 0xFF) : X); opJSR(readWord(offset)); } break;  // JSR (addr,X)
        case 0x12: opJSR((DB << 16) | fetch16()); break;  // JSR addr

        // 0x13-0x1B: Control & extended
        case 0x13: opLOOP(); break;
        case 0x14: opLPEND(); break;
        case 0x15: opCALL(); break;
        case 0x16: opRET(); break;
        case 0x17: opRTI(); break;
        case 0x18: opRTL(); break;
        case 0x19: opRTS(); break;
        case 0x1A: opSEP(); break;
        case 0x1B: opSDB(); break;

        // 0x1C-0x23: WAI, XBA, XCHG variants
        case 0x1C: opWAI(); break;
        case 0x1D: opXBA(); break;
        case 0x1E: { uint32_t longAddr = fetch24(); uint8_t dp = fetch(); uint16_t dpAddr = (D + dp) & 0xFFFF; uint16_t val1 = readWord(longAddr); uint16_t val2 = readWord(dpAddr); writeWord(longAddr, val2); writeWord(dpAddr, val1); cycleCount += 8; } break;  // XCHG long,dp
        case 0x1F: { uint8_t dp = fetch(); uint16_t dpAddr = (D + dp) & 0xFFFF; uint16_t temp = X; X = readWord(dpAddr); writeWord(dpAddr, temp); cycleCount += 8; } break;  // XCHG X,dp
        case 0x20: { uint32_t addr = fetch24(); uint16_t temp = X; X = readWord(addr); writeWord(addr, temp); cycleCount += 8; } break;  // XCHG X,long
        case 0x21: { uint16_t temp = X; X = Y; Y = temp; cycleCount += 8; } break;  // XCHG X,Y
        case 0x22: { uint8_t dp = fetch(); uint16_t dpAddr = (D + dp) & 0xFFFF; uint16_t temp = Y; Y = readWord(dpAddr); writeWord(dpAddr, temp); cycleCount += 8; } break;  // XCHG Y,dp
        case 0x23: { uint32_t addr = fetch24(); uint16_t temp = Y; Y = readWord(addr); writeWord(addr, temp); cycleCount += 8; } break;  // XCHG Y,long

        // 0x24-0x26: Register transfers
        case 0x24: opTXY(); break;
        case 0x25: opTYA(); break;
        case 0x26: opTYX(); break;

        // 0x27-0x2F: Stack operations
        case 0x27: opPUSH(); break;
        case 0x28: opPEA(); break;
        case 0x29: opPEI(); break;
        case 0x2A: opPER(); break;
        case 0x2B: opPHA(); break;
        case 0x2C: opPHB(); break;
        case 0x2D: opPHD(); break;
        case 0x2E: opPHK(); break;
        case 0x2F: opPHP(); break;

        // 0x30-0x36: Stack operations continued
        case 0x30: opPHX(); break;
        case 0x31: opPHY(); break;
        case 0x32: opPLA(); break;
        case 0x33: opPOPF(); break;

        // 0x34-0x3F: LDA variants
        case 0x34: opLDA(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0x35: opLDA(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0x36: opLDA(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;
        case 0x37: opLDA(addrImmediate()); cycleCount += 2; break;
        case 0x38: opLDA(addrAbsolute()); cycleCount += 3; break;
        case 0x39: opLDA(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x3A: opLDA(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0x3B: opLDA(addrDirectPage()); cycleCount += 3; break;
        case 0x3C: opLDA(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0x3D: opLDA(addrAbsoluteLong()); cycleCount += 5; break;
        case 0x3E: opLDA(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0x3F: opLDA(addrStackRelative()); cycleCount += 4; break;

        // 0x40-0x44: LDX variants
        case 0x40: opLDX(addrImmediate()); cycleCount += 2; break;
        case 0x41: opLDX(addrAbsolute()); cycleCount += 3; break;
        case 0x42: opLDX(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0x43: opLDX(addrDirectPage()); cycleCount += 3; break;
        case 0x44: opLDX(addrDirectPageIndexedY()); cycleCount += 4; break;

        // 0x45-0x49: LDY variants
        case 0x45: opLDY(addrImmediate()); cycleCount += 2; break;
        case 0x46: opLDY(addrAbsolute()); cycleCount += 3; break;
        case 0x47: opLDY(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x48: opLDY(addrDirectPage()); cycleCount += 3; break;
        case 0x49: opLDY(addrDirectPageIndexedX()); cycleCount += 4; break;

        // 0x4A-0x50: STA variants
        case 0x4A: opSTA(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0x4B: opSTA(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0x4C: opSTA(addrDirectPageIndirect()); cycleCount += 5; break;
        case 0x4D: opSTA(addrDirectPageIndirectIndexedY()); cycleCount += 6; break;
        case 0x4E: opSTA(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0x4F: opSTA(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0x50: opSTA(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;

        // 0x51-0x5D: STA, STX, STY, STZ variants
        case 0x51: opSTA(addrAbsolute()); cycleCount += 3; break;
        case 0x52: opSTA(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x53: opSTA(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0x54: opSTA(addrDirectPage()); cycleCount += 3; break;
        case 0x55: opSTA(addrAbsoluteLong()); cycleCount += 5; break;
        case 0x56: opSTA(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0x57: opSTA(addrStackRelative()); cycleCount += 4; break;
        case 0x58: opSTX(addrAbsolute()); cycleCount += 3; break;
        case 0x59: opSTX(addrDirectPage()); cycleCount += 3; break;
        case 0x5A: opSTX(addrDirectPageIndexedY()); cycleCount += 4; break;
        case 0x5B: opSTY(addrAbsolute()); cycleCount += 3; break;
        case 0x5C: opSTY(addrDirectPage()); cycleCount += 3; break;
        case 0x5D: opSTY(addrDirectPageIndexedX()); cycleCount += 4; break;

        // 0x5E-0x62: STZ, SEC, SED, SEI, BRK
        case 0x5E: opSTZ(addrAbsolute()); cycleCount += 3; break;
        case 0x5F: opSTZ(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x60: opSTZ(addrDirectPage()); cycleCount += 3; break;
        case 0x61: opSTZ(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0x62: opBRK(); break;

        // 0x63-0x65: SEC, SED, SEI
        case 0x63: opSEC(); break;
        case 0x64: opSED(); break;
        case 0x65: opSEI(); break;

        // 0x66: ADC dp (special case - already in ADC section but can appear here too)
        case 0x66: opADC(addrDirectPage()); cycleCount += 3; break;

        // 0x67-0x68: Block moves
        case 0x67: opMVN(); break;
        case 0x68: opMVP(); break;

        // 0x69-0x78: INC, DEC variants
        case 0x69: { if (P.M) { A = (A & 0xFF00) | ((A + 1) & 0xFF); setNZ8(A & 0xFF); } else { A = (A + 1) & 0xFFFF; setNZ16(A); } cycleCount += 2; } break;  // INC A
        case 0x6A: opINC(addrAbsolute()); cycleCount += 3; break;
        case 0x6B: opINC(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x6C: opINC(addrDirectPage()); cycleCount += 5; break;
        case 0x6D: opINC(addrDirectPageIndexedX()); cycleCount += 6; break;
        case 0x6E: opINC(addrAbsoluteLong()); cycleCount += 6; break;
        case 0x6F: opINX(); cycleCount += 2; break;
        case 0x70: opINY(); cycleCount += 2; break;
        case 0x71: { if (P.M) { A = (A & 0xFF00) | ((A - 1) & 0xFF); setNZ8(A & 0xFF); } else { A = (A - 1) & 0xFFFF; setNZ16(A); } cycleCount += 2; } break;  // DEC A
        case 0x72: opDEC(addrAbsolute()); cycleCount += 3; break;
        case 0x73: opDEC(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x74: opDEC(addrDirectPage()); cycleCount += 5; break;
        case 0x75: opDEC(addrDirectPageIndexedX()); cycleCount += 6; break;
        case 0x76: opDEC(addrAbsoluteLong()); cycleCount += 6; break;
        case 0x77: opDEX(); cycleCount += 2; break;
        case 0x78: opDEY(); cycleCount += 2; break;

        // 0x79-0x7F: CPX, CPY, CLD, CLI, CLV, CLC, REP
        case 0x79: opCPX(addrAbsolute()); cycleCount += 3; break;
        case 0x7A: opCPY(addrAbsolute()); cycleCount += 3; break;
        case 0x7B: opCLD(); break;
        case 0x7C: opCLI(); break;
        case 0x7D: opCLV(); break;
        case 0x7E: opCLC(); break;
        case 0x7F: opREP(); break;

        // 0x80-0x87: ROL, ROR variants
        case 0x80: opROL(0, true); cycleCount += 2; break;  // ROL A
        case 0x81: opROL(addrAbsolute(), false); cycleCount += 3; break;
        case 0x82: opROL(addrAbsoluteIndexedX(), false); cycleCount += 3; break;
        case 0x83: opROL(addrDirectPage(), false); cycleCount += 5; break;
        case 0x84: opROR(0, true); cycleCount += 2; break;  // ROR A
        case 0x85: opROR(addrAbsolute(), false); cycleCount += 3; break;
        case 0x86: opROR(addrAbsoluteIndexedX(), false); cycleCount += 3; break;
        case 0x87: opROR(addrDirectPage(), false); cycleCount += 5; break;

        // 0x88-0x94: SHL, SHR, RCL, ASL, LSR, DIV
        case 0x88: opSHL(false); break;  // SHL X
        case 0x89: opSHL(true); break;   // SHL Y
        case 0x8A: opSHR(false); break;  // SHR X
        case 0x8B: opSHR(true); break;   // SHR Y
        case 0x8C: opRCL(); break;
        case 0x8D: opLSR(0, true); cycleCount += 2; break;  // LSR A
        case 0x8E: opLSR(addrAbsolute(), false); cycleCount += 3; break;
        case 0x8F: opLSR(addrAbsoluteIndexedX(), false); cycleCount += 3; break;
        case 0x90: opLSR(addrDirectPage(), false); cycleCount += 5; break;
        case 0x91: opLSR(addrDirectPageIndexedX(), false); cycleCount += 6; break;
        case 0x92: { if (Y == 0) { P.C = true; } else { uint16_t result = X / Y; X = result; P.C = false; } cycleCount += 8; } break;  // DIV X,Y
        case 0x93: { uint32_t addr = fetch24(); uint16_t divisor = readWord(addr + (P.X ? (X & 0xFF) : X)); if (divisor == 0) { P.C = true; } else { X = X / divisor; P.C = false; } cycleCount += 8; } break;  // DIV long,X
        case 0x94: { uint32_t addr = fetch24(); uint16_t divisor = readWord(addr + (P.X ? (Y & 0xFF) : Y)); if (divisor == 0) { P.C = true; } else { X = X / divisor; P.C = false; } cycleCount += 8; } break;  // DIV long,Y

        // 0x95-0x9F: XOR variants
        case 0x95: opXOR(addrAbsolute()); cycleCount += 3; break;
        case 0x96: opXOR(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0x97: opXOR(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0x98: opASL(0, true); cycleCount += 2; break;  // ASL A
        case 0x99: opASL(addrAbsolute(), false); cycleCount += 3; break;
        case 0x9A: opASL(addrAbsoluteIndexedX(), false); cycleCount += 3; break;
        case 0x9B: opASL(addrDirectPage(), false); cycleCount += 5; break;
        case 0x9C: opASL(addrDirectPageIndexedX(), false); cycleCount += 6; break;
        case 0x9D: opXOR(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0x9E: opXOR(addrDirectPageIndirect()); cycleCount += 5; break;
        case 0x9F: opXOR(addrDirectPageIndirectIndexedY()); cycleCount += 5; break;

        // 0xA0-0xAE: CMP variants
        case 0xA0: opCMP(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0xA1: opCMP(addrDirectPageIndirect()); cycleCount += 5; break;
        case 0xA2: opCMP(addrDirectPageIndirectIndexedY()); cycleCount += 5; break;
        case 0xA3: opCMP(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0xA4: opCMP(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0xA5: opCMP(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;
        case 0xA6: opCMP(addrImmediate()); cycleCount += 2; break;
        case 0xA7: opCMP(addrAbsolute()); cycleCount += 3; break;
        case 0xA8: opCMP(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0xA9: opCMP(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0xAA: opCMP(addrDirectPage()); cycleCount += 3; break;
        case 0xAB: opCMP(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0xAC: opCMP(addrAbsoluteLong()); cycleCount += 5; break;
        case 0xAD: opCMP(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0xAE: opCMP(addrStackRelative()); cycleCount += 4; break;

        // 0xAF: COP
        case 0xAF: opCOP(); break;

        // 0xB0-0xBD: ADC variants
        case 0xB0: opADC(addrDirectPageIndirectIndexedY()); cycleCount += 5; break;
        case 0xB1: opADC(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0xB2: opADC(addrDirectPageIndirect()); cycleCount += 5; break;
        case 0xB3: opADC(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0xB4: opADC(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0xB5: opADC(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;
        case 0xB6: opADC(addrImmediate()); cycleCount += 2; break;
        case 0xB7: opADC(addrAbsolute()); cycleCount += 3; break;
        case 0xB8: opADC(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0xB9: opADC(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0xBA: opADC(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0xBB: opADC(addrAbsoluteLong()); cycleCount += 5; break;
        case 0xBC: opADC(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0xBD: opADC(addrStackRelative()); cycleCount += 4; break;

        // 0xBE-0xCC: AND variants
        case 0xBE: opAND(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0xBF: opAND(addrDirectPageIndirect()); cycleCount += 5; break;
        case 0xC0: opAND(addrDirectPageIndirectIndexedY()); cycleCount += 5; break;
        case 0xC1: opAND(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0xC2: opAND(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0xC3: opAND(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;
        case 0xC4: opAND(addrImmediate()); cycleCount += 2; break;
        case 0xC5: opAND(addrAbsolute()); cycleCount += 3; break;
        case 0xC6: opAND(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0xC7: opAND(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0xC8: opAND(addrDirectPage()); cycleCount += 3; break;
        case 0xC9: opAND(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0xCA: opAND(addrAbsoluteLong()); cycleCount += 5; break;
        case 0xCB: opAND(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0xCC: opAND(addrStackRelative()); cycleCount += 4; break;

        // 0xCD-0xDB: ORA variants
        case 0xCD: opORA(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0xCE: opORA(addrDirectPageIndirect()); cycleCount += 5; break;
        case 0xCF: opORA(addrDirectPageIndirectIndexedY()); cycleCount += 5; break;
        case 0xD0: opORA(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0xD1: opORA(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0xD2: opORA(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;
        case 0xD3: opORA(addrImmediate()); cycleCount += 2; break;
        case 0xD4: opORA(addrAbsolute()); cycleCount += 3; break;
        case 0xD5: opORA(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0xD6: opORA(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0xD7: opORA(addrDirectPage()); cycleCount += 3; break;
        case 0xD8: opORA(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0xD9: opORA(addrAbsoluteLong()); cycleCount += 5; break;
        case 0xDA: opORA(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0xDB: opORA(addrStackRelative()); cycleCount += 4; break;

        // 0xDC-0xE9: SBC variants
        case 0xDC: opSBC(addrDirectPageIndexedIndirectX()); cycleCount += 6; break;
        case 0xDD: opSBC(addrDirectPageIndirectIndexedY()); cycleCount += 5; break;
        case 0xDE: opSBC(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;
        case 0xDF: opSBC(addrDirectPageIndirectLong()); cycleCount += 6; break;
        case 0xE0: opSBC(addrDirectPageIndirectLongIndexedY()); cycleCount += 6; break;
        case 0xE1: opSBC(addrImmediate()); cycleCount += 2; break;
        case 0xE2: opSBC(addrAbsolute()); cycleCount += 3; break;
        case 0xE3: opSBC(addrAbsoluteIndexedX()); cycleCount += 3; break;
        case 0xE4: opSBC(addrAbsoluteIndexedY()); cycleCount += 3; break;
        case 0xE5: opSBC(addrDirectPage()); cycleCount += 3; break;
        case 0xE6: opSBC(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0xE7: opSBC(addrAbsoluteLong()); cycleCount += 5; break;
        case 0xE8: opSBC(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0xE9: opSBC(addrStackRelative()); cycleCount += 4; break;

        // 0xEA-0xF8: MUL variants and XOR
        case 0xEA: opMUL(addrDirectPageIndexedIndirectX()); break;
        case 0xEB: opMUL(addrDirectPageIndirectIndexedY()); break;
        case 0xEC: opMUL(addrStackRelativeIndirectIndexedY()); break;
        case 0xED: opMUL(addrDirectPageIndirectLong()); break;
        case 0xEE: opMUL(addrDirectPageIndirectLongIndexedY()); break;
        case 0xEF: opMUL(addrImmediate()); break;
        case 0xF0: opMUL(addrAbsolute()); break;
        case 0xF1: opMUL(addrAbsoluteIndexedX()); break;
        case 0xF2: opMUL(addrAbsoluteIndexedY()); break;
        case 0xF3: opMUL(addrDirectPage()); break;
        case 0xF4: opMUL(addrDirectPageIndexedX()); break;
        case 0xF5: opMUL(addrAbsoluteLong()); break;
        case 0xF6: opMUL(addrAbsoluteLongIndexedX()); break;
        case 0xF7: opMUL(addrStackRelative()); break;
        case 0xF8: opXOR(addrStackRelativeIndirectIndexedY()); cycleCount += 7; break;

        // 0xF9-0xFE: XOR variants
        case 0xF9: opXOR(addrImmediate()); cycleCount += 2; break;
        case 0xFA: opXOR(addrDirectPage()); cycleCount += 3; break;
        case 0xFB: opXOR(addrDirectPageIndexedX()); cycleCount += 4; break;
        case 0xFC: opXOR(addrAbsoluteLong()); cycleCount += 5; break;
        case 0xFD: opXOR(addrAbsoluteLongIndexedX()); cycleCount += 5; break;
        case 0xFE: opXOR(addrStackRelative()); cycleCount += 4; break;

        // 0xFF: HLT
        case 0xFF: opHLT(); break;
    }
}

// === SHIFT AND ROTATE OPERATIONS ===

void CPU::opASL(uint32_t addr, bool accumulator) {
    if (accumulator) {
        if (P.M) {
            uint8_t value = A & 0xFF;
            P.C = (value & 0x80) != 0;
            value = (value << 1) & 0xFF;
            A = (A & 0xFF00) | value;
            setNZ8(value);
        } else {
            P.C = (A & 0x8000) != 0;
            A = (A << 1) & 0xFFFF;
            setNZ16(A);
        }
    } else {
        if (P.M) {
            uint8_t value = readByte(addr);
            P.C = (value & 0x80) != 0;
            value = (value << 1) & 0xFF;
            writeByte(addr, value);
            setNZ8(value);
        } else {
            uint16_t value = readWord(addr);
            P.C = (value & 0x8000) != 0;
            value = (value << 1) & 0xFFFF;
            writeWord(addr, value);
            setNZ16(value);
        }
    }
}

void CPU::opLSR(uint32_t addr, bool accumulator) {
    if (accumulator) {
        if (P.M) {
            uint8_t value = A & 0xFF;
            P.C = (value & 0x01) != 0;
            value = value >> 1;
            A = (A & 0xFF00) | value;
            setNZ8(value);
        } else {
            P.C = (A & 0x0001) != 0;
            A = A >> 1;
            setNZ16(A);
        }
    } else {
        if (P.M) {
            uint8_t value = readByte(addr);
            P.C = (value & 0x01) != 0;
            value = value >> 1;
            writeByte(addr, value);
            setNZ8(value);
        } else {
            uint16_t value = readWord(addr);
            P.C = (value & 0x0001) != 0;
            value = value >> 1;
            writeWord(addr, value);
            setNZ16(value);
        }
    }
}

void CPU::opROL(uint32_t addr, bool accumulator) {
    if (accumulator) {
        if (P.M) {
            uint8_t value = A & 0xFF;
            bool oldCarry = P.C;
            P.C = (value & 0x80) != 0;
            value = ((value << 1) | (oldCarry ? 1 : 0)) & 0xFF;
            A = (A & 0xFF00) | value;
            setNZ8(value);
        } else {
            bool oldCarry = P.C;
            P.C = (A & 0x8000) != 0;
            A = ((A << 1) | (oldCarry ? 1 : 0)) & 0xFFFF;
            setNZ16(A);
        }
    } else {
        if (P.M) {
            uint8_t value = readByte(addr);
            bool oldCarry = P.C;
            P.C = (value & 0x80) != 0;
            value = ((value << 1) | (oldCarry ? 1 : 0)) & 0xFF;
            writeByte(addr, value);
            setNZ8(value);
        } else {
            uint16_t value = readWord(addr);
            bool oldCarry = P.C;
            P.C = (value & 0x8000) != 0;
            value = ((value << 1) | (oldCarry ? 1 : 0)) & 0xFFFF;
            writeWord(addr, value);
            setNZ16(value);
        }
    }
}

void CPU::opROR(uint32_t addr, bool accumulator) {
    if (accumulator) {
        if (P.M) {
            uint8_t value = A & 0xFF;
            bool oldCarry = P.C;
            P.C = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            A = (A & 0xFF00) | value;
            setNZ8(value);
        } else {
            bool oldCarry = P.C;
            P.C = (A & 0x0001) != 0;
            A = (A >> 1) | (oldCarry ? 0x8000 : 0);
            setNZ16(A);
        }
    } else {
        if (P.M) {
            uint8_t value = readByte(addr);
            bool oldCarry = P.C;
            P.C = (value & 0x01) != 0;
            value = (value >> 1) | (oldCarry ? 0x80 : 0);
            writeByte(addr, value);
            setNZ8(value);
        } else {
            uint16_t value = readWord(addr);
            bool oldCarry = P.C;
            P.C = (value & 0x0001) != 0;
            value = (value >> 1) | (oldCarry ? 0x8000 : 0);
            writeWord(addr, value);
            setNZ16(value);
        }
    }
}

void CPU::opSHL(bool isY) {
    if (isY) {
        P.C = (Y & (P.X ? 0x80 : 0x8000)) != 0;
        Y = (Y << 1) & (P.X ? 0xFF : 0xFFFF);
    } else {
        P.C = (X & (P.X ? 0x80 : 0x8000)) != 0;
        X = (X << 1) & (P.X ? 0xFF : 0xFFFF);
    }
    cycleCount += 2;
}

void CPU::opSHR(bool isY) {
    if (isY) {
        P.C = (Y & 0x01) != 0;
        Y = Y >> 1;
    } else {
        P.C = (X & 0x01) != 0;
        X = X >> 1;
    }
    cycleCount += 2;
}

void CPU::opRCL() {
    // RCL A - Rotate through carry left
    if (P.M) {
        uint8_t value = A & 0xFF;
        bool oldCarry = P.C;
        P.C = (value & 0x80) != 0;
        value = ((value << 1) | (oldCarry ? 1 : 0)) & 0xFF;
        A = (A & 0xFF00) | value;
        setNZ8(value);
    } else {
        bool oldCarry = P.C;
        P.C = (A & 0x8000) != 0;
        A = ((A << 1) | (oldCarry ? 1 : 0)) & 0xFFFF;
        setNZ16(A);
    }
    cycleCount += 4;
}

// === BRANCH OPERATIONS ===

void CPU::opBMI() {
    uint32_t addr = fetch24();
    if (P.N) {
        PC = addr & 0xFFFF;
        PB = (addr >> 16) & 0xFF;
    }
    cycleCount += 2;
}

void CPU::opBRA() {
    uint16_t addr = fetch16();
    PC = addr;
    cycleCount += 3;
}

void CPU::opBRL() {
    int16_t offset = (int16_t)fetch16();
    PC = (PC + offset) & 0xFFFF;
    cycleCount += 4;
}

void CPU::opBVS() {
    uint16_t addr = fetch16();
    if (P.V) {
        PC = addr;
    }
    cycleCount += 2;
}

void CPU::opBCS() {
    uint32_t addr = fetch24();
    if (P.C) {
        PC = addr & 0xFFFF;
        PB = (addr >> 16) & 0xFF;
    }
    cycleCount += 2;
}

void CPU::opBEQ() {
    uint32_t addr = fetch24();
    if (P.Z) {
        PC = addr & 0xFFFF;
        PB = (addr >> 16) & 0xFF;
    }
    cycleCount += 2;
}

// === JUMP AND SUBROUTINE OPERATIONS ===

void CPU::opJMP(uint32_t addr) {
    PC = addr & 0xFFFF;
    PB = (addr >> 16) & 0xFF;
}

void CPU::opJSR(uint32_t addr) {
    uint16_t returnAddr = PC - 1;
    push16(returnAddr);
    PC = addr & 0xFFFF;
    PB = (addr >> 16) & 0xFF;
    cycleCount += 4;
}

void CPU::opCALL() {
    uint32_t addr = fetch24();
    push8(PB);
    push16(PC);
    push8(P.toByte());
    PC = addr & 0xFFFF;
    PB = (addr >> 16) & 0xFF;
    cycleCount += 16;
}

void CPU::opRTS() {
    uint16_t addr = pull16();
    PC = (addr + 1) & 0xFFFF;
    cycleCount += 6;
}

void CPU::opRTL() {
    uint16_t offset = pull16();
    uint8_t bank = pull8();
    PC = (offset + 1) & 0xFFFF;
    PB = bank;
    cycleCount += 6;
}

void CPU::opRTI() {
    P.fromByte(pull8());
    PC = pull16();
    PB = pull8();
    cycleCount += 6;
}

void CPU::opRET() {
    // Return to OS - similar to RTI but different timing
    P.fromByte(pull8());
    PC = pull16();
    PB = pull8();
    cycleCount += 8;
}

// === STACK OPERATIONS ===

void CPU::opPHA() {
    if (P.M) {
        push8(A & 0xFF);
    } else {
        push16(A);
    }
    cycleCount += 3;
}

void CPU::opPHX() {
    if (P.X) {
        push8(X & 0xFF);
    } else {
        push16(X);
    }
    cycleCount += 3;
}

void CPU::opPHY() {
    if (P.X) {
        push8(Y & 0xFF);
    } else {
        push16(Y);
    }
    cycleCount += 3;
}

void CPU::opPHP() {
    push8(P.toByte());
    cycleCount += 3;
}

void CPU::opPHB() {
    push8(DB);
    cycleCount += 3;
}

void CPU::opPHD() {
    push16(D);
    cycleCount += 4;
}

void CPU::opPHK() {
    push8(PB);
    cycleCount += 3;
}

void CPU::opPUSH() {
    uint16_t value = fetch16();
    push16(value);
    cycleCount += 3;
}

void CPU::opPEA() {
    uint16_t addr = fetch16();
    push16(addr);
    cycleCount += 3;
}

void CPU::opPEI() {
    uint8_t dp = fetch();
    uint16_t dpAddr = (D + dp) & 0xFFFF;
    uint16_t value = readWord(dpAddr);
    push16(value);
    cycleCount += 6;
}

void CPU::opPER() {
    int16_t offset = (int16_t)fetch16();
    uint16_t addr = (PC + offset) & 0xFFFF;
    push16(addr);
    cycleCount += 6;
}

void CPU::opPLA() {
    if (P.M) {
        uint8_t value = pull8();
        A = (A & 0xFF00) | value;
        setNZ8(value);
    } else {
        A = pull16();
        setNZ16(A);
    }
    cycleCount += 4;
}

void CPU::opPOPF() {
    P.fromByte(pull8());
    cycleCount += 3;
}

// === COMPARISON OPERATIONS ===

void CPU::opCMP(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        uint8_t acc = A & 0xFF;
        uint16_t result = acc - value;
        P.C = acc >= value;
        setNZ8(result & 0xFF);
    } else {
        uint16_t value = readWord(addr);
        uint32_t result = A - value;
        P.C = A >= value;
        setNZ16(result & 0xFFFF);
    }
}

void CPU::opCPX(uint32_t addr) {
    if (P.X) {
        uint8_t value = readByte(addr);
        uint8_t reg = X & 0xFF;
        uint16_t result = reg - value;
        P.C = reg >= value;
        setNZ8(result & 0xFF);
    } else {
        uint16_t value = readWord(addr);
        uint32_t result = X - value;
        P.C = X >= value;
        setNZ16(result & 0xFFFF);
    }
}

void CPU::opCPY(uint32_t addr) {
    if (P.X) {
        uint8_t value = readByte(addr);
        uint8_t reg = Y & 0xFF;
        uint16_t result = reg - value;
        P.C = reg >= value;
        setNZ8(result & 0xFF);
    } else {
        uint16_t value = readWord(addr);
        uint32_t result = Y - value;
        P.C = Y >= value;
        setNZ16(result & 0xFFFF);
    }
}

// === REGISTER TRANSFER OPERATIONS ===

void CPU::opTXY() {
    Y = X;
    if (P.X) {
        setNZ8(Y & 0xFF);
    } else {
        setNZ16(Y);
    }
    cycleCount += 2;
}

void CPU::opTYA() {
    if (P.M && P.X) {
        // Both 8-bit
        A = (A & 0xFF00) | (Y & 0xFF);
        setNZ8(A & 0xFF);
    } else if (!P.M && !P.X) {
        // Both 16-bit
        A = Y;
        setNZ16(A);
    } else if (P.M && !P.X) {
        // A is 8-bit, Y is 16-bit
        A = (A & 0xFF00) | (Y & 0xFF);
        setNZ8(A & 0xFF);
    } else {
        // A is 16-bit, Y is 8-bit
        A = Y & 0xFF;
        setNZ16(A);
    }
    cycleCount += 2;
}

void CPU::opTYX() {
    X = Y;
    if (P.X) {
        setNZ8(X & 0xFF);
    } else {
        setNZ16(X);
    }
    cycleCount += 2;
}

void CPU::opXBA() {
    // Exchange A and B (swap high and low bytes)
    uint8_t al = A & 0xFF;
    uint8_t ah = (A >> 8) & 0xFF;
    A = (al << 8) | ah;
    cycleCount += 2;
}

void CPU::opXCHG() {
    // XCHG is handled inline in the dispatch table
    // This function shouldn't be called
}

// === PROCESSOR STATUS FLAG OPERATIONS ===

void CPU::opSEP() {
    uint8_t bits = fetch();
    P.fromByte(P.toByte() | bits);
    cycleCount += 3;
}

void CPU::opREP() {
    uint8_t bits = fetch();
    P.fromByte(P.toByte() & ~bits);
    cycleCount += 3;
}

void CPU::opSEC() {
    P.C = true;
    cycleCount += 2;
}

void CPU::opCLC() {
    P.C = false;
    cycleCount += 2;
}

void CPU::opSED() {
    P.D = true;
    cycleCount += 2;
}

void CPU::opCLD() {
    P.D = false;
    cycleCount += 2;
}

void CPU::opSEI() {
    P.I = true;
    cycleCount += 2;
}

void CPU::opCLI() {
    P.I = false;
    cycleCount += 2;
}

void CPU::opCLV() {
    P.V = false;
    cycleCount += 2;
}

// === INTERRUPT AND CONTROL OPERATIONS ===

void CPU::opBRK() {
    fetch();  // Skip signature byte
    push8(PB);
    push16(PC);
    push8(P.toByte());
    P.I = true;
    P.D = false;
    // Load BRK vector (would be from $00:FFFE/FFFF in real hardware)
    // For now, just halt
    state = CPUState::Halted;
    cycleCount += 7;
}

void CPU::opCOP() {
    fetch();  // Skip signature byte
    push8(PB);
    push16(PC);
    push8(P.toByte());
    P.I = true;
    P.D = false;
    // Load COP vector (would be from $00:FFF4/FFF5 in real hardware)
    // For now, just halt
    state = CPUState::Halted;
    cycleCount += 7;
}

void CPU::opWAI() {
    state = CPUState::Waiting;
    cycleCount += 3;
}

// === BLOCK MOVE OPERATIONS ===

void CPU::opMVN() {
    uint8_t srcBank = fetch();
    uint8_t destBank = fetch();

    while (A != 0xFFFF) {
        uint32_t srcAddr = (srcBank << 16) | X;
        uint32_t destAddr = (destBank << 16) | Y;
        uint8_t value = readByte(srcAddr);
        writeByte(destAddr, value);

        X = (X - 1) & 0xFFFF;
        Y = (Y - 1) & 0xFFFF;
        A = (A - 1) & 0xFFFF;

        cycleCount += 1;

        if (A == 0xFFFF) break;
    }

    DB = destBank;
}

void CPU::opMVP() {
    uint8_t srcBank = fetch();
    uint8_t destBank = fetch();

    while (A != 0xFFFF) {
        uint32_t srcAddr = (srcBank << 16) | X;
        uint32_t destAddr = (destBank << 16) | Y;
        uint8_t value = readByte(srcAddr);
        writeByte(destAddr, value);

        X = (X + 1) & 0xFFFF;
        Y = (Y + 1) & 0xFFFF;
        A = (A - 1) & 0xFFFF;

        cycleCount += 1;

        if (A == 0xFFFF) break;
    }

    DB = destBank;
}

} // namespace ZeroPoint
