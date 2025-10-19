#ifndef ZEROPOINT_DISPLAY_H
#define ZEROPOINT_DISPLAY_H

#include <cstdint>

namespace ZeroPoint {

// Display timing constants
constexpr int TOTAL_SCANLINES = 261;
constexpr int VISIBLE_SCANLINES = 256;
constexpr int VBLANK_SCANLINES = 5;

constexpr int TOTAL_PIXELS_PER_LINE = 340;
constexpr int VISIBLE_PIXELS_PER_LINE = 256;
constexpr int HBLANK_PIXELS = 84;

// Framebuffer dimensions
constexpr int FB_WIDTH = 256;
constexpr int FB_HEIGHT = 256;

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

    // Check if we're in visible area
    bool isVisibleArea() const;

    // Check if we're in VBlank
    bool isVBlank() const;

    // Check if we're in HBlank
    bool isHBlank() const;

    // Get/set render mode
    RenderMode getRenderMode() const { return renderMode; }
    void setRenderMode(RenderMode mode);

    // Get the current pixel color being output (if in visible area)
    // Returns 32-bit color (16-bit values are expanded)
    Color32 getCurrentColor() const;

    // Get 16-bit color (for legacy/compatibility)
    Color16 getCurrentColor16() const;

    // Direct framebuffer access (for testing/debugging)
    // 16-bit mode
    Color16* getFramebuffer16() { return framebuffer16; }
    const Color16* getFramebuffer16() const { return framebuffer16; }

    // 32-bit mode
    Color32* getFramebuffer32() { return framebuffer32; }
    const Color32* getFramebuffer32() const { return framebuffer32; }

    // Set a pixel in the framebuffer (16-bit)
    void setPixel(int x, int y, Color16 color);

    // Set a pixel in the framebuffer (32-bit)
    void setPixel32(int x, int y, Color32 color);

    // Helper: Convert 16-bit color to 32-bit
    static Color32 color16To32(Color16 color);

    // Helper: Convert 32-bit color to 16-bit
    static Color16 color32To16(Color32 color);

private:
    // Internal 256x256 framebuffers (union to save space)
    union {
        Color16 framebuffer16[FB_WIDTH * FB_HEIGHT];
        Color32 framebuffer32[FB_WIDTH * FB_HEIGHT];
    };

    // Current render mode
    RenderMode renderMode;

    // Current position
    int currentScanline;
    int currentPixel;
};

} // namespace ZeroPoint

#endif // ZEROPOINT_DISPLAY_H
