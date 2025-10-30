# Adding macOS App Icons to ZeroPoint

## Current Status

✅ macOS app bundles are now created for:
- `zeropoint_qt.app` - Main Qt emulator
- `zeropoint_sdl.app` - SDL emulator
- `run_demo.app` - PPU demo runner
- `run_apu_demo.app` - APU demo runner

The bundles are fully functional and can be double-clicked in Finder!

## How to Add an Icon

When you have an icon file ready, follow these steps:

### 1. Create the Icon File

Your icon should be in `.icns` format (macOS icon format). You can create this from a PNG using:

```bash
# From a 1024x1024 PNG
mkdir zeropoint.iconset
sips -z 16 16     icon.png --out zeropoint.iconset/icon_16x16.png
sips -z 32 32     icon.png --out zeropoint.iconset/icon_16x16@2x.png
sips -z 32 32     icon.png --out zeropoint.iconset/icon_32x32.png
sips -z 64 64     icon.png --out zeropoint.iconset/icon_32x32@2x.png
sips -z 128 128   icon.png --out zeropoint.iconset/icon_128x128.png
sips -z 256 256   icon.png --out zeropoint.iconset/icon_128x128@2x.png
sips -z 256 256   icon.png --out zeropoint.iconset/icon_256x256.png
sips -z 512 512   icon.png --out zeropoint.iconset/icon_256x256@2x.png
sips -z 512 512   icon.png --out zeropoint.iconset/icon_512x512.png
sips -z 1024 1024 icon.png --out zeropoint.iconset/icon_512x512@2x.png
iconutil -c icns zeropoint.iconset
```

This creates `zeropoint.icns`.

### 2. Add Icon to Project

```bash
# Create resources directory
mkdir -p resources

# Copy your icon file
cp zeropoint.icns resources/

# You can create different icons for each app if desired:
cp zeropoint.icns resources/zeropoint_qt.icns
cp zeropoint.icns resources/zeropoint_sdl.icns
cp zeropoint.icns resources/run_demo.icns
cp zeropoint.icns resources/run_apu_demo.icns
```

### 3. Update CMakeLists.txt

Edit `CMakeLists.txt` and uncomment the icon line (around line 271):

```cmake
# Change this:
# MACOSX_BUNDLE_ICON_FILE "zeropoint.icns"  # Uncomment when icon is added

# To this:
MACOSX_BUNDLE_ICON_FILE "zeropoint_qt.icns"
```

Then add the icon as a resource:

```cmake
# After the add_executable(zeropoint_qt ...) line, add:
set_source_files_properties(resources/zeropoint_qt.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
target_sources(zeropoint_qt PRIVATE resources/zeropoint_qt.icns)
```

Repeat for each app (zeropoint_sdl, run_demo, run_apu_demo).

### 4. Rebuild

```bash
cd build_qt
cmake .. && cmake --build . -j
```

Your apps will now have icons in Finder and the Dock!

## Alternative: Quick Method (After Build)

If you just want to test an icon without rebuilding:

```bash
# Copy icon directly into built bundle
cp zeropoint.icns build_qt/bin/zeropoint_qt.app/Contents/Resources/

# Update Info.plist
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile zeropoint.icns" \
    build_qt/bin/zeropoint_qt.app/Contents/Info.plist

# Force macOS to update icon cache
touch build_qt/bin/zeropoint_qt.app
killall Finder
```

## Icon Requirements

- **Format**: `.icns` (macOS Icon format)
- **Sizes**: Should include 16x16 through 1024x1024 (all standard sizes)
- **Transparency**: PNG source should have transparency if desired
- **Style**: Should look good at both small (16x16 Dock) and large (512x512+ Finder) sizes

## Notes

- Icons are cached by macOS, so you may need to `killall Finder` to see changes
- The icon file must be named exactly as specified in `MACOSX_BUNDLE_ICON_FILE`
- Icons are embedded in the bundle at build time
- You can have different icons for each .app (Qt vs SDL vs demos)

---

**When you have an icon ready, just follow steps 2-4 above!**
