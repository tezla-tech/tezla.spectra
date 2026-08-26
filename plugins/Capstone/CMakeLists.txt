tezla_add_plugin(
    NAME            Capstone
    PRODUCT_NAME    "Tezla Capstone"
    PLUGIN_CODE     Tzcp
    DESCRIPTION     "True-peak brickwall limiter and clipper"
    VERSION         0.1.0
    VST3_CATEGORIES Fx Dynamics Mastering
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/CapstoneEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
