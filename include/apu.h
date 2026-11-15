#ifndef APU_H
#define APU_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

// ZeroPoint Audio Processing Unit (APU)
// 8-bit RISC processor @ 4.250710 MHz (master/16), 4 CPI
// Instruction set: 47 instructions (5-bit opcode + 11-bit operands)
class APU {
public:
    enum class HleMode : uint8_t { None, WaitHeader, Engine };

    APU();
    ~APU();

    // Execution
    void reset();
    void step();                    // Execute one instruction (4 cycles)
    void run(uint64_t cycles);      // Run for N cycles

    // Memory
    void loadROM(const uint8_t* data, size_t size, uint16_t address);

    // Replace the internal BIOS (the region APU execution starts in - see
    // BIOS_BASE/BIOS_SIZE below) with a user-supplied payload, e.g. one
    // built from the ZPbootROM apu/internalBIOS sources. Unlike a plain
    // loadROM(data, size, BIOS_BASE) call, this survives reset(): the APU's
    // memory is a flat array that reset() zero-fills wholesale (there's no
    // per-region bookkeeping like the CPU's bank map to leave a loaded ROM
    // region alone), so the payload is kept and reapplied after every
    // reset() for as long as the process runs. With no BIOS loaded, APU
    // execution starts on zero-filled memory (an endless run of NOP).
    void loadBIOS(const uint8_t* data, size_t size);
    bool hasBIOS() const { return !customBios.empty(); }

    // High-level-emulated BIOS (src/apu_hle.cpp): a native C++ stand-in for the
    // APU internal BIOS (ZPbootROM apu/internalBIOS) used when no BIOS image
    // is loaded. It waits for a program/score header at $3000, checks the
    // checksum, runs a $0A program or plays a $0B ZPA score with the same
    // sequencer semantics as engine.uas, and gives the same MMP register
    // writes per tick (tools/test_zpa_engine --hle diffs the two). Off by
    // default so a bare APU (unit tests, demos that load code at $8000) runs
    // exactly what it is given; System turns it on unless --apu-bios is used.
    // It only takes over when execution sits at an EMPTY BIOS entry ($8000),
    // so a program loaded there still runs normally. loadBIOS() turns it off.
    void enableHLEBios(bool on);
    bool hleBiosEnabled() const { return hleBios; }
    bool hleEngineActive() const { return hleMode == HleMode::Engine; }

    uint8_t readByte(uint16_t address);
    void writeByte(uint16_t address, uint8_t value);

    // Observe every byte write that reaches RAM/MMP (tests only: the BIOS
    // engine trace harness uses it to log MMP register writes). Called before
    // the write is applied; null (the default) costs one branch.
    using WriteHook = void (*)(void* ctx, uint16_t address, uint8_t value);
    void setWriteHook(WriteHook hook, void* ctx) { writeHook = hook; writeHookCtx = ctx; }
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
    //   $0070-$007F  Stream port 16 bytes             (stereo PCM stream, see below)
    //   $0080-$00FF  Reserved2   128 bytes            (reserved)

    // Stream port ($0070-$007F): a stereo 8-bit PCM ring buffer in APU RAM that the CPU/DMA keeps filled
    // and the mixer drains at its own rate, added on top of the 16 channels. Frame = 2 bytes (L, R signed).
    //   $70 CTRL    bit0 ENABLE, bit1 RESET (self-clearing: RDPTR = WRPTR = 0, position and status cleared)
    //   $71 STATUS  bit0 UNDERRUN (sticky; write 1 to clear), bit1 PLAYING (read-only: last tick had data)
    //   $72 BASE    16-bit, APU address of the ring (must be below the AROM window, $D000)
    //   $74 SIZE    16-bit, ring length in frames (so 2*SIZE bytes); set before RESET/ENABLE
    //   $76 RDPTR   16-bit read-only, frame index the mixer is at (CPU writes ignored)
    //   $78 WRPTR   16-bit, frame index one past the last frame the CPU has written; applies on the high-byte write
    //   $7A PITCH   16-bit, 0x1000 = one stream frame per 48 kHz output tick (cubic-resampled); high-byte write applies
    //   $7C VOL     0-255, default 255
    // The ring is full at SIZE-1 frames. The mixer needs 3 frames ahead of RDPTR (cubic lookahead): with fewer it
    // outputs silence, holds position and sets UNDERRUN. Free space = (RDPTR - WRPTR - 1) mod SIZE.

    // Statistics
    uint64_t getCycleCount() const { return cycleCount; }
    uint64_t getInstructionCount() const { return instructionCount; }

    // FNV-1a over all APU state (memory, AROM, registers, banking, loops, call stack, MMP channels, stream port,
    // HLE engine). Field by field. For tools/test_determinism.
    uint64_t stateHash() const;

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
    void execSCR(uint16_t operand);
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
    void execSTRX(uint16_t operand);  // Indexed load: dataReg = mem[(dp<<8)|offsetReg]  opcode 0x1D
    void execSTAX(uint16_t operand);  // Indexed store: mem[(dp<<8)|offsetReg] = dataReg  opcode 0x1D
    void execJMX(uint16_t operand);   // Jump to (Y<<8)|X, no stack push               opcode 0x1E
    void execJSX(uint16_t operand);   // Jump to (Y<<8)|X, push return address          opcode 0x1E

    // Stack helper functions
    void pushByte(uint8_t value);
    uint8_t popByte();
    void pushWord(uint16_t value);
    uint16_t popWord();

    // MMP helper functions
    void resetMMP();
    void updateStream();                          // one 48 kHz tick of the stream port
    void writeStreamReg(uint16_t address, uint8_t value);
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
    std::array<uint8_t, MEMORY_SIZE> memory{};
    std::array<uint8_t, AROM_SIZE> arom{};
    std::array<uint8_t, 256> registers{};  // Up to 256 general purpose registers

    // User-loaded internal BIOS payload (see loadBIOS()), reapplied at
    // BIOS_BASE after every reset() since memory.fill(0) would otherwise
    // wipe it. Empty when no custom BIOS has been loaded.
    std::vector<uint8_t> customBios;

    WriteHook writeHook = nullptr;
    void* writeHookCtx = nullptr;

    // --- HLE BIOS state (apu_hle.cpp) ---
    struct HleChannel {
        uint16_t stl = 0, pending = 0;      // STL address playing / waiting to start
        bool live = false;                  // a NOTE or SET has given it a pitch
        bool force = true;                  // nothing written yet: write all fields next
        uint16_t pitch = 0, vol = 0, pan = 0;   // vol/pan are 8.8; the high byte is the register
        uint16_t wPitch = 0;
        uint8_t wVol = 0, wPan = 0;
        uint8_t slideTicks[3] = {0, 0, 0};  // pitch, vol, pan
        int16_t slideDelta[3] = {0, 0, 0};
    };
    struct HleEngine {
        HleChannel ch[16];
        uint16_t ptr = 0;                   // address of the next unread score byte
        uint16_t cnt = 0;                   // ticks until the next event
        bool waiting = false;
        struct Frame { uint16_t ptr; uint8_t count; bool started; } frames[4] = {};
        int depth = 0;
        uint32_t tick = 0;
        uint64_t startCycle = 0;
    };
    bool hleBios = false;
    HleMode hleMode = HleMode::None;
    HleEngine hle;
    bool hleIntercept();                    // true if HLE consumed this step()
    void hleWaitForHeader();
    void hleStartEngine(uint16_t entry);
    void hleTick();                         // one 120 Hz engine tick
    bool hleRunEvent();                     // false on END
    void hleReadDelta();
    uint8_t hleFetch();
    void hleStop(int c);
    void hleWrite16(uint16_t reg, uint16_t value);

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
    std::array<LoopState, 16> loops{};

    // Function state
    struct FunctionState {
        uint16_t address;
        bool defined;
    };
    std::array<FunctionState, 2048> functions{};
    std::vector<uint16_t> callStack;

    // Statistics
    uint64_t cycleCount;
    uint64_t instructionCount;
    bool halted;

    // MMP State (16 channels with pan)
    struct MMPChannel {
        uint16_t pitch = 0;           // Playback rate (0x1000 = 1.0×)
        uint8_t volume = 0;           // 0 = silent, 255 = max
        uint8_t pan = 0;              // 0 = full L, 128 = center, 255 = full R
        uint8_t reserved0 = 0;        // Reserved for future per-channel flags
        uint16_t stlAddress = 0;      // Pointer to STL entry (0 = off)
        uint32_t samplePosition = 0;  // Fixed-point position (upper bits = index, lower 12 = fraction)
        bool active = false;

        // Sample data (loaded from STL/SST)
        uint16_t sampleDataAddress = 0;
        uint16_t loopAddress = 0;     // STL loop word: sample index to wrap to (0 = whole sample)
        bool oneShot = false;         // Z bit of the final block: play to the end once, then stop
        std::vector<uint8_t> sampleCache;
    };

    std::array<MMPChannel, 16> mmpChannels;
    uint32_t loopStartSample(const MMPChannel& ch) const;

    // Stream port state ($0070-$007F); RDPTR/STATUS are mirrored into memory[] so CPU reads see them
    struct StreamPort {
        bool enable;
        uint16_t base, size, rd, wr, pitch;
        uint8_t volume;
        uint32_t frac;            // Q12 position between the frame at rd and the next
        int8_t prevL, prevR;      // frame before rd (cubic s0); equals the current frame until one has passed
        bool havePrev;
        bool underrun, playing;
        int32_t outL, outR;       // this tick's output, already scaled to 16 bits and volume
    };
    StreamPort stream{};
    uint8_t  reserved2[128] = {};
};

#endif // APU_H
