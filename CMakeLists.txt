cmake_minimum_required(VERSION 3.16)
project(ZeroPoint VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(SDL2 REQUIRED)

# Try Qt6 first, fall back to Qt5
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

# Set automatic MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${SDL2_INCLUDE_DIRS})

# Core emulator library
set(CORE_SOURCES
    src/display.cpp
    src/rom.cpp
    src/dma.cpp
    src/ppu.cpp
    src/ppu_jit.cpp
    src/apu.cpp
    src/cpu.cpp
    src/window.cpp
)

set(CORE_HEADERS
    include/display.h
    include/rom.h
    include/dma.h
    include/ppu.h
    include/ppu_jit.h
    include/apu.h
    include/cpu.h
    include/window.h
)

add_library(zeropoint_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_link_libraries(zeropoint_core ${SDL2_LIBRARIES})

# SDL frontend (old standalone version)
add_executable(zeropoint_sdl
    src/main.cpp
)
target_link_libraries(zeropoint_sdl zeropoint_core ${SDL2_LIBRARIES})

# Qt frontend
set(QT_SOURCES
    qt/main.cpp
    qt/mainwindow.cpp
    qt/configdialog.cpp
    qt/emulatorwidget.cpp
)

set(QT_HEADERS
    qt/mainwindow.h
    qt/configdialog.h
    qt/emulatorwidget.h
)

set(QT_UI
    qt/mainwindow.ui
    qt/configdialog.ui
)

add_executable(zeropoint_qt ${QT_SOURCES} ${QT_HEADERS} ${QT_UI})
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(zeropoint_qt
        zeropoint_core
        Qt6::Core
        Qt6::Widgets
        ${SDL2_LIBRARIES}
    )
else()
    target_link_libraries(zeropoint_qt
        zeropoint_core
        Qt5::Core
        Qt5::Widgets
        ${SDL2_LIBRARIES}
    )
endif()

# Test ROM creation tool
add_executable(make_test_rom tools/make_test_rom.cpp)

# DMA test tool
add_executable(test_dma tools/test_dma.cpp)
target_link_libraries(test_dma zeropoint_core)

# PPU test tool
add_executable(test_ppu tools/test_ppu.cpp)
target_link_libraries(test_ppu zeropoint_core)

# Demo runner (with SDL)
add_executable(run_demo tools/run_demo.cpp)
target_link_libraries(run_demo zeropoint_core ${SDL2_LIBRARIES})

# Demo test (no SDL)
add_executable(test_demo tools/test_demo.cpp)
target_link_libraries(test_demo zeropoint_core)

# APU test tool
add_executable(test_apu tools/test_apu.cpp)
target_link_libraries(test_apu zeropoint_core)

# APU demo runner (with SDL audio)
add_executable(run_apu_demo tools/run_apu_demo.cpp)
target_link_libraries(run_apu_demo zeropoint_core ${SDL2_LIBRARIES})

# CPU test tool
add_executable(test_cpu tools/test_cpu.cpp)
target_link_libraries(test_cpu zeropoint_core)

# White fill test (color format verification)
add_executable(test_white_fill tools/test_white_fill.cpp)
target_link_libraries(test_white_fill zeropoint_core)

# White fill demo test (PPU framebuffer verification)
add_executable(test_demo_white tools/test_demo_white.cpp)
target_link_libraries(test_demo_white zeropoint_core)

# White fill demo debug test
add_executable(test_demo_white_debug tools/test_demo_white_debug.cpp)
target_link_libraries(test_demo_white_debug zeropoint_core)

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

set_target_properties(run_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_apu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(run_apu_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set_target_properties(test_cpu PROPERTIES
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
