# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

tezla_add_plugin(
    NAME            Phonoss
    PRODUCT_NAME    "Tezla Phonoss"
    PLUGIN_CODE     Tzps
    DESCRIPTION     "Vocal channel strip: gate, de-esser, two compressors, tone"
    VERSION         0.1.0
    VST3_CATEGORIES Fx Dynamics EQ
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
