#include "boot_hle.h"

#include <cmath>
#include <cstring>

// Vendored from ZPdevtools (zpsha256.h, zpblake2s.h, zprsa.h minus signing, zpkey.h minus the
// private exponent): header-only C89 with static functions, so they stay private to this file.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "zpsha256.h"
#include "zpblake2s.h"
#include "zprsa_verify.h"
#pragma GCC diagnostic pop

namespace ZeroPoint {
namespace BootHLE {

namespace {

constexpr uint32_t CHUNK_SIZE = 16384;     // same as CPU::CHUNK_SIZE / zpbuild / rsa.def
constexpr size_t HEADER_SIZE = 64;

// ---- trig tables, same formula as trig_model.py (Python's round() is round-half-even) --------
struct Tables {
    int32_t sinTab[258];
    int32_t atanTab[258];
    Tables() {
        const double pi = 3.14159265358979323846;
        for (int i = 0; i <= 256; i++) {
            sinTab[i] = (int32_t)std::nearbyint(32767.0 * std::sin(i * pi / 512.0));
            atanTab[i] = (int32_t)std::nearbyint(std::atan(i / 256.0) * 65536.0 / (2 * pi));
        }
        sinTab[257] = sinTab[256];
        atanTab[257] = atanTab[256];
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}

int32_t s16(uint16_t v) { return (int16_t)v; }

uint64_t isqrt64(uint64_t n) {
    uint64_t r = (uint64_t)std::sqrt((double)n);
    while (r * r > n) r--;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

}  // namespace

uint16_t sinQ15(uint16_t angle) {
    const Tables& t = tables();
    uint32_t q = angle >> 14, x = angle & 0x3FFF;
    if (q & 1) x = 0x4000 - x;
    uint32_t i = x >> 6, f = x & 63;
    int32_t a = t.sinTab[i], b = t.sinTab[i + 1];
    int32_t v = a + (((b - a) * (int32_t)f + 32) >> 6);
    return (uint16_t)(q >= 2 ? -v : v);
}

uint16_t cosQ15(uint16_t angle) { return sinQ15((uint16_t)(angle + 0x4000)); }

uint16_t atan2Angle(uint16_t yw, uint16_t xw) {
    const Tables& t = tables();
    int32_t y = s16(yw), x = s16(xw);
    if (x == 0 && y == 0) return 0;
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    bool swap = ay > ax;
    int32_t mn = swap ? ax : ay, mx = swap ? ay : ax;
    int32_t th;
    if (mn == mx) {
        th = 0x2000;
    } else {
        uint32_t r = ((uint32_t)mn << 16) / (uint32_t)mx;   // exact floor, like the ROM's restoring divide
        uint32_t i = r >> 8, f = r & 255;
        int32_t a = t.atanTab[i], b = t.atanTab[i + 1];
        th = a + (((b - a) * (int32_t)f + 128) >> 8);
    }
    if (swap) th = 0x4000 - th;
    if (x < 0) th = 0x8000 - th;
    if (y < 0) th = -th;
    return (uint16_t)th;
}

uint16_t atanQ88(uint16_t tv) { return atan2Angle(tv, 0x0100); }

uint16_t sqrt32(uint16_t lo, uint16_t hi) {
    return (uint16_t)isqrt64(((uint64_t)hi << 16) | lo);
}

uint16_t hypot16(uint16_t xw, uint16_t yw) {
    int64_t x = s16(xw), y = s16(yw);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    return (uint16_t)isqrt64((uint64_t)(x * x + y * y));
}

const char* describe(VerifyResult r) {
    switch (r) {
        case VerifyResult::Ok: return "signature valid";
        case VerifyResult::Unsigned: return "ROM is not signed";
        case VerifyResult::Malformed: return "signed ROM's header/trailer is malformed";
        case VerifyResult::BadSignature: return "signature does not match the ROM";
    }
    return "?";
}

// Trailer layout (zpb-format.md): +0 "ZPSG", +4 trailer version, +5 key words, +6 siglen (u16),
// +8 stored digest (ignored - recomputed below), +40 signature, +296 codeSize (v2/v3), +300 manifest (v3).
VerifyResult verifyROM(const SignedROMView& rom) {
    if (!rom.header || !rom.trailer) return VerifyResult::Unsigned;
    const uint8_t* tr = rom.trailer;
    if (rom.trailerSize < 296 || std::memcmp(tr, "ZPSG", 4) != 0) return VerifyResult::Malformed;
    uint8_t ver = tr[4];
    if (tr[5] != 32 || (tr[6] | (tr[7] << 8)) != ZP_RSA_BYTES) return VerifyResult::Malformed;
    // The header's own size field must describe what we actually hold; it's covered by the digest, but
    // a mismatch means the cartridge isn't the image that was signed.
    if (rd32(rom.header + 8) != rom.payloadSize) return VerifyResult::Malformed;

    unsigned char digest[32];
    zp_sha256 sh;
    if (ver == 1) {
        zp_sha256_init(&sh);
        zp_sha256_update(&sh, rom.header, HEADER_SIZE);
        zp_sha256_update(&sh, rom.payload, (unsigned long)rom.payloadSize);
        zp_sha256_final(&sh, digest);
    } else if (ver == 2 || ver == 3) {
        if (rom.trailerSize < 300) return VerifyResult::Malformed;
        uint32_t codeSize = rd32(tr + 296);
        if (codeSize > rom.payloadSize) return VerifyResult::Malformed;
        size_t dataSize = rom.payloadSize - codeSize;
        unsigned char codeDigest[32], dataDigest[32];
        zp_blake2s_buf(rom.payload, codeSize, codeDigest);
        if (ver == 2) {
            zp_blake2s_buf(rom.payload + codeSize, (unsigned long)dataSize, dataDigest);
        } else {
            size_t chunks = (dataSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
            if (rom.trailerSize < 300 + chunks * 32) return VerifyResult::Malformed;
            // manifest_digest = BLAKE2s(manifest); the manifest itself is trusted only through this
            // digest, so each entry must be recomputable - but boot doesn't hash the data (COP #$FF
            // does, lazily), it just authenticates the manifest the trailer carries.
            zp_blake2s_buf(tr + 300, (unsigned long)(chunks * 32), dataDigest);
        }
        zp_sha256_init(&sh);
        zp_sha256_update(&sh, rom.header, HEADER_SIZE);
        zp_sha256_update(&sh, codeDigest, 32);
        zp_sha256_update(&sh, dataDigest, 32);
        zp_sha256_final(&sh, digest);
    } else {
        return VerifyResult::Malformed;
    }
    return zp_rsa_verify(digest, tr + 40) ? VerifyResult::Ok : VerifyResult::BadSignature;
}

bool chunkMatchesManifest(const SignedROMView& rom, uint32_t codeSize, uint32_t index) {
    if (!rom.trailer || rom.trailerSize < 300 || rom.trailer[4] != 3) return false;
    if (codeSize > rom.payloadSize) return false;
    size_t dataSize = rom.payloadSize - codeSize;
    size_t chunks = (dataSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    if (index >= chunks || rom.trailerSize < 300 + chunks * 32) return false;
    size_t off = (size_t)index * CHUNK_SIZE;
    size_t len = dataSize - off;
    if (len > CHUNK_SIZE) len = CHUNK_SIZE;
    unsigned char h[32];
    zp_blake2s_buf(rom.payload + codeSize + off, (unsigned long)len, h);
    return std::memcmp(h, rom.trailer + 300 + (size_t)index * 32, 32) == 0;
}

}  // namespace BootHLE
}  // namespace ZeroPoint
