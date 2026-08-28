// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The house colours, and the one knob each plugin turns.
//
// Every plugin shares the same dark panel, the same dim label grey and the same
// meter behaviour; what distinguishes one from another is its accent. Emberdrive
// glows ember orange, Halo glows gold. Passing a Palette rather than hard-coding
// colours in each editor is what lets the shared components below carry a
// plugin's identity without knowing which plugin they are in.

#include <juce_gui_basics/juce_gui_basics.h>

namespace tezla::ui
{

struct Palette
{
    juce::Colour background { 0xff141416 };
    juce::Colour panel      { 0xff1d1d20 };
    juce::Colour text       { 0xffd8d5cf };
    juce::Colour dimText    { 0xff86837e };

    /// The plugin's own colour. Knobs, meters and the header rule use it.
    juce::Colour accent       { 0xffd8722c };
    juce::Colour accentBright { 0xfff2a03d };

    /// A second colour for a reading that is not a level -- gain reduction on a
    /// compressor, added harmonics on an exciter. Deliberately not the accent,
    /// so the two cannot be confused at a glance.
    juce::Colour secondary { 0xff54c7c0 };

    /// Bypass is the same warning orange in every plugin, whatever the accent.
    /// It is the one control whose state a user must be able to read from across
    /// the room, and a per-plugin colour would make it a guess.
    juce::Colour bypassGlow { 0xffff7a18 };

    /// A level that went over, which is a different statement from "bypassed"
    /// and must not share its colour. Appended rather than inserted: the
    /// existing plugins initialise this struct positionally, and moving a field
    /// would silently repaint every one of them.
    juce::Colour over { 0xffe2483d };

    /// A reading that has been frozen rather than one that is live -- a
    /// permanent peak hold, a captured maximum. Deliberately outside the
    /// green/teal/red family the live curves already use, because its whole
    /// job is to be told apart from them at a glance. Appended, for the reason
    /// `over` was: these structs are initialised positionally.
    juce::Colour hold { 0xffab9bf5 };
};

} // namespace tezla::ui
