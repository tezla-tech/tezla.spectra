# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

tezla_add_plugin(
    NAME            Ictus
    PRODUCT_NAME    "Tezla Ictus"
    PLUGIN_CODE     Tzic
    DESCRIPTION     "Drum synthesiser for drum and bass"
    VERSION         0.1.0
    IS_SYNTH
    NEEDS_MIDI_INPUT
    VST3_CATEGORIES Instrument Drum
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PlatePage.cpp
        Source/Displays.cpp
        Source/PadStrip.cpp
    DSP_SOURCES
        Dsp/IctusEngine.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
