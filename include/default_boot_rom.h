#ifndef ZEROPOINT_DEFAULT_BOOT_ROM_H
#define ZEROPOINT_DEFAULT_BOOT_ROM_H

#include <cstddef>
#include <cstdint>

namespace ZeroPoint {

// Assembled from examples/boot-rom/boot.asm (see that file for the source
// and for how to regenerate this array with cpuasm).
extern const uint8_t kDefaultBootROM[];
extern const size_t kDefaultBootROMSize;

} // namespace ZeroPoint

#endif // ZEROPOINT_DEFAULT_BOOT_ROM_H
