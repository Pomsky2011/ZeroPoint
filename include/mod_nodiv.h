#ifndef ZEROPOINT_MOD_NODIV_H
#define ZEROPOINT_MOD_NODIV_H

#include <cstdint>

namespace ZeroPoint {

// x % d with no divide instruction, exact for every uint32 x and d (a divisor of 0, which traps in hardware, returns x).
// The cases that happen are tried first - positions and ring indices advance by small steps, so x is under d or under
// 2d - and anything larger falls back to shift-subtract long division.
inline uint32_t modU32(uint32_t x, uint32_t d) {
    if (x < d) return x;
    if (x - d < d) return x - d;
    if (d == 0) return x;
    uint32_t r = x, dd = d;
    unsigned shift = 0;
    while (dd < (1u << 31) && (dd << 1) <= r) { dd <<= 1; shift++; }   // dd = d << shift, the largest one not above r
    for (;;) {
        if (r >= dd) r -= dd;
        if (shift == 0) break;
        dd >>= 1;
        shift--;
    }
    return r;
}

} // namespace ZeroPoint

#endif // ZEROPOINT_MOD_NODIV_H
