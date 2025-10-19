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
- **Window Scale:** 1x to 4x scaling (native is 256x256)
- **VSync:** Enable/disable vertical synchronization
- **Filtering:** Nearest (sharp pixels) or Linear (smooth)

### Audio Tab
- **Enable Audio:** Toggle audio output
- **Volume:** 0-100% volume control

### Input Tab
- **Keyboard Controls:** View and configure key mappings
  - D-pad: Arrow keys
  - A Button: Z
  - B Button: X
  - Start: Return/Enter
  - Select: Right Shift

## Emulator Widget

The emulator widget (`emulatorwidget.h/cpp`) is a custom QWidget that:
- Integrates the ZeroPoint display system
- Renders the 256x256 framebuffer using Qt's QPainter
- Runs emulation loop at ~60 FPS using QTimer
- Converts the 16-bit BBBBBGGGGGRRRRR- color format to Qt RGB

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
The ZeroPoint uses 16-bit color in BBBBBGGGGGRRRRR- format:
- Bits 15-11: Blue (5 bits)
- Bits 10-6: Green (5 bits)
- Bits 5-1: Red (5 bits)
- Bit 0: Ignored

This is converted to Qt RGB888 by scaling each 5-bit component to 8 bits.

### Timing
The emulator runs at approximately 60 FPS:
- Each frame: 261 scanlines × 340 pixels = 88,740 pixels
- Visible area: 256 × 256 = 65,536 pixels
- QTimer interval: 16ms (~60 Hz)

### Thread Safety
Currently all emulation runs on the main Qt thread. For better performance, the emulation could be moved to a worker thread with the display updated via signals/slots.
