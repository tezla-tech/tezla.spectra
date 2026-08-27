tezla_add_plugin(
    NAME            Transpectus
    PRODUCT_NAME    "Tezla Transpectus"
    PLUGIN_CODE     Tztr
    DESCRIPTION     "Loudness, true peak and stereo analysis"
    VERSION         0.1.0
    VST3_CATEGORIES Fx Analyzer
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/TranspectusEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
