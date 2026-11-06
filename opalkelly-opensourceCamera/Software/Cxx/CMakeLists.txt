# Camera Example Design - C++ (FrontPanel-Platform) getting-started example
#
# Cross-platform: builds on Windows (MSVC), Linux (GCC/Clang), and macOS (Clang). The same
# sources compile everywhere; only the FrontPanel-Platform SDK layout and runtime-library
# packaging differ per OS, which this file handles automatically.
#
# Configure/build:
#   cmake --workflow --preset release          # builds the okCameraApp GUI
#
# or, with CMake older than 3.25 (no preset support):
#   cmake -S . -B build -DBUILD_GUI=ON [-DOKFP_SDK=<path to FrontPanel-Platform API dir>]
#   cmake --build build --config Release
#
# -DBUILD_GUI=ON matters: BUILD_GUI defaults OFF, so configuring without it builds only the
# backend static library. That succeeds, prints no warning, and leaves you with no application
# -- so the omission looks like a working build. The `release` preset sets it for you.
#
# OKFP_SDK must point at the SDK's "API" directory.
#
# From 6.1.0 the SDK is not laid out on disk by the installer. It ships inside the FrontPanel
# Platform application and is unpacked from within it, to a location the user chooses, as a
# single folder named for the version -- e.g. FrontPanel-DevKit-6.1.0/API -- with the headers
# and the platform library flat inside API/. That layout is the same on all three platforms.
#
# Because the unpack location is chosen by the user, the per-OS defaults below are a
# best-effort guess and will usually NOT exist; set OKFP_SDK explicitly.

cmake_minimum_required(VERSION 3.16)
# The single source of the application version. macOS reads it into Info.plist, Windows into a
# VERSIONINFO resource, and both applications report it at runtime, so a customer and support
# can always tell which build is in front of them.
project(okCamera VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)  # default for single-config generators
endif()

# --- FrontPanel-Platform (FrontPanel 6) SDK ---------------------------------------------------
# Export the FrontPanel SDK from inside the FrontPanel Platform application, then point the build at
# it: either pass -DOKFP_SDK=<path to the SDK API dir> or set the OKFP_SDK environment variable (handy
# with the CMake preset, which takes no extra flags). The per-OS fallbacks below are pre-6.1 install
# locations and will usually not exist, so OKFP_SDK is effectively required.
#
# Note OKFP_SDK is a CACHE PATH: once a configure has run, a corrected environment variable is
# ignored. Re-run with --fresh, or delete the build directory.
# (A copy under third_party/FrontPanel, if present, is used first -- it is absent from public releases.)
if(NOT DEFINED OKFP_SDK)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FrontPanel/include")
        set(_okfp_default "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FrontPanel")
    elseif(DEFINED ENV{OKFP_SDK})
        set(_okfp_default "$ENV{OKFP_SDK}")
    elseif(WIN32)
        set(_okfp_default "C:/Program Files/Opal Kelly/FrontPanel-Platform/API")
    elseif(APPLE)
        set(_okfp_default "/Applications/FrontPanel-Platform/API")
    else()
        set(_okfp_default "/opt/Opal Kelly/FrontPanel-Platform/API")
    endif()
    set(OKFP_SDK "${_okfp_default}" CACHE PATH "Path to the FrontPanel-Platform SDK")
endif()

# Locate the header and import/shared library across the bundled per-OS lib subdirs AND the
# layouts used by installed SDKs (Windows include/+lib/x64, or the flat API/ of the Linux/macOS
# bundles). Hint only the HOST's per-OS subdir: the bundled SDK carries every platform's library
# side by side, and find_library on macOS treats a Linux .so as a valid library, so an unguarded
# search would link lib/linux-x64/libokFrontPanel.so into the macOS build. The generic lib/x64,
# lib/, and root hints stay as fallbacks for the installed-SDK layouts.
find_path(OKFP_INCLUDE_DIR okFrontPanel.h HINTS "${OKFP_SDK}/include" "${OKFP_SDK}")
if(WIN32)
    set(_okfp_lib_hints "${OKFP_SDK}/lib/win-x64")
elseif(APPLE)
    set(_okfp_lib_hints "${OKFP_SDK}/lib/mac-arm64" "${OKFP_SDK}/lib/mac-x64")
else()
    set(_okfp_lib_hints "${OKFP_SDK}/lib/linux-x64")
endif()
find_library(OKFP_LIBRARY NAMES okFrontPanel
             HINTS ${_okfp_lib_hints} "${OKFP_SDK}/lib/x64" "${OKFP_SDK}/lib" "${OKFP_SDK}")
if(NOT OKFP_INCLUDE_DIR OR NOT OKFP_LIBRARY)
    message(FATAL_ERROR
        "FrontPanel-Platform SDK not found. Pass -DOKFP_SDK=<path to an SDK 'API' directory>.\n"
        "  OKFP_SDK         = ${OKFP_SDK}\n"
        "  OKFP_INCLUDE_DIR = ${OKFP_INCLUDE_DIR}\n"
        "  OKFP_LIBRARY     = ${OKFP_LIBRARY}")
endif()
get_filename_component(OKFP_LIB_DIR "${OKFP_LIBRARY}" DIRECTORY)

find_package(Threads REQUIRED)  # std::thread / sleep_for needs -pthread on Linux

# Backend static library — the FrontPanel hardware-driving stack + the GuiCameraBackend facade.
# The single source of truth for the hardware bring-up + capture flow, used by the wxWidgets GUI.
# NOMINMAX (PUBLIC) keeps windows.h (via okFrontPanel.h) from clobbering std::min/max in consumers.
add_library(okcamera_backend STATIC
    src/AxiOverClassicDataPort.cpp
    src/AxiOverAxiDataPort.cpp
    src/CapturePipelineSequencer.cpp
    src/GuiCameraBackend.cpp
)
target_compile_definitions(okcamera_backend PUBLIC NOMINMAX)
target_compile_definitions(okcamera_backend PUBLIC OKCAM_VERSION="${PROJECT_VERSION}")
target_include_directories(okcamera_backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${OKFP_INCLUDE_DIR}")
target_link_libraries(okcamera_backend PUBLIC "${OKFP_LIBRARY}" Threads::Threads)

# Include any optional CMake add-ons present under cmake/ (none in a standard checkout).
file(GLOB _addon_cmakes "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake")
foreach(_addon ${_addon_cmakes})
    include("${_addon}")
endforeach()

# --- Optional wxWidgets GUI ---------------------------------------------------------
# Off by default. Enable with -DBUILD_GUI=ON.
option(BUILD_GUI "Build the wxWidgets camera GUI (requires wxWidgets)" OFF)
if(BUILD_GUI)
    add_subdirectory(gui)
endif()
