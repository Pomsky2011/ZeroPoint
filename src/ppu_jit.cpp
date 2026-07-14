#include "ppu_jit.h"
#include "ppu.h"
#include <cstring>
#include <iostream>

// Platform-specific includes
#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

// Handle MAP_ANONYMOUS vs MAP_ANON differences across platforms
#if !defined(_WIN32)
    #if defined(MAP_ANONYMOUS)
        #define ZEROPOINT_MAP_ANON MAP_ANONYMOUS
    #elif defined(MAP_ANON)
        #define ZEROPOINT_MAP_ANON MAP_ANON
    #else
        #error "Neither MAP_ANONYMOUS nor MAP_ANON is defined"
    #endif
#endif

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
#if defined(_WIN32)
    // Windows: use VirtualAlloc
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return ptr;
#else
    // POSIX: use mmap
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | ZEROPOINT_MAP_ANON, -1, 0);
    if (ptr == MAP_FAILED) {
        return nullptr;
    }
    return ptr;
#endif
}

void PPUJIT::freeExecutable(void* ptr, size_t size) {
    if (ptr) {
#if defined(_WIN32)
        VirtualFree(ptr, 0, MEM_RELEASE);
#else
        munmap(ptr, size);
#endif
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
    (void)ppu; (void)startAddr; (void)maxInstructions;
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
    // Keep the cycle count in r14d (callee-saved). It MUST survive the call to
    // ppu->tick() each iteration; the old code used ecx, which is caller-saved
    // and gets clobbered by the call, so the loop exited after a single tick.
    emit(code, 0x41); emit(code, 0x89); emit(code, 0xF6);  // mov r14d, esi (save cycles)

    // Loop: for (uint32_t i = 0; i < cycles; i++) { ppu->tick(); }
    emit(code, 0x45); emit(code, 0x31); emit(code, 0xFF);  // xor r15d, r15d (i = 0)

    // loop_start:
    size_t loop_start = code.size();

    // if (i >= cycles) goto done
    emit(code, 0x45); emit(code, 0x39); emit(code, 0xFE);  // cmp r14d, r15d
    // Placeholder for jbe - we'll fix this after we know the offset
    size_t jbe_location = code.size();
    emit(code, 0x76); emit(code, 0x00);  // jbe done (offset will be patched)

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
    size_t done_location = code.size();

    // Patch the jbe offset now that we know where 'done' is
    int8_t jbe_offset = done_location - (jbe_location + 2);  // +2 for the jbe instruction size
    code[jbe_location + 1] = jbe_offset;

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

    // Prologue - save frame pointer, link register, and callee-saved registers
    // stp x29, x30, [sp, #-48]!  (save FP and LR, allocate 48 bytes)
    emit32(code, 0xa9bd7bfd);

    // mov x29, sp
    emit32(code, 0x910003fd);

    // stp x19, x20, [sp, #16]  (save x19, x20)
    emit32(code, 0xa90153f3);

    // str x21, [sp, #32]  (save x21)
    emit32(code, 0xf90023f5);

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
    // Placeholder - we'll patch this after we know where 'done' is
    size_t bge_location = code.size();
    emit32(code, 0x5400000a);  // b.ge with offset 0 (condition code 0xA = ge)

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
    size_t done_location = code.size();

    // Patch the b.ge offset now that we know where 'done' is
    // ARM64 conditional branch offset is signed, in units of 4 bytes, stored in bits 5-23
    int32_t bge_offset = (done_location - bge_location) / 4;
    uint32_t bge_instruction = 0x5400000a | ((bge_offset & 0x7FFFF) << 5);
    code[bge_location] = bge_instruction & 0xFF;
    code[bge_location + 1] = (bge_instruction >> 8) & 0xFF;
    code[bge_location + 2] = (bge_instruction >> 16) & 0xFF;
    code[bge_location + 3] = (bge_instruction >> 24) & 0xFF;

    // Epilogue - restore callee-saved registers
    // ldr x21, [sp, #32]  (restore x21)
    emit32(code, 0xf94023f5);

    // ldp x19, x20, [sp, #16]  (restore x19, x20)
    emit32(code, 0xa94153f3);

    // ldp x29, x30, [sp], #48  (restore FP and LR, deallocate 48 bytes)
    emit32(code, 0xa8c37bfd);

    // ret
    emit32(code, 0xd65f03c0);
}

#endif

JITBlock* PPUJIT::compileBlock(PPU* ppu, uint16_t startAddr, size_t maxInstructions) {
    if (!isSupported()) {
        return nullptr;
    }

    std::vector<uint8_t> code;

#if defined(__x86_64__) || defined(_M_X64)
    emitX86_64(code, ppu, startAddr, maxInstructions);
#elif defined(__aarch64__) || defined(_M_ARM64)
    emitARM64(code, ppu, startAddr, maxInstructions);
#else
    return nullptr;
#endif

    // Allocate executable memory
    void* execMem = allocateExecutable(code.size());
    if (!execMem) {
        return nullptr;
    }

    // Copy code to executable memory
    memcpy(execMem, code.data(), code.size());

    // Flush instruction cache on ARM64 (required for self-modifying code)
#if defined(__aarch64__) || defined(_M_ARM64)
    #if defined(_WIN32)
        // MSVC has no __builtin___clear_cache; FlushInstructionCache is the
        // portable Win32 equivalent (works on ARM64, a no-op-safe call on x64).
        FlushInstructionCache(GetCurrentProcess(), execMem, code.size());
    #else
        // Compiler builtin to flush instruction cache (GCC/Clang)
        __builtin___clear_cache((char*)execMem, (char*)execMem + code.size());
    #endif
#endif

    // Make it executable
#if defined(_WIN32)
    DWORD oldProtect;
    if (!VirtualProtect(execMem, code.size(), PAGE_EXECUTE_READ, &oldProtect)) {
        freeExecutable(execMem, code.size());
        return nullptr;
    }
#else
    if (mprotect(execMem, code.size(), PROT_READ | PROT_EXEC) != 0) {
        freeExecutable(execMem, code.size());
        return nullptr;
    }
#endif

    // Create block
    JITBlock* block = new JITBlock();
    block->code = execMem;
    block->codeSize = code.size();
    block->startAddr = startAddr;
    block->endAddr = startAddr + maxInstructions;
    block->valid = true;

    return block;
}

void PPUJIT::execute(JITBlock* block, PPU* ppu) {
    if (!block || !block->valid) {
        return;
    }

    // The "JIT" is a batched-interpreter executor: it drives PPU::runBatch,
    // which runs whole instructions and collapses multi-cycle stalls (1.2x-4x
    // faster than ticking cycle by cycle) and is provably state-identical to it.
    // It is NOT a native compiler -- the emitX86_64/emitARM64 path only ever
    // emitted a native loop around tick(), which runBatch now supersedes. A real
    // per-opcode code generator was scoped but deliberately not built: every
    // heavy opcode (TILEDRAW, palette loads) is a C++ helper a JIT would just
    // call, so the batched path already captures nearly all the achievable gain.
    (void)block;
    ppu->runBatch(1000);  // 1000 PPU cycles per call
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

} // namespace ZeroPoint
