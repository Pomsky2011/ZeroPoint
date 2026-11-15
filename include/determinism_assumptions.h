#pragma once
// The platform facts the machine's determinism rests on (docs/determinism-proof.md, premise P2). C++17 leaves each of
// these implementation-defined; the emulator's arithmetic is written assuming them, so a host that differs must fail to
// compile rather than quietly run a different machine.
#include <climits>
#include <cstdint>

static_assert(CHAR_BIT == 8, "8-bit bytes");
static_assert(sizeof(short) == 2 && sizeof(int) == 4 && sizeof(long long) == 8, "short/int/long long are 16/32/64 bits");

static_assert((-1 >> 1) == -1, "arithmetic right shift of negative ints");
static_assert(static_cast<int8_t>(static_cast<uint8_t>(200)) == -56, "two's complement unsigned->signed conversion");
static_assert(static_cast<int16_t>(static_cast<uint16_t>(0xFFFF)) == -1, "two's complement unsigned->signed conversion");
static_assert(static_cast<int32_t>(static_cast<uint32_t>(0x80000000u)) == INT32_MIN, "two's complement unsigned->signed conversion");
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "little-endian host (ROM headers are read as raw structs)");
#endif
