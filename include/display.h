#ifndef ZEROPOINT_DISPLAY_H
#define ZEROPOINT_DISPLAY_H

#include <cstdint>
#include <cstddef>
#include <array>

namespace ZeroPoint {

// Display timing constants
constexpr int TOTAL_SCANLINES = 261;
constexpr int VISIBLE_SCANLINES = 256;
constexpr int VBLANK_SCANLINES = 5;

constexpr int TOTAL_PIXELS_PER_LINE = 340;
constexpr int VISIBLE_PIXELS_PER_LINE = 256;
constexpr int HBLANK_PIXELS = 84;

// Framebuffer dimensions (rolling bank-based buffer)
constexpr int FB_WIDTH = 256;
constexpr int FB_BANKS = 8;           // 8 banks of 1 KiB each
constexpr int FB_BANK_SIZE = 1024;    // 1 KiB per bank
constexpr int FB_SCANLINES_16BIT = 16; // 16 scanlines in 16-bit mode (2 per bank)
constexpr int FB_SCANLINES_32BIT = 8;  // 8 scanlines in 32-bit mode (1 per bank)
constexpr int FULL_HEIGHT = 256;       // Full display height for compatibility

/**
 * 16-bit color format: BBBBBGGGGGRRRRR-
 * Bits 15-11: Blue (5 bits)
 * Bits 10-6:  Green (5 bits)
 * Bits 5-1:   Red (5 bits)
 * Bit 0:      Ignored
 */
using Color16 = uint16_t;

/**
 * 32-bit color format: RRRRRRRRGGGGGGGGBBBBBBBBAAAAAAAA
 * Standard RGBA8888 format
 */
using Color32 = uint32_t;

// Render mode
enum class RenderMode {
    RGBA16,  // 16-bit color (5-5-5-1)
    RGBA32   // 32-bit color (8-8-8-8)
};

class Display {
public:
    Display();
    ~Display();

    // Clock the display by one pixel
    void tick();

    // Get current scanline (0-260)
    int getCurrentScanline() const { return currentScanline; }

    // Get current pixel position in scanline (0-339)
    int getCurrentPixel() const { return currentPixel; }

    // Check if we're in visible area (inlined for performance)
    inline bool isVisibleArea() const {
        return (currentScanline >= 1 && currentScanline <= 256) &&
               (currentPixel < VISIBLE_PIXELS_PER_LINE);
    }

    // Check if we're in VBlank (inlined for performance)
    inline bool isVBlank() const {
        return (currentScanline == 0) || (currentScanline >= 257 && currentScanline <= 260);
    }

    // Check if we're in HBlank (inlined for performance)
    inline bool isHBlank() const {
        return currentPixel >= VISIBLE_PIXELS_PER_LINE;
    }

    // Get/set render mode
    RenderMode getRenderMode() const { return renderMode; }
    void setRenderMode(RenderMode mode);

    // Get the current pixel color being output (if in visible area)
    // Returns 32-bit color (16-bit values are expanded)
    Color32 getCurrentColor() const;

    // Get 16-bit color (for legacy/compatibility)
    Color16 getCurrentColor16() const;

    // Direct framebuffer access (for testing/debugging)
    // Returns pointer to rolling 8-bank buffer (8 KiB)
    // 16-bit mode
    Color16* getFramebuffer16() { return reinterpret_cast<Color16*>(framebuffer.data()); }
    const Color16* getFramebuffer16() const { return reinterpret_cast<const Color16*>(framebuffer.data()); }

    // 32-bit mode
    Color32* getFramebuffer32() { return reinterpret_cast<Color32*>(framebuffer.data()); }
    const Color32* getFramebuffer32() const { return reinterpret_cast<const Color32*>(framebuffer.data()); }

    // Get framebuffer size in bytes
    size_t getFramebufferSize() const;

    // Get the bank and offset for a given Y coordinate in the rolling buffer
    // Returns bank index (0-7) or -1 if outside buffer window
    int getBufferBank(int y, int& offsetInBank) const;

    // Set a pixel in the framebuffer (16-bit)
    // Note: Writes to scanlines outside the current 8-scanline window will be forgotten
    void setPixel(int x, int y, Color16 color);

    // Set a pixel in the framebuffer (32-bit)
    // Note: Writes to scanlines outside the current 8-scanline window will be forgotten
    void setPixel32(int x, int y, Color32 color);

    // Get pixel color at coordinates (for window rendering)
    // Returns 0 (black/transparent) if outside rolling buffer window
    Color32 getPixel(int x, int y) const;

    // Helper: Convert 16-bit color to 32-bit
    static Color32 color16To32(Color16 color);

    // Helper: Convert 32-bit color to 16-bit
    static Color16 color32To16(Color32 color);

private:
    // TEMPORARY HACK: Full framebuffer for direct access (no rolling banks)
    // 256×256×2 = 128 KiB for 16-bit mode
    // OLD: 8 banks × 1 KiB each = 8 KiB total
    std::array<uint8_t, 131072> framebuffer;  // 128 KiB raw buffer (TEMPORARY!)

    // Current render mode
    RenderMode renderMode;

    // Current position
    int currentScanline;
    int currentPixel;

    // Rolling bank management
    int bufferStartBank;  // Which bank is at index 0 (oldest data)
    int scanlinesInCurrentBank;  // Track how many scanlines written to current bank

    // Clear a bank in the buffer when it rolls out
    void clearBank(int bankIndex);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_DISPLAY_H
