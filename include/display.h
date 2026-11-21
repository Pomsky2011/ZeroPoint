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

// Pixel positions along a line (what the guest reads from the Display Status registers): 256 visible, 84 of H-blank.
constexpr int TOTAL_PIXELS_PER_LINE = 340;
constexpr int VISIBLE_PIXELS_PER_LINE = 256;
constexpr int HBLANK_PIXELS = 84;

// Core revision 13: the raster is paced like a real one. A line is LINE_CYCLES master cycles (68.011355 MHz / 15.66 kHz), not one
// cycle per pixel, so a frame is exactly 1/60 s (see System::TIMER_VBLANK_PERIOD) and the PPU has ~12.8x the cycles per line and
// per frame it had at one pixel per cycle (revision 12 and before: 340 cycles a line, ~1.3 ms a frame). The beam is at pixel
// floor(cycleInLine * 340 / LINE_CYCLES); H-blank starts on the first cycle whose pixel is 256.
constexpr int LINE_CYCLES = 4343;
constexpr int HBLANK_START_CYCLE = 3271;
static_assert((HBLANK_START_CYCLE * TOTAL_PIXELS_PER_LINE) / LINE_CYCLES == VISIBLE_PIXELS_PER_LINE &&
              ((HBLANK_START_CYCLE - 1) * TOTAL_PIXELS_PER_LINE) / LINE_CYCLES == VISIBLE_PIXELS_PER_LINE - 1,
              "HBLANK_START_CYCLE must be the first cycle of pixel VISIBLE_PIXELS_PER_LINE");

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

    // Clock the display by one master cycle
    void tick();

    // Bulk advance for a batching scheduler. quietTicks() is how many of the next tick()s are guaranteed to do
    // nothing but lineCycle++ (no H-blank edge at HBLANK_START_CYCLE, no end of line); advanceQuiet(n)
    // applies n <= quietTicks() of them at once. The tick after those is the one that must be run for real.
    int quietTicks() const {
        const int next = lineCycle < HBLANK_START_CYCLE ? HBLANK_START_CYCLE : LINE_CYCLES;
        return next - lineCycle - 1;
    }
    void advanceQuiet(int n) { lineCycle += n; }

    // Latching copies each finished scanline into the frame a frontend reads (getScanline*()); nothing in the
    // machine reads it back. A frontend that steps several rasters per displayed frame can switch it off for all
    // but the last one (the only one it shows). Does not change anything else: the rolling buffer, the PPU and
    // the CPU see no difference. Default on.
    void setLatchEnabled(bool on) { latchEnabled = on; }
    // Back to the power-on state (a power cycle): black buffers, raster at (0,0), default render mode. The latch
    // switch is the frontend's setting, not display state, so it is kept.
    void reset() {
        const bool keep = latchEnabled;
        *this = Display();
        latchEnabled = keep;
    }

    // Get current scanline (0-260)
    int getCurrentScanline() const { return currentScanline; }

    // The rolling window, for the PPU's tile-row queue (PPU::stepQueueEngine): rows [windowStart, windowStart +
    // windowRows) are writable, in the same ever-growing absolute row space as frameRowOffset + y.
    // While a queued blit completes, its rows are absolute and may lie in the frame before or after the one the raster is
    // in (a row queued for the next frame's top, finishing before scanline 257), so the writers accept one frame either
    // side of 0..255. Everything else keeps the strict 0..255 range: a TILEDRAW below row 255 is still dropped.
    void setAbsoluteRows(bool on) { absoluteRows = on; }
    int64_t getWindowStart() const { return windowStart; }
    int64_t getFrameRowOffset() const { return frameRowOffset; }
    int getWindowRows() const { return windowScanlines(); }
    bool isRowWritable(int y) const { int offsetInBank; return getBufferBank(y, offsetInBank) >= 0; }
    // Whether a write to absolute row `absRow` would survive: its slot must have been cleared of the row one window
    // earlier. In RGBA16 block mode slots clear in bank pairs (see tick()), so an even row that has only just entered
    // the window shares a slot whose clear is still deferred, and a write to it is wiped a scanline later - the window
    // check alone (getBufferBank) lets that write through.
    bool rowIsClear(int64_t absRow) const {
        int64_t previous = absRow - windowScanlines();
        if (rollingModeBlock && renderMode == RenderMode::RGBA16) previous |= 1;
        return previous < windowStart;
    }

    // Get current pixel position in scanline (0-339): where the beam is, derived from the cycle within the line
    int getCurrentPixel() const { return (lineCycle * TOTAL_PIXELS_PER_LINE) / LINE_CYCLES; }
    // Master cycles into the current line (0 .. LINE_CYCLES - 1)
    int getLineCycle() const { return lineCycle; }

    // Check if we're in visible area (inlined for performance)
    inline bool isVisibleArea() const {
        return (currentScanline >= 1 && currentScanline <= 256) &&
               (lineCycle < HBLANK_START_CYCLE);
    }

    // Check if we're in VBlank (inlined for performance)
    inline bool isVBlank() const {
        return (currentScanline == 0) || (currentScanline >= 257 && currentScanline <= 260);
    }

    // Check if we're in HBlank (inlined for performance)
    inline bool isHBlank() const {
        return lineCycle >= HBLANK_START_CYCLE;
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

    // v % window for the window's size (8 or 16 rows, a power of two) with no divide. A negative v keeps the signed
    // remainder C++ gives it (negative unless v is a multiple of window), so every caller sees the same slot as before.
    static int windowSlot(int64_t v, int window) {
        const int r = static_cast<int>(v & (window - 1));
        return (v < 0 && r != 0) ? r - window : r;
    }

    // Get the bank and offset for a given Y coordinate in the rolling buffer
    // Returns bank index (0-7) or -1 if outside buffer window
    int getBufferBank(int y, int& offsetInBank) const;

    // Set a pixel in the framebuffer (16-bit)
    // Note: Writes to scanlines outside the current 8-scanline window will be forgotten
    void setPixel(int x, int y, Color16 color);

    // setPixel32 for `count` pixels along one row, resolving the rolling-buffer slot once (what a tile blit's
    // 8-pixel rows want). Same checks and result as count calls to setPixel32(x + i, y, colors[i]).
    void setPixelRow32(int x, int y, const Color32* colors, int count);

    // Direct pointer to the framebuffer bytes of `count` pixels starting at (x, y), in the current render mode's
    // layout (2 bytes/pixel little-endian Color16, or 4 bytes/pixel R,G,B,A), so a blit can store a whole run
    // without per-pixel calls. Null when setPixelRow32 would write anything less than the full run: y off the
    // screen or outside the rolling window, or the run not wholly inside the scanline. Valid until the next
    // call that moves the window.
    uint8_t* rowSpan(int x, int y, int count) {
        if (!rowInRange(y) || x < 0 || x + count > FB_WIDTH) return nullptr;
        int offsetInBank = 0;
        const int bank = getBufferBank(y, offsetInBank);
        if (bank < 0) return nullptr;
        const size_t bpp = renderMode == RenderMode::RGBA16 ? 2 : 4;
        return &framebuffer[static_cast<size_t>(bank) * FB_BANK_SIZE + offsetInBank + static_cast<size_t>(x) * bpp];
    }

    // The framebuffer bytes of scanline y's column 0 (rowSpan's address without the column checks), or null when a
    // pixel store to that row would be dropped (off the screen or outside the rolling window). Column x of the row
    // is at + x * bytes-per-pixel; the caller keeps its columns on the scanline. Valid until the window moves.
    uint8_t* rowBase(int y) {
        if (!rowInRange(y)) return nullptr;
        int offsetInBank = 0;
        const int bank = getBufferBank(y, offsetInBank);
        if (bank < 0) return nullptr;
        return &framebuffer[static_cast<size_t>(bank) * FB_BANK_SIZE + offsetInBank];
    }

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

    // FNV-1a hash of all emulated state (rolling buffer, latched frame, raster position, window).
    // For regression tests that compare two paths that must be state-identical.
    uint64_t stateHash() const;
    // The same without the latched output frame, which is presentation: it depends on whether the frontend skipped
    // frames (setLatchEnabled), not on the machine. What a movie checkpoint uses.
    uint64_t machineHash() const;

    // Helper: Convert 16-bit color to 32-bit
    static Color32 color16To32(Color16 color);

    // Helper: Convert 32-bit color to 16-bit
    static Color16 color32To16(Color32 color);

private:
    uint64_t hashImpl(bool withOutputFrame) const;

    // Rolling framebuffer: 8 banks × 1 KiB = 8 KiB, mapped at $E000-$FFFF in
    // PPU memory. Holds a sliding window of scanlines (16 in RGBA16, 8 in
    // RGBA32); scanline y lives in slot (y % window). Writes outside the
    // current window are dropped (see getBufferBank).
    std::array<uint8_t, FB_BANKS * FB_BANK_SIZE> framebuffer{};  // 8 KiB

    // Latched full-frame output (RGBA32). As each visible scanline is scanned
    // out, it is copied here from the rolling buffer, so the frontend can read
    // a complete 256×256 frame even though the live buffer only holds a window.
    bool latchEnabled = true;
    std::array<Color32, FB_WIDTH * FULL_HEIGHT> outputFrame{};  // 256 KiB

    // Current render mode
    RenderMode renderMode;

    // Current position
    int currentScanline;
    int lineCycle;

    // fbY of the oldest scanline currently in the rolling window. The window
    // covers [windowStart, windowStart + windowScanlines) and tracks the beam.
    // Lives in the same ever-growing absolute coordinate space as
    // frameRowOffset (see below) rather than being reset to 0 every frame -
    // that reset used to also nuke a real one-band lead the vblank tail
    // would otherwise hand the next frame's row 0. int64 so it never
    // meaningfully overflows across a long-running session.
    int64_t windowStart;

    // Absolute row-space offset of the frame currently being written to (or,
    // during the vblank tail after the last visible scanline, the frame
    // about to start). Bumped by VISIBLE_SCANLINES exactly once per frame, at
    // the transition out of the visible region - deliberately *before* the
    // wrap back to scanline 0, so that a setPixel/setPixel32(x, y=0..) call
    // made during the vblank tail already lands in the *next* frame's row
    // space instead of the just-finished one. y (always local, 0-255) is
    // converted to this absolute space as `frameRowOffset + y` everywhere
    // window membership is checked; always a multiple of both possible
    // window sizes (8, 16), so slot = y % window is unaffected.
    int64_t frameRowOffset = 0;

    // See setRollingMode(). Defaults to true (block), matching the VOC
    // render-mode-control register's power-on value of 0x00 (bit 6 clear).
    bool rollingModeBlock = true;
    bool absoluteRows = false;   // see setAbsoluteRows()
    bool rowInRange(int y) const { return absoluteRows ? (y >= -FULL_HEIGHT && y < 2 * FULL_HEIGHT) : (y >= 0 && y < FULL_HEIGHT); }

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
