#include "display.h"
#include <cstring>

namespace ZeroPoint {

Display::Display()
    : renderMode(RenderMode::RGBA16)
    , currentScanline(0)
    , currentPixel(0)
{
    // Initialize framebuffer to black (0x0000)
    std::memset(framebuffer16, 0, sizeof(framebuffer16));
}

Display::~Display() {
}

void Display::tick() {
    // Advance pixel position
    currentPixel++;

    if (currentPixel >= TOTAL_PIXELS_PER_LINE) {
        // Move to next scanline
        currentPixel = 0;
        currentScanline++;

        if (currentScanline >= TOTAL_SCANLINES) {
            // Wrap back to scanline 0 (start of new frame)
            currentScanline = 0;
        }
    }
}

bool Display::isVisibleArea() const {
    // Scanlines 1-256 are visible, pixels 0-255 are visible
    return (currentScanline >= 1 && currentScanline <= 256) &&
           (currentPixel < VISIBLE_PIXELS_PER_LINE);
}

bool Display::isVBlank() const {
    // Scanline 0 (preline) and scanlines 257-260 are VBlank
    return (currentScanline == 0) || (currentScanline >= 257 && currentScanline <= 260);
}

bool Display::isHBlank() const {
    // Pixels 256-339 are HBlank
    return currentPixel >= VISIBLE_PIXELS_PER_LINE;
}

void Display::setRenderMode(RenderMode mode) {
    if (mode == renderMode) {
        return; // No change
    }

    if (mode == RenderMode::RGBA32) {
        // Convert 16-bit framebuffer to 32-bit
        Color16 temp[FB_WIDTH * FB_HEIGHT];
        std::memcpy(temp, framebuffer16, sizeof(temp));

        for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
            framebuffer32[i] = color16To32(temp[i]);
        }
    } else {
        // Convert 32-bit framebuffer to 16-bit
        Color32 temp[FB_WIDTH * FB_HEIGHT];
        std::memcpy(temp, framebuffer32, sizeof(temp));

        for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
            framebuffer16[i] = color32To16(temp[i]);
        }
    }

    renderMode = mode;
}

Color32 Display::getCurrentColor() const {
    if (!isVisibleArea()) {
        return 0x00000000; // Return transparent black if not in visible area
    }

    // Calculate framebuffer position
    // Scanline 1 maps to framebuffer row 0
    int fbY = currentScanline - 1;
    int fbX = currentPixel;
    int index = fbY * FB_WIDTH + fbX;

    if (renderMode == RenderMode::RGBA16) {
        return color16To32(framebuffer16[index]);
    } else {
        return framebuffer32[index];
    }
}

Color16 Display::getCurrentColor16() const {
    if (!isVisibleArea()) {
        return 0x0000; // Return black if not in visible area
    }

    // Calculate framebuffer position
    // Scanline 1 maps to framebuffer row 0
    int fbY = currentScanline - 1;
    int fbX = currentPixel;
    int index = fbY * FB_WIDTH + fbX;

    if (renderMode == RenderMode::RGBA16) {
        return framebuffer16[index];
    } else {
        return color32To16(framebuffer32[index]);
    }
}

void Display::setPixel(int x, int y, Color16 color) {
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT) {
        int index = y * FB_WIDTH + x;
        if (renderMode == RenderMode::RGBA16) {
            framebuffer16[index] = color;
        } else {
            framebuffer32[index] = color16To32(color);
        }
    }
}

void Display::setPixel32(int x, int y, Color32 color) {
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT) {
        int index = y * FB_WIDTH + x;
        if (renderMode == RenderMode::RGBA32) {
            framebuffer32[index] = color;
        } else {
            framebuffer16[index] = color32To16(color);
        }
    }
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
