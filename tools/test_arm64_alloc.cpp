// Minimal test to isolate ARM64 JIT memory allocation issue
#include <iostream>
#include <cstring>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

#ifndef MAP_ANONYMOUS
    #ifdef MAP_ANON
        #define MAP_ANONYMOUS MAP_ANON
    #endif
#endif

void* allocateExecutable(size_t size) {
#if defined(_WIN32)
    void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return ptr;
#else
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return nullptr;
    }
    return ptr;
#endif
}

void freeExecutable(void* ptr, size_t size) {
    if (ptr) {
#if defined(_WIN32)
        VirtualFree(ptr, 0, MEM_RELEASE);
#else
        munmap(ptr, size);
#endif
    }
}

int main() {
    std::cout << "=== ARM64 Memory Allocation Test ===\n\n";

    const size_t size = 84;  // Same size as the JIT code
    std::cout << "Allocating " << size << " bytes of executable memory...\n";

    void* ptr = allocateExecutable(size);
    if (!ptr) {
        std::cout << "Allocation failed!\n";
        return 1;
    }

    std::cout << "Allocated at: " << ptr << "\n";

    // Write some dummy data
    memset(ptr, 0x90, size);  // NOP instructions
    std::cout << "Wrote dummy data\n";

    // Make it executable
#if defined(_WIN32)
    DWORD oldProtect;
    if (!VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &oldProtect)) {
        std::cout << "VirtualProtect failed!\n";
        freeExecutable(ptr, size);
        return 1;
    }
#else
    if (mprotect(ptr, size, PROT_READ | PROT_EXEC) != 0) {
        std::cout << "mprotect failed!\n";
        freeExecutable(ptr, size);
        return 1;
    }
#endif

    std::cout << "Made executable\n";

    // Flush cache on ARM64
#if defined(__aarch64__) || defined(_M_ARM64)
    std::cout << "Flushing instruction cache...\n";
    __builtin___clear_cache((char*)ptr, (char*)ptr + size);
    std::cout << "Cache flushed\n";
#endif

    std::cout << "About to free memory...\n";
    freeExecutable(ptr, size);
    std::cout << "Memory freed successfully\n";

    std::cout << "\n=== Test Complete ===\n";
    return 0;
}
