// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The Ictus editor at I2: the shared header (output, oversampling, render
// quality, A/B, tooltips), a pad strip with a HIT button so the kick can be
// auditioned without a keyboard, and one page of Kick 1's controls in the
// house look. The pad grid and the per-pad pages arrive with the other
// engines (I9).

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>

#include "PluginProcessor.h"

namespace tezla::ictus {

/// A grid of house controls, each attached to a parameter: knobs, choices and
/// lamp switches (never a tick box -- CLAUDE.md section 8).
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                 int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name,
                  const juce::String& tooltip);
    /// `legend` is what the lamp itself says -- a word, since the cap is
    /// 64 px wide -- under the full `name` above it.
    void addSwitch (const char* parameterId, const juce::String& name,
                    const juce::String& legend, const juce::String& tooltip);
    void addGap();

    void setNote (const juce::String& note);
    void setControlEnabled (const char* parameterId, bool enabled);

    [[nodiscard]] juce::Colour nameColour() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Switch
    {
        juce::String id;
        std::unique_ptr<ui::LampButton> button;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, lamp, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Switch>> switches_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

class IctusEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit IctusEditor (IctusProcessor& processor);
    ~IctusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPage();
    void refreshHeaderTooltips();
    void updateGreying();

    IctusProcessor& ictus_;

    ui::TooltipHost tooltips_ { *this };
    ui::Palette palette_;
    ui::KnobLookAndFeel knobLook_ { palette_ };

    std::unique_ptr<ui::HeaderBar> header_;

    // The pad strip: the pad's name and note, a HIT button, and how many hits
    // are sounding.
    juce::Label padLabel_;
    juce::TextButton hitButton_ { "HIT" };
    juce::Label hitsLabel_;

    std::unique_ptr<ControlPage> page_;

    int shownFactor_ { -1 };
    bool shownOffline_ { false };
    int shownRender_ { -1 };
    int shownOversampling_ { -1 };
    int shownRateHz_ { 0 };
    bool shownToneOn_ { true };
    bool shownHarmonics_ { true };
    bool shownTail_ { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IctusEditor)
};

} // namespace tezla::ictus
