# tezla::dsp -- header-only, framework-free C++20 DSP.
#
# No JUCE, no VST3 headers, no platform code, no dependencies. This is what lets
# the DSP be compiled and *measured* offline, on any machine, with no DAW.

add_library(tezla_dsp INTERFACE)
add_library(tezla::dsp ALIAS tezla_dsp)

target_include_directories(tezla_dsp INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include")

target_compile_features(tezla_dsp INTERFACE cxx_std_20)

# Header-only, but list the headers so IDEs show them in the target.
file(GLOB_RECURSE _tezla_dsp_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp")
target_sources(tezla_dsp INTERFACE FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
    FILES ${_tezla_dsp_headers})
