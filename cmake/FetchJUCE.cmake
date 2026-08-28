# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

# ==============================================================================
#  Where JUCE comes from.
#
#  Three ways, in order of precedence. All of them end with the juce::* targets
#  available; nothing downstream cares which was used.
#
#   1. A source tree you already have          -DTEZLA_JUCE_PATH=C:/dev/JUCE
#      (or the JUCE_PATH / TEZLA_JUCE_PATH environment variable)
#
#   2. A JUCE you installed with `cmake --install`   -DTEZLA_JUCE_SOURCE=System
#      Found through find_package, so CMAKE_PREFIX_PATH or JUCE_DIR applies.
#
#   3. Downloaded and pinned, the default. Needs nothing installed by hand,
#      which is what keeps the promise that a clean clone just builds.
#
#  JUCE bundles the VST3 wrapper, so there is no separate Steinberg SDK to
#  install in any of the three cases.
#
#  Licence note: JUCE is dual-licensed AGPLv3 / commercial, with a free tier
#  covering personal and small-revenue use. Whichever route you use, the licence
#  is JUCE's LICENSE.md, not this project's. (Steinberg's VST3 SDK has itself
#  been MIT-licensed since v3.8, October 2025, so a direct-to-SDK build is an
#  option if we ever drop JUCE. Because all our DSP is framework-free, that swap
#  costs a wrapper rather than a rewrite.)
#
#  See docs/BUILD.md section 4 for the step-by-step version of all of this.
# ==============================================================================

include_guard(GLOBAL)
include(FetchContent)

set(TEZLA_JUCE_VERSION "9.0.1" CACHE STRING
    "JUCE version to download and the version this project is tested against")
set(TEZLA_JUCE_MINIMUM_VERSION "8.0.0")

set(TEZLA_JUCE_PATH "" CACHE PATH
    "Path to an existing JUCE source tree. Overrides downloading. Also read from the JUCE_PATH environment variable.")

set(TEZLA_JUCE_SOURCE "Auto" CACHE STRING
    "Where to get JUCE: Auto, Path, System (find_package), or Fetch (download)")
set_property(CACHE TEZLA_JUCE_SOURCE PROPERTY STRINGS Auto Path System Fetch)

# ------------------------------------------------------------ version read --
# JUCE's own project() call sets JUCE_VERSION, but add_subdirectory (which both
# the Path and Fetch routes end in) puts that in a child scope where we cannot
# see it. Read it out of the file instead.
# Rejects a version we know cannot work, with a message that says what to do.
# Called before add_subdirectory, so an old JUCE fails here rather than as a
# wall of template errors from inside JUCE's own headers.
function(_tezla_require_juce_version version)
    if(NOT version)
        return()
    endif()

    if(version VERSION_LESS TEZLA_JUCE_MINIMUM_VERSION)
        message(FATAL_ERROR
            "JUCE ${version} is too old.\n"
            "This project needs at least ${TEZLA_JUCE_MINIMUM_VERSION} -- it uses "
            "juce::FontOptions and the AudioParameter*Attributes API -- and is tested "
            "against ${TEZLA_JUCE_VERSION}.\n"
            "Either update that JUCE, or drop TEZLA_JUCE_PATH / TEZLA_JUCE_SOURCE so "
            "CMake downloads the pinned version into the build folder instead.")
    endif()
endfunction()

function(_tezla_read_juce_version sourceDir outVariable)
    set(${outVariable} "" PARENT_SCOPE)

    if(NOT EXISTS "${sourceDir}/CMakeLists.txt")
        return()
    endif()

    file(READ "${sourceDir}/CMakeLists.txt" _contents)
    if(_contents MATCHES "project[ \t]*\\([ \t]*JUCE[ \t]+VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(${outVariable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------- resolve a path --
# An environment variable means someone who keeps JUCE in one place does not
# have to remember a -D flag on every fresh build folder.
set(_tezla_juce_path "${TEZLA_JUCE_PATH}")
if(NOT _tezla_juce_path AND DEFINED ENV{TEZLA_JUCE_PATH})
    set(_tezla_juce_path "$ENV{TEZLA_JUCE_PATH}")
    message(STATUS "Tezla: JUCE path taken from the TEZLA_JUCE_PATH environment variable")
endif()
if(NOT _tezla_juce_path AND DEFINED ENV{JUCE_PATH})
    set(_tezla_juce_path "$ENV{JUCE_PATH}")
    message(STATUS "Tezla: JUCE path taken from the JUCE_PATH environment variable")
endif()

if(_tezla_juce_path)
    file(TO_CMAKE_PATH "${_tezla_juce_path}" _tezla_juce_path)
endif()

# ------------------------------------------------------------ decide which --
if(TEZLA_JUCE_SOURCE STREQUAL "Auto")
    if(_tezla_juce_path)
        set(_tezla_juce_mode "Path")
    else()
        set(_tezla_juce_mode "Fetch")
    endif()
else()
    set(_tezla_juce_mode "${TEZLA_JUCE_SOURCE}")
endif()

# ------------------------------------------------------------------- apply --
if(_tezla_juce_mode STREQUAL "Path")
    if(NOT _tezla_juce_path)
        message(FATAL_ERROR
            "TEZLA_JUCE_SOURCE=Path but no path was given.\n"
            "Pass -DTEZLA_JUCE_PATH=/path/to/JUCE, or set the JUCE_PATH environment variable.")
    endif()

    # Checking for a CMakeLists.txt is not enough to identify a JUCE root:
    # JUCE's own modules/ subfolder has one too, and pointing at it configures
    # happily and then fails with "Unknown CMake command juce_add_modules",
    # which tells the user nothing. These two files exist only at the top of a
    # JUCE tree and are exactly what this build needs from it.
    if(NOT EXISTS "${_tezla_juce_path}/extras/Build/CMake/JUCEUtils.cmake"
       OR NOT EXISTS "${_tezla_juce_path}/modules/juce_core/juce_core.h")
        message(FATAL_ERROR
            "'${_tezla_juce_path}' is not the top of a JUCE source tree.\n"
            "It should be the folder that directly contains JUCE's modules/ and extras/ "
            "directories -- the root of the JUCE repository -- not modules/ itself and not "
            "a subfolder of it.\n"
            "If you do not have JUCE, leave TEZLA_JUCE_PATH unset and CMake will download "
            "the pinned version (${TEZLA_JUCE_VERSION}) into the build folder.\n"
            "See docs/BUILD.md section 4.")
    endif()

    message(STATUS "Tezla: using the JUCE source tree at ${_tezla_juce_path}")
    _tezla_read_juce_version("${_tezla_juce_path}" _tezla_juce_found_version)
    _tezla_require_juce_version("${_tezla_juce_found_version}")
    add_subdirectory("${_tezla_juce_path}" "${CMAKE_BINARY_DIR}/_juce" EXCLUDE_FROM_ALL)

elseif(_tezla_juce_mode STREQUAL "System")
    find_package(JUCE ${TEZLA_JUCE_MINIMUM_VERSION} CONFIG REQUIRED)
    message(STATUS "Tezla: using the installed JUCE found at ${JUCE_DIR}")
    set(_tezla_juce_found_version "${JUCE_VERSION}")

elseif(_tezla_juce_mode STREQUAL "Fetch")
    message(STATUS "Tezla: downloading JUCE ${TEZLA_JUCE_VERSION} "
                   "(first configure in this build folder only; it is cached afterwards)")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        "${TEZLA_JUCE_VERSION}"
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
    _tezla_read_juce_version("${juce_SOURCE_DIR}" _tezla_juce_found_version)

else()
    message(FATAL_ERROR
        "TEZLA_JUCE_SOURCE is '${TEZLA_JUCE_SOURCE}'. Valid values are Auto, Path, System, Fetch.")
endif()

# ----------------------------------------------------------- sanity checks --
if(NOT TARGET juce::juce_audio_processors)
    message(FATAL_ERROR
        "JUCE was located but its targets are missing. That usually means the path points at "
        "something other than a JUCE source tree, or the install is incomplete.")
endif()

# Checking the version catches the case that would otherwise waste an hour: a
# JUCE old enough to be missing something this project uses, failing later as a
# wall of template errors inside JUCE's own headers.
if(_tezla_juce_found_version)
    _tezla_require_juce_version("${_tezla_juce_found_version}")

    if(NOT _tezla_juce_found_version VERSION_EQUAL TEZLA_JUCE_VERSION)
        message(WARNING
            "Using JUCE ${_tezla_juce_found_version}; this project is pinned to and tested "
            "against ${TEZLA_JUCE_VERSION}. Supported, but untested -- if the build fails "
            "inside JUCE's own headers, try the pinned version before assuming the plugin "
            "is at fault.")
    endif()

    message(STATUS "Tezla: JUCE ${_tezla_juce_found_version}")
else()
    message(STATUS "Tezla: JUCE version could not be determined; skipping the version check")
endif()
