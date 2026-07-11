#ifndef ZEROPOINT_INTERRUPT_CONTROLLER_H
#define ZEROPOINT_INTERRUPT_CONTROLLER_H

#include <cstdint>

namespace ZeroPoint {

/**
 * Interrupt Controller
 *
 * The DEF88186 has a single maskable IRQ line shared by every peripheral.
 * On its own that line tells an ISR nothing about *what* interrupted it.
 * This controller sits between the peripherals and the CPU: every source
 * records a latched request bit, and the ISR reads the STATUS register to
 * discover which source(s) fired, then acknowledges by writing 1s to clear
 * them.
 *
 * Two-level masking (matching real hardware):
 *   - Each peripheral keeps its own local enable (e.g. the display's V-blank
 *     IRQ enable, the timer INT_ENABLE bits).
 *   - This controller's ENABLE register is the top-level mask that decides
 *     whether an enabled source is allowed to assert the CPU line.
 *
 * The pending bits are latched for discrimination but the CPU line itself is
 * edge-driven (one service per raised event), so a source that is never
 * acknowledged records its bit without producing an interrupt storm. If
 * several sources fire while interrupts are masked, their bits accumulate and
 * a single ISR entry can see and handle all of them.
 */
enum class IRQSource : uint8_t {
    VBlank = 1u << 0,  // Display entered vertical blank
    HBlank = 1u << 1,  // Display entered horizontal blank
    Timer  = 1u << 2,  // A hardware timer expired (read TIMER_STATUS for which)
    DMA    = 1u << 3,  // DMA transfer complete (reserved for future use)
};

class InterruptController {
public:
    InterruptController() { reset(); }

    void reset() {
        pending_ = 0;
        enable_  = 0xFF;  // transparent by default: sources gated at the peripheral
    }

    // A source signals an event. The request is latched until acknowledged.
    void raise(IRQSource src) { pending_ |= static_cast<uint8_t>(src); }

    // Acknowledge (clear) pending bits — a 1 clears the corresponding source.
    void acknowledge(uint8_t mask) { pending_ &= static_cast<uint8_t>(~mask); }

    // Raw latched requests — what the ISR reads to discriminate the source.
    uint8_t pending() const { return pending_; }

    // Top-level per-source mask.
    uint8_t enable() const { return enable_; }
    void setEnable(uint8_t mask) { enable_ = mask; }

    // True if this source is allowed to assert the CPU line.
    bool isEnabled(IRQSource src) const {
        return (enable_ & static_cast<uint8_t>(src)) != 0;
    }

private:
    uint8_t pending_;
    uint8_t enable_;
};

} // namespace ZeroPoint

#endif // ZEROPOINT_INTERRUPT_CONTROLLER_H
