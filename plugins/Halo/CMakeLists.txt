tezla_add_plugin(
    NAME            Halo
    PRODUCT_NAME    "Tezla Halo"
    PLUGIN_CODE     Tzha
    DESCRIPTION     "Harmonic exciter and bass enhancer"
    VERSION         0.1.0
    VST3_CATEGORIES Fx EQ Distortion
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/HaloEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
