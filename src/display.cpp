#include "display.h"
#include <cstring>

namespace ZeroPoint {

// Static lookup table for color conversion
uint32_t Display::color16ToSDL_LUT[65536];
bool Display::lutInitialized = false;

void Display::initializeLUT() {
    if (lutInitialized) return;

    // Pre-compute all 65536 RGBA16 to SDL ARGB conversions
    for (uint32_t color16 = 0; color16 < 65536; color16++) {
        // Extract 5-bit RGB and 1-bit alpha
        uint8_t r5 = (color16 >> 1) & 0x1F;
        uint8_t g5 = (color16 >> 6) & 0x1F;
        uint8_t b5 = (color16 >> 11) & 0x1F;
        uint8_t a1 = color16 & 0x01;

        // Expand 5-bit to 8-bit
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        uint8_t a8 = a1 ? 0xFF : 0x00;

        // Pack as SDL ARGB8888
        color16ToSDL_LUT[color16] = (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
    }

    lutInitialized = true;
}

Display::Display()
    : renderMode(RenderMode::RGBA16)
    , currentScanline(0)
    , currentPixel(0)
    , bufferStartBank(0)
    , scanlinesInCurrentBank(0)
{
    // Initialize lookup table on first Display creation
    initializeLUT();

    // Initialize 8 KiB framebuffer to black
    framebuffer.fill(0);
}

Display::~Display() {
}

void Display::tick() {
    // Advance pixel position (hot path optimization)
    currentPixel++;

    if (currentPixel >= TOTAL_PIXELS_PER_LINE) [[unlikely]] {
        // H-Blank: Move to next scanline
        currentPixel = 0;
        currentScanline++;

        if (currentScanline >= TOTAL_SCANLINES) [[unlikely]] {
            // Wrap back to scanline 0 (start of new frame)
            currentScanline = 0;
            bufferStartBank = 0;
            scanlinesInCurrentBank = 0;
            return;
        }

        // H-Blank bank rolling logic (only during visible scanlines)
        if (currentScanline > 0 && currentScanline < VISIBLE_SCANLINES) [[likely]] {
            if (renderMode == RenderMode::RGBA32) [[unlikely]] {
                // 32-bit mode: Roll 2 banks per H-Blank
                // Each scanline takes 1 bank, so roll 2 banks (= 2 scanlines)
                clearBank(bufferStartBank);
                bufferStartBank = (bufferStartBank + 1) & 0x7;  // Optimize modulo 8
                clearBank(bufferStartBank);
                bufferStartBank = (bufferStartBank + 1) & 0x7;  // Optimize modulo 8
                scanlinesInCurrentBank = 0;
            } else {
                // 16-bit mode: Roll 1 bank per H-Blank
                // Each bank holds 2 scanlines, so increment counter
                scanlinesInCurrentBank++;
                if (scanlinesInCurrentBank >= 2) [[unlikely]] {
                    // Bank is full, roll to next bank
                    clearBank(bufferStartBank);
                    bufferStartBank = (bufferStartBank + 1) & 0x7;  // Optimize modulo 8
                    scanlinesInCurrentBank = 0;
                }
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
    bufferStartBank = 0;
    scanlinesInCurrentBank = 0;

    renderMode = mode;
}

size_t Display::getFramebufferSize() const {
    return FB_BANKS * FB_BANK_SIZE;  // Always 8 KiB
}

int Display::getBufferBank(int y, int& offsetInBank) const {
    // Map absolute scanline Y to bank index and offset within bank
    int maxScanlines = (renderMode == RenderMode::RGBA16) ? FB_SCANLINES_16BIT : FB_SCANLINES_32BIT;

    // Calculate relative position from bufferStartBank
    // The buffer contains the NEXT N scanlines to be displayed
    int relativeY = (y - currentScanline + maxScanlines) % maxScanlines;

    if (relativeY < 0 || relativeY >= maxScanlines) {
        return -1;  // Outside buffer window
    }

    if (renderMode == RenderMode::RGBA16) {
        // 16-bit mode: 2 scanlines per bank
        int bankOffset = relativeY / 2;
        int scanlineInBank = relativeY % 2;
        int bank = (bufferStartBank + bankOffset) % FB_BANKS;
        offsetInBank = scanlineInBank * FB_WIDTH * sizeof(Color16);  // 512 bytes per scanline
        return bank;
    } else {
        // 32-bit mode: 1 scanline per bank
        int bank = (bufferStartBank + relativeY) % FB_BANKS;
        offsetInBank = 0;
        return bank;
    }
}

void Display::clearBank(int bankIndex) {
    if (bankIndex < 0 || bankIndex >= FB_BANKS) {
        return;
    }

    // Clear 1 KiB bank as part of rolling buffer
    // In 16-bit mode: 1 KiB = 512 pixels = 2 scanlines
    // In 32-bit mode: 1 KiB = 256 pixels = 1 scanline
    // The rolling buffer clears old scanlines to make room for new ones
    std::memset(&framebuffer[bankIndex * FB_BANK_SIZE], 0, FB_BANK_SIZE);
}

Color32 Display::getCurrentColor() const {
    if (!isVisibleArea()) {
        return 0x00000000; // Return transparent black if not in visible area
    }

    // Scanline 1 maps to framebuffer row 0
    int fbY = currentScanline - 1;
    int fbX = currentPixel;

    if (fbY < 0 || fbY >= 256 || fbX < 0 || fbX >= 256) {
        return 0x00000000;
    }

    // Use rolling bank system
    int offsetInBank;
    int bank = getBufferBank(fbY, offsetInBank);

    if (bank < 0) {
        return 0x00000000; // Outside buffer window
    }

    if (renderMode == RenderMode::RGBA16) {
        // 2 bytes per pixel
        size_t pixelOffset = fbX * 2;
        size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
        if (offset + 1 < framebuffer.size()) {
            uint8_t low = framebuffer[offset];
            uint8_t high = framebuffer[offset + 1];
            Color16 color16 = (high << 8) | low;
            return color16To32(color16);
        }
    } else {
        // 4 bytes per pixel
        size_t pixelOffset = fbX * 4;
        size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
        if (offset + 3 < framebuffer.size()) {
            uint8_t r = framebuffer[offset];
            uint8_t g = framebuffer[offset + 1];
            uint8_t b = framebuffer[offset + 2];
            uint8_t a = framebuffer[offset + 3];
            return (r << 24) | (g << 16) | (b << 8) | a;
        }
    }

    return 0x00000000;
}

Color16 Display::getCurrentColor16() const {
    if (!isVisibleArea()) {
        return 0x0000; // Return black if not in visible area
    }

    // Scanline 1 maps to framebuffer row 0
    int fbY = currentScanline - 1;
    int fbX = currentPixel;

    if (fbY < 0 || fbY >= 256 || fbX < 0 || fbX >= 256) {
        return 0x0000;
    }

    // Use rolling bank system
    int offsetInBank;
    int bank = getBufferBank(fbY, offsetInBank);

    if (bank < 0) {
        return 0x0000; // Outside buffer window
    }

    if (renderMode == RenderMode::RGBA16) {
        // 2 bytes per pixel
        size_t pixelOffset = fbX * 2;
        size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
        if (offset + 1 < framebuffer.size()) {
            uint8_t low = framebuffer[offset];
            uint8_t high = framebuffer[offset + 1];
            return (high << 8) | low;
        }
    } else {
        // 4 bytes per pixel
        size_t pixelOffset = fbX * 4;
        size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
        if (offset + 3 < framebuffer.size()) {
            uint8_t r = framebuffer[offset];
            uint8_t g = framebuffer[offset + 1];
            uint8_t b = framebuffer[offset + 2];
            uint8_t a = framebuffer[offset + 3];
            Color32 color32 = (r << 24) | (g << 16) | (b << 8) | a;
            return color32To16(color32);
        }
    }

    return 0x0000;
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

    // Use rolling bank system
    int offsetInBank;
    int bank = getBufferBank(y, offsetInBank);

    if (bank < 0) {
        return 0x00000000; // Outside buffer window
    }

    if (renderMode == RenderMode::RGBA16) {
        // 2 bytes per pixel
        size_t pixelOffset = x * 2;
        size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
        if (offset + 1 < framebuffer.size()) {
            uint8_t low = framebuffer[offset];
            uint8_t high = framebuffer[offset + 1];
            Color16 color16 = (high << 8) | low;
            return color16To32(color16);
        }
    } else {
        // 4 bytes per pixel
        size_t pixelOffset = x * 4;
        size_t offset = bank * FB_BANK_SIZE + offsetInBank + pixelOffset;
        if (offset + 3 < framebuffer.size()) {
            uint8_t r = framebuffer[offset];
            uint8_t g = framebuffer[offset + 1];
            uint8_t b = framebuffer[offset + 2];
            uint8_t a = framebuffer[offset + 3];
            return (r << 24) | (g << 16) | (b << 8) | a;
        }
    }

    return 0x00000000;
}

bool Display::getScanline(int y, Color32* buffer) const {
    if (y < 0 || y >= FULL_HEIGHT || !buffer) {
        return false;
    }

    // Get bank and offset for this scanline
    int offsetInBank;
    int bank = getBufferBank(y, offsetInBank);

    if (bank < 0) {
        // Outside buffer window - fill with black
        for (int x = 0; x < FB_WIDTH; x++) {
            buffer[x] = 0x00000000;
        }
        return false;
    }

    // Read entire scanline at once
    if (renderMode == RenderMode::RGBA16) {
        // 16-bit mode: 2 bytes per pixel, need to convert to 32-bit
        size_t offset = bank * FB_BANK_SIZE + offsetInBank;
        for (int x = 0; x < FB_WIDTH; x++) {
            size_t pixelOffset = offset + x * 2;
            if (pixelOffset + 1 < framebuffer.size()) {
                uint8_t low = framebuffer[pixelOffset];
                uint8_t high = framebuffer[pixelOffset + 1];
                Color16 color16 = (high << 8) | low;
                buffer[x] = color16To32(color16);
            } else {
                buffer[x] = 0x00000000;
            }
        }
    } else {
        // 32-bit mode: 4 bytes per pixel, can use memcpy
        size_t offset = bank * FB_BANK_SIZE + offsetInBank;
        if (offset + FB_WIDTH * 4 <= framebuffer.size()) {
            // Fast path: direct memory copy
            std::memcpy(buffer, &framebuffer[offset], FB_WIDTH * sizeof(Color32));
        } else {
            // Slow path: bounds-checked copy
            for (int x = 0; x < FB_WIDTH; x++) {
                size_t pixelOffset = offset + x * 4;
                if (pixelOffset + 3 < framebuffer.size()) {
                    uint8_t r = framebuffer[pixelOffset];
                    uint8_t g = framebuffer[pixelOffset + 1];
                    uint8_t b = framebuffer[pixelOffset + 2];
                    uint8_t a = framebuffer[pixelOffset + 3];
                    buffer[x] = (r << 24) | (g << 16) | (b << 8) | a;
                } else {
                    buffer[x] = 0x00000000;
                }
            }
        }
    }

    return true;
}

bool Display::getScanlineSDL(int y, uint32_t* buffer) const {
    if (y < 0 || y >= FULL_HEIGHT || !buffer) {
        return false;
    }

    // Get bank and offset for this scanline
    int offsetInBank;
    int bank = getBufferBank(y, offsetInBank);

    if (bank < 0) {
        // Outside buffer window - fill with black
        std::memset(buffer, 0, FB_WIDTH * sizeof(uint32_t));
        return false;
    }

    size_t offset = bank * FB_BANK_SIZE + offsetInBank;

    // Single-pass conversion to SDL ARGB format
    if (renderMode == RenderMode::RGBA16) {
        // 16-bit mode: Use lookup table for instant conversion
        // Bounds check once for entire scanline (not per pixel!)
        size_t scanlineEnd = offset + FB_WIDTH * 2;
        if (scanlineEnd <= framebuffer.size()) {
            // Fast path: entire scanline is in bounds (no per-pixel bounds check!)
            const uint16_t* src = reinterpret_cast<const uint16_t*>(&framebuffer[offset]);
            for (int x = 0; x < FB_WIDTH; x++) {
                buffer[x] = color16ToSDL_LUT[src[x]];
            }
        } else {
            // Slow path: partial scanline (rare, only at buffer edges)
            for (int x = 0; x < FB_WIDTH; x++) {
                size_t pixelOffset = offset + x * 2;
                if (pixelOffset + 1 < framebuffer.size()) {
                    uint8_t low = framebuffer[pixelOffset];
                    uint8_t high = framebuffer[pixelOffset + 1];
                    Color16 color16 = (high << 8) | low;
                    buffer[x] = color16ToSDL_LUT[color16];
                } else {
                    buffer[x] = 0x00000000;
                }
            }
        }
    } else {
        // 32-bit mode: Just reorder bytes from RRGGBBAA to ARGB
        size_t scanlineEnd = offset + FB_WIDTH * 4;
        if (scanlineEnd <= framebuffer.size()) {
            // Fast path: entire scanline is in bounds
            const uint8_t* src = &framebuffer[offset];
            for (int x = 0; x < FB_WIDTH; x++) {
                uint8_t r = src[x * 4];
                uint8_t g = src[x * 4 + 1];
                uint8_t b = src[x * 4 + 2];
                uint8_t a = src[x * 4 + 3];
                buffer[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        } else {
            // Slow path: partial scanline (rare)
            for (int x = 0; x < FB_WIDTH; x++) {
                size_t pixelOffset = offset + x * 4;
                if (pixelOffset + 3 < framebuffer.size()) {
                    uint8_t r = framebuffer[pixelOffset];
                    uint8_t g = framebuffer[pixelOffset + 1];
                    uint8_t b = framebuffer[pixelOffset + 2];
                    uint8_t a = framebuffer[pixelOffset + 3];
                    buffer[x] = (a << 24) | (r << 16) | (g << 8) | b;
                } else {
                    buffer[x] = 0x00000000;
                }
            }
        }
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
