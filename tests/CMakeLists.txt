# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

# Framework-free DSP tests. No JUCE, no external test library, no network.
# Build and run them anywhere:  cmake --build build --target tezla-tests && ctest

file(GLOB _test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")

# Each plugin's DSP is framework-free by construction, so it compiles straight
# into the test runner with no JUCE anywhere near it. This is the whole point of
# the Dsp/ and Source/ split: the part that makes the sound is measurable on any
# machine, with no DAW and no GUI stack.
file(GLOB _plugin_dsp_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../plugins/*/Dsp/*.cpp")
file(GLOB _plugin_dsp_dirs    CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../plugins/*/Dsp")

add_executable(tezla-tests
    main.cpp
    TestFramework.cpp
    ${_test_sources}
    ${_plugin_dsp_sources})

target_include_directories(tezla-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    ${_plugin_dsp_dirs})

target_link_libraries(tezla-tests PRIVATE
    tezla::dsp
    tezla::measure
    tezla::compiler-options)

set_target_properties(tezla-tests PROPERTIES FOLDER "Tests")

add_test(NAME tezla-dsp COMMAND tezla-tests)
