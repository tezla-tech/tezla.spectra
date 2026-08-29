// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// What a plugin provides for the shared tuning panel (TuningPanel.hpp) to
// drive it. Its own header, free of juce_gui_basics, because it is the
// PROCESSOR that implements this -- the method names are exactly the ones
// the instruments already had, so implementing it is a base-class clause,
// not a rewrite.

#include <juce_core/juce_core.h>

#include <tezla/dsp/Tuning.hpp>

namespace tezla::ui {

/// Loaders return an empty string on success, else the reason (with the
/// parser's line number) for the panel to show.
class TuningHost
{
public:
    virtual ~TuningHost() = default;

    virtual juce::String loadScalaText (const juce::String& text,
                                        const juce::String& name) = 0;
    virtual juce::String loadKeyboardMapText (const juce::String& text) = 0;
    virtual juce::String selectBuiltInScale (const juce::String& name) = 0;
    virtual void resetTuning() = 0;

    [[nodiscard]] virtual const tezla::dsp::Scale& getScale() const = 0;
    [[nodiscard]] virtual juce::String getScaleName() const = 0;

    /// One line for the top of the panel: scale, degree count, root note and
    /// its sounding frequency at the current concert pitch.
    [[nodiscard]] virtual juce::String describeTuning() const = 0;

    /// Degree 0's sounding frequency, for the table's Hz column.
    [[nodiscard]] virtual double getRootHz() const = 0;

    virtual void setConcertPitch (double hz) = 0;
    [[nodiscard]] virtual double getConcertPitch() const = 0;
};

} // namespace tezla::ui
