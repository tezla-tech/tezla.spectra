# Fetches JUCE. Nothing to install by hand: this is what keeps the promise that
# a clean clone builds with only Build Tools + CMake + Git present.
#
# JUCE bundles the VST3 wrapper, so there is no separate SDK to download.
#
# Licence note: JUCE is dual-licensed AGPLv3 / commercial, with a free tier that
# covers personal and small-revenue use. See LICENSE.md in the fetched copy.
# (Steinberg's VST3 SDK itself has been MIT-licensed since v3.8, Oct 2025, so a
# direct-to-SDK build is also an option should we ever drop JUCE. Because all of
# our DSP is framework-free, that swap costs a wrapper, not a rewrite.)

include_guard(GLOBAL)
include(FetchContent)

set(TEZLA_JUCE_VERSION "9.0.1" CACHE STRING "JUCE git tag to build against")
set(TEZLA_JUCE_PATH ""        CACHE PATH   "Path to a local JUCE checkout; overrides the download")

if(TEZLA_JUCE_PATH)
    if(NOT EXISTS "${TEZLA_JUCE_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "TEZLA_JUCE_PATH is set to '${TEZLA_JUCE_PATH}' but there is no CMakeLists.txt there.")
    endif()
    message(STATUS "Tezla: using local JUCE at ${TEZLA_JUCE_PATH}")
    add_subdirectory("${TEZLA_JUCE_PATH}" "${CMAKE_BINARY_DIR}/_juce" EXCLUDE_FROM_ALL)
else()
    message(STATUS "Tezla: fetching JUCE ${TEZLA_JUCE_VERSION} (first configure only; it is cached in the build folder)")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        "${TEZLA_JUCE_VERSION}"
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
