# Qt Frontend Guide

## Overview

The ZeroPoint Qt frontend provides a full graphical user interface for the emulator with the following features:

- ROM loading via file dialog (.zpb files)
- Configuration management with persistent settings
- Emulation controls (Start, Stop, Reset)
- Display configuration (window scale, VSync, filtering)
- Audio settings (enable/disable, volume control)
- Input configuration (keyboard mappings)

## Main Window

The main window (`mainwindow.h/cpp`) provides:

### Menu Bar

**File Menu:**
- Load ROM... (Ctrl+O) - Open a .zpb ROM file
- Exit (Ctrl+Q) - Close the emulator

**Emulation Menu:**
- Start - Begin emulation (enabled when ROM is loaded)
- Stop - Pause emulation
- Reset - Reset the emulator state

**Settings Menu:**
- Configuration... - Open configuration dialog

## Configuration Dialog

The configuration dialog (`configdialog.h/cpp`) contains three tabs:

### Display Tab
- **Window Scale:** configurable scaling (native is 256x256), persisted via `QSettings`
- **VSync:** the checkbox exists and is persisted, but nothing in the Qt
  frontend actually reads it to affect rendering/swap behavior yet
- **Filtering:** the combo box (Nearest/Linear) exists in the UI file, but
  `ConfigDialog` exposes no getter/setter for it and nothing in
  `mainwindow.cpp` references it — selecting a filter has no effect

### Audio Tab
- **Enable Audio / Volume** are applied: `AudioOutput` (`qt/audiooutput.cpp`) pushes the mixer's 48 kHz stereo S16 samples
  to an SDL device in push mode, scaled by the volume, dropped while muted.
- **Stability:** the emulator thread runs the console `FramePacer::AUDIO_LEAD` (80 ms) *ahead* of the wall clock, so the
  device queue holds a real buffer instead of one tick's worth and a host stall shorter than that is inaudible (the SDL
  frontend does the same). The price is ~80 ms between what the console does and what you hear; the picture is not
  delayed. If the queue still runs dry (the emulator fell behind: a loaded host, a heavy ROM) `AudioOutput` counts it
  and logs `audio: the device queue ran dry (N so far)` to stderr at most once a second. Past 0.25 s queued it clears the
  backlog instead of letting the sound drift behind the picture.

### Input Tab
- The UI has an input tab, but `ConfigDialog` exposes no way to view or
  remap keys — bindings are hardcoded in `emulatorwidget.cpp`:
  - D-pad: Arrow keys
  - Buttons 1-4: Z / X / C / V
  - Shoulder controls: A (big-left) / S (little-left) / D (little-right) / F (big-right)
  - Menu: either Shift key
  - Pause: Return/Enter

## Emulator Widget

The emulator widget (`emulatorwidget.h/cpp`) is a custom QWidget that:
- Integrates the ZeroPoint display system
- Renders the 256x256 framebuffer using Qt's QPainter
- Runs emulation loop at ~60 FPS using QTimer
- Reads `Display::getPixel()` (RGBA32) and extracts 8-bit R/G/B directly for
  Qt — there is no 16→8-bit scaling step (see corrected "Color Conversion" below)

## ROM Loading

When a ROM is loaded:
1. The ROM file is validated (magic number, version, size)
2. ROM metadata (title, developer) is displayed
3. The ROM data is loaded into memory
4. The emulator widget replaces the placeholder in the main window
5. Emulation controls become enabled

## Settings Persistence

Settings are saved automatically using QSettings:
- Organization: "ZeroPoint"
- Application: "Emulator"
- Stored in platform-specific location:
  - macOS: ~/Library/Preferences/com.ZeroPoint.Emulator.plist
  - Linux: ~/.config/ZeroPoint/Emulator.conf
  - Windows: Registry

Settings include:
- Display scale
- VSync enabled
- Audio enabled
- Volume level

## Creating Test ROMs

Use the `make_test_rom` tool:

```bash
./bin/make_test_rom myrom.zpb
```

This creates a simple test ROM with:
- Title: "Test ROM"
- Developer: "ZeroPoint Team"
- 1KB of test data
- Entry point at 0x00000000

## Implementation Notes

### Color Conversion
The display's internal 16-bit format (5-5-5-1, RGBA16) exists, but
`EmulatorWidget::updateFrameBuffer()` (`qt/emulatorwidget.cpp`) reads
`Display::getPixel()` in its **RGBA32** form (`RRGGBBAA`, one byte per
channel) and takes the 8-bit R/G/B directly — there is no 5-bit-to-8-bit
scaling step in the Qt frontend; that conversion only happens internally in
`Display` when translating between its 16-bit and 32-bit representations
(see `docs/display.md`).

### Timing
The emulator runs at approximately 60 FPS:
- Each frame: 261 scanlines × 340 pixels = 88,740 pixels
- Visible area: 256 × 256 = 65,536 pixels
- QTimer interval: 16ms (~60 Hz)

### Thread Safety
Currently all emulation runs on the main Qt thread. For better performance, the emulation could be moved to a worker thread with the display updated via signals/slots.
