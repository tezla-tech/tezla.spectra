# Copyright (c) 2026 The Tezla <thetezla@proton.me>
# Created by The Tezla -- https://github.com/wingit33/tezla.tech
# Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
# Built with development assistance from Claude (Anthropic).
# SPDX-License-Identifier: AGPL-3.0-only
# GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

tezla_add_plugin(
    NAME            Membrana
    PRODUCT_NAME    "Tezla Membrana"
    PLUGIN_CODE     Tzmb
    DESCRIPTION     "The microphone stage: capsule, position, presence, detail"
    VERSION         0.1.0
    VST3_CATEGORIES Fx EQ Dynamics
    SOURCES
        Source/PluginProcessor.cpp
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/Dsp")
