#ifndef ZEROPOINT_PPU_JIT_H
#define ZEROPOINT_PPU_JIT_H

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace ZeroPoint {

// Forward declaration
class PPU;

// JIT-compiled code block
struct JITBlock {
    void* code;           // Executable code pointer
    size_t codeSize;      // Size of compiled code
    uint16_t startAddr;   // PPU address where this block starts
    uint16_t endAddr;     // PPU address where this block ends
    bool valid;           // Whether this block is still valid
};

class PPUJIT {
public:
    PPUJIT();
    ~PPUJIT();

    // Compile a block of PPU code starting at address
    JITBlock* compileBlock(PPU* ppu, uint16_t startAddr, size_t maxInstructions = 1000);

    // Execute compiled block
    void execute(JITBlock* block, PPU* ppu);

    // Invalidate all compiled blocks
    void invalidateAll();

    // Get or compile block at address
    JITBlock* getBlock(PPU* ppu, uint16_t addr);

    // Check if JIT is supported on this platform
    static bool isSupported();

    // Get architecture name
    static const char* getArchitecture();

private:
    std::unordered_map<uint16_t, JITBlock*> blocks;

    // Allocate executable memory
    void* allocateExecutable(size_t size);

    // Free executable memory
    void freeExecutable(void* ptr, size_t size);

    // Code generation for different architectures
#if defined(__x86_64__) || defined(_M_X64)
    void emitX86_64(std::vector<uint8_t>& code, PPU* ppu, uint16_t startAddr, size_t maxInstructions);
#elif defined(__aarch64__) || defined(_M_ARM64)
    void emitARM64(std::vector<uint8_t>& code, PPU* ppu, uint16_t startAddr, size_t maxInstructions);
#endif

    // Helper: Emit instruction bytes
    void emit(std::vector<uint8_t>& code, uint8_t byte);
    void emit16(std::vector<uint8_t>& code, uint16_t word);
    void emit32(std::vector<uint8_t>& code, uint32_t dword);
    void emit64(std::vector<uint8_t>& code, uint64_t qword);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_PPU_JIT_H
