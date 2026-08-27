tezla_add_plugin(
    NAME            Anvil
    PRODUCT_NAME    "Tezla Anvil"
    PLUGIN_CODE     Tzan
    DESCRIPTION     "Valve amplifier, speaker load and cabinet"
    VERSION         0.1.0
    VST3_CATEGORIES Fx Distortion
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/AnvilEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
