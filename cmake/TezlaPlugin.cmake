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

    set(_formats VST3)
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
        IS_SYNTH                  ${ARG_IS_SYNTH}
        NEEDS_MIDI_INPUT          ${ARG_NEEDS_MIDI_INPUT}
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   ${TEZLA_COPY_AFTER_BUILD})

    target_sources(${ARG_NAME} PRIVATE ${ARG_SOURCES} ${ARG_DSP_SOURCES})

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
        ${ARG_INCLUDE_DIRS})

    target_link_libraries(${ARG_NAME} PRIVATE
        tezla::dsp
        tezla::compiler-options
        ${ARG_LINK_LIBS}
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    set_target_properties(${ARG_NAME} PROPERTIES FOLDER "Plugins")

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
