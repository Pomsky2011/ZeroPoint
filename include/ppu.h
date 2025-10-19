#ifndef ZEROPOINT_PPU_H
#define ZEROPOINT_PPU_H

#include <cstdint>
#include <array>

namespace ZeroPoint {

// PPU operates at 64 MHz (64*1024*1024 Hz)
// Executes 1 instruction per cycle
constexpr uint32_t PPU_CLOCK_HZ = 64 * 1024 * 1024;
constexpr uint32_t PPU_CYCLES_PER_INSTRUCTION = 1;

// PPU opcodes (4-bit)
enum class PPUOpcode : uint8_t {
    DEFCALL    = 0x0,  // Define callable function
    ENDDEFCALL = 0x1,  // End function definition
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
    HALT       = 0xE,  // Halt execution
    PRESET     = 0xF   // Extended preset instructions
};

// Preset F sub-opcodes
enum class PresetFOpcode : uint8_t {
    SETPOS           = 0x0,  // Set position
    SETTILE          = 0x1,  // Set tile
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
    STRTDEFTILE      = 0xC,  // Start define tile
    ENDDEFTILE       = 0xD,  // End define tile
    CALL             = 0xE,  // Call function
    GBLS             = 0xF   // Get blank status
};

// Comparison flags
struct PPUFlags {
    bool zero;     // Result was zero
    bool greater;  // Left operand > right operand

    PPUFlags() : zero(false), greater(false) {}
};

// Render mode
enum class RenderMode {
    RGBA16,  // 16-bit RGBA
    RGBA32   // 32-bit RGBA
};

// PPU state
enum class PPUState {
    WaitingForInterrupts,  // Waiting for VBlank/HBlank interrupt addresses
    RunningBootROM,        // Displaying logo
    WaitingForStart,       // Stalled until CPU sends start interrupt
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

    // Set interrupt jump addresses (called during init)
    void setVBlankInterrupt(uint16_t address);
    void setHBlankInterrupt(uint16_t address);

    // Start execution (after boot ROM)
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

    // Get register value (for debugging)
    uint16_t getRegister(uint8_t reg) const {
        if (reg < 64) return registers[reg];
        return 0;
    }

    // Direct memory access (for debugging)
    uint8_t readMemory(uint16_t address) const {
        return memory[address];
    }

private:
    // 64 registers, 16-bit each
    std::array<uint16_t, 64> registers;

    // Special register indices
    static constexpr uint8_t REG_DP = 63;  // Data Pointer
    static constexpr uint8_t REG_PC = 62;  // Program Counter

    // 64 KiB memory (shared with ROM)
    std::array<uint8_t, 65536> memory;

    // Execution pointer (actual instruction pointer)
    uint16_t executionPointer;

    // Comparison flags
    PPUFlags flags;

    // Current state
    PPUState state;

    // Interrupt addresses
    uint16_t vblankInterruptAddr;
    uint16_t hblankInterruptAddr;

    // Blank status
    bool vblank;
    bool hblank;

    // Render mode
    RenderMode renderMode;

    // Register banks for preset F setpos (4-bit register IDs)
    uint8_t regBankX;
    uint8_t regBankY;

    // Cycle counter
    uint32_t cycleCounter;

    // Boot ROM state
    uint8_t initStage;  // 0 = waiting for VBlank addr, 1 = waiting for HBlank addr, 2+ = done

    // Execution methods
    void executeInstruction();
    void executePresetF(uint8_t subopcode, uint8_t operand);

    // Fetch instruction
    uint16_t fetchInstruction();

    // Helper methods
    void updateFlags(uint16_t result, uint16_t left, uint16_t right);
    void clearFlags();
};

} // namespace ZeroPoint

#endif // ZEROPOINT_PPU_H
