# Cross-compilation toolchain: Linux -> Windows x64 (MinGW-w64)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32)
set(MINGW_SYSROOT /usr/${MINGW_PREFIX})

find_program(CMAKE_C_COMPILER   ${MINGW_PREFIX}-gcc   REQUIRED)
find_program(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++   REQUIRED)
find_program(CMAKE_RC_COMPILER  ${MINGW_PREFIX}-windres)

# Fedora puts the MinGW sysroot at .../sys-root/mingw; Arch uses the prefix directly
if(EXISTS "${MINGW_SYSROOT}/sys-root/mingw")
    set(CMAKE_FIND_ROOT_PATH "${MINGW_SYSROOT}/sys-root/mingw")
else()
    set(CMAKE_FIND_ROOT_PATH "${MINGW_SYSROOT}")
endif()

# Search headers/libs only in the target sysroot, programs on the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt6 AUTOMOC/AUTOUIC/AUTORCC need host tools (moc, uic, rcc) — these run on Linux.
# Fedora cross packages put host tools at /usr/lib64/qt6/bin or /usr/bin.
# On Arch, they're at /usr/lib/qt6/bin.
# Override with -DQT_HOST_PATH=/path if needed.
if(NOT DEFINED QT_HOST_PATH)
    find_program(_HOST_QT6_MOC moc
        PATHS
            /usr/lib64/qt6/bin
            /usr/lib/qt6/bin
            /usr/bin
        NO_DEFAULT_PATH)
    if(_HOST_QT6_MOC)
        get_filename_component(_HOST_QT6_BIN_DIR "${_HOST_QT6_MOC}" DIRECTORY)
        get_filename_component(QT_HOST_PATH "${_HOST_QT6_BIN_DIR}/../.." ABSOLUTE)
    else()
        set(QT_HOST_PATH /usr)
    endif()
    message(STATUS "QT_HOST_PATH auto-detected: ${QT_HOST_PATH}")
endif()
