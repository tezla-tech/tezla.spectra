# Offscreen renderer for the shared UI components.
#
# Only built when JUCE is present, because it draws with it. It exists so that a
# display can be checked against synthetic audio in a container with no sound
# card -- see main.cpp.

juce_add_console_app(tezla-ui-preview PRODUCT_NAME "tezla-ui-preview")

# Named rather than globbed. The other shared UI sources need
# juce_audio_processors, which a drawing preview has no business pulling in --
# the point of this tool is that a component can be rendered without a plugin
# around it.
target_sources(tezla-ui-preview PRIVATE
    main.cpp
    "${TEZLA_ROOT_DIR}/shared/tezla-ui/Source/SpectrumDisplay.cpp")

target_compile_definitions(tezla-ui-preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_include_directories(tezla-ui-preview PRIVATE
    "${TEZLA_ROOT_DIR}/shared/tezla-ui/include")

target_link_libraries(tezla-ui-preview PRIVATE
    tezla::dsp
    tezla::compiler-options
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags)

set_target_properties(tezla-ui-preview PROPERTIES FOLDER "Tools")
