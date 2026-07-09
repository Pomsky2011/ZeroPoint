#ifndef ZEROPOINT_CPU_H
#define ZEROPOINT_CPU_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <functional>
#include <memory>
#include <vector>

// Forward declaration (APU is in global namespace)
class APU;

namespace ZeroPoint {

// Forward declarations
class PPU;
class Display;
class DMAController;

// DEF88186 Main CPU - Hybrid 65816/8086 16-bit processor
// 256 opcodes, 24-bit addressing, little-endian
// Flags: NVMXDIZC (Negative, Overflow, M, X, Decimal, Interrupt, Zero, Carry)

// CPU status flags (Processor Status Register P)
struct CPUFlags {
    bool N;  // Negative
    bool V;  // Overflow
    bool M;  // Memory/Accumulator size (0=16-bit, 1=8-bit)
    bool X;  // Index size (0=16-bit, 1=8-bit)
    bool D;  // Decimal mode
    bool I;  // Interrupt disable
    bool Z;  // Zero
    bool C;  // Carry

    CPUFlags() : N(false), V(false), M(true), X(true), D(false), I(true), Z(false), C(false) {}

    // Convert to/from 8-bit value
    uint8_t toByte() const {
        return (N ? 0x80 : 0) | (V ? 0x40 : 0) | (M ? 0x20 : 0) | (X ? 0x10 : 0) |
               (D ? 0x08 : 0) | (I ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
    }

    void fromByte(uint8_t value) {
        N = value & 0x80;
        V = value & 0x40;
        M = value & 0x20;
        X = value & 0x10;
        D = value & 0x08;
        I = value & 0x04;
        Z = value & 0x02;
        C = value & 0x01;
    }
};

// CPU state
enum class CPUState {
    Running,
    Halted,
    Waiting   // WAI instruction
};

class CPU {
public:
    CPU();
    ~CPU();

    // Execution
    void reset();
    void step();                        // Execute one instruction
    void run(uint64_t cycles);          // Run for N cycles

    // Interrupt handling
    void triggerIRQ();                  // Assert maskable interrupt line (latched)
    void triggerNMI();                  // Assert non-maskable interrupt line (latched)
    void serviceInterrupts();          // Service any pending interrupts (at instruction boundary)
    bool isIRQPending() const { return irqPending; }
    bool isNMIPending() const { return nmiPending; }

    // Memory interface
    void setMemory(uint8_t* mem, size_t size);  // Legacy interface
    uint8_t readByte(uint32_t address);
    void writeByte(uint32_t address, uint8_t value);
    uint16_t readWord(uint32_t address);
    void writeWord(uint32_t address, uint16_t value);
    uint32_t readLong(uint32_t address);
    void writeLong(uint32_t address, uint32_t value);

    // Memory mapping
    void setPPU(PPU* ppu) { ppuPtr = ppu; }
    void setAPU(APU* apu) { apuPtr = apu; }
    void setDisplay(Display* disp) { displayPtr = disp; }
    void setDMA(DMAController* dma) { dmaPtr = dma; }
    void setSystem(class System* sys) { systemPtr = sys; }
    void loadROM(const uint8_t* data, size_t size, uint8_t startBank);
    void allocateRAM(uint8_t startBank, uint8_t numBanks);
    void mapPPUWindow(uint8_t bank);
    void mapAPUWindow(uint8_t bank);

    // Setup all I/O registers (requires PPU, APU, Display to be set first)
    void setupIORegisters();

    // I/O register callbacks
    using IOReadCallback = std::function<uint8_t(uint16_t offset)>;
    using IOWriteCallback = std::function<void(uint16_t offset, uint8_t value)>;
    void registerIORegion(uint8_t bank, uint16_t baseOffset, uint16_t size,
                          IOReadCallback readFn, IOWriteCallback writeFn);

    // Register access
    uint16_t getA() const { return A; }
    uint16_t getX() const { return X; }
    uint16_t getY() const { return Y; }
    uint16_t getSP() const { return SP; }
    uint16_t getD() const { return D; }
    uint16_t getPC() const { return PC; }
    uint8_t getPB() const { return PB; }
    uint8_t getDB() const { return DB; }
    CPUFlags getFlags() const { return P; }

    void setA(uint16_t value) { A = value; }
    void setX(uint16_t value) { X = value; }
    void setY(uint16_t value) { Y = value; }
    void setSP(uint16_t value) { SP = value; }
    void setD(uint16_t value) { D = value; }
    void setPC(uint16_t value) { PC = value; }
    void setPB(uint8_t value) { PB = value; }
    void setDB(uint8_t value) { DB = value; }
    void setFlags(CPUFlags value) { P = value; }

    // State
    CPUState getState() const { return state; }
    bool isHalted() const { return state == CPUState::Halted; }

    // Statistics
    uint64_t getCycleCount() const { return cycleCount; }
    uint64_t getInstructionCount() const { return instructionCount; }

    // ========================================================================
    // Public API for instruction handlers (cpu_instructions.cpp)
    // ========================================================================
    // These methods are public so individual instruction handlers can use them

    // Fetch operations
    uint8_t fetch();
    uint16_t fetch16();
    uint32_t fetch24();

    // Addressing modes
    uint32_t addrImmediate();
    uint32_t addrAbsolute();
    uint32_t addrAbsoluteLong();
    uint32_t addrAbsoluteIndexedX();
    uint32_t addrAbsoluteIndexedY();
    uint32_t addrAbsoluteLongIndexedX();
    uint32_t addrDirectPage();
    uint32_t addrDirectPageIndexedX();
    uint32_t addrDirectPageIndexedY();
    uint32_t addrDirectPageIndirect();
    uint32_t addrDirectPageIndirectLong();
    uint32_t addrDirectPageIndexedIndirectX();
    uint32_t addrDirectPageIndirectIndexedY();
    uint32_t addrDirectPageIndirectLongIndexedY();
    uint32_t addrStackRelative();
    uint32_t addrStackRelativeIndirectIndexedY();
    uint32_t addrAbsoluteLongIndexedY();
    uint32_t addrAbsoluteIndirect();          // JMP (addr)
    uint32_t addrAbsoluteIndirectLong();      // JMP [addr]
    uint32_t addrAbsoluteIndexedIndirect();   // JMP (addr,X)

    // Stack operations
    void push8(uint8_t value);
    void push16(uint16_t value);
    void push24(uint32_t value);
    uint8_t pull8();
    uint16_t pull16();
    uint32_t pull24();

    // Flag operations (inlined for performance)
    inline void setNZ8(uint8_t value) {
        P.Z = (value == 0);
        P.N = (value & 0x80) != 0;
    }

    inline void setNZ16(uint16_t value) {
        P.Z = (value == 0);
        P.N = (value & 0x8000) != 0;
    }

    inline void setNZC8(uint8_t value) {
        setNZ8(value);
    }

    inline void setNZC16(uint16_t value) {
        setNZ16(value);
    }

    // Instruction implementations (organized by category)

    // Data Transfer
    void opLDA(uint32_t addr);
    void opLDX(uint32_t addr);
    void opLDY(uint32_t addr);
    void opSTA(uint32_t addr);
    void opSTX(uint32_t addr);
    void opSTY(uint32_t addr);
    void opSTZ(uint32_t addr);

    // Arithmetic
    void opADC(uint32_t addr);
    void opSBC(uint32_t addr);
    void opMUL(uint32_t addr);
    void opDIV();                    // DIV X,Y  (X/Y -> A, remainder -> X)
    void opDIVmem(uint32_t addr);    // DIV A by memory (A/[addr] -> A, rem -> X)
    void opINC(uint32_t addr);
    void opDEC(uint32_t addr);
    void opINCA();  // INC A (accumulator)
    void opDECA();  // DEC A (accumulator)
    void opINX();
    void opINY();
    void opDEX();
    void opDEY();

    // Logical
    void opAND(uint32_t addr);
    void opORA(uint32_t addr);
    void opXOR(uint32_t addr);
    void opBIT(uint32_t addr);

    // Shifts and Rotates
    void opASL(uint32_t addr, bool accumulator);
    void opLSR(uint32_t addr, bool accumulator);
    void opROL(uint32_t addr, bool accumulator);
    void opROR(uint32_t addr, bool accumulator);
    void opSHL(bool isY);
    void opSHR(bool isY);
    void opRCL();

    // Branches
    void opBMI();
    void opBRA();
    void opBRL();
    void opBVS();
    void opBCS();
    void opBEQ();

    // Jumps and Subroutines
    void opJMP(uint32_t addr);
    void opJSR(uint32_t addr);
    void opCALL();
    void opRTS();
    void opRTL();
    void opRTI();
    void opRET();

    // Stack Operations
    void opPHA();
    void opPHX();
    void opPHY();
    void opPHP();
    void opPHB();
    void opPHD();
    void opPHK();
    void opPUSH();
    void opPEA();
    void opPEI();
    void opPER();
    void opPLA();
    void opPOPF();

    // Comparison
    void opCMP(uint32_t addr);
    void opCPX(uint32_t addr);
    void opCPY(uint32_t addr);

    // Register Transfer
    void opTDC();
    void opTSC();
    void opTCS();
    void opTAX();
    void opTXA();
    void opTAY();
    void opTCD();
    void opTXY();
    void opTYA();
    void opTYX();

    // Flags
    void opSEP();
    void opREP();
    void opSEC();
    void opCLC();
    void opSED();
    void opCLD();
    void opSEI();
    void opCLI();
    void opCLV();

    // Control
    void opNOP();
    void opBRK();
    void opCOP();
    void opWAI();
    void opHLT();
    void opLOOP();
    void opLPEND();
    void opSDB();

    // Block Move
    void opMVN();
    void opMVP();

    // Direct register and cycle counter access for instruction handlers
    uint64_t cycleCount;

    // Registers (public for instruction handlers)
    uint16_t A;      // Accumulator (16-bit or 8-bit based on M flag)
    uint16_t X;      // Index X (16-bit or 8-bit based on X flag)
    uint16_t Y;      // Index Y (16-bit or 8-bit based on X flag)
    uint16_t SP;     // Stack Pointer (16-bit, grows downward)
    uint16_t D;      // Direct Page (16-bit base for direct page addressing)
    uint16_t PC;     // Program Counter (16-bit)
    uint8_t PB;      // Program Bank (8-bit)
    uint8_t DB;      // Data Bank (8-bit)
    CPUFlags P;      // Processor Status (8 flags)

private:
    // Hardware loop counter (LOOP/LPEND)
    uint16_t loopCounter;
    uint16_t loopStart;

    // Memory regions
    struct MemoryRegion {
        enum Type {
            UNMAPPED,
            ROM,
            RAM,
            PPU_WINDOW,
            APU_WINDOW,
            IO_REGISTERS
        };

        Type type;
        uint8_t startBank;
        uint8_t endBank;
        uint16_t startOffset;  // Offset within bank
        uint16_t endOffset;
        uint8_t* data;         // For ROM/RAM
        size_t dataSize;
        IOReadCallback ioRead;
        IOWriteCallback ioWrite;

        MemoryRegion() : type(UNMAPPED), startBank(0), endBank(0),
                        startOffset(0), endOffset(0xFFFF),
                        data(nullptr), dataSize(0) {}
    };

    std::vector<MemoryRegion> memoryMap;
    bool useMemoryMap;  // Optimization: avoid checking vector size every access

    // Bank-indexed acceleration for findRegion(). Every access derives its
    // bank from address>>16, so instead of scanning memoryMap linearly per
    // byte we index by bank:
    //   bankFast[bank]  - the region if this bank is covered by a single
    //                     full-offset-range region (ROM/RAM/PPU/APU window);
    //                     hit directly with no scan. nullptr otherwise.
    //   bankScan[bank]  - true only for banks with partial/multiple regions
    //                     (the I/O bank $D8); these fall back to a scan
    //                     restricted to that bank. false = bank is unmapped.
    // Rebuilt whenever the memory map changes.
    std::array<MemoryRegion*, 256> bankFast;
    std::array<bool, 256> bankScan;
    void rebuildBankTable();

    // Legacy memory interface
    uint8_t* memory;
    size_t memorySize;

    // Hardware pointers
    PPU* ppuPtr;
    APU* apuPtr;
    Display* displayPtr;
    DMAController* dmaPtr;
    class System* systemPtr;

    // Memory mapping helpers
    uint8_t readMapped(uint32_t address);
    void writeMapped(uint32_t address, uint8_t value);
    MemoryRegion* findRegion(uint32_t address);

    // State
    CPUState state;
    uint64_t instructionCount;

    // Interrupt line latches. Assertions are held pending until serviced,
    // so a maskable IRQ raised while I=1 is not lost — it fires once the
    // program clears I. NMI is edge-latched and always serviced.
    bool irqPending;
    bool nmiPending;

    // Actual interrupt sequences (push state + vector). Invoked by
    // serviceInterrupts() once the interrupt is eligible to run.
    void serviceIRQ();
    void serviceNMI();

    // Execution
    void executeInstruction();
};

} // namespace ZeroPoint

#endif // ZEROPOINT_CPU_H
