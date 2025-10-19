# ZeroPoint

A fantasy console emulator for the ZeroPoint, a gaming system from 1992 that never existed.

## Overview

ZeroPoint is an emulator for a fictional 16-bit gaming console representing early 1990s hardware design.

## Features

- Accurate display timing (261 scanlines, 340 pixels per scanline)
- 256x256 visible display area
- 16-bit color (BBBBBGGGGGRRRRR- format)
- DMA controller with 16 channels (max 2 active), 4 transfer modes, and interrupt support
- .zpb ROM file format support
- Two frontends:
  - **SDL**: Standalone emulator with simple display
  - **Qt**: Full GUI with ROM loading and configuration

## Building

### SDL Frontend (Standalone)
```bash
make
./build/zeropoint
```

### Qt Frontend (Full GUI)
Requires CMake and Qt5/Qt6. See [docs/building.md](docs/building.md) for details.

```bash
mkdir build_qt && cd build_qt
cmake ..
make
./bin/zeropoint_qt
```

## ROM Format

ZeroPoint uses the .zpb (ZeroPoint Binary) format. See [docs/zpb-format.md](docs/zpb-format.md) for specification.

Create a test ROM:
```bash
./bin/make_test_rom test.zpb
```

## Documentation

- [Display Timing](docs/display.md) - Screen and timing specifications
- [DMA Controller](docs/dma.md) - DMA transfer modes and usage
- [ROM Format](docs/zpb-format.md) - .zpb file format
- [Building](docs/building.md) - Build instructions
- [Qt Frontend](docs/qt-frontend.md) - Qt GUI guide

## Testing

Run the DMA test suite:
```bash
cd build_qt
./bin/test_dma
```

## Status

Early development. Display system, DMA controller, and ROM loading implemented.

## License

To be determined.
