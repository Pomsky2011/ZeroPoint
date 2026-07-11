#include "display.h"
#include <cstring>

namespace ZeroPoint {

// Static lookup table for color conversion
uint32_t Display::color16To32_LUT[65536];
bool Display::lutInitialized = false;

void Display::initializeLUT() {
    if (lutInitialized) return;

    // Pre-compute all 65536 RGBA16 -> RGBA32 conversions in the same packing as
    // color16To32() / outputFrame: (R<<24)|(G<<16)|(B<<8)|A.
    for (uint32_t color16 = 0; color16 < 65536; color16++) {
        color16To32_LUT[color16] = color16To32(static_cast<Color16>(color16));
    }

    lutInitialized = true;
}

Display::Display()
    : renderMode(RenderMode::RGBA16)
    , currentScanline(0)
    , currentPixel(0)
    , windowStart(0)
{
    // Initialize lookup table on first Display creation
    initializeLUT();

    // Initialize rolling buffer and latched output frame to black
    framebuffer.fill(0);
    outputFrame.fill(0);
}

Display::~Display() {
}

void Display::tick() {
    // Advance pixel position (hot path optimization)
    currentPixel++;

    if (currentPixel >= TOTAL_PIXELS_PER_LINE) [[unlikely]] {
        // End of scanline (H-Blank). Latch the scanline we just displayed into
        // the full-frame output before the rolling window slides past it.
        if (currentScanline >= 1 && currentScanline <= VISIBLE_SCANLINES) [[likely]] {
            latchScanline(currentScanline - 1);
        }

        currentPixel = 0;
        currentScanline++;

        if (currentScanline >= TOTAL_SCANLINES) [[unlikely]] {
            // Wrap back to scanline 0 (start of new frame): reset the window
            // and clear the rolling buffer so stale slots don't bleed through.
            currentScanline = 0;
            windowStart = 0;
            framebuffer.fill(0);
            pendingClearSlot = -1;
            return;
        }

        // Slide the rolling window forward to track the beam. Each scanline that
        // leaves the top of the window frees its slot for a future scanline.
        if (currentScanline >= 1 && currentScanline <= VISIBLE_SCANLINES) [[likely]] {
            int target = currentScanline - 1;
            int window = windowScanlines();
            int slotsPerBank = (renderMode == RenderMode::RGBA16) ? 2 : 1;
            while (windowStart < target) {
                int slot = windowStart % window;
                // In block mode, the earlier half of a bank pair is held
                // (not cleared) until its later half also exits, so both
                // clear together. slotsPerBank alternates defer/flush every
                // iteration, so a pending slot - if any - is always exactly
                // this one's bank partner, never stale.
                bool isLastOfBank = (slot % slotsPerBank) == slotsPerBank - 1;
                if (!rollingModeBlock || slotsPerBank == 1 || isLastOfBank) {
                    // Flush a deferral left over from before rollingModeBlock
                    // changed mid-pair, so toggling the VOC bit mid-frame can
                    // never permanently strand an uncleared slot.
                    if (pendingClearSlot >= 0) {
                        clearSlot(pendingClearSlot);
                        pendingClearSlot = -1;
                    }
                    clearSlot(slot);
                } else {
                    pendingClearSlot = slot;
                }
                windowStart++;
            }
        }
    }
}

void Display::setRenderMode(RenderMode mode) {
    if (mode == renderMode) {
        return; // No change
    }

    // When switching modes, clear the framebuffer since bank layouts differ
    // 16-bit mode: 2 scanlines per bank
    // 32-bit mode: 1 scanline per bank
    framebuffer.fill(0);
    windowStart = 0;
    pendingClearSlot = -1;

    renderMode = mode;
}

int Display::getBufferBank(int y, int& offsetInBank) const {
    // Map an absolute scanline Y to its slot in the rolling window. Scanline y
    // lives in slot (y % window); only scanlines currently inside the window
    // [windowStart, windowStart + window) are addressable — everything else is
    // "forgotten" (writes dropped, reads return -1), matching the hardware.
    int window = windowScanlines();
    int relativeY = y - windowStart;
    if (relativeY < 0 || relativeY >= window) {
        return -1;  // Outside the rolling window
    }

    int slot = y % window;
    if (renderMode == RenderMode::RGBA16) {
        // 2 scanlines per bank, 512 bytes each
        offsetInBank = (slot & 1) * FB_WIDTH * sizeof(Color16);
        return slot >> 1;
    } else {
        // 1 scanline per bank
        offsetInBank = 0;
        return slot;
    }
}

void Display::clearSlot(int slot) {
    // Zero one scanline's worth of bytes at the given slot (byte base is
    // slot * bytesPerScanline, which equals bank*1KiB + within-bank offset).
    int bytesPerScanline = (renderMode == RenderMode::RGBA16)
                               ? FB_WIDTH * static_cast<int>(sizeof(Color16))
                               : FB_WIDTH * static_cast<int>(sizeof(Color32));
    size_t base = static_cast<size_t>(slot) * bytesPerScanline;
    if (base + bytesPerScanline <= framebuffer.size()) {
        std::memset(&framebuffer[base], 0, bytesPerScanline);
    }
}

void Display::latchScanline(int fbY) {
    if (fbY < 0 || fbY >= FULL_HEIGHT) return;

    int offsetInBank;
    int bank = getBufferBank(fbY, offsetInBank);
    Color32* out = &outputFrame[static_cast<size_t>(fbY) * FB_WIDTH];

    if (bank < 0) {
        // Scanline not in the window (never written) - leave it black.
        std::memset(out, 0, FB_WIDTH * sizeof(Color32));
        return;
    }

    size_t base = static_cast<size_t>(bank) * FB_BANK_SIZE + offsetInBank;
    if (renderMode == RenderMode::RGBA16) {
        for (int x = 0; x < FB_WIDTH; x++) {
            uint8_t low  = framebuffer[base + x * 2];
            uint8_t high = framebuffer[base + x * 2 + 1];
            out[x] = color16To32_LUT[(high << 8) | low];
        }
    } else {
        for (int x = 0; x < FB_WIDTH; x++) {
            size_t o = base + x * 4;
            out[x] = (framebuffer[o] << 24) | (framebuffer[o + 1] << 16) |
                     (framebuffer[o + 2] << 8) | framebuffer[o + 3];
        }
    }
}

void Display::setPixel(int x, int y, Color16 color) {
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FULL_HEIGHT) {
        // Use rolling bank system
        int offsetInBank;
        int bank = getBufferBank(y, offsetInBank);

        if (bank < 0) {
            return; // Outside buffer window
        }

        if (renderMode == RenderMode::RGBA16) {
            size_t pixelOffset = x * 2;
            size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
            if (offset + 1 < framebuffer.size()) {
                framebuffer[offset] = color & 0xFF;
                framebuffer[offset + 1] = (color >> 8) & 0xFF;
            }
        } else {
            Color32 color32 = color16To32(color);
            size_t pixelOffset = x * 4;
            size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
            if (offset + 3 < framebuffer.size()) {
                framebuffer[offset] = (color32 >> 24) & 0xFF;
                framebuffer[offset + 1] = (color32 >> 16) & 0xFF;
                framebuffer[offset + 2] = (color32 >> 8) & 0xFF;
                framebuffer[offset + 3] = color32 & 0xFF;
            }
        }
    }
}

void Display::setPixel32(int x, int y, Color32 color) {
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FULL_HEIGHT) {
        // Use rolling bank system
        int offsetInBank;
        int bank = getBufferBank(y, offsetInBank);

        if (bank < 0) {
            return; // Outside buffer window
        }

        if (renderMode == RenderMode::RGBA32) {
            size_t pixelOffset = x * 4;
            size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
            if (offset + 3 < framebuffer.size()) {
                framebuffer[offset] = (color >> 24) & 0xFF;
                framebuffer[offset + 1] = (color >> 16) & 0xFF;
                framebuffer[offset + 2] = (color >> 8) & 0xFF;
                framebuffer[offset + 3] = color & 0xFF;
            }
        } else {
            Color16 color16 = color32To16(color);
            size_t pixelOffset = x * 2;
            size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
            if (offset + 1 < framebuffer.size()) {
                framebuffer[offset] = color16 & 0xFF;
                framebuffer[offset + 1] = (color16 >> 8) & 0xFF;
            }
        }
    }
}

Color32 Display::getPixel(int x, int y) const {
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FULL_HEIGHT) {
        return 0x00000000; // Out of bounds
    }

    // Live read from the rolling buffer: valid only for scanlines currently in
    // the window; returns 0 (black) outside it, per the hardware's no-random-
    // access rule. (Full-frame output for the frontend goes through getScanline*.)
    int offsetInBank;
    int bank = getBufferBank(y, offsetInBank);
    if (bank < 0) {
        return 0x00000000; // Outside the rolling window
    }

    size_t base = static_cast<size_t>(bank) * FB_BANK_SIZE + offsetInBank;
    if (renderMode == RenderMode::RGBA16) {
        size_t offset = base + x * 2;
        uint8_t low  = framebuffer[offset];
        uint8_t high = framebuffer[offset + 1];
        return color16To32((high << 8) | low);
    } else {
        size_t offset = base + x * 4;
        uint8_t r = framebuffer[offset];
        uint8_t g = framebuffer[offset + 1];
        uint8_t b = framebuffer[offset + 2];
        uint8_t a = framebuffer[offset + 3];
        return (r << 24) | (g << 16) | (b << 8) | a;
    }
}

bool Display::getScanline(int y, Color32* buffer) const {
    if (y < 0 || y >= FULL_HEIGHT || !buffer) {
        return false;
    }
    // The latched output frame always holds a complete scanline.
    std::memcpy(buffer, &outputFrame[static_cast<size_t>(y) * FB_WIDTH],
                FB_WIDTH * sizeof(Color32));
    return true;
}

bool Display::getScanlineSDL(int y, uint32_t* buffer) const {
    if (y < 0 || y >= FULL_HEIGHT || !buffer) {
        return false;
    }
    // Convert the latched RGBA output row to SDL ARGB8888.
    const Color32* src = &outputFrame[static_cast<size_t>(y) * FB_WIDTH];
    for (int x = 0; x < FB_WIDTH; x++) {
        Color32 c = src[x];  // packed as (R<<24)|(G<<16)|(B<<8)|A
        uint8_t r = (c >> 24) & 0xFF;
        uint8_t g = (c >> 16) & 0xFF;
        uint8_t b = (c >> 8) & 0xFF;
        uint8_t a = c & 0xFF;
        buffer[x] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return true;
}

Color32 Display::color16To32(Color16 color) {
    // Extract 5-bit components from 16-bit color
    // Format: BBBBBGGGGGRRRRR-
    uint8_t r5 = (color >> 1) & 0x1F;   // Bits 5-1
    uint8_t g5 = (color >> 6) & 0x1F;   // Bits 10-6
    uint8_t b5 = (color >> 11) & 0x1F;  // Bits 15-11
    uint8_t a1 = color & 0x01;          // Bit 0

    // Expand 5-bit to 8-bit (scale up: multiply by 255/31 = 8.225)
    // Use (x << 3) | (x >> 2) for accurate expansion
    uint8_t r8 = (r5 << 3) | (r5 >> 2);
    uint8_t g8 = (g5 << 3) | (g5 >> 2);
    uint8_t b8 = (b5 << 3) | (b5 >> 2);
    uint8_t a8 = a1 ? 0xFF : 0x00;

    // Pack into 32-bit RGBA
    return (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
}

Color16 Display::color32To16(Color32 color) {
    // Extract 8-bit components from 32-bit color
    uint8_t r8 = (color >> 24) & 0xFF;
    uint8_t g8 = (color >> 16) & 0xFF;
    uint8_t b8 = (color >> 8) & 0xFF;
    uint8_t a8 = color & 0xFF;

    // Convert 8-bit to 5-bit (divide by 8)
    uint8_t r5 = r8 >> 3;
    uint8_t g5 = g8 >> 3;
    uint8_t b5 = b8 >> 3;
    uint8_t a1 = (a8 > 127) ? 1 : 0;

    // Pack into 16-bit format: BBBBBGGGGGRRRRR-
    return (b5 << 11) | (g5 << 6) | (r5 << 1) | a1;
}

} // namespace ZeroPoint
