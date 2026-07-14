#include "cpu.h"
#include "cpu_instructions.h"
#include "ppu.h"
#include "apu.h"
#include "dma.h"
#include "display.h"
#include "system.h"
#include <cstring>
#include <stdexcept>

namespace ZeroPoint {

CPU::CPU()
    : cycleCount(0),
      A(0), X(0), Y(0), SP(0x01FF), D(0), PC(0), PB(0), DB(0),
      loopCounter(0), loopStart(0),
      useMemoryMap(false),
      memory(nullptr), memorySize(0),
      ppuPtr(nullptr), apuPtr(nullptr), displayPtr(nullptr), dmaPtr(nullptr), systemPtr(nullptr),
      p1Direction(0), p1Control(PlayerInput::CTRL_CONNECTION), p1Buttons(0),
      state(CPUState::Running),
      instructionCount(0),
      irqPending(false), nmiPending(false), abortPending(false)
{
    // Initialize with 8-bit mode, interrupts disabled
    P.M = true;
    P.X = true;
    P.I = true;

    bankFast.fill(nullptr);
    bankScan.fill(false);
}

CPU::~CPU() {
}

void CPU::reset() {
    A = 0;
    X = 0;
    Y = 0;
    SP = 0x01FF;
    D = 0;
    // With a Boot ROM mapped, hardware reset lands there ($E0:0000) instead
    // of jumping straight into the cartridge at bank $00.
    PC = 0;
    PB = bootROMLoaded ? 0xE0 : 0x00;
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
    irqPending = false;
    nmiPending = false;
    abortPending = false;
}

void CPU::setMemory(uint8_t* mem, size_t size) {
    memory = mem;
    memorySize = size;
}

// Memory access with 24-bit addressing
uint8_t CPU::readByte(uint32_t address) {
    // If memory mapping is enabled, use it (optimized flag check)
    if (useMemoryMap) [[likely]] {
        // Fast path: a bank served by a single full-range ROM/RAM region maps
        // offset->data linearly (bankFast is prebuilt for exactly these). This
        // is nearly every access — instruction fetch and RAM/ROM data reads —
        // and skips the findRegion() call plus the region-type switch that
        // readMapped() would do. Windows have data==null and IO banks aren't in
        // bankFast, so both fall through to the full readMapped() path.
        uint8_t bank = (address >> 16) & 0xFF;
        MemoryRegion* r = bankFast[bank];
        if (r && r->data) [[likely]] {
            size_t dataOffset =
                static_cast<size_t>(bank - r->startBank) * 65536 + (address & 0xFFFF);
            return (dataOffset < r->dataSize) ? r->data[dataOffset] : 0xFF;
        }
        return readMapped(address);
    }

    // Otherwise fall back to legacy flat memory
    if (!memory || address >= memorySize) {
        return 0xFF;
    }
    return memory[address & (memorySize - 1)];
}

void CPU::writeByte(uint32_t address, uint8_t value) {
    // If memory mapping is enabled, use it (optimized flag check)
    if (useMemoryMap) [[likely]] {
        // Fast path mirrors readByte: RAM writes go straight to the backing
        // array, ROM writes are dropped. Only windows/IO (side-effecting) need
        // the full writeMapped() path.
        uint8_t bank = (address >> 16) & 0xFF;
        MemoryRegion* r = bankFast[bank];
        if (r) [[likely]] {
            if (r->type == MemoryRegion::RAM && r->data) {
                size_t dataOffset = static_cast<size_t>(bank - r->startBank) * 65536 +
                                    (address & 0xFFFF);
                if (dataOffset < r->dataSize) r->data[dataOffset] = value;
                return;
            }
            if (r->type == MemoryRegion::ROM) {
                return;  // ROM writes ignored
            }
            // Full-range window (PPU/APU) region: fall through to writeMapped.
        }
        writeMapped(address, value);
        return;
    }

    // Otherwise fall back to legacy flat memory
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
    writeByte((STACK_BANK << 16) | SP, value);
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
    return readByte((STACK_BANK << 16) | SP);
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

// Main execution
void CPU::step() {
    if (state != CPUState::Running) [[unlikely]] {
        return;
    }

    // Service any latched interrupt at this instruction boundary. If one
    // vectors, PC now points at the handler and we execute its first
    // instruction below.
    serviceInterrupts();

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

uint32_t CPU::addrAbsoluteIndexedX(bool alwaysPenalize) {
    uint16_t offset = fetch16();
    uint16_t effectiveOffset = offset + (P.X ? (X & 0xFF) : X);
    // 65C816: reads pay +1 cycle only when indexing crosses a page boundary;
    // writes/RMW always pay it (alwaysPenalize), since the fixup cycle can't
    // be skipped once the CPU commits to a write.
    bool crossed = (offset & 0xFF00) != (effectiveOffset & 0xFF00);
    if (crossed || alwaysPenalize) cycleCount += 1;
    return (DB << 16) | effectiveOffset;
}

uint32_t CPU::addrAbsoluteIndexedY(bool alwaysPenalize) {
    uint16_t offset = fetch16();
    uint16_t effectiveOffset = offset + (P.X ? (Y & 0xFF) : Y);
    bool crossed = (offset & 0xFF00) != (effectiveOffset & 0xFF00);
    if (crossed || alwaysPenalize) cycleCount += 1;  // page cross / forced write penalty
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

uint32_t CPU::addrDirectPageIndirectIndexedY(bool alwaysPenalize) {
    uint8_t offset = fetch();
    uint16_t dpAddr = (D + offset) & 0xFFFF;
    uint16_t pointer = readWord(dpAddr);
    uint16_t effectiveAddr = pointer + (P.X ? (Y & 0xFF) : Y);
    // 65C816: +1 cycle when the (dp),Y index crosses a page on a read; a
    // write (alwaysPenalize) always pays it.
    bool crossed = (pointer & 0xFF00) != (effectiveAddr & 0xFF00);
    if (crossed || alwaysPenalize) cycleCount += 1;
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
    return (STACK_BANK << 16) | ((SP + offset) & 0xFFFF);
}

uint32_t CPU::addrStackRelativeIndirectIndexedY() {
    uint8_t offset = fetch();
    uint32_t spAddr = (STACK_BANK << 16) | ((SP + offset) & 0xFFFF);
    uint16_t pointer = readWord(spAddr);
    uint16_t effectiveAddr = pointer + (P.X ? (Y & 0xFF) : Y);
    return (DB << 16) | effectiveAddr;
}

uint32_t CPU::addrAbsoluteLongIndexedY() {
    uint32_t addr = fetch24();
    addr += (P.X ? (Y & 0xFF) : Y);
    return addr & 0xFFFFFF;
}

// JMP (addr): 16-bit pointer read from bank 0; target stays in the current PB.
uint32_t CPU::addrAbsoluteIndirect() {
    uint16_t ptr = fetch16();
    uint16_t target = readWord(ptr);
    return (PB << 16) | target;
}

// JMP [addr]: 24-bit pointer read from bank 0; full 24-bit target.
uint32_t CPU::addrAbsoluteIndirectLong() {
    uint16_t ptr = fetch16();
    return readLong(ptr);
}

// JMP (addr,X): pointer = (addr + X) in the program bank; target stays in PB.
uint32_t CPU::addrAbsoluteIndexedIndirect() {
    uint16_t ptr = fetch16() + (P.X ? (X & 0xFF) : X);
    uint16_t target = readWord((PB << 16) | ptr);
    return (PB << 16) | target;
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

        if (P.D) {
            // BCD mode - apply the same nibble-pair correction as the 8-bit
            // path to the low and high bytes in turn, carrying between them.
            uint8_t aLo = A & 0xFF, aHi = (A >> 8) & 0xFF;
            uint8_t vLo = value & 0xFF, vHi = (value >> 8) & 0xFF;

            uint16_t lo = (aLo & 0x0F) + (vLo & 0x0F) + (P.C ? 1 : 0);
            if (lo > 0x09) lo += 0x06;
            lo = (aLo & 0xF0) + (vLo & 0xF0) + (lo > 0x0F ? 0x10 : 0) + (lo & 0x0F);
            if (lo > 0x9F) lo += 0x60;
            bool carryLo = lo > 0xFF;

            uint16_t hi = (aHi & 0x0F) + (vHi & 0x0F) + (carryLo ? 1 : 0);
            if (hi > 0x09) hi += 0x06;
            hi = (aHi & 0xF0) + (vHi & 0xF0) + (hi > 0x0F ? 0x10 : 0) + (hi & 0x0F);
            if (hi > 0x9F) hi += 0x60;

            result = ((hi & 0xFF) << 8) | (lo & 0xFF);
            if (hi > 0xFF) result |= 0x10000;
        }

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

        if (P.D) {
            // BCD mode - apply the same nibble-pair correction as the 8-bit
            // path to the low and high bytes in turn, borrowing between them.
            uint8_t aLo = A & 0xFF, aHi = (A >> 8) & 0xFF;
            uint8_t vLo = value & 0xFF, vHi = (value >> 8) & 0xFF;

            uint16_t lo = (aLo & 0x0F) - (vLo & 0x0F) - (P.C ? 0 : 1);
            if (lo & 0x10) lo = ((lo - 0x06) & 0x0F) | ((aLo & 0xF0) - (vLo & 0xF0) - 0x10);
            else lo = (lo & 0x0F) | ((aLo & 0xF0) - (vLo & 0xF0));
            bool borrowLo = (lo & 0x100) != 0;
            if (borrowLo) lo -= 0x60;

            uint16_t hi = (aHi & 0x0F) - (vHi & 0x0F) - (borrowLo ? 1 : 0);
            if (hi & 0x10) hi = ((hi - 0x06) & 0x0F) | ((aHi & 0xF0) - (vHi & 0xF0) - 0x10);
            else hi = (hi & 0x0F) | ((aHi & 0xF0) - (vHi & 0xF0));
            bool borrowHi = (hi & 0x100) != 0;
            if (borrowHi) hi -= 0x60;

            result = ((hi & 0xFF) << 8) | (lo & 0xFF);
            if (borrowHi) result |= 0x10000;
        }

        P.C = result < 0x10000;
        P.V = ((A ^ value) & (A ^ result) & 0x8000) != 0;
        A = result & 0xFFFF;
        setNZ16(A);
    }
}

// Arithmetic - MUL (hardware multiply).  Produces a double-width product:
//   8-bit mode : AL * mem -> A (AL = low byte, AH = high byte)
//   16-bit mode: A  * mem -> A (low word) : X (high word)   [see docs' 32-bit
//                 return convention "A = low word, X = high word"]
// Flags NV--ZC: Z = product == 0, N = MSB of product, C = V = product did not
// fit in the operand width (i.e. the high half is non-zero).
void CPU::opMUL(uint32_t addr) {
    if (P.M) {
        uint8_t value = readByte(addr);
        uint16_t result = (uint16_t)(A & 0xFF) * value;
        A = result;                       // AL:AH hold the 16-bit product
        P.Z = (result == 0);
        P.N = (result & 0x8000) != 0;
        P.C = P.V = (result > 0xFF);
    } else {
        uint16_t value = readWord(addr);
        uint32_t result = (uint32_t)A * value;
        A = result & 0xFFFF;              // low word
        X = (result >> 16) & 0xFFFF;      // high word (full 32-bit product)
        P.Z = (result == 0);
        P.N = (result & 0x80000000UL) != 0;
        P.C = P.V = (result > 0xFFFF);
    }
    cycleCount += 8;
}

// Arithmetic - DIV X,Y (hardware divide).  Quotient -> A, remainder -> X.
// Flag C is the divide-by-zero error flag (set on divide by zero, otherwise
// cleared); A/X are left unchanged on a divide-by-zero.
void CPU::opDIV() {
    uint16_t dividend = P.X ? (X & 0xFF) : X;
    uint16_t divisor  = P.X ? (Y & 0xFF) : Y;
    if (divisor == 0) {
        P.C = true;                       // divide-by-zero: error, no result
        cycleCount += 8;
        return;
    }
    uint16_t quot = dividend / divisor;
    uint16_t rem  = dividend % divisor;
    if (P.M) A = (A & 0xFF00) | (quot & 0xFF); else A = quot;
    if (P.X) X = (X & 0xFF00) | (rem & 0xFF);  else X = rem;
    P.C = false;
    cycleCount += 8;
}

// DIV long,X / DIV long,Y - divide the accumulator by a memory operand.
// Quotient -> A, remainder -> X.  C is the divide-by-zero error flag.
void CPU::opDIVmem(uint32_t addr) {
    uint16_t dividend = P.M ? (A & 0xFF) : A;
    uint16_t divisor  = P.M ? readByte(addr) : readWord(addr);
    if (divisor == 0) {
        P.C = true;
        cycleCount += 8;
        return;
    }
    uint16_t quot = dividend / divisor;
    uint16_t rem  = dividend % divisor;
    if (P.M) { A = (A & 0xFF00) | (quot & 0xFF); X = (X & 0xFF00) | (rem & 0xFF); }
    else     { A = quot; X = rem; }
    P.C = false;
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

// INC A / DEC A - accumulator increment/decrement (respects the M width flag).
void CPU::opINCA() {
    if (P.M) { A = (A & 0xFF00) | ((A + 1) & 0xFF); setNZ8(A & 0xFF); }
    else     { A = (A + 1) & 0xFFFF; setNZ16(A); }
}
void CPU::opDECA() {
    if (P.M) { A = (A & 0xFF00) | ((A - 1) & 0xFF); setNZ8(A & 0xFF); }
    else     { A = (A - 1) & 0xFFFF; setNZ16(A); }
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

// Execute instruction (Table-driven dispatch - 256 opcodes)
void CPU::executeInstruction() {
    uint8_t opcode = fetch();

    // Dispatch to instruction handler via function pointer table
    CPU_INSTRUCTION_TABLE[opcode](this);
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

// Conditional/unconditional branch timing (65C816-style):
//   base cost when not taken; +1 when taken; +1 more when the target lands in
//   a different page than the fall-through address (PC after operand fetch).
void CPU::opBMI() {
    uint32_t addr = fetch24();
    cycleCount += 2;
    if (P.N) {
        cycleCount += 1;                                        // branch taken
        if ((PC & 0xFF00) != (addr & 0xFF00)) cycleCount += 1; // page crossed
        PC = addr & 0xFFFF;
        PB = (addr >> 16) & 0xFF;
    }
}

void CPU::opBRA() {
    uint16_t addr = fetch16();
    cycleCount += 3;  // always taken
    if ((PC & 0xFF00) != (addr & 0xFF00)) cycleCount += 1;  // page crossed
    PC = addr;
}

void CPU::opBRL() {
    int16_t offset = (int16_t)fetch16();
    PC = (PC + offset) & 0xFFFF;
    cycleCount += 4;  // 16-bit relative branch: fixed 4 cycles, no page penalty
}

void CPU::opBVS() {
    uint16_t addr = fetch16();
    cycleCount += 2;
    if (P.V) {
        cycleCount += 1;                                        // branch taken
        if ((PC & 0xFF00) != (addr & 0xFF00)) cycleCount += 1; // page crossed
        PC = addr;
    }
}

void CPU::opBCS() {
    uint32_t addr = fetch24();
    cycleCount += 2;
    if (P.C) {
        cycleCount += 1;                                        // branch taken
        if ((PC & 0xFF00) != (addr & 0xFF00)) cycleCount += 1; // page crossed
        PC = addr & 0xFFFF;
        PB = (addr >> 16) & 0xFF;
    }
}

void CPU::opBEQ() {
    uint32_t addr = fetch24();
    cycleCount += 2;
    if (P.Z) {
        cycleCount += 1;                                        // branch taken
        if ((PC & 0xFF00) != (addr & 0xFF00)) cycleCount += 1; // page crossed
        PC = addr & 0xFFFF;
        PB = (addr >> 16) & 0xFF;
    }
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

void CPU::opTDC() {
    // Transfer Direct Page to Accumulator
    A = D;
    setNZ16(A);  // D is always 16-bit
    cycleCount += 2;
}

void CPU::opTSC() {
    // Transfer Stack Pointer to Accumulator
    A = SP;
    setNZ16(A);  // SP is always 16-bit
    cycleCount += 2;
}

void CPU::opTCS() {
    // Transfer Accumulator to Stack Pointer
    SP = A & 0xFFFF;  // SP is always 16-bit
    cycleCount += 2;
}

void CPU::opTAX() {
    // Transfer Accumulator to Index X
    if (P.M && P.X) {
        // Both 8-bit
        X = A & 0xFF;
        setNZ8(X & 0xFF);
    } else if (!P.M && !P.X) {
        // Both 16-bit
        X = A;
        setNZ16(X);
    } else if (P.M && !P.X) {
        // A is 8-bit, X is 16-bit
        X = A & 0xFF;
        setNZ16(X);
    } else {
        // A is 16-bit, X is 8-bit
        X = A & 0xFF;
        setNZ8(X & 0xFF);
    }
    cycleCount += 2;
}

void CPU::opTXA() {
    // Transfer Index X to Accumulator
    if (P.M && P.X) {
        // Both 8-bit
        A = (A & 0xFF00) | (X & 0xFF);
        setNZ8(A & 0xFF);
    } else if (!P.M && !P.X) {
        // Both 16-bit
        A = X;
        setNZ16(A);
    } else if (P.M && !P.X) {
        // A is 8-bit, X is 16-bit
        A = (A & 0xFF00) | (X & 0xFF);
        setNZ8(A & 0xFF);
    } else {
        // A is 16-bit, X is 8-bit
        A = X & 0xFF;
        setNZ16(A);
    }
    cycleCount += 2;
}

void CPU::opTAY() {
    // Transfer Accumulator to Index Y
    if (P.M && P.X) {
        // Both 8-bit
        Y = A & 0xFF;
        setNZ8(Y & 0xFF);
    } else if (!P.M && !P.X) {
        // Both 16-bit
        Y = A;
        setNZ16(Y);
    } else if (P.M && !P.X) {
        // A is 8-bit, Y is 16-bit
        Y = A & 0xFF;
        setNZ16(Y);
    } else {
        // A is 16-bit, Y is 8-bit
        Y = A & 0xFF;
        setNZ8(Y & 0xFF);
    }
    cycleCount += 2;
}

void CPU::opTCD() {
    // Transfer Accumulator to Direct Page
    D = A & 0xFFFF;  // D is always 16-bit
    cycleCount += 2;
}

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
    // Vector through the BRK handler at $00:FFE6-FFE7 (native 65C816). The
    // handler returns to the pushed state with RTI, just like real hardware.
    PC = readWord(0x00FFE6);
    PB = 0x00;
    cycleCount += 7;
}

void CPU::opCOP() {
    fetch();  // Skip signature byte (software-readable by the handler via the
              // pushed PC, same convention as BRK; hardware doesn't act on it)
    push8(PB);
    push16(PC);
    push8(P.toByte());
    P.I = true;
    P.D = false;
    // Vector through the COP handler at $00:FFE4-FFE5 (native 65C816), one
    // slot below BRK's $00:FFE6 in the same vector table. Like BRK, this is a
    // direct CPU-internal software trap — it does not go through the shared
    // peripheral InterruptController at $D80058, which only aggregates
    // hardware-sourced IRQs (V-blank/H-blank/Timer/DMA).
    PC = readWord(0x00FFE4);
    PB = 0x00;
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

// === MEMORY MAPPING IMPLEMENTATION ===

void CPU::rebuildBankTable() {
    bankFast.fill(nullptr);
    bankScan.fill(false);

    // Count regions per bank and remember any full-offset-range region, so a
    // bank served by exactly one full region gets the direct fast path and
    // everything else (partial or overlapping regions) is flagged for a scan.
    std::array<int, 256> count{};              // regions covering each bank
    std::array<MemoryRegion*, 256> fullRegion{};
    fullRegion.fill(nullptr);

    for (auto& region : memoryMap) {
        bool full = (region.startOffset == 0x0000 && region.endOffset == 0xFFFF);
        for (int b = region.startBank; b <= region.endBank; ++b) {
            count[b]++;
            if (full) fullRegion[b] = &region;
        }
    }

    for (int b = 0; b < 256; ++b) {
        if (count[b] == 1 && fullRegion[b]) {
            bankFast[b] = fullRegion[b];       // single whole-bank region
        } else if (count[b] >= 1) {
            bankScan[b] = true;                // partial/multiple: needs scan
        }
        // count[b] == 0 -> unmapped: both stay null/false (open bus)
    }
}

CPU::MemoryRegion* CPU::findRegion(uint32_t address) {
    uint8_t bank = (address >> 16) & 0xFF;

    // Fast path: bank covered by a single full-range region (ROM/RAM/windows).
    if (MemoryRegion* r = bankFast[bank]) [[likely]] {
        return r;
    }
    // Unmapped bank: no region at all.
    if (!bankScan[bank]) {
        return nullptr;
    }

    // Slow path: only banks with partial/multiple regions (I/O bank $D8).
    // Scan is naturally limited — that bank holds a handful of regions.
    uint16_t offset = address & 0xFFFF;
    for (auto& region : memoryMap) {
        if (bank >= region.startBank && bank <= region.endBank &&
            offset >= region.startOffset && offset <= region.endOffset) {
            return &region;
        }
    }

    return nullptr;
}

uint8_t CPU::readMapped(uint32_t address) {
    MemoryRegion* region = findRegion(address);
    if (!region) {
        // Unmapped memory returns open bus (0xFF)
        return 0xFF;
    }

    uint8_t bank = (address >> 16) & 0xFF;
    uint16_t offset = address & 0xFFFF;

    switch (region->type) {
        case MemoryRegion::ROM:
        case MemoryRegion::RAM: {
            if (!region->data) return 0xFF;

            // Calculate offset within the region's data
            size_t bankOffset = (bank - region->startBank) * 65536;
            size_t dataOffset = bankOffset + (offset - region->startOffset);

            if (dataOffset < region->dataSize) {
                return region->data[dataOffset];
            }
            return 0xFF;
        }

        case MemoryRegion::PPU_WINDOW: {
            if (!ppuPtr) return 0xFF;
            // PPU has 64 KiB memory, map directly
            return ppuPtr->readMemory(offset);
        }

        case MemoryRegion::APU_WINDOW: {
            if (!apuPtr) return 0xFF;
            // APU has 64 KiB memory (+ banked ROM)
            return apuPtr->readByte(offset);
        }

        case MemoryRegion::IO_REGISTERS: {
            if (!region->ioRead) return 0xFF;
            uint16_t ioOffset = offset - region->startOffset;
            return region->ioRead(ioOffset);
        }

        default:
            return 0xFF;
    }
}

void CPU::writeMapped(uint32_t address, uint8_t value) {
    MemoryRegion* region = findRegion(address);
    if (!region) {
        // Writes to unmapped memory are ignored
        return;
    }

    uint8_t bank = (address >> 16) & 0xFF;
    uint16_t offset = address & 0xFFFF;

    switch (region->type) {
        case MemoryRegion::ROM: {
            // ROM writes are ignored
            break;
        }

        case MemoryRegion::RAM: {
            if (!region->data) break;

            // Calculate offset within the region's data
            size_t bankOffset = (bank - region->startBank) * 65536;
            size_t dataOffset = bankOffset + (offset - region->startOffset);

            if (dataOffset < region->dataSize) {
                region->data[dataOffset] = value;
            }
            break;
        }

        case MemoryRegion::PPU_WINDOW: {
            if (!ppuPtr) break;
            ppuPtr->writeMemory(offset, value);
            break;
        }

        case MemoryRegion::APU_WINDOW: {
            if (!apuPtr) break;
            apuPtr->writeByte(offset, value);
            break;
        }

        case MemoryRegion::IO_REGISTERS: {
            if (!region->ioWrite) break;
            uint16_t ioOffset = offset - region->startOffset;
            region->ioWrite(ioOffset, value);
            break;
        }

        default:
            break;
    }
}

void CPU::loadROM(const uint8_t* data, size_t size, uint8_t startBank) {
    MemoryRegion region;
    region.type = MemoryRegion::ROM;
    region.startBank = startBank;
    region.startOffset = 0x0000;
    region.endOffset = 0xFFFF;

    // Calculate how many banks this ROM spans
    uint8_t numBanks = (size + 65535) / 65536;
    region.endBank = startBank + numBanks - 1;

    // Allocate ROM data
    region.data = new uint8_t[size];
    std::memcpy(region.data, data, size);
    region.dataSize = size;

    memoryMap.push_back(region);
    useMemoryMap = true;  // Enable memory mapping
    rebuildBankTable();   // refresh bank index (push_back may have realloc'd)
}

void CPU::loadBootROM(const uint8_t* data, size_t size) {
    // Replace semantics: calling this twice (e.g. to swap in an alternate
    // boot ROM payload after System's constructor already installed the
    // default stub) must not leave two overlapping bank-$E0 regions behind -
    // rebuildBankTable() would fall back to a linear scan and the stale
    // first-pushed region would keep winning, silently discarding the new one.
    for (auto it = memoryMap.begin(); it != memoryMap.end(); ) {
        if (it->startBank <= 0xE0 && it->endBank >= 0xE0) {
            delete[] it->data;
            it = memoryMap.erase(it);
        } else {
            ++it;
        }
    }
    loadROM(data, size, 0xE0);
    bootROMLoaded = true;
}

void CPU::loadSignedROMMetadata(const uint8_t* data, size_t size) {
    // Same replace-semantics reasoning as loadBootROM(): clear any stale
    // bank-$E1 region first so reloading (or loading an unsigned ROM after
    // a signed one) never leaves two overlapping regions for
    // rebuildBankTable() to silently mis-resolve.
    for (auto it = memoryMap.begin(); it != memoryMap.end(); ) {
        if (it->startBank <= 0xE1 && it->endBank >= 0xE1) {
            delete[] it->data;
            it = memoryMap.erase(it);
        } else {
            ++it;
        }
    }
    if (data && size > 0) {
        loadROM(data, size, 0xE1);
    } else {
        rebuildBankTable();
    }
}

void CPU::allocateRAM(uint8_t startBank, uint8_t numBanks) {
    MemoryRegion region;
    region.type = MemoryRegion::RAM;
    region.startBank = startBank;
    region.endBank = startBank + numBanks - 1;
    region.startOffset = 0x0000;
    region.endOffset = 0xFFFF;

    // Allocate RAM
    size_t ramSize = numBanks * 65536;
    region.data = new uint8_t[ramSize];
    std::memset(region.data, 0, ramSize);
    region.dataSize = ramSize;

    memoryMap.push_back(region);
    useMemoryMap = true;  // Enable memory mapping
    rebuildBankTable();   // refresh bank index (push_back may have realloc'd)
}

void CPU::registerIORegion(uint8_t bank, uint16_t baseOffset, uint16_t size,
                           IOReadCallback readFn, IOWriteCallback writeFn) {
    MemoryRegion region;
    region.type = MemoryRegion::IO_REGISTERS;
    region.startBank = bank;
    region.endBank = bank;
    region.startOffset = baseOffset;
    region.endOffset = baseOffset + size - 1;
    region.ioRead = readFn;
    region.ioWrite = writeFn;

    memoryMap.push_back(region);
    useMemoryMap = true;  // Enable memory mapping
    rebuildBankTable();   // refresh bank index (push_back may have realloc'd)
}

void CPU::mapPPUWindow(uint8_t bank) {
    MemoryRegion region;
    region.type = MemoryRegion::PPU_WINDOW;
    region.startBank = bank;
    region.endBank = bank;
    region.startOffset = 0x0000;
    region.endOffset = 0xFFFF;  // Full 64 KB

    memoryMap.push_back(region);
    useMemoryMap = true;  // Enable memory mapping
    rebuildBankTable();   // refresh bank index (push_back may have realloc'd)
}

void CPU::mapAPUWindow(uint8_t bank) {
    MemoryRegion region;
    region.type = MemoryRegion::APU_WINDOW;
    region.startBank = bank;
    region.endBank = bank;
    region.startOffset = 0x0000;
    region.endOffset = 0xFFFF;  // Full 64 KB

    memoryMap.push_back(region);
    useMemoryMap = true;  // Enable memory mapping
    rebuildBankTable();   // refresh bank index (push_back may have realloc'd)
}

// Setup all I/O registers at Bank $D8
void CPU::setupIORegisters() {
    // I/O Register addresses (Bank $D8)
    constexpr uint8_t IO_BANK = 0xD8;

    // ========================================================================
    // 1. PPU CONTROL BLOCK ($D80000-$D8000F, 16 bytes)
    // ========================================================================
    // Latch holding which PPU register (R0-R63) PPUREG_DATA reads/writes target.
    // Set by writing PPUREG_ADDR ($D80008); persists across accesses.
    static uint8_t ppuRegSelect = 0;
    registerIORegion(IO_BANK, 0x0000, 16,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            if (!ppuPtr) return 0xFF;

            switch (offset) {
                case 0x00: // PPUCTRL (write-only, returns 0)
                    return 0x00;

                case 0x01: { // PPUSTATUS
                    PPUState state = ppuPtr->getState();
                    return static_cast<uint8_t>(state);
                }

                case 0x02: // PPUPC low byte
                    return ppuPtr->getExecutionPointer() & 0xFF;

                case 0x03: // PPUPC high byte
                    return (ppuPtr->getExecutionPointer() >> 8) & 0xFF;

                case 0x04: // PPUSP low byte
                    return ppuPtr->getRegister(61) & 0xFF;

                case 0x05: // PPUSP high byte
                    return (ppuPtr->getRegister(61) >> 8) & 0xFF;

                case 0x06: // PPUDP low byte
                    return ppuPtr->getRegister(63) & 0xFF;

                case 0x07: // PPUDP high byte
                    return (ppuPtr->getRegister(63) >> 8) & 0xFF;

                case 0x08: // PPUREG_ADDR - returns the currently selected register
                    return ppuRegSelect;

                case 0x09: // PPUREG_DATA low byte (selected register)
                    return ppuPtr->getRegister(ppuRegSelect) & 0xFF;

                case 0x0A: // PPUREG_DATA high byte (selected register)
                    return (ppuPtr->getRegister(ppuRegSelect) >> 8) & 0xFF;

                case 0x0B: // PPU_VBLANK_INT low byte (R59)
                    return ppuPtr->getRegister(59) & 0xFF;

                case 0x0C: // PPU_VBLANK_INT high byte (R59)
                    return (ppuPtr->getRegister(59) >> 8) & 0xFF;

                case 0x0D: // PPU_HBLANK_INT low byte (R60)
                    return ppuPtr->getRegister(60) & 0xFF;

                case 0x0E: // PPU_HBLANK_INT high byte (R60)
                    return (ppuPtr->getRegister(60) >> 8) & 0xFF;

                default:
                    return 0x00;
            }
        },
        // Write handler
        [this](uint16_t offset, uint8_t value) {
            if (!ppuPtr) return;

            // Helpers to write one byte of a 16-bit PPU register without
            // disturbing the other byte (writes arrive a byte at a time).
            auto writeRegLow = [this](uint8_t reg, uint8_t v) {
                ppuPtr->setRegister(reg, (ppuPtr->getRegister(reg) & 0xFF00) | v);
            };
            auto writeRegHigh = [this](uint8_t reg, uint8_t v) {
                ppuPtr->setRegister(reg, (ppuPtr->getRegister(reg) & 0x00FF) | (v << 8));
            };

            switch (offset) {
                case 0x00: // PPUCTRL
                    if (value & 0x01) ppuPtr->start();
                    if (value & 0x02) ppuPtr->reset();
                    break;

                case 0x02: // PPUPC low byte (live execution pointer)
                    ppuPtr->setExecutionPointer(
                        (ppuPtr->getExecutionPointer() & 0xFF00) | value);
                    break;
                case 0x03: // PPUPC high byte
                    ppuPtr->setExecutionPointer(
                        (ppuPtr->getExecutionPointer() & 0x00FF) | (value << 8));
                    break;

                case 0x04: writeRegLow(61, value); break;   // PPUSP low
                case 0x05: writeRegHigh(61, value); break;  // PPUSP high
                case 0x06: writeRegLow(63, value); break;   // PPUDP low
                case 0x07: writeRegHigh(63, value); break;  // PPUDP high

                case 0x08: // PPUREG_ADDR - select register R0-R63 for PPUREG_DATA
                    ppuRegSelect = value & 0x3F;
                    break;
                case 0x09: writeRegLow(ppuRegSelect, value); break;   // PPUREG_DATA low
                case 0x0A: writeRegHigh(ppuRegSelect, value); break;  // PPUREG_DATA high

                case 0x0B: writeRegLow(59, value); break;   // PPU_VBLANK_INT low  (R59)
                case 0x0C: writeRegHigh(59, value); break;  // PPU_VBLANK_INT high (R59)
                case 0x0D: writeRegLow(60, value); break;   // PPU_HBLANK_INT low  (R60)
                case 0x0E: writeRegHigh(60, value); break;  // PPU_HBLANK_INT high (R60)

                default:
                    break;
            }
        }
    );

    // ========================================================================
    // 2. APU CONTROL BLOCK ($D80010-$D8001F, 16 bytes)
    // ========================================================================
    registerIORegion(IO_BANK, 0x0010, 16,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            if (!apuPtr) return 0xFF;

            switch (offset) {
                case 0x00: // APUCTRL (write-only)
                    return 0x00;

                case 0x01: // APUSTATUS
                    return apuPtr->isHalted() ? 0x01 : 0x00;

                case 0x02: // APUPC low byte
                    return apuPtr->getPC() & 0xFF;

                case 0x03: // APUPC high byte
                    return (apuPtr->getPC() >> 8) & 0xFF;

                case 0x04: // APUSP low byte
                    return apuPtr->getSP() & 0xFF;

                case 0x05: // APUSP high byte
                    return (apuPtr->getSP() >> 8) & 0xFF;

                case 0x06: // APU_ROMBANK (write-only)
                    return 0x00;

                case 0x07: // APU_IOBANK (write-only)
                    return 0x00;

                case 0x08: // APUREG_ADDR (write-only)
                    return 0x00;

                case 0x09: { // APUREG_DATA
                    // Would need to track selected register
                    return 0x00;
                }

                default:
                    return 0x00;
            }
        },
        // Write handler
        [this](uint16_t offset, uint8_t value) {
            if (!apuPtr) return;

            switch (offset) {
                case 0x00: // APUCTRL
                    if (value & 0x01) apuPtr->reset();
                    if (value & 0x02) apuPtr->setHalted(true);
                    if (value & 0x04) apuPtr->setHalted(false); // Resume
                    break;

                case 0x02: { // APUPC low byte
                    uint16_t pc = apuPtr->getPC();
                    pc = (pc & 0xFF00) | value;
                    apuPtr->setPC(pc);
                    break;
                }

                case 0x03: { // APUPC high byte
                    uint16_t pc = apuPtr->getPC();
                    pc = (pc & 0x00FF) | (value << 8);
                    apuPtr->setPC(pc);
                    break;
                }

                case 0x04: { // APUSP low byte
                    uint16_t sp = apuPtr->getSP();
                    sp = (sp & 0xFF00) | value;
                    apuPtr->setSP(sp);
                    break;
                }

                case 0x05: { // APUSP high byte
                    uint16_t sp = apuPtr->getSP();
                    sp = (sp & 0x00FF) | (value << 8);
                    apuPtr->setSP(sp);
                    break;
                }

                case 0x06: // APU_ROMBANK
                    apuPtr->setROMBank(value);
                    break;

                case 0x07: // APU_IOBANK
                    apuPtr->setIOBank(value);
                    break;

                default:
                    break;
            }
        }
    );

    // ========================================================================
    // 3. DMA CONTROL BLOCK ($D80020-$D8002F, 16 bytes)
    // ========================================================================
    // Write-only: Write 9 bytes to $D80020-$D80028 to configure DMA
    // Read-only: Status and channel information
    static uint8_t dmaConfigBuffer[9] = {0};
    static uint8_t dmaConfigIndex = 0;

    registerIORegion(IO_BANK, 0x0020, 16,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            if (!dmaPtr) return 0xFF;

            switch (offset) {
                case 0x00: // DMA config buffer index (returns current write position)
                    return dmaConfigIndex;

                case 0x01: { // DMA status byte (QQQQ AAAA)
                    uint8_t queued = (dmaPtr->getQueuedCount() & 0x0F);
                    uint8_t active = (dmaPtr->getActiveChannelCount() & 0x0F);
                    return (queued << 4) | active;
                }

                case 0x02: // DMA interrupt status (1 if paused, 0 if running)
                    return dmaPtr->isInterruptActive() ? 0x01 : 0x00;

                // Channels 0-11 status (0x03-0x0E); channels 12-15 have no
                // status byte here (block only has 16 bytes total)
                case 0x03: case 0x04: case 0x05: case 0x06:
                case 0x07: case 0x08: case 0x09: case 0x0A:
                case 0x0B: case 0x0C: case 0x0D: case 0x0E: {
                    uint8_t channel = offset - 0x03;
                    DMAState state = dmaPtr->getChannelState(channel);
                    return static_cast<uint8_t>(state);
                }

                default:
                    return 0x00;
            }
        },
        // Write handler
        [this](uint16_t offset, uint8_t value) {
            if (!dmaPtr) return;

            switch (offset) {
                case 0x00: // Reset config buffer
                    dmaConfigIndex = 0;
                    memset(dmaConfigBuffer, 0, sizeof(dmaConfigBuffer));
                    break;

                // $D80020-$D80028: 9-byte DMA configuration
                case 0x01: case 0x02: case 0x03: case 0x04:
                case 0x05: case 0x06: case 0x07: case 0x08: {
                    // Write to config buffer
                    if (dmaConfigIndex < 9) {
                        dmaConfigBuffer[dmaConfigIndex++] = value;

                        // If we've written all 9 bytes, queue the DMA transfer
                        if (dmaConfigIndex == 9) {
                            dmaPtr->queueDMA(dmaConfigBuffer);
                            dmaConfigIndex = 0;  // Auto-reset for next transfer
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }
    );

    // ========================================================================
    // 4. PLAYER PORTS & DEBUG INTERFACE ($D80030-$D8003F, 16 bytes)
    // ========================================================================
    // Port 1: $D80030-$D80037 (Player 1 controller)
    // Port 2: $D80038-$D8003F (Player 2 / Debug serial interface)
    [[maybe_unused]] static uint8_t p2_tx_buffer[256];  // TX queue; no consumer yet
    static uint8_t p2_rx_buffer[256];
    static uint8_t p2_tx_head = 0;
    static uint8_t p2_tx_tail = 0;
    static uint8_t p2_rx_head = 0;
    static uint8_t p2_rx_tail = 0;

    registerIORegion(IO_BANK, 0x0030, 16,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            switch (offset) {
                // Port 1 (Controller) - see PlayerInput:: for bit layout
                case 0x00: // P1_DIR (8-way directional)
                    return p1Direction;
                case 0x01: // P1_CTRL (control pad/bumpers/triggers/menu/pause/connection)
                    return p1Control;
                case 0x02: // P1_BTN (4 face buttons + DATA nibble)
                    return p1Buttons;

                // Port 2 (Debug Interface)
                case 0x08: { // P2_STATUS
                    uint8_t status = 0x00;
                    // Bit 0: RX_READY (data available to read)
                    if (p2_rx_head != p2_rx_tail) {
                        status |= 0x01;
                    }
                    // Bit 1: TX_EMPTY (can write)
                    uint8_t next_head = (p2_tx_head + 1) & 0xFF;
                    if (next_head != p2_tx_tail) {
                        status |= 0x02;
                    }
                    return status;
                }

                case 0x09: { // P2_DATA (read)
                    if (p2_rx_head != p2_rx_tail) {
                        uint8_t data = p2_rx_buffer[p2_rx_tail];
                        p2_rx_tail = (p2_rx_tail + 1) & 0xFF;
                        return data;
                    }
                    return 0x00;
                }

                default:
                    return 0x00;
            }
        },
        // Write handler
        [this](uint16_t offset, uint8_t value) {
            switch (offset) {
                case 0x09: { // P2_DATA (write)
                    uint8_t next_head = (p2_tx_head + 1) & 0xFF;
                    if (next_head != p2_tx_tail) {
                        p2_tx_buffer[p2_tx_head] = value;
                        p2_tx_head = next_head;
                    }
                    break;
                }

                case 0x0A: // P2_CTRL
                    if (value & 0x01) {
                        // Clear buffers
                        p2_tx_head = p2_tx_tail = 0;
                        p2_rx_head = p2_rx_tail = 0;
                    }
                    break;

                default:
                    break;
            }
        }
    );

    // ========================================================================
    // 5. DISPLAY STATUS BLOCK ($D80040-$D80047, 8 bytes, read-only)
    // ========================================================================
    registerIORegion(IO_BANK, 0x0040, 8,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            if (!displayPtr) return 0xFF;

            switch (offset) {
                case 0x00: // DISP_SCANLINE low byte
                    return displayPtr->getCurrentScanline() & 0xFF;

                case 0x01: // DISP_SCANLINE high byte
                    return (displayPtr->getCurrentScanline() >> 8) & 0xFF;

                case 0x02: // DISP_PIXEL low byte
                    return displayPtr->getCurrentPixel() & 0xFF;

                case 0x03: // DISP_PIXEL high byte
                    return (displayPtr->getCurrentPixel() >> 8) & 0xFF;

                case 0x04: { // DISP_STATUS
                    uint8_t status = 0;
                    if (displayPtr->isVBlank()) status |= 0x01;
                    if (displayPtr->isHBlank()) status |= 0x02;
                    if (displayPtr->isVisibleArea()) status |= 0x04;
                    return status;
                }

                case 0x05: // DISP_MODE
                    return (displayPtr->getRenderMode() == RenderMode::RGBA16) ? 0x00 : 0x01;

                default:
                    return 0x00;
            }
        },
        // Write handler (read-only, writes ignored)
        [](uint16_t, uint8_t) { /* Read-only block */ }
    );

    // ========================================================================
    // 6. SYSTEM CONTROL ($D80048-$D8004F, 8 bytes)
    // ========================================================================
    static bool devModeFlag = false;

    registerIORegion(IO_BANK, 0x0048, 8,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            switch (offset) {
                case 0x00: // DEV_MODE flag
                    return devModeFlag ? 0x01 : 0x00;
                case 0x01: // CART_ENTRY low byte (set by System::loadROM)
                    return cartridgeEntryPoint & 0xFF;
                case 0x02: // CART_ENTRY mid byte
                    return (cartridgeEntryPoint >> 8) & 0xFF;
                case 0x03: // CART_ENTRY bank byte
                    return (cartridgeEntryPoint >> 16) & 0xFF;
                default:
                    return 0x00;
            }
        },
        // Write handler
        [](uint16_t offset, uint8_t value) {
            switch (offset) {
                case 0x00: // DEV_MODE flag (set by emulator, read by boot ROM)
                    devModeFlag = (value & 0x01) != 0;
                    break;
                default:
                    break;
            }
        }
    );

    // ========================================================================
    // 7. TIMER CONTROL ($D80050-$D80057, 8 bytes)
    // ========================================================================
    registerIORegion(IO_BANK, 0x0050, 8,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            if (!systemPtr) return 0xFF;

            switch (offset) {
                case 0x00: // TIMER_CONTROL - Timer enable bits (0bVHSQETAR)
                    return systemPtr->timers.control;
                case 0x01: // TIMER_STATUS - Timer status flags (read/write)
                    return systemPtr->timers.status;
                case 0x02: // TIMER_INT_ENABLE - Timer interrupt enable bits (0bVHSQETAR)
                    return systemPtr->timers.intEnable;
                default:
                    return 0x00;
            }
        },
        // Write handler
        [this](uint16_t offset, uint8_t value) {
            if (!systemPtr) return;

            switch (offset) {
                case 0x00: // TIMER_CONTROL - Timer enable bits (0bVHSQETAR)
                    // Go through setTimerControl so the next-fire schedule is
                    // rebuilt (don't write timers.control directly).
                    systemPtr->setTimerControl(value);
                    break;
                case 0x01: // TIMER_STATUS - Write to clear status flags
                    // Writing a 1 to a bit clears that flag
                    systemPtr->timers.status &= ~value;
                    // Once every timer flag is cleared, drop the aggregate
                    // Timer request in the interrupt controller too, so an ISR
                    // that only knows about TIMER_STATUS still deasserts it.
                    if (systemPtr->timers.status == 0) {
                        systemPtr->intc.acknowledge(
                            static_cast<uint8_t>(IRQSource::Timer));
                    }
                    break;
                case 0x02: // TIMER_INT_ENABLE - Timer interrupt enable bits (0bVHSQETAR)
                    systemPtr->timers.intEnable = value;
                    break;
                default:
                    break;
            }
        }
    );

    // ========================================================================
    // 8. INTERRUPT CONTROLLER ($D80058-$D8005F, 8 bytes)
    // ========================================================================
    // Aggregates every IRQ source onto the CPU's single maskable line and lets
    // the ISR discover which source fired.
    //   $D80058 IRQ_STATUS  - read: pending sources (bit0 V-blank, bit1 H-blank,
    //                         bit2 Timer, bit3 DMA); write: 1 clears (ack)
    //   $D80059 IRQ_ENABLE  - top-level per-source mask (read/write)
    registerIORegion(IO_BANK, 0x0058, 8,
        // Read handler
        [this](uint16_t offset) -> uint8_t {
            if (!systemPtr) return 0xFF;

            switch (offset) {
                case 0x00: // IRQ_STATUS - which sources are latched pending
                    return systemPtr->intc.pending();
                case 0x01: // IRQ_ENABLE - top-level source mask
                    return systemPtr->intc.enable();
                default:
                    return 0x00;
            }
        },
        // Write handler
        [this](uint16_t offset, uint8_t value) {
            if (!systemPtr) return;

            switch (offset) {
                case 0x00: // IRQ_STATUS - write 1 to acknowledge/clear a source
                    systemPtr->intc.acknowledge(value);
                    break;
                case 0x01: // IRQ_ENABLE - top-level source mask
                    systemPtr->intc.setEnable(value);
                    break;
                default:
                    break;
            }
        }
    );
}

// === INTERRUPT HANDLING ===

void CPU::triggerIRQ() {
    // Assert (latch) the maskable IRQ line. The request is held pending
    // until it can be serviced, so an IRQ raised while interrupts are
    // disabled (I=1) is NOT lost — it fires as soon as the program clears
    // I. Actual servicing happens at an instruction boundary in
    // serviceInterrupts().
    irqPending = true;

    // WAI resumes on any interrupt assertion, regardless of the I flag.
    if (state == CPUState::Waiting) {
        state = CPUState::Running;
    }
}

void CPU::triggerNMI() {
    // Assert (latch) the non-maskable NMI line. Serviced unconditionally at
    // the next instruction boundary.
    nmiPending = true;

    // WAI resumes on any interrupt assertion.
    if (state == CPUState::Waiting) {
        state = CPUState::Running;
    }
}

void CPU::triggerAbort() {
    // Assert (latch) the abort-class exception. Modeled x86-style: this is
    // not the restartable 65C816 bus-fault pin (which must preempt an
    // in-flight instruction and later resume it via RTI). It's simply the
    // highest-priority vector, serviced at the next instruction boundary
    // like BRK/COP, for conditions the emulator itself judges unrecoverable.
    abortPending = true;

    if (state == CPUState::Waiting) {
        state = CPUState::Running;
    }
}

void CPU::serviceInterrupts() {
    // ABORT preempts everything, including NMI.
    if (abortPending) [[unlikely]] {
        abortPending = false;
        serviceAbort();
        return;
    }

    // NMI has priority and cannot be masked.
    if (nmiPending) [[unlikely]] {
        nmiPending = false;
        serviceNMI();
        return;
    }

    // Maskable IRQ: serviced only when the I flag is clear. Otherwise the
    // latch stays set and it will fire once the program clears I.
    if (irqPending && !P.I) [[unlikely]] {
        irqPending = false;
        serviceIRQ();
    }
}

void CPU::serviceIRQ() {
    // IRQ (Interrupt Request) - Maskable interrupt
    // Vector: $00:FFFE-FFFF (16-bit address, PB set to 0)

    // 1. Push PB to stack
    push8(PB);

    // 2. Push PC high byte to stack
    push8((PC >> 8) & 0xFF);

    // 3. Push PC low byte to stack
    push8(PC & 0xFF);

    // 4. Push P (processor status) to stack
    uint8_t statusByte = 0;
    if (P.N) statusByte |= 0x80;
    if (P.V) statusByte |= 0x40;
    if (P.M) statusByte |= 0x20;
    if (P.X) statusByte |= 0x10;
    if (P.D) statusByte |= 0x08;
    if (P.I) statusByte |= 0x04;
    if (P.Z) statusByte |= 0x02;
    if (P.C) statusByte |= 0x01;
    push8(statusByte);

    // 5. Set I flag (disable further IRQs)
    P.I = true;

    // 6. Clear D flag (binary mode)
    P.D = false;

    // 7. Read IRQ vector from $00:FFFE-FFFF
    uint32_t vectorAddr = 0x00FFFE;
    uint16_t handlerAddr = readWord(vectorAddr);

    // 8. Set PC to handler address, PB to 0
    PC = handlerAddr;
    PB = 0x00;

    // Add interrupt overhead cycles
    cycleCount += 7;  // Interrupt acknowledge + stack ops + vector fetch
}

void CPU::serviceNMI() {
    // NMI (Non-Maskable Interrupt) - Cannot be masked
    // Vector: $00:FFFA-FFFB (16-bit address, PB set to 0)

    // NMI cannot be masked - always executes

    // 1. Push PB to stack
    push8(PB);

    // 2. Push PC high byte to stack
    push8((PC >> 8) & 0xFF);

    // 3. Push PC low byte to stack
    push8(PC & 0xFF);

    // 4. Push P (processor status) to stack
    uint8_t statusByte = 0;
    if (P.N) statusByte |= 0x80;
    if (P.V) statusByte |= 0x40;
    if (P.M) statusByte |= 0x20;
    if (P.X) statusByte |= 0x10;
    if (P.D) statusByte |= 0x08;
    if (P.I) statusByte |= 0x04;
    if (P.Z) statusByte |= 0x02;
    if (P.C) statusByte |= 0x01;
    push8(statusByte);

    // 5. I flag NOT modified (NMI ignores I flag)
    // (No change to P.I)

    // 6. Clear D flag (binary mode)
    P.D = false;

    // 7. Read NMI vector from $00:FFFA-FFFB
    uint32_t vectorAddr = 0x00FFFA;
    uint16_t handlerAddr = readWord(vectorAddr);

    // 8. Set PC to handler address, PB to 0
    PC = handlerAddr;
    PB = 0x00;

    // Add interrupt overhead cycles
    cycleCount += 7;  // Interrupt acknowledge + stack ops + vector fetch
}

void CPU::serviceAbort() {
    // ABORT - abort-class exception, x86-style (see triggerAbort()): not
    // maskable, not restartable. Dispatches like NMI/BRK/COP rather than
    // trying to reproduce the 65C816 pin's mid-instruction bus-fault retry.
    // Vector: $00:FFF8-FFF9 (16-bit address, PB set to 0) — the same
    // address native-mode 65C816 hardware reserves for ABORT.

    // 1. Push PB to stack
    push8(PB);

    // 2. Push PC high byte to stack
    push8((PC >> 8) & 0xFF);

    // 3. Push PC low byte to stack
    push8(PC & 0xFF);

    // 4. Push P (processor status) to stack
    uint8_t statusByte = 0;
    if (P.N) statusByte |= 0x80;
    if (P.V) statusByte |= 0x40;
    if (P.M) statusByte |= 0x20;
    if (P.X) statusByte |= 0x10;
    if (P.D) statusByte |= 0x08;
    if (P.I) statusByte |= 0x04;
    if (P.Z) statusByte |= 0x02;
    if (P.C) statusByte |= 0x01;
    push8(statusByte);

    // 5. I flag NOT modified (ABORT ignores I flag, same as NMI)
    // (No change to P.I)

    // 6. Clear D flag (binary mode)
    P.D = false;

    // 7. Read ABORT vector from $00:FFF8-FFF9
    uint32_t vectorAddr = 0x00FFF8;
    uint16_t handlerAddr = readWord(vectorAddr);

    // 8. Set PC to handler address, PB to 0
    PC = handlerAddr;
    PB = 0x00;

    // Add interrupt overhead cycles
    cycleCount += 7;  // Interrupt acknowledge + stack ops + vector fetch
}

} // namespace ZeroPoint
