#ifndef APU_H
#define APU_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

// ZeroPoint Audio Processing Unit (APU)
// 8-bit RISC processor @ 4.2 MHz, 4 CPI
// Instruction set: 47 instructions (5-bit opcode + 11-bit operands)
class APU {
public:
    APU();
    ~APU();

    // Execution
    void reset();
    void step();                    // Execute one instruction (4 cycles)
    void run(uint64_t cycles);      // Run for N cycles

    // Memory
    void loadROM(const uint8_t* data, size_t size, uint16_t address);
    uint8_t readByte(uint16_t address);
    void writeByte(uint16_t address, uint8_t value);
    uint16_t readWord(uint16_t address);
    void writeWord(uint16_t address, uint16_t value);

    // Registers
    uint8_t getRegister(uint8_t reg);
    void setRegister(uint8_t reg, uint8_t value);
    uint16_t getPC() const { return pc; }
    void setPC(uint16_t value) { pc = value; }
    uint16_t getSP() const { return sp; }
    void setSP(uint16_t value) { sp = value; }

    // Banking
    void setROMBank(uint16_t bank);
    void setIOBank(uint16_t bank);

    // DEF88186 Interface (CPU communication)
    uint8_t readDEF88186Input(uint16_t offset);   // CPU → APU
    void writeDEF88186Input(uint16_t offset, uint8_t value);
    uint8_t readDEF88186Output(uint16_t offset);  // APU → CPU
    void writeDEF88186Output(uint16_t offset, uint8_t value);

    // MMP (Music Mixing Processor)
    void updateMMP();               // Called every audio sample
    int16_t getMixedSampleLeft();   // Get left channel output
    int16_t getMixedSampleRight();  // Get right channel output

    // MMP register map ($0000-$00FF):
    //   $0000-$001F  Pitch       ch0-15, 2 bytes each (little-endian, 0x1000 = 1.0x)
    //   $0020-$002F  Volume      ch0-15, 1 byte each  (0 = silent, 255 = max)
    //   $0030-$003F  Pan         ch0-15, 1 byte each  (0 = full L, 128 = center, 255 = full R)
    //   $0040-$004F  Reserved0   ch0-15, 1 byte each  (reserved for future per-channel flags)
    //   $0050-$006F  STL address ch0-15, 2 bytes each (0 = channel off)
    //   $0070-$007F  Reserved1   16 bytes             (reserved for future global flags)
    //   $0080-$00FF  Reserved2   128 bytes            (reserved)

    // Statistics
    uint64_t getCycleCount() const { return cycleCount; }
    uint64_t getInstructionCount() const { return instructionCount; }

    // Debug
    bool isHalted() const { return halted; }
    void setHalted(bool value) { halted = value; }

private:
    // Instruction execution
    void executeInstruction(uint16_t instruction);

    // MMP mixer helper
    int16_t mixChannels(bool right);

    // Instruction handlers
    void execNOP(uint16_t operand);
    void execJMP(uint16_t operand);
    void execJNZ(uint16_t operand);
    void execSRP(uint16_t operand);
    void execSDP(uint16_t operand);
    void execNOR(uint16_t operand);
    void execAND(uint16_t operand);
    void execADD(uint16_t operand);
    void execSUB(uint16_t operand);
    void execSTA(uint16_t operand);
    void execSTR(uint16_t operand);
    void execSBF(uint16_t operand);
    void execSCR(uint16_t operand);
    void execIOO(uint16_t operand);
    void execIOI(uint16_t operand);
    void execZOR(uint16_t operand);
    void execZOA(uint16_t operand);
    void execLST(uint16_t operand);
    void execLFN(uint16_t operand);
    void execBRT(uint16_t operand);
    void execBRP(uint16_t operand);
    void execADC(uint16_t operand);
    void execSBC(uint16_t operand);
    void execBEQ(uint16_t operand);
    void execBNE(uint16_t operand);
    void execBLT(uint16_t operand);
    void execBGT(uint16_t operand);
    void execSDB(uint16_t operand);
    void execJMS(uint16_t operand);   // Jump (no stack)         opcode 0x15
    void execINC(uint16_t operand);   // Increment X/Y/DP/SP     opcode 0x16
    void execWRH(uint16_t operand);   // reserved
    void execWRL(uint16_t operand);   // reserved
    void execCFN(uint16_t operand);   // reserved
    void execSTACK(uint16_t operand); // Stack operations (replaces CFE)
    void execCCF(uint16_t operand);
    void execCME(uint16_t operand);
    void execCMN(uint16_t operand);
    void execCMG(uint16_t operand);
    void execCML(uint16_t operand);

    // Stack helper functions
    void pushByte(uint8_t value);
    uint8_t popByte();
    void pushWord(uint16_t value);
    uint16_t popWord();

    // MMP helper functions
    void resetMMP();
    void processChannel(int channel);
    static int16_t cubicInterpolate(int8_t s0, int8_t s1, int8_t s2, int8_t s3, uint32_t frac);

    // Memory regions
    static constexpr size_t MEMORY_SIZE = 65536;  // 64 KiB
    static constexpr size_t AROM_SIZE = 458752;   // 448 KiB
    static constexpr size_t AROM_BANK_SIZE = 24576; // 24 KiB per bank
    static constexpr size_t AROM_BANKS = 18;      // 18 banks

    // Memory map addresses
    static constexpr uint16_t MMP_BASE = 0x0000;
    static constexpr uint16_t MMP_SIZE = 0x0100;
    static constexpr uint16_t RAM_BASE = 0x0100;
    static constexpr uint16_t RAM_SIZE = 0x0F00;
    static constexpr uint16_t DEF88186_INPUT_BASE = 0x1000;
    static constexpr uint16_t DEF88186_INPUT_SIZE = 0x0800;
    static constexpr uint16_t DEF88186_OUTPUT_BASE = 0x1800;
    static constexpr uint16_t DEF88186_OUTPUT_SIZE = 0x0800;
    static constexpr uint16_t EXIO_BASE = 0x2000;
    static constexpr uint16_t EXIO_SIZE = 0x1000;
    static constexpr uint16_t ARAM_BASE = 0x3000;
    static constexpr uint16_t ARAM_SIZE = 0x4000;
    static constexpr uint16_t STL_BASE = 0x7000;
    static constexpr uint16_t STL_SIZE = 0x1000;
    static constexpr uint16_t BIOS_BASE = 0x8000;
    static constexpr uint16_t BIOS_SIZE = 0x1000;
    static constexpr uint16_t SST_BASE = 0x9000;
    static constexpr uint16_t SST_SIZE = 0x4000;
    static constexpr uint16_t AROM_BASE = 0xD000;
    static constexpr uint16_t AROM_WINDOW_SIZE = 0x3000; // 12 KiB visible

    // Core state
    std::array<uint8_t, MEMORY_SIZE> memory;
    std::array<uint8_t, AROM_SIZE> arom;
    std::array<uint8_t, 256> registers;  // Up to 256 general purpose registers

    // Flag bits (0bzglc)
    static constexpr uint8_t FLAG_Z = 0x08;  // zero
    static constexpr uint8_t FLAG_G = 0x04;  // greater
    static constexpr uint8_t FLAG_L = 0x02;  // less than
    static constexpr uint8_t FLAG_C = 0x01;  // carry

    // Special registers
    uint16_t pc;        // Program Counter
    uint16_t sp;        // Stack Pointer (16-bit)
    uint8_t rp;         // ROM Page
    uint8_t dp;         // Data Page
    uint8_t db;         // Data Byte
    uint8_t flags;      // Flags: 0bzglc
    bool bf;            // Byte Flip (0=low byte, 1=high byte)

    // Banking
    uint16_t romBank;   // Current ROM bank (0-17)
    uint16_t ioBank;    // Current I/O bank

    // Loop state (16 hardware loops)
    struct LoopState {
        uint16_t startAddress;
        uint16_t endAddress;
        uint16_t count;
        uint16_t remaining;
        bool active;
    };
    std::array<LoopState, 16> loops;

    // Function state
    struct FunctionState {
        uint16_t address;
        bool defined;
    };
    std::array<FunctionState, 2048> functions;
    std::vector<uint16_t> callStack;

    // Statistics
    uint64_t cycleCount;
    uint64_t instructionCount;
    bool halted;

    // MMP State (16 channels with pan)
    struct MMPChannel {
        uint16_t pitch;           // Playback rate (0x1000 = 1.0×)
        uint8_t volume;           // 0 = silent, 255 = max
        uint8_t pan;              // 0 = full L, 128 = center, 255 = full R
        uint8_t reserved0;        // Reserved for future per-channel flags
        uint16_t stlAddress;      // Pointer to STL entry (0 = off)
        uint32_t samplePosition;  // Fixed-point position (upper bits = index, lower 12 = fraction)
        bool active;

        // Sample data (loaded from STL/SST)
        uint16_t sampleDataAddress;
        uint16_t loopAddress;
        std::vector<uint8_t> sampleCache;
    };

    std::array<MMPChannel, 16> mmpChannels;

    // Reserved for future global MMP flags
    uint16_t reserved1;
    uint8_t  reserved2[128];
};

#endif // APU_H
