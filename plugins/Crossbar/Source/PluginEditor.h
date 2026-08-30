// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>

#include "PluginProcessor.h"

namespace tezla::crossbar {

/// Wraps, so the "what this is doing right now" sentence is never truncated.
class WrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

/// One page of the control surface. The house grid, copied from Ferrite --
/// per-plugin rather than shared, because every plugin ends up wanting one
/// thing from it that the others do not.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette, int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addChoice (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addGap();

    /// A line of guidance under the grid, for the things too important to
    /// leave in a tooltip nobody hovers over.
    void setNote (const juce::String& note);

    /// Greys a control out. A knob that moves and does nothing reads as a
    /// broken plugin.
    void setControlEnabled (const char* parameterId, bool enabled);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::String   id;
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, choice, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

class CrossbarEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit CrossbarEditor (CrossbarProcessor& processorToUse);
    ~CrossbarEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    /// Greys the controls the current settings make inert, and refreshes the
    /// notes. Only when something actually changed -- recomputing this fifteen
    /// times a second would repaint the panel for nothing.
    void updateForSwitches();

    CrossbarProcessor& crossbar_;

    ui::TooltipHost tooltips_ { *this };
    ui::Palette palette_;

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label vendorLabel_;
    juce::Label voicesLabel_;
    juce::ToggleButton tooltipsButton_ { "?" };

    static constexpr int kNumPages = 3;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    /// What the panel is currently dressed for, so the greying and the notes
    /// are not recomputed every tick. Deliberately impossible starting values,
    /// so the first tick always applies the state.
    int shownCodec_ { -1 };
    int shownRate_ { -1 };
    int shownBand_ { -1 };
    int shownDialMode_ { -1 };
    int shownCadence_ { -1 };
    double shownEffectiveRate_ { -1.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossbarEditor)
};

} // namespace tezla::crossbar
