# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

# tezla_add_plugin() — one call per plugin, so every plugin gets identical
# vendor identity, formats, warning levels and JUCE configuration.
#
#   tezla_add_plugin(
#       NAME            Foo                     # target + folder name, PascalCase
#       PRODUCT_NAME    "Tezla Foo"             # what the DAW shows
#       PLUGIN_CODE     Tzfo                    # 4 chars, unique; register it in plugins/README.md
#       DESCRIPTION     "Short one-liner"
#       VST3_CATEGORIES Fx Distortion
#       SOURCES         Source/a.cpp Source/b.cpp
#       DSP_SOURCES     Dsp/x.cpp               # framework-free; also fed to the test build
#       LINK_LIBS       something::extra
#       IS_SYNTH                                # optional flag
#   )

include_guard(GLOBAL)

# Where the repository root is, as seen from this file. Everything shared is
# addressed relative to it rather than to whichever plugin folder happens to be
# calling.
get_filename_component(TEZLA_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(tezla_add_plugin)
    set(_flags   IS_SYNTH NEEDS_MIDI_INPUT)
    set(_single  NAME PRODUCT_NAME PLUGIN_CODE DESCRIPTION VERSION)
    set(_multi   SOURCES DSP_SOURCES VST3_CATEGORIES LINK_LIBS INCLUDE_DIRS)
    cmake_parse_arguments(ARG "${_flags}" "${_single}" "${_multi}" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "tezla_add_plugin: NAME is required")
    endif()
    string(LENGTH "${ARG_PLUGIN_CODE}" _code_len)
    if(NOT _code_len EQUAL 4)
        message(FATAL_ERROR
            "tezla_add_plugin(${ARG_NAME}): PLUGIN_CODE must be exactly 4 characters "
            "(got '${ARG_PLUGIN_CODE}'). It forms the plugin's unique ID -- pick an "
            "unused one and record it in plugins/README.md.")
    endif()

    if(NOT ARG_PRODUCT_NAME)
        set(ARG_PRODUCT_NAME "${ARG_NAME}")
    endif()
    if(NOT ARG_VERSION)
        set(ARG_VERSION "${PROJECT_VERSION}")
    endif()
    if(NOT ARG_VST3_CATEGORIES)
        if(ARG_IS_SYNTH)
            set(ARG_VST3_CATEGORIES Instrument Synth)
        else()
            set(ARG_VST3_CATEGORIES Fx)
        endif()
    endif()

    if(ARG_IS_SYNTH)
        set(_au_main_type kAudioUnitType_MusicDevice)
    else()
        set(_au_main_type kAudioUnitType_Effect)
    endif()

    set(_formats VST3)

    # Audio Unit, macOS only. Logic Pro and GarageBand load nothing else, so on
    # a Mac this is not optional in practice -- and it is the same DSP, the same
    # editor and the same presets behind a second wrapper.
    if(APPLE AND TEZLA_BUILD_AU)
        list(APPEND _formats AU)
    endif()

    if(TEZLA_BUILD_STANDALONE)
        list(APPEND _formats Standalone)
    endif()

    juce_add_plugin(${ARG_NAME}
        COMPANY_NAME              "Tezla Tech"
        COMPANY_WEBSITE           "https://tezla.tech"
        BUNDLE_ID                 "tech.tezla.${ARG_NAME}"
        PLUGIN_MANUFACTURER_CODE  Tzla
        PLUGIN_CODE               "${ARG_PLUGIN_CODE}"
        PRODUCT_NAME              "${ARG_PRODUCT_NAME}"
        DESCRIPTION               "${ARG_DESCRIPTION}"
        VERSION                   "${ARG_VERSION}"
        FORMATS                   ${_formats}
        VST3_CATEGORIES           ${ARG_VST3_CATEGORIES}
        # Apple requires the manufacturer code to contain at least one character
        # that is not lower case; "Tzla" and the plugin codes all satisfy that.
        AU_MAIN_TYPE              ${_au_main_type}
        IS_SYNTH                  ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT          ${ARG_NEEDS_MIDI_INPUT}
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   ${TEZLA_COPY_AFTER_BUILD})

    # The shared UI compiles into every plugin target rather than into a library
    # of its own. JUCE's module defines are generated per plugin target by
    # juce_add_plugin, so a separate static library would have to have them
    # reconstructed by hand and would drift the first time one changed. Compiled
    # in, these sources inherit the plugin's configuration exactly.
    file(GLOB _tezla_ui_sources CONFIGURE_DEPENDS
        "${TEZLA_ROOT_DIR}/shared/tezla-ui/Source/*.cpp")

    target_sources(${ARG_NAME} PRIVATE ${ARG_SOURCES} ${ARG_DSP_SOURCES} ${_tezla_ui_sources})

    target_compile_definitions(${ARG_NAME} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        # Parameter changes must be reported to the host from the audio thread
        # without allocating; these two keep JUCE's own behaviour predictable.
        JUCE_COREGRAPHICS_DRAW_ASYNC=1)

    target_include_directories(${ARG_NAME} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
        "${TEZLA_ROOT_DIR}/shared/tezla-ui/include"
        ${ARG_INCLUDE_DIRS})

    target_link_libraries(${ARG_NAME} PRIVATE
        tezla::dsp
        tezla::compiler-options
        ${ARG_LINK_LIBS}
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # LTO is NOT applied here, and deliberately not through JUCE's
    # `juce_recommended_lto_flags`: that target passes a plain `-flto`, serial
    # on GCC and monolithic on Apple clang, and it cost CI six hours on macOS
    # before TEZLA_LTO became a choice. When TEZLA_LTO is on, the parallel
    # per-compiler flags arrive through tezla::compiler-options above -- the
    # same target tezla-tests and tezla-measure link, so the instrument and the
    # plugin are built the same way. See cmake/TezlaCompilerOptions.cmake.

    set_target_properties(${ARG_NAME} PROPERTIES FOLDER "Plugins")

    # An offline renderer for the plugin's *JUCE layer*.
    #
    # tezla-measure exercises the engines, which leaves the wrapper untested --
    # parameter plumbing, block handling, bypass mixing and modulation all live
    # above the engine and none of it is reachable from a framework-free test.
    # This runs the real processor over a test signal and writes raw doubles, so
    # two builds can be compared with cmp.
    #
    # Off by default: it links the whole plugin again, which is not free.
    if(TEZLA_BUILD_RENDER)
        juce_add_console_app(${ARG_NAME}Render PRODUCT_NAME "tezla-render-${ARG_NAME}")

        target_sources(${ARG_NAME}Render PRIVATE "${TEZLA_ROOT_DIR}/tools/render/main.cpp")

        # The plugin's own Dsp/ directory, so the tool can reach a
        # framework-free header the plugin also uses. One main.cpp serves every
        # plugin, so anything plugin-specific in it is guarded with
        # `__has_include` and simply is not compiled where the header is absent
        # -- which is how `tezla-render dice` exists for Sonitus and nowhere
        # else without a per-plugin source file.
        target_include_directories(${ARG_NAME}Render PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}"
            ${ARG_INCLUDE_DIRS})

        target_link_libraries(${ARG_NAME}Render PRIVATE
            ${ARG_NAME}
            tezla::compiler-options
            juce::juce_audio_utils
            juce::juce_dsp
            PUBLIC
            juce::juce_recommended_config_flags)

        set_target_properties(${ARG_NAME}Render PROPERTIES FOLDER "Tools")
    endif()

    # The DSP half is also compiled into the test build, where it is measured
    # without JUCE anywhere near it. Expose it for tests/CMakeLists.txt.
    if(ARG_DSP_SOURCES)
        set(_abs "")
        foreach(_s IN LISTS ARG_DSP_SOURCES)
            list(APPEND _abs "${CMAKE_CURRENT_LIST_DIR}/${_s}")
        endforeach()
        set_property(GLOBAL APPEND PROPERTY TEZLA_PLUGIN_DSP_SOURCES ${_abs})
    endif()
    set_property(GLOBAL APPEND PROPERTY TEZLA_PLUGIN_DSP_DIRS "${CMAKE_CURRENT_LIST_DIR}")
endfunction()
