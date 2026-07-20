cmake_minimum_required(VERSION 3.16)
project(ZeroPoint VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build. Without this a plain `cmake ..` produces an
# unoptimized (-O0) binary, which runs the cycle-accurate master clock several
# times slower than real time. Callers can still override with
# -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "No build type specified; defaulting to Release")
endif()

# Enable link-time optimization for optimized builds. The master clock crosses
# translation units every cycle (System -> PPU/Display/DMA); LTO lets those hot
# calls inline, which roughly halves per-cycle overhead. Skipped if the
# toolchain doesn't support it (e.g. some cross builds).
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ZP_IPO_SUPPORTED OUTPUT ZP_IPO_ERROR)
    if(ZP_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "Link-time optimization (IPO/LTO) enabled")
    else()
        message(STATUS "LTO not supported by toolchain: ${ZP_IPO_ERROR}")
    endif()
endif()

# Platform detection
# CMAKE_SYSTEM_NAME is "Windows" when cross-compiling via toolchain file
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
endif()

# When cross-compiling for Windows from Linux, CMAKE_CROSSCOMPILING is TRUE
# and CMAKE_HOST_SYSTEM_NAME is "Linux"
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CROSS_WINDOWS TRUE)
    message(STATUS "Cross-compiling for Windows (MinGW-w64)")
endif()

# Find SDL2
if(CROSS_WINDOWS)
    # AUR mingw-w64-sdl2 installs CMake config into the MinGW sysroot
    find_package(SDL2 CONFIG REQUIRED)
elseif(PLATFORM_WINDOWS)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_package(SDL2 REQUIRED)
    endif()
elseif(PLATFORM_MACOS)
    # Homebrew SDL2 (respects -DCMAKE_PREFIX_PATH=$(brew --prefix))
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        find_library(SDL2_LIBRARIES SDL2 REQUIRED)
        find_path(SDL2_INCLUDE_DIRS SDL.h PATH_SUFFIXES SDL2 REQUIRED)
    endif()
elseif(PLATFORM_LINUX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# Find Vulkan
if(CROSS_WINDOWS)
    # AUR mingw-w64-vulkan-headers + mingw-w64-vulkan-icd-loader
    find_package(Vulkan REQUIRED)
else()
    find_package(Vulkan REQUIRED)
endif()

# Find Qt6 (no Qt5 fallback — we require Qt6)
if(CROSS_WINDOWS)
    # mingw-w64-qt6-base installs CMake config into the MinGW sysroot.
    # QT_HOST_PATH must point to native Qt6 for moc/uic/rcc (set by toolchain file).
    find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt6 COMPONENTS Core Widgets QUIET)
    if(NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
        set(QT_VERSION_MAJOR 5)
    else()
        set(QT_VERSION_MAJOR 6)
    endif()
endif()

# Set automatic MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${Vulkan_INCLUDE_DIRS})

# Platform-specific compile options
if(CROSS_WINDOWS)
    # MinGW cross-compile flags
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    add_compile_options(-Wall -Wextra)
    # Static link the MinGW runtime so the .exe doesn't need libgcc/libstdc++ DLLs
    add_link_options(-static-libgcc -static-libstdc++)
elseif(PLATFORM_WINDOWS)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W3)
    add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00)
elseif(PLATFORM_LINUX OR PLATFORM_MACOS)
    add_compile_options(-Wall -Wextra)
endif()

# Core emulator library — pure emulation (CPU/PPU/APU/DMA/Display/System/ROM),
# no rendering backend. Links nothing but the standard library so it can be
# embedded by any frontend (SDL, Qt, Vulkan, or a future libretro/BizHawk-style
# host) without dragging in a graphics API it doesn't use.
set(CORE_SOURCES
    src/display.cpp
    src/rom.cpp
    src/dma.cpp
    src/ppu.cpp
    src/ppu_jit.cpp
    src/apu.cpp
    src/cpu.cpp
    src/cpu_instructions.cpp
    src/system.cpp
    src/default_boot_rom.cpp
)

set(CORE_HEADERS
    include/display.h
    include/rom.h
    include/dma.h
    include/ppu.h
    include/ppu_jit.h
    include/apu.h
    include/cpu.h
)

add_library(zeropoint_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# SDL2 rendering/input backend (Window). Leaf consumer of zeropoint_core, not
# part of it — only frontends that actually want an SDL window link this.
add_library(zeropoint_sdl_window STATIC
    src/window.cpp
    include/window.h
)
if(CROSS_WINDOWS OR PLATFORM_WINDOWS)
    target_link_libraries(zeropoint_sdl_window zeropoint_core SDL2::SDL2)
else()
    target_link_libraries(zeropoint_sdl_window zeropoint_core $<IF:$<OR:$<BOOL:${CROSS_WINDOWS}>,$<BOOL:${PLATFORM_WINDOWS}>>,SDL2::SDL2,${SDL2_LIBRARIES}>)
endif()

# Vulkan rendering backend (VulkanWindow). Same idea: a leaf consumer, not core.
add_library(zeropoint_vulkan_window STATIC
    src/vulkan_window.cpp
    include/vulkan_window.h
)
target_link_libraries(zeropoint_vulkan_window zeropoint_core Vulkan::Vulkan)

# SDL frontend (old standalone version)
add_executable(zeropoint_sdl
    src/main.cpp
)
if(CROSS_WINDOWS OR PLATFORM_WINDOWS)
    target_link_libraries(zeropoint_sdl zeropoint_sdl_window zeropoint_core SDL2::SDL2)
else()
    target_link_libraries(zeropoint_sdl zeropoint_sdl_window zeropoint_core $<IF:$<OR:$<BOOL:${CROSS_WINDOWS}>,$<BOOL:${PLATFORM_WINDOWS}>>,SDL2::SDL2,${SDL2_LIBRARIES}>)
endif()

# Qt frontend
set(QT_SOURCES
    qt/main.cpp
    qt/mainwindow.cpp
    qt/configdialog.cpp
    qt/emulatorwidget.cpp
    qt/keycapturebutton.cpp
)

set(QT_HEADERS
    qt/mainwindow.h
    qt/configdialog.h
    qt/emulatorwidget.h
    qt/keycapturebutton.h
    qt/keybindings.h
)

set(QT_UI
    qt/mainwindow.ui
    qt/configdialog.ui
)

add_executable(zeropoint_qt ${QT_SOURCES} ${QT_HEADERS} ${QT_UI})

# uic-generated ui_*.h headers #include custom-widget headers (e.g.
# keycapturebutton.h) by bare name, resolved relative to qt/ - needed since
# that's not otherwise on the include path.
target_include_directories(zeropoint_qt PRIVATE ${CMAKE_SOURCE_DIR}/qt)

# Qt never touches window.h/vulkan_window.h — it renders via QImage — so it
# links only the core and Qt itself, no SDL2/Vulkan.
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(zeropoint_qt
        zeropoint_core
        Qt6::Core
        Qt6::Widgets
    )
else()
    target_link_libraries(zeropoint_qt
        zeropoint_core
        Qt5::Core
        Qt5::Widgets
    )
endif()

# Test ROM creation tools
add_executable(make_test_rom tools/make_test_rom.cpp)

add_executable(test_rom_bare tools/test_rom_bare.cpp)
set_target_properties(test_rom_bare PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# DMA test tool
add_executable(test_dma tools/test_dma.cpp)
target_link_libraries(test_dma zeropoint_core)

# PPU test tool
add_executable(test_ppu tools/test_ppu.cpp)
target_link_libraries(test_ppu zeropoint_core)

# Demo runner (with SDL) — Linux only (dev tool)
if(PLATFORM_LINUX)
    add_executable(run_demo tools/run_demo.cpp)
    target_link_libraries(run_demo zeropoint_sdl_window zeropoint_core ${SDL2_LIBRARIES})

    # Demo runner (with Vulkan)
    add_executable(run_demo_vulkan tools/run_demo_vulkan.cpp)
    target_link_libraries(run_demo_vulkan zeropoint_vulkan_window zeropoint_core ${SDL2_LIBRARIES} Vulkan::Vulkan)
endif()

# Demo test (no SDL)
add_executable(test_demo tools/test_demo.cpp)
target_link_libraries(test_demo zeropoint_core)

# JIT test (no SDL, for Docker testing)
add_executable(test_jit tools/test_jit.cpp)
target_link_libraries(test_jit zeropoint_core)

# ARM64 JIT debug test
add_executable(test_arm64_jit_debug tools/test_arm64_jit_debug.cpp)
target_link_libraries(test_arm64_jit_debug zeropoint_core)

# APU test tool
add_executable(test_apu tools/test_apu.cpp)
target_link_libraries(test_apu zeropoint_core)

# APU demo runner (with SDL audio) — Linux only (dev tool)
if(PLATFORM_LINUX)
    add_executable(run_apu_demo tools/run_apu_demo.cpp)
    target_link_libraries(run_apu_demo zeropoint_core ${SDL2_LIBRARIES})
endif()

# CPU test tool
add_executable(test_cpu tools/test_cpu.cpp)
target_link_libraries(test_cpu zeropoint_core)

# Boot ROM handoff test (System -> Boot ROM stub -> cartridge)
add_executable(test_boot_rom tools/test_boot_rom.cpp)
target_link_libraries(test_boot_rom zeropoint_core)

# Basic interactive debugger (CPU/PPU/APU register inspection, stepping,
# breakpoints, memory read/write). Headless - no SDL/Qt/Vulkan.
add_executable(debugger tools/debugger.cpp)
target_link_libraries(debugger zeropoint_core)
set_target_properties(debugger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Hardware self-test Boot ROM runner (examples/boot-rom/tester/)
add_executable(test_diag_rom tools/test_diag_rom.cpp)
target_link_libraries(test_diag_rom zeropoint_core)

# Headless profiling workload: boots a two-bank Boot ROM image (e.g.
# ZPbootROM's splash) with no cartridge and runs it for a fixed number of
# frames, for use under `perf record`/gprof. Not part of the shipped test
# suite.
add_executable(profile_bootrom tools/profile_bootrom.cpp)
target_link_libraries(profile_bootrom zeropoint_core)

# White fill test (color format verification)
add_executable(test_white_fill tools/test_white_fill.cpp)
target_link_libraries(test_white_fill zeropoint_core)

# White fill demo test (PPU framebuffer verification)
add_executable(test_demo_white tools/test_demo_white.cpp)
target_link_libraries(test_demo_white zeropoint_core)

# White fill demo debug test
add_executable(test_demo_white_debug tools/test_demo_white_debug.cpp)
target_link_libraries(test_demo_white_debug zeropoint_core)

# On Windows, SDL2 sets GUI subsystem — force console for SDL frontend
if(CROSS_WINDOWS OR PLATFORM_WINDOWS)
    target_link_options(zeropoint_sdl PRIVATE -mconsole)
endif()

# Set output directories
set_target_properties(zeropoint_sdl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(zeropoint_qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(make_test_rom PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_dma PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_ppu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(PLATFORM_LINUX)
    set_target_properties(run_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    set_target_properties(run_demo_vulkan PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

set_target_properties(test_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_jit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_arm64_jit_debug PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_apu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(PLATFORM_LINUX)
    set_target_properties(run_apu_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

set_target_properties(test_cpu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_boot_rom PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_diag_rom PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_white_fill PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_demo_white PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_demo_white_debug PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

