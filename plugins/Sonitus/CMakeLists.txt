tezla_add_plugin(
    NAME            Sonitus
    PRODUCT_NAME    "Tezla Sonitus"
    PLUGIN_CODE     Tzso
    DESCRIPTION     "Growl and reese instrument"
    VERSION         0.1.0
    IS_SYNTH
    NEEDS_MIDI_INPUT
    VST3_CATEGORIES Instrument Synth
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/SonitusEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
