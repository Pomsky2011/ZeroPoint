#ifndef ZEROPOINT_PPU_H
#define ZEROPOINT_PPU_H

#include <cstdint>
#include <cstddef>
#include <array>

namespace ZeroPoint {

// Forward declaration
class Display;

// PPU operates at 68.011355 MHz (master clock, NTSC colorburst x19)
// Executes 1 instruction per cycle
constexpr uint32_t PPU_CLOCK_HZ = 68011355;
constexpr uint32_t PPU_CYCLES_PER_INSTRUCTION = 1;

// PPU opcodes (4-bit)
enum class PPUOpcode : uint8_t {
    DEFCALL    = 0x0,  // Define callable function
    MOVXP_NOP  = 0x1,  // Move execution pointer / NOP
    SWAPREG    = 0x2,  // Swap two registers
    CLR        = 0x3,  // Clear register
    CMP        = 0x4,  // Compare registers
    CLRF       = 0x5,  // Clear flags
    JUMP_ZG    = 0x6,  // Jump if zero/greater
    JUMP_NZG   = 0x7,  // Jump if not zero/not greater
    INC        = 0x8,  // Increment register
    DEC        = 0x9,  // Decrement register
    ADD        = 0xA,  // Add registers
    SUB        = 0xB,  // Subtract registers
    MUL        = 0xC,  // Multiply registers
    INTDIV     = 0xD,  // Integer division
    PRESET_E   = 0xE,  // Preset E: immediate operations
    PRESET_F   = 0xF   // Preset F: extended instructions
};

// Preset E sub-opcodes (immediate/byte operations)
enum class PresetEOpcode : uint8_t {
    TARREG     = 0x0,  // Set target register
    SETBYTE    = 0x1,  // Set byte in target register
    BUILD      = 0x2,  // Build register from target registers
    CPREG      = 0x3   // Copy register to register
};

// Preset F sub-opcodes
enum class PresetFOpcode : uint8_t {
    SETPOS           = 0x0,  // Set position
    SETTILE          = 0x1,  // Set tile with mode
    SETDP            = 0x2,  // Set data pointer
    MOVDP            = 0x3,  // Move to (DP)
    SETRENDMOD       = 0x4,  // Set render mode (16/32-bit RGBA)
    PALETTE16_LOAD   = 0x5,  // Load 16-color palette from DP
    PALETTE256_LOAD  = 0x6,  // Load 256-color palette from DP
    JMR              = 0x7,  // Jump relative (PC)
    MOV              = 0x8,  // Move from (DP)
    SETREGBANK       = 0x9,  // Set register bank
    CLRTILE          = 0xA,  // Clear tile
    CLRPALETTE       = 0xB,  // Clear palette
    TILEDRAW         = 0xC,  // Draw tile at position
    RESERVED_D       = 0xD,  // Reserved
    CALL             = 0xE,  // Call function
    GBLS             = 0xF   // Get blank status
};

// Comparison flags
struct PPUFlags {
    bool zero;     // Result was zero
    bool greater;  // Left operand > right operand

    PPUFlags() : zero(false), greater(false) {}
};

// PPU state
enum class PPUState {
    WaitingForStart,       // Waiting for start signal
    Running,               // Executing microcode
    Halted                 // Halted by instruction
};

class PPU {
public:
    PPU();
    ~PPU();

    // Clock the PPU by one cycle
    void tick();

    // Run up to `cycles` PPU cycles in one call and return how many were
    // actually consumed. Semantically identical to calling tick() `cycles`
    // times, but collapses multi-cycle stalls into a single step instead of
    // spinning one function call per stalled cycle. This is the fast interpreter
    // path used by the standalone runner/JIT executor.
    uint32_t runBatch(uint32_t cycles);

    // Load microcode into memory
    void loadMicrocode(const uint8_t* code, size_t length, uint16_t offset = 0);

    // Raise a CPU-driven interrupt on the PPU: at its next instruction boundary
    // the PPU pushes the return address and vectors to `vector`. Highest priority
    // (above V/H-blank), one-shot. Used by the host/boot ROM to notify a running
    // PPU program that its code is about to be switched so it can react.
    void raiseCpuInterrupt(uint16_t vector) {
        cpuIrqPending = true;
        cpuIrqVector = vector;
    }

    // Start execution
    void start();

    // Reset PPU
    void reset();

    // Get current state
    PPUState getState() const { return state; }

    // Set blank status (from display controller)
    void setBlankStatus(bool vblank, bool hblank) {
        this->vblank = vblank;
        this->hblank = hblank;
    }

    // Set display pointer (for memory-mapped display I/O)
    void setDisplay(Display* disp) {
        display = disp;
    }

    // Get register value (for debugging)
    uint16_t getRegister(uint8_t reg) const {
        if (reg < 64) return registers[reg];
        return 0;
    }

    // Get execution pointer (for HLT detection)
    uint16_t getExecutionPointer() const {
        return executionPointer;
    }

    // Direct memory access (for debugging and CPU window)
    uint8_t readMemory(uint16_t address) const {
        return handleMemoryRead(address);
    }

    void writeMemory(uint16_t address, uint8_t value) {
        memory[address] = value;
        handleMemoryWrite(address, value);
    }

    // Register access for CPU I/O
    void setRegister(uint8_t reg, uint16_t value) {
        if (reg < 64) registers[reg] = value;
    }

    // Set the live execution pointer (CPU I/O write to PPUPC). Also mirrors
    // into R62 so reads via getRegister(REG_PC) stay consistent.
    void setExecutionPointer(uint16_t value) {
        executionPointer = value;
        registers[REG_PC] = value;
    }

private:
    // 64 registers, 16-bit each
    std::array<uint16_t, 64> registers;

    // Special register indices
    static constexpr uint8_t REG_DP = 63;  // Data Pointer
    static constexpr uint8_t REG_PC = 62;  // Program Counter
    static constexpr uint8_t REG_SP = 61;  // Stack Pointer
    static constexpr uint8_t REG_HBLANK_INT = 60;  // H-Blank Interrupt Address
    static constexpr uint8_t REG_VBLANK_INT = 59;  // V-Blank Interrupt Address

    // Target registers for Preset E (4 target registers, each 16-bit)
    std::array<uint16_t, 4> targetRegisters;
    std::array<uint8_t, 4> targetBytes;  // 0 = LSB, 1 = MSB

    // 64 KiB memory (shared with ROM)
    std::array<uint8_t, 65536> memory;

    // Execution pointer (actual instruction pointer)
    uint16_t executionPointer;

    // Comparison flags
    PPUFlags flags;

    // Current state
    PPUState state;

    // Blank status
    bool vblank;
    bool hblank;

    // Previous-tick blank state. Blank interrupts are edge-triggered: they fire
    // once on the 0->1 transition, not continuously while the blank line is
    // asserted. Without this, an ISR that runs longer than a few cycles would be
    // re-entered at every instruction boundary during the blank period, blowing
    // the stack. Sampled at each instruction boundary in serviceInterrupts().
    bool prevVblank;
    bool prevHblank;

    // CPU-raised (host/boot ROM) interrupt: pending flag + vector. Serviced at
    // the next instruction boundary with priority over the blank interrupts.
    bool cpuIrqPending;
    uint16_t cpuIrqVector;

    // Register banks for preset F setpos (4-bit register IDs)
    uint8_t regBankX;
    uint8_t regBankY;

    // Call table (DEFCALL/CALL): DEFCALL registers an entry-point address
    // under an 8-bit slot number; CALL's 8-bit operand looks the address up
    // and jumps to it, matching PresetFOpcode::CALL's addressing range.
    std::array<uint16_t, 256> callTable;

    // Cycle counter
    uint32_t cycleCounter;

    // Multi-cycle instruction pacing. An instruction applies its effect on the
    // cycle it is issued, then occupies the PPU for the remaining cycles of its
    // cost. While stallCycles > 0 the PPU is "busy" and tick() does nothing but
    // count down. instrCycles is the cost of the instruction most recently
    // issued (set during executeInstruction / executePresetF).
    uint32_t stallCycles;
    uint16_t instrCycles;

    // Display pointer (for memory-mapped I/O)
    Display* display;

    // Tile system (8x8 tiles, 64 bytes each, up to 256 tiles)
    static constexpr size_t TILE_SIZE = 64;      // 8x8 pixels
    static constexpr size_t MAX_TILES = 256;
    std::array<std::array<uint8_t, TILE_SIZE>, MAX_TILES> tileStorage;
    uint8_t currentTileId;                        // Currently selected tile for drawing
    uint8_t currentTileMode;                      // Tile mode (0-3): 16BBGR 4bpp, 32RGBA 4bpp, 16BBGR 8bpp, 32RGBA 8bpp

    // Palette system
    std::array<uint16_t, 16> palette16;           // 16-color palette (16-bit BBGR format)
    std::array<uint32_t, 256> palette256;         // 256-color palette (32-bit RGBA format)
    std::array<uint8_t, 4> tileTranslucency;      // Translucency for last 4 tiles (4 bits each)
    uint8_t tileBlendMode;                        // Blend mode for tiles (0-3)

    // Video Output Coprocessor (VOC) registers at $00F0-$00FF
    struct VOCRegisters {
        uint8_t renderModeControl;      // $00F0: CRHVPLIW
        uint8_t paletteAddrLow;         // $00F1: Palette address LSB
        uint8_t paletteAddrHigh;        // $00F2: Palette address MSB
        uint8_t bankOrder[8];           // $00F3-$00FA: Framebuffer bank order
        uint8_t autoRollToggle;         // $00FB: Auto-roll toggle
        uint8_t translucency[4];        // $00FC-$00FF: Tile translucency (32-bit)
    } vocRegisters;

    // VOC bit masks for $00F0
    static constexpr uint8_t VOC_COLOR_DEPTH = 0x80;   // Bit 7: 0=16-bit, 1=32-bit
    static constexpr uint8_t VOC_ROLLING_MODE = 0x40;  // Bit 6: 0=block, 1=single
    static constexpr uint8_t VOC_HBLANK_INT = 0x20;    // Bit 5: H-blank interrupt enable
    static constexpr uint8_t VOC_VBLANK_INT = 0x10;    // Bit 4: V-blank interrupt enable
    static constexpr uint8_t VOC_PALETTE_MODE = 0x08;  // Bit 3: 0=16 color, 1=256 color
    static constexpr uint8_t VOC_RESET = 0x01;         // Bit 0: Reset switch

    // Execution methods
    void executeInstruction();
    void executePresetE(uint8_t subopcode, uint16_t operand);
    void executePresetF(uint8_t subopcode, uint8_t operand);

    // Recognize a pending V/H-blank interrupt at an instruction boundary. If one
    // fires, pushes the return address, redirects the execution pointer to the
    // handler, sets the interrupt-entry stall, and returns true. Shared by tick()
    // and runBatch() so their boundary behavior can never drift apart.
    bool serviceInterrupts();

    // Push a return address onto the PPU stack (SP grows upward, matching the
    // ppuasm PUSH/RET shorthands). Shared by every interrupt-entry path and
    // CALL. Wraps SP and the memory index at 16 bits rather than indexing past
    // the end of `memory` when SP is 0xFFFF.
    void pushReturnAddress(uint16_t returnAddr) {
        registers[REG_SP] += 2;
        memory[registers[REG_SP]] = returnAddr & 0xFF;
        memory[(registers[REG_SP] + 1) & 0xFFFF] = (returnAddr >> 8) & 0xFF;
    }

    // Helper methods
    // Inline hot-path flag functions to eliminate function call overhead
    inline void updateFlags(uint16_t result, uint16_t left, uint16_t right) {
        flags.zero = (result == 0);
        flags.greater = (left > right);
    }

    inline void clearFlags() {
        flags.zero = false;
        flags.greater = false;
    }

    // Memory-mapped I/O handlers
    void handleMemoryWrite(uint16_t address, uint8_t value);
    uint8_t handleMemoryRead(uint16_t address) const;

    // VOC helper methods
    void handleVOCWrite(uint16_t address, uint8_t value);
    uint8_t handleVOCRead(uint16_t address) const;
    void applyVOCRenderMode();
    void applyVOCReset();

    // Palette helper methods
    void loadPalette16();
    void loadPalette256();
    uint32_t applyTranslucency(uint32_t color, uint8_t tileIndex);

    // Expand a 16-bit palette entry (BBBBBGGGGGRRRRR-A) to 32-bit RGBA,
    // shared by TILEDRAW's 16-color paths and the palette-indexed pixel-draw
    // port.
    static inline uint32_t expandPalette16(uint16_t pal16) {
        uint8_t r5 = (pal16 >> 1) & 0x1F;
        uint8_t g5 = (pal16 >> 6) & 0x1F;
        uint8_t b5 = (pal16 >> 11) & 0x1F;
        uint8_t a1 = pal16 & 0x01;
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        uint8_t a8 = a1 ? 0xFF : 0x00;
        return (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
    }
    uint32_t blendColors(uint32_t src, uint32_t dst, uint8_t mode, uint8_t alpha);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_PPU_H
