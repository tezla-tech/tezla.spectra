tezla_add_plugin(
    NAME            Emberdrive
    PRODUCT_NAME    "Tezla Emberdrive"
    PLUGIN_CODE     Tzem
    DESCRIPTION     "Saturation, wavefolder, bit crusher, feedback and three-band limiter"
    VERSION         0.4.0
    VST3_CATEGORIES Fx Distortion Dynamics
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/EmberdriveEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
