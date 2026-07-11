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

    // VOC rolling mode (bit 6 of $00F0): block=true rotates a whole 1 KiB
    // bank (2 scanlines in RGBA16, 1 in RGBA32) as a unit, clearing both
    // halves together only once the later scanline of the pair exits the
    // window. block=false (VOC "single") clears each scanline slot the
    // instant it exits, independent of its bank partner. In RGBA32 mode a
    // bank already holds exactly one scanline, so the two are identical.
    void setRollingMode(bool block) { rollingModeBlock = block; }

    // Direct framebuffer access (for testing/debugging)
    // Returns pointer to rolling 8-bank buffer (8 KiB)
    // 16-bit mode
    Color16* getFramebuffer16() { return reinterpret_cast<Color16*>(framebuffer.data()); }
    const Color16* getFramebuffer16() const { return reinterpret_cast<const Color16*>(framebuffer.data()); }

    // 32-bit mode
    Color32* getFramebuffer32() { return reinterpret_cast<Color32*>(framebuffer.data()); }
    const Color32* getFramebuffer32() const { return reinterpret_cast<const Color32*>(framebuffer.data()); }

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

    // Get entire scanline (optimized for window rendering)
    // Fills buffer with 256 pixels (RGBA32 format)
    // Returns false if scanline is outside rolling buffer window
    bool getScanline(int y, Color32* buffer) const;

    // Get scanline in SDL ARGB format (optimized - single conversion)
    // Fills buffer with 256 pixels in SDL ARGB8888 format
    // Returns false if scanline is outside rolling buffer window
    bool getScanlineSDL(int y, uint32_t* buffer) const;

    // Helper: Convert 16-bit color to 32-bit
    static Color32 color16To32(Color16 color);

    // Helper: Convert 32-bit color to 16-bit
    static Color16 color32To16(Color32 color);

private:
    // Lookup table for RGBA16 -> RGBA32 (outputFrame format) conversion.
    // Pre-computed to eliminate per-pixel bit manipulation in latchScanline().
    static uint32_t color16To32_LUT[65536];
    static bool lutInitialized;
    static void initializeLUT();

    // Rolling framebuffer: 8 banks × 1 KiB = 8 KiB, mapped at $E000-$FFFF in
    // PPU memory. Holds a sliding window of scanlines (16 in RGBA16, 8 in
    // RGBA32); scanline y lives in slot (y % window). Writes outside the
    // current window are dropped (see getBufferBank).
    std::array<uint8_t, FB_BANKS * FB_BANK_SIZE> framebuffer;  // 8 KiB

    // Latched full-frame output (RGBA32). As each visible scanline is scanned
    // out, it is copied here from the rolling buffer, so the frontend can read
    // a complete 256×256 frame even though the live buffer only holds a window.
    std::array<Color32, FB_WIDTH * FULL_HEIGHT> outputFrame;  // 256 KiB

    // Current render mode
    RenderMode renderMode;

    // Current position
    int currentScanline;
    int currentPixel;

    // fbY of the oldest scanline currently in the rolling window. The window
    // covers [windowStart, windowStart + windowScanlines) and tracks the beam.
    int windowStart;

    // See setRollingMode(). Defaults to true (block), matching the VOC
    // render-mode-control register's power-on value of 0x00 (bit 6 clear).
    bool rollingModeBlock = true;

    // Block-mode's deferred clear: the earlier half of a bank pair, held
    // until its partner exits so both clear together. -1 when nothing is
    // deferred. See Display::tick().
    int pendingClearSlot = -1;

    // Number of scanlines the rolling window holds for the current mode.
    int windowScanlines() const {
        return (renderMode == RenderMode::RGBA16) ? FB_SCANLINES_16BIT : FB_SCANLINES_32BIT;
    }

    // Zero one scanline slot when it leaves the window / is reused.
    void clearSlot(int slot);

    // Copy a fully-scanned scanline from the rolling buffer to outputFrame.
    void latchScanline(int fbY);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_DISPLAY_H
