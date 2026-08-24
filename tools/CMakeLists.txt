# Offline measurement. These are how a DSP claim gets checked before it reaches
# a DAW: frequency response, harmonic distortion, aliasing, null tests.
# Results go to CSV so two commits can be diffed.

add_library(tezla_measure INTERFACE)
add_library(tezla::measure ALIAS tezla_measure)
target_include_directories(tezla_measure INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(tezla_measure INTERFACE cxx_std_20)

if(TEZLA_BUILD_TOOLS)
    # Same trick as tests/: the plugins' DSP is framework-free, so it links
    # straight into the measurement tool with no JUCE involved.
    file(GLOB _plugin_dsp_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../plugins/*/Dsp/*.cpp")
    file(GLOB _plugin_dsp_dirs    CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../plugins/*/Dsp")

    add_executable(tezla-measure measure_main.cpp ${_plugin_dsp_sources})
    target_include_directories(tezla-measure PRIVATE ${_plugin_dsp_dirs})
    target_link_libraries(tezla-measure PRIVATE
        tezla::dsp
        tezla::measure
        tezla::compiler-options)
    set_target_properties(tezla-measure PROPERTIES FOLDER "Tools")
endif()
