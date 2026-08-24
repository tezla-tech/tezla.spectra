tezla_add_plugin(
    NAME            Emberdrive
    PRODUCT_NAME    "Tezla Emberdrive"
    PLUGIN_CODE     Tzem
    DESCRIPTION     "Tube and tape saturation into a soft-knee limiter"
    VST3_CATEGORIES Fx Distortion Dynamics
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/EmberdriveEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
