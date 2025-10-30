#ifndef ZEROPOINT_PPU_H
#define ZEROPOINT_PPU_H

#include <cstdint>
#include <array>

namespace ZeroPoint {

// Forward declaration
class Display;

// PPU operates at 64 MHz (64*1024*1024 Hz)
// Executes 1 instruction per cycle
constexpr uint32_t PPU_CLOCK_HZ = 64 * 1024 * 1024;
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

    // Load microcode into memory
    void loadMicrocode(const uint8_t* code, size_t length, uint16_t offset = 0);

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

    // Register banks for preset F setpos (4-bit register IDs)
    uint8_t regBankX;
    uint8_t regBankY;

    // Cycle counter
    uint32_t cycleCounter;

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

    // Fetch instruction
    uint16_t fetchInstruction();

    // Helper methods
    void updateFlags(uint16_t result, uint16_t left, uint16_t right);
    void clearFlags();

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
    uint32_t blendColors(uint32_t src, uint32_t dst, uint8_t mode, uint8_t alpha);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_PPU_H
