# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

# ==============================================================================
#  tezla.spectra — top-level build
#
#  Builds the framework-free DSP library and its tests/tools with no external
#  dependencies at all, and optionally builds any subset of the VST3 plugins
#  (which pull in JUCE via FetchContent).
#
#  Common invocations:
#    cmake -B build -DCMAKE_BUILD_TYPE=Release
#    cmake -B build -DTEZLA_PLUGINS=NONE          # DSP + tests only, no JUCE
#    cmake -B build -DTEZLA_PLUGINS="Foo;Bar"     # a specific list
#  See docs/BUILD.md, or just use scripts/build.ps1.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------- macOS ------
# Both of these must be set BEFORE project(), because CMake bakes them into the
# compiler checks it runs there. Setting them afterwards silently does nothing.
if(APPLE)
    option(TEZLA_UNIVERSAL_BINARY "Build a universal arm64 + x86_64 binary on macOS" ON)

    if(TEZLA_UNIVERSAL_BINARY AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        # One bundle that runs natively on Apple Silicon and on Intel. Roughly
        # doubles compile time and binary size; the alternative is shipping two
        # downloads and having people pick the wrong one.
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures to build")
    endif()

    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        # 11.0 is the first release with Apple Silicon, so a universal binary
        # cannot usefully target anything older on the arm64 side anyway.
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

project(TezlaSpectra
    VERSION 0.1.0
    DESCRIPTION "tezla.spectra plugin workshop"
    # C as well as C++: JUCE vendors zlib, libpng and friends as C, and refuses
    # to configure without it.
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators default to an unoptimised build, which is a trap for
# a DSP project: never judge CPU cost or even correctness from a debug build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ------------------------------------------------------------------ options --
set(TEZLA_PLUGINS "ALL" CACHE STRING
    "Which plugins to build: ALL, NONE, or a semicolon-separated list of folder names under plugins/")
option(TEZLA_BUILD_TESTS      "Build the framework-free DSP unit tests"       ON)
option(TEZLA_BUILD_TOOLS      "Build the offline measurement tools"           ON)
option(TEZLA_BUILD_STANDALONE "Also build a standalone app for each plugin"   OFF)
option(TEZLA_BUILD_RENDER     "Also build an offline renderer for each plugin" OFF)
option(TEZLA_COPY_AFTER_BUILD "Copy built VST3 bundles to the system VST3 folder" OFF)
option(TEZLA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"           OFF)
option(TEZLA_ENABLE_AVX2      "Build with AVX2 (needs a 2013-or-newer CPU)"   OFF)
option(TEZLA_BUILD_AU         "Also build Audio Unit versions (macOS only)"   ON)

# **Off, deliberately -- and measured, not assumed.**
#
# What LTO buys is cross-translation-unit inlining. This tree's DSP is
# header-only by design (CLAUDE.md section 4), so every audio hot path is
# already visible to the compiler inside the one translation unit that
# instantiates it, and there is nothing left across a boundary to inline.
# Measured on 2026-09-01, GCC 13.3 on x86-64, the six CPU-budget tests with
# and without `-flto=auto`, five interleaved runs each, best of five:
#
#     ferrite, one stereo instance     0.1565 s   0.1568 s   x1.002
#     16 bowed voices (Malleus)        0.1972 s   0.1983 s   x1.006
#     phonoss full strip, stereo       0.0143 s   0.0143 s   x1.000
#     64 modes (resonator)             0.0036 s   0.0036 s   x1.000
#     dead engine                      0.0039 s   0.0039 s   x1.000
#     lowpass gate                     0.0027 s   0.0028 s   x1.037 (0.1 ms: noise)
#
# No runtime gain at all. That is GCC; MSVC's /GL+/LTCG on the Windows rig has
# not been measured from here and should be, in FL Studio's CPU meter, before
# anyone believes a different answer for it -- but the structural reason
# applies to every compiler.
#
# What it costs is real. JUCE's recommended flag is a plain `-flto`: serial
# LTRANS on GCC, and on Apple clang *monolithic* LTO -- the whole program
# merged into one module and optimised on one core at link time, per format,
# twice over for a universal binary. CI run 38 spent **six hours and four
# minutes** there before being cancelled. The parallel forms (`-flto=auto`,
# `-flto=thin`) are what this tree passes when asked, and they are still a
# multiple of a normal link.
#
# So: off during development, where the numbers are being measured and every
# build is a wait; available as an option for a release build, applied through
# tezla::compiler-options so tests, tools and plugins all get it alike (see
# cmake/TezlaCompilerOptions.cmake); `-lto` / `--lto` in the build scripts.
option(TEZLA_LTO              "Link-time optimisation (slow to link; measured no runtime gain here)" OFF)

include(TezlaCompilerOptions)

# ------------------------------------------------------ framework-free core --
# Deliberately first, and deliberately free of any dependency: this is the part
# that must build and be measurable anywhere, including CI with no GUI stack.
add_subdirectory(shared/tezla-dsp)

# tools/ always defines the header-only measurement library (the tests use it);
# TEZLA_BUILD_TOOLS only controls whether the CLI executable is built.
add_subdirectory(tools)

if(TEZLA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ----------------------------------------------------------- plugin targets --
# Any folder under plugins/ containing a CMakeLists.txt is a plugin.
set(TEZLA_AVAILABLE_PLUGINS "")
file(GLOB _tezla_plugin_candidates RELATIVE "${CMAKE_CURRENT_LIST_DIR}/plugins" "${CMAKE_CURRENT_LIST_DIR}/plugins/*")
foreach(_candidate IN LISTS _tezla_plugin_candidates)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/plugins/${_candidate}/CMakeLists.txt")
        list(APPEND TEZLA_AVAILABLE_PLUGINS "${_candidate}")
    endif()
endforeach()
list(SORT TEZLA_AVAILABLE_PLUGINS)

if(TEZLA_AVAILABLE_PLUGINS)
    message(STATUS "Tezla: plugins available -- ${TEZLA_AVAILABLE_PLUGINS}")
else()
    message(STATUS "Tezla: no plugins in plugins/ yet")
endif()

if(TEZLA_PLUGINS STREQUAL "ALL")
    set(_tezla_selected "${TEZLA_AVAILABLE_PLUGINS}")
elseif(TEZLA_PLUGINS STREQUAL "NONE" OR TEZLA_PLUGINS STREQUAL "")
    set(_tezla_selected "")
else()
    # Accept commas as well as semicolons, so -DTEZLA_PLUGINS=Foo,Bar works.
    string(REPLACE "," ";" _tezla_selected "${TEZLA_PLUGINS}")
    foreach(_name IN LISTS _tezla_selected)
        if(NOT _name IN_LIST TEZLA_AVAILABLE_PLUGINS)
            message(FATAL_ERROR
                "Unknown plugin '${_name}'.\n"
                "Available: ${TEZLA_AVAILABLE_PLUGINS}\n"
                "Use -DTEZLA_PLUGINS=ALL, =NONE, or a list of the names above.")
        endif()
    endforeach()
endif()

if(_tezla_selected)
    message(STATUS "Tezla: building plugins -- ${_tezla_selected}")
    include(FetchJUCE)
    include(TezlaPlugin)
    foreach(_name IN LISTS _tezla_selected)
        add_subdirectory("plugins/${_name}")
    endforeach()

    # Draws the shared UI components offscreen so they can be checked against
    # synthetic audio. Needs JUCE, so it only exists when plugins are being
    # built; TezlaPlugin.cmake is what defines TEZLA_ROOT_DIR.
    add_subdirectory(tools/ui-preview)
else()
    message(STATUS "Tezla: building DSP core only (no plugins selected, JUCE not fetched)")
endif()
