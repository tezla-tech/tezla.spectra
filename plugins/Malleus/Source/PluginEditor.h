// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The panel. Bone ivory against Anvil's steel -- the hammer bone.
//
// The page grid follows Ferrite's ControlPage (knobs, choices, a note under
// the grid carrying the measured numbers), because a player moving between
// these plugins should not have to learn a second layout. What is new here
// is the MODE STACK: the object's own partials drawn as lines, with the
// loaded scale's degrees ghosted behind them as Overtone Lock rises.
//
// That picture is the instrument's whole argument in one control, so it
// lives above the tabs and stays on screen on every page except TUNING
// (which needs the room, and where the scale itself is the subject).

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/TuningPanel.hpp>

#include "PluginProcessor.h"

namespace tezla::malleus {

/// One page of the control surface: its own controls on a grid, plus a note
/// underneath for the things too important to leave in a tooltip.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                 int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name,
                  const juce::String& tooltip);
    void addChoice (const char* parameterId, const juce::String& name,
                    const juce::String& tooltip);
    void addGap();

    void setNote (const juce::String& note);

    /// Greys a control out. A knob that moves and does nothing reads as a
    /// broken plugin, so the controls a setting makes inert say so.
    void setControlEnabled (const char* parameterId, bool enabled);

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

    struct Choice
    {
        juce::String id;
        juce::ComboBox box;
        juce::Label label;
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

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

/// The identity picture: what the object's partials ARE, and where the
/// scale would put them.
///
/// Log-frequency axis, 20 Hz to 20 kHz. Each sounding partial is a line
/// whose height falls with its index (the ear's own ordering of them);
/// behind them, the scale's degrees are replicated across every repeat of
/// the lattice and drawn as faint verticals whose opacity follows Overtone
/// Lock. At lock 0 the ghosts are invisible and the partials sit wherever
/// the physics put them; at lock 1 every line stands on a ghost. Turning
/// the knob is watching an object be pulled into a tuning.
class ModeStackView final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    ModeStackView (MalleusProcessor& processor, ui::Palette palette);

    /// Re-reads the stack and the scale. Repaints only when something moved
    /// -- this runs at 15 Hz and the panel behind it is expensive.
    void refresh();

    void paint (juce::Graphics&) override;

private:
    [[nodiscard]] static float xForHz (double hz, juce::Rectangle<float> area);

    MalleusProcessor& processor_;
    ui::Palette palette_;

    std::vector<double> modes_;
    std::vector<double> degrees_;   // ghosted lattice, Hz
    double lockAmount_ { 0.0 };
    juce::String caption_;
};

class MalleusEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit MalleusEditor (MalleusProcessor& processor);
    ~MalleusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    /// Greys the controls the current exciter makes inert. Recomputed only
    /// when the exciter actually changes.
    void updateForExciter();

    MalleusProcessor& malleus_;

    ui::TooltipHost tooltips_ { *this };
    ui::Palette palette_;

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label vendorLabel_;
    juce::Label voicesLabel_;
    juce::TextButton tooltipsButton_ { "?" };

    static constexpr int kNumPages = 4;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    std::unique_ptr<ModeStackView> modeStack_;
    ui::TuningPanel tuningPanel_;

    /// What the panel is currently dressed for. Deliberately impossible, so
    /// the first tick always applies the state.
    int shownExciter_ { -1 };
    int shownExciterB_ { -1 };

    // -1 rather than a legal value, so the first timer tick always runs the
    // greying rather than deciding nothing has changed yet.
    float shownBlend_ { -1.0f };
    float shownListen_ { -1.0f };
    int shownSympCount_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MalleusEditor)
};

} // namespace tezla::malleus
