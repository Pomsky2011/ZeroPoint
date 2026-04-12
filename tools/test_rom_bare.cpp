#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

// ZPB header (must match rom.h)
#pragma pack(push, 1)
struct ZPBHeader {
    char     magic[4];
    uint8_t  version;
    uint8_t  reserved;
    uint16_t headerSize;
    uint32_t romSize;
    uint32_t entryPoint;
    char     title[32];
    char     developer[16];
};
#pragma pack(pop)
static_assert(sizeof(ZPBHeader) == 64, "ZPBHeader must be 64 bytes");

// ─── Emit helpers ─────────────────────────────────────────────────────────────

static void emit(std::vector<uint8_t>& r, uint8_t b) { r.push_back(b); }

static void emit16(std::vector<uint8_t>& r, uint16_t w) {
    r.push_back(w & 0xFF);
    r.push_back((w >> 8) & 0xFF);
}

static void emit24(std::vector<uint8_t>& r, uint32_t v) {
    r.push_back(v & 0xFF);
    r.push_back((v >> 8) & 0xFF);
    r.push_back((v >> 16) & 0xFF);
}

// ─── Instruction macros ───────────────────────────────────────────────────────
// RGBA16 pixel format (display.cpp): BBBBB GGGGG RRRRR A  (bit 15 = MSB)
//   red   = R=31,G=0,B=0,A=1 → 0x003F
//   black = R=0, G=0,B=0,A=1 → 0x0001   (transparent=0x0000)

// 8-bit mode (M=1, X=1 — CPU default)
#define NOP(r)              emit(r, 0x00)
#define SDB(r, bank)        emit(r, 0x1B); emit(r, bank)
#define LDA8(r, v)          emit(r, 0x37); emit(r, (uint8_t)(v))
#define STA_ABS(r, addr)    emit(r, 0x51); emit16(r, addr)

// Switch to 16-bit A+X: REP #$30 clears M and X flags
// addrImmediate() uses !P.M to decide width, opLDX/opSTA use P.X/P.M
#define REP(r, mask)        emit(r, 0x7F); emit(r, mask)
#define LDA16(r, v)         emit(r, 0x37); emit16(r, (uint16_t)(v))   // M=0 → 2-byte imm
#define LDX16(r, v)         emit(r, 0x40); emit16(r, (uint16_t)(v))   // M=0 → 2-byte imm
#define STA_ABS_X(r, addr)  emit(r, 0x52); emit16(r, addr)            // M=0 → writes word
#define INX(r)              emit(r, 0x6F)
#define LOOP(r, count)      emit(r, 0x13); emit16(r, (uint16_t)(count))
#define LPEND(r)            emit(r, 0x14)
#define JMP_LONG(r, addr24) emit(r, 0x10); emit24(r, addr24)

// Full 8KB framebuffer = $E000-$FFFF = 8192 bytes = 4096 pixels
// In 16-bit mode: STA writes a word per instruction, INX INX advances 2 bytes
// → LOOP 4096 covers the entire framebuffer in one pass

int main(int argc, char* argv[]) {
    const char* outFile = (argc >= 2) ? argv[1] : "test_bare.zpb";

    std::vector<uint8_t> code;

    // ── 1. RAM sanity check ($DEADBEEF → $800000) ────────────────────────────
    SDB(code, 0x80);
    LDA8(code, 0xDE); STA_ABS(code, 0x0000);
    LDA8(code, 0xAD); STA_ABS(code, 0x0001);
    LDA8(code, 0xBE); STA_ABS(code, 0x0002);
    LDA8(code, 0xEF); STA_ABS(code, 0x0003);

    // ── 2. Switch to 16-bit A+X ───────────────────────────────────────────────
    REP(code, 0x30);   // M=0 (16-bit A), X=0 (16-bit X)

    // ── 3. Clear entire framebuffer to black ──────────────────────────────────
    SDB(code, 0xB0);           // DB = PPU window bank
    LDA16(code, 0x0001);       // black pixel (A=1 so it's opaque)
    LDX16(code, 0x0000);       // X = 0
    LOOP(code, 4096);          // 4096 × 2 bytes = 8192 bytes = full 8KB framebuffer
        STA_ABS_X(code, 0xE000);
        INX(code);
        INX(code);
    LPEND(code);

    // ── 4. Main loop: continuously fill framebuffer with red ─────────────────
    uint32_t redLoopAddr = static_cast<uint32_t>(code.size());

    LDA16(code, 0x003F);       // red pixel (R=31, G=0, B=0, A=1)
    LDX16(code, 0x0000);
    LOOP(code, 4096);
        STA_ABS_X(code, 0xE000);
        INX(code);
        INX(code);
    LPEND(code);

    JMP_LONG(code, redLoopAddr);  // keep re-filling forever

    // ── Pack into ZPB ─────────────────────────────────────────────────────────
    ZPBHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, "ZPB", 3);
    header.magic[3]   = '\0';
    header.version    = 1;
    header.headerSize = 64;
    header.romSize    = static_cast<uint32_t>(code.size());
    header.entryPoint = 0x000000;
    std::strncpy(header.title,     "Bare Metal Test", 32);
    std::strncpy(header.developer, "ZeroPoint", 16);

    std::ofstream f(outFile, std::ios::binary);
    if (!f) { std::cerr << "Failed to open: " << outFile << "\n"; return 1; }
    f.write(reinterpret_cast<const char*>(&header), sizeof(header));
    f.write(reinterpret_cast<const char*>(code.data()), code.size());
    f.close();

    std::cout << "Bare metal test ROM: " << outFile << "\n";
    std::cout << "  Entry:      $000000\n";
    std::cout << "  Code size:  " << code.size() << " bytes\n";
    std::cout << "  Red loop:   $" << std::hex << redLoopAddr << "\n\n";
    std::cout << "Sequence:\n";
    std::cout << "  1. Write $DEADBEEF to RAM $800000\n";
    std::cout << "  2. Switch to 16-bit A+X mode\n";
    std::cout << "  3. Clear full 8KB framebuffer to black (one pass)\n";
    std::cout << "  4. Loop: fill full framebuffer with red continuously\n";

    return 0;
}
