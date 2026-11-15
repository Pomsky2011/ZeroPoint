#pragma once
// Native (high-level-emulated) stand-ins for what the real ZPbootROM does: the COP #$F0-#$F6 math
// services, and the signature check that gates boot. Pure functions - no CPU/System state in here;
// System wires them to COP dispatch and to reset() (src/system.cpp, src/boot_hle/).
//
// Bit-exact with ZPbootROM/def88186/trig_model.py (the reference the real services are tested
// against) and with ZPdevtools/src/zpbuild.c's signing scheme. tools/test_hle_cop.cpp runs the same
// vectors through the real boot ROM and through these and diffs them. See docs/boot-hle.md.
#include <cstddef>
#include <cstdint>

namespace ZeroPoint {
namespace BootHLE {

// ---- COP #$F0-#$F6 ---------------------------------------------------------------------------
// Angles are unsigned binary angles (65536 = one turn, 0 = +x, $4000 = +y); sin/cos are Q1.15.
uint16_t sinQ15(uint16_t angle);
uint16_t cosQ15(uint16_t angle);
uint16_t atan2Angle(uint16_t y, uint16_t x);   // y, x are two's-complement words
uint16_t atanQ88(uint16_t t);                  // t is signed Q8.8; result is a signed angle in [-$4000, +$4000]
uint16_t sqrt32(uint16_t lo, uint16_t hi);     // floor(sqrt(hi:lo))
uint16_t hypot16(uint16_t x, uint16_t y);      // floor(sqrt(x*x + y*y)), x/y signed

// ---- signature verification --------------------------------------------------------------------
// Everything the real boot ROM sees at bank $E2 (the cached 64-byte ZPB header plus the "ZPSG"
// trailer) and bank $00 (the payload), laid out as zpb-format.md describes.
struct SignedROMView {
    const uint8_t* header = nullptr;        // 64 bytes, nullptr for an unsigned ROM
    const uint8_t* trailer = nullptr;       // "ZPSG"...
    size_t trailerSize = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
};

enum class VerifyResult { Ok, Unsigned, Malformed, BadSignature };
const char* describe(VerifyResult r);

// Trailer v1/v2/v3. Recomputes the digest from the payload (never trusts the one stored in the
// trailer) and checks the RSA-2048 PKCS#1 v1.5 signature against the developer key.
VerifyResult verifyROM(const SignedROMView& rom);

// COP #$FF: does payload chunk `index` (16 KiB, counted from codeSize) match manifest entry `index`
// of a v3 trailer? Pure check; the caller marks the chunk verified on true.
bool chunkMatchesManifest(const SignedROMView& rom, uint32_t codeSize, uint32_t index);

}  // namespace BootHLE
}  // namespace ZeroPoint
