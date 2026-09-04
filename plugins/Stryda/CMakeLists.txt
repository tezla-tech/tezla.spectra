# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

tezla_add_plugin(
    NAME            Stryda
    PRODUCT_NAME    "Tezla Stryda"
    PLUGIN_CODE     Tzst
    DESCRIPTION     "FM synthesiser for neurofunk drum and bass"
    VERSION         0.1.0
    IS_SYNTH
    NEEDS_MIDI_INPUT
    VST3_CATEGORIES Instrument Synth
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
