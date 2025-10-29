#include "ppu_jit.h"
#include "ppu.h"
#include <sys/mman.h>
#include <cstring>
#include <iostream>

namespace ZeroPoint {

// Helper function for JIT to call PPU::tick()
extern "C" void ppu_jit_tick_wrapper(PPU* ppu) {
    ppu->tick();
}

PPUJIT::PPUJIT() {
}

PPUJIT::~PPUJIT() {
    invalidateAll();
}

bool PPUJIT::isSupported() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

const char* PPUJIT::getArchitecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86-64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#else
    return "unknown";
#endif
}

void* PPUJIT::allocateExecutable(size_t size) {
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return nullptr;
    }
    return ptr;
}

void PPUJIT::freeExecutable(void* ptr, size_t size) {
    if (ptr) {
        munmap(ptr, size);
    }
}

void PPUJIT::emit(std::vector<uint8_t>& code, uint8_t byte) {
    code.push_back(byte);
}

void PPUJIT::emit16(std::vector<uint8_t>& code, uint16_t word) {
    code.push_back(word & 0xFF);
    code.push_back((word >> 8) & 0xFF);
}

void PPUJIT::emit32(std::vector<uint8_t>& code, uint32_t dword) {
    code.push_back(dword & 0xFF);
    code.push_back((dword >> 8) & 0xFF);
    code.push_back((dword >> 16) & 0xFF);
    code.push_back((dword >> 24) & 0xFF);
}

void PPUJIT::emit64(std::vector<uint8_t>& code, uint64_t qword) {
    emit32(code, qword & 0xFFFFFFFF);
    emit32(code, (qword >> 32) & 0xFFFFFFFF);
}

#if defined(__x86_64__) || defined(_M_X64)

void PPUJIT::emitX86_64(std::vector<uint8_t>& code, PPU* ppu, uint16_t startAddr, size_t maxInstructions) {
    // Very simple JIT: just execute tick() in a tight loop
    // This removes all the debug overhead from run_demo.cpp

    // Function signature: void jit_execute(PPU* ppu, uint32_t cycles)
    // Arguments: rdi = ppu, rsi = cycles

    // Prologue - ensure 16-byte stack alignment
    emit(code, 0x55);                    // push rbp
    emit(code, 0x48); emit(code, 0x89); emit(code, 0xE5);  // mov rbp, rsp
    emit(code, 0x41); emit(code, 0x57);  // push r15
    emit(code, 0x41); emit(code, 0x56);  // push r14
    emit(code, 0x53);                    // push rbx
    emit(code, 0x48); emit(code, 0x83); emit(code, 0xEC); emit(code, 0x08);  // sub rsp, 8 (align to 16 bytes)

    emit(code, 0x48); emit(code, 0x89); emit(code, 0xFB);  // mov rbx, rdi (save ppu*)
    emit(code, 0x89); emit(code, 0xF1);  // mov ecx, esi (save cycles)

    // Loop: for (uint32_t i = 0; i < cycles; i++) { ppu->tick(); }
    emit(code, 0x45); emit(code, 0x31); emit(code, 0xFF);  // xor r15d, r15d (i = 0)

    // loop_start:
    size_t loop_start = code.size();

    // if (i >= cycles) goto done
    emit(code, 0x44); emit(code, 0x39); emit(code, 0xF9);  // cmp ecx, r15d
    emit(code, 0x76); emit(code, 0x10);  // jbe done (skip ahead)

    // ppu->tick()
    emit(code, 0x48); emit(code, 0x89); emit(code, 0xDF);  // mov rdi, rbx (ppu*)

    // Call PPU::tick() via wrapper function
    emit(code, 0x48); emit(code, 0xB8);  // movabs rax, <address>
    uint64_t tick_addr = (uint64_t)(&ppu_jit_tick_wrapper);
    emit64(code, tick_addr);
    emit(code, 0xFF); emit(code, 0xD0);  // call rax

    // i++
    emit(code, 0x41); emit(code, 0xFF); emit(code, 0xC7);  // inc r15d

    // goto loop_start
    int32_t offset = loop_start - (code.size() + 2);
    emit(code, 0xEB); emit(code, (uint8_t)(offset & 0xFF));  // jmp rel8

    // done:
    // Epilogue
    emit(code, 0x48); emit(code, 0x83); emit(code, 0xC4); emit(code, 0x08);  // add rsp, 8
    emit(code, 0x5B);                    // pop rbx
    emit(code, 0x41); emit(code, 0x5E);  // pop r14
    emit(code, 0x41); emit(code, 0x5F);  // pop r15
    emit(code, 0x5D);                    // pop rbp
    emit(code, 0xC3);                    // ret
}

#elif defined(__aarch64__) || defined(_M_ARM64)

void PPUJIT::emitARM64(std::vector<uint8_t>& code, PPU* ppu, uint16_t startAddr, size_t maxInstructions) {
    // Very simple JIT: just execute tick() in a tight loop
    // Function signature: void jit_execute(PPU* ppu, uint32_t cycles)
    // Arguments: x0 = ppu, x1 = cycles

    // Prologue - save frame pointer and link register
    // stp x29, x30, [sp, #-32]!
    emit32(code, 0xa9be7bfd);

    // mov x29, sp
    emit32(code, 0x910003fd);

    // str x19, [sp, #16]  (save x19)
    emit32(code, 0xf90013f3);

    // mov x19, x0  (save ppu* in x19)
    emit32(code, 0xaa0003f3);

    // mov w20, w1  (save cycles in w20)
    emit32(code, 0x2a0103f4);

    // mov w21, #0  (i = 0)
    emit32(code, 0x52800015);

    // loop_start:
    size_t loop_start = code.size();

    // cmp w21, w20  (compare i with cycles)
    emit32(code, 0x6b14029f);

    // b.ge done (branch if i >= cycles)
    emit32(code, 0x54000068);  // offset will be fixed

    // mov x0, x19  (load ppu*)
    emit32(code, 0xaa1303e0);

    // Call PPU::tick() via wrapper function
    uint64_t tick_addr = (uint64_t)(&ppu_jit_tick_wrapper);
    // movz x9, #<low 16 bits>
    emit32(code, 0xd2800009 | ((tick_addr & 0xFFFF) << 5));
    // movk x9, #<bits 16-31>, lsl #16
    emit32(code, 0xf2a00009 | (((tick_addr >> 16) & 0xFFFF) << 5));
    // movk x9, #<bits 32-47>, lsl #32
    emit32(code, 0xf2c00009 | (((tick_addr >> 32) & 0xFFFF) << 5));
    // movk x9, #<bits 48-63>, lsl #48
    emit32(code, 0xf2e00009 | (((tick_addr >> 48) & 0xFFFF) << 5));
    // blr x9
    emit32(code, 0xd63f0120);

    // add w21, w21, #1  (i++)
    emit32(code, 0x110006b5);

    // b loop_start
    int32_t offset = (loop_start - code.size()) / 4;
    emit32(code, 0x14000000 | (offset & 0x3FFFFFF));

    // done:
    // ldr x19, [sp, #16]
    emit32(code, 0xf94013f3);

    // ldp x29, x30, [sp], #32
    emit32(code, 0xa8c27bfd);

    // ret
    emit32(code, 0xd65f03c0);
}

#endif

JITBlock* PPUJIT::compileBlock(PPU* ppu, uint16_t startAddr, size_t maxInstructions) {
    if (!isSupported()) {
        std::cerr << "JIT: Platform not supported\n";
        return nullptr;
    }

    std::vector<uint8_t> code;

#if defined(__x86_64__) || defined(_M_X64)
    std::cerr << "JIT: Emitting x86-64 code...\n";
    emitX86_64(code, ppu, startAddr, maxInstructions);
#elif defined(__aarch64__) || defined(_M_ARM64)
    std::cerr << "JIT: Emitting ARM64 code...\n";
    emitARM64(code, ppu, startAddr, maxInstructions);
#else
    std::cerr << "JIT: Unknown architecture\n";
    return nullptr;
#endif

    std::cerr << "JIT: Generated " << code.size() << " bytes of code\n";

    // Allocate executable memory
    void* execMem = allocateExecutable(code.size());
    if (!execMem) {
        std::cerr << "JIT: Failed to allocate executable memory\n";
        return nullptr;
    }

    std::cerr << "JIT: Allocated executable memory at " << execMem << "\n";

    // Copy code to executable memory
    memcpy(execMem, code.data(), code.size());

    // Make it executable
    if (mprotect(execMem, code.size(), PROT_READ | PROT_EXEC) != 0) {
        std::cerr << "JIT: Failed to set memory protection\n";
        freeExecutable(execMem, code.size());
        return nullptr;
    }

    std::cerr << "JIT: Memory protection set to RX\n";

    // Create block
    JITBlock* block = new JITBlock();
    block->code = execMem;
    block->codeSize = code.size();
    block->startAddr = startAddr;
    block->endAddr = startAddr + maxInstructions;
    block->valid = true;

    std::cerr << "JIT: Block compiled successfully\n";

    return block;
}

void PPUJIT::execute(JITBlock* block, PPU* ppu) {
    if (!block || !block->valid) {
        std::cerr << "JIT: Invalid block\n";
        return;
    }

    static bool first_call = true;
    if (first_call) {
        std::cerr << "JIT: First execute call, block at " << block->code << "\n";
        first_call = false;
    }

    // Call the JIT-compiled function
    // Signature: void jit_execute(PPU* ppu, uint32_t cycles)
    typedef void (*JITFunc)(PPU*, uint32_t);
    JITFunc func = (JITFunc)block->code;
    func(ppu, 1000);  // Execute 1000 PPU ticks per JIT call
}

void PPUJIT::invalidateAll() {
    for (auto& pair : blocks) {
        if (pair.second) {
            freeExecutable(pair.second->code, pair.second->codeSize);
            delete pair.second;
        }
    }
    blocks.clear();
}

JITBlock* PPUJIT::getBlock(PPU* ppu, uint16_t addr) {
    auto it = blocks.find(addr);
    if (it != blocks.end() && it->second->valid) {
        return it->second;
    }

    // Compile new block
    JITBlock* block = compileBlock(ppu, addr, 1000);
    if (block) {
        blocks[addr] = block;
    }
    return block;
}

} // namespace ZeroPoint
