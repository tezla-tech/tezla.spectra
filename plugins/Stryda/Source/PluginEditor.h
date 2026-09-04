// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The F3 panel: six operator strips, the matrix, and the bandwidth readout.
//
// One page deliberately. The point of F3 is to get the instrument onto the rig
// and into the user's hands before anything else is designed around it, so the
// layout says what the engine does and nothing more.

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/PanelDesign.hpp>
#include <tezla/ui/Plate.hpp>
#include <tezla/ui/TooltipHost.hpp>

#include "PluginProcessor.h"

namespace tezla::stryda {

/// A knob plus its caption plus its attachment, which is the unit the layout
/// actually deals in.
struct Control
{
    juce::Label caption;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/// The 6x6 grid. Every cell is an index in cycles; the diagonal is the
/// operator's own feedback, which is the same idea and belongs in the same
/// square rather than hidden on a strip.
class MatrixGrid final : public juce::Component
{
public:
    MatrixGrid (StrydaProcessor& owner, ui::Palette palette);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    ui::Palette palette_;
    std::array<std::array<std::unique_ptr<Control>, kNumOperators>, kNumOperators> cells_ {};
    std::array<std::unique_ptr<Control>, kNumOperators> noise_ {};
};

class StrydaEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit StrydaEditor (StrydaProcessor& owner);
    ~StrydaEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    /// The predicted top of the spectrum, from the parameters rather than from
    /// the audio thread, so the readout is live with the transport stopped.
    [[nodiscard]] double predictedTop() const;

    /// Set by predictedTop(): whether the index cap is doing work at the moment.
    mutable bool capBiting_ { false };

    [[nodiscard]] Control& addControl (const juce::String& id,
                                       const juce::String& caption,
                                       juce::Colour tint,
                                       ui::design::Emphasis emphasis = ui::design::Emphasis::normal);

    StrydaProcessor& processor_;
    ui::Palette palette_ {};
    ui::KnobLookAndFeel lookAndFeel_ { palette_ };
    ui::TooltipHost tooltips_;

    ui::HeaderBar header_;
    MatrixGrid matrix_;

    std::vector<std::unique_ptr<Control>> controls_;

    /// Per operator, in the order they are laid out down a strip.
    std::array<std::vector<Control*>, kNumOperators> strips_ {};
    std::vector<Control*> globals_;

    juce::Label bandwidth_;
    juce::Label voices_;

    juce::ComboBox indexCapBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> indexCapAttachment_;

    juce::ComboBox presetBox_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrydaEditor)
};

} // namespace tezla::stryda
