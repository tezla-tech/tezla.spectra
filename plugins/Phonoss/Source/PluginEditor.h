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

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>

#include "PluginProcessor.h"

namespace tezla::phonoss
{

/// How far down a stage is pulling, drawn as a bar that grows **leftward from
/// the right edge**, because that is the direction gain reduction goes.
///
/// Its own component rather than `ui::LevelMeter` because it is measuring a
/// different thing: a level meter reads a signal and fills from the bottom,
/// while this reads a reduction and empties from full. Reusing the level meter
/// would mean a meter that reads full when nothing is happening.
class GainReductionBar final : public juce::Component
{
public:
    explicit GainReductionBar (ui::Palette palette) : palette_ (palette) {}

    /// `db` is the reduction, at or below zero.
    void setReductionDb (double db);

    /// Greyed when the stage is switched off, so a bar sitting at zero because
    /// nothing is happening is distinguishable from one sitting at zero
    /// because the stage is not in circuit.
    void setStageEnabled (bool enabled);

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;
    double reductionDb_ { 0.0 };
    double peakDb_ { 0.0 };
    int holdTicks_ { 0 };
    bool enabled_ { true };
};

/// The de-esser's identity display, and the reason it is worth the space: this
/// is the one stage in the strip whose detector measures something a level
/// meter cannot show.
///
/// Sibilance here is a **ratio** -- high-band energy against the body of the
/// voice -- so the useful picture is that ratio over time with the threshold
/// drawn across it. A vowel sits well below the line, an /s/ crosses it, and
/// whether the threshold is set right becomes something you can see rather
/// than something you guess at.
class SibilanceDisplay final : public juce::Component
{
public:
    explicit SibilanceDisplay (ui::Palette palette) : palette_ (palette) {}

    /// One tick of history. `reductionDb` is what the stage did about it.
    void push (double sibilanceDb, double reductionDb);

    void setThresholdDb (double db);
    void setStageEnabled (bool enabled);

    void paint (juce::Graphics&) override;

private:
    static constexpr int kHistory = 240;
    static constexpr double kFloorDb = -48.0;
    static constexpr double kCeilingDb = 24.0;

    [[nodiscard]] float yFor (double db) const;

    ui::Palette palette_;
    std::array<float, kHistory> sibilance_ {};
    std::array<float, kHistory> reduction_ {};
    int writeIndex_ { 0 };
    bool filled_ { false };
    double thresholdDb_ { -6.0 };
    bool enabled_ { true };
};

/// One box in the chain: a title, an optional in/out switch, a grid of knobs,
/// and an optional gain-reduction bar along its foot.
class StagePanel final : public juce::Component
{
public:
    StagePanel (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                juce::String title, const char* enableParameterId, int columns);

    void addKnob (const char* parameterId, const juce::String& name,
                  const juce::String& tooltip);
    void addToggle (const char* parameterId, const juce::String& name,
                    const juce::String& tooltip);

    /// Gives this stage a reduction bar. Not every stage has one -- IN and EQ
    /// do not reduce anything, and a dead meter under them would imply they
    /// might.
    void addReductionBar();

    /// A line of text under the knobs. Used for the input and output levels,
    /// which are numbers rather than settings and have no knob to sit beside.
    void addReadout (const juce::String& tooltip);
    void setReadout (int index, const juce::String& text);

    [[nodiscard]] GainReductionBar* getReductionBar() const noexcept { return bar_.get(); }

    /// True when the stage's own switch is on, or when it has no switch.
    [[nodiscard]] bool isStageEnabled() const;

    /// Greys the knobs when the stage is switched out, so a control that moves
    /// and does nothing does not read as a broken plugin.
    void refreshEnablement();

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

    /// **Never a tick box.** A tick box says "an option in a list"; an on/off
    /// control on an instrument is a switch, and reads faster as one -- see
    /// ui/LampButton.hpp.
    struct Toggle
    {
        explicit Toggle (const juce::String& name) : button (name) {}

        juce::String    id;
        ui::LampButton  button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    juce::String title_;
    const char* enableId_ { nullptr };
    int columns_;

    std::unique_ptr<ui::LampButton> enableButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment_;

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    std::vector<std::unique_ptr<juce::Label>> readouts_;
    std::unique_ptr<GainReductionBar> bar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagePanel)
};

/// The panel, laid out **as the chain**.
///
/// Six boxes left to right in signal order, each with its own reduction bar,
/// so the question a strip's display exists to answer -- *which* stage is
/// doing the work -- is answered by looking rather than by soloing. Underneath
/// them, full width, the de-esser's sibilance history.
class PhonossEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PhonossEditor (PhonossProcessor& processorToUse);
    ~PhonossEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildStages();

    /// Refreshes what the switches make inert. Only when something actually
    /// changed -- recomputing this thirty times a second would repaint the
    /// whole panel for nothing.
    void updateForSwitches();

    PhonossProcessor& phonoss_;

    ui::Palette palette_;
    ui::KnobLookAndFeel knobLook_;
    ui::TooltipHost tooltips_ { *this };

    std::unique_ptr<ui::HeaderBar> header_;

    enum Stage { input, gate, deEss, leveller, peak, eq, kNumStages };
    std::array<std::unique_ptr<StagePanel>, kNumStages> stages_;

    std::unique_ptr<SibilanceDisplay> sibilance_;
    juce::Label sibilanceLabel_;
    juce::Label statusLabel_;

    /// What the panel is currently dressed for, so the greying is not
    /// recomputed every tick. Deliberately impossible starting values, so the
    /// first tick always applies the state.
    std::array<int, kNumStages> shownEnabled_ { -1, -1, -1, -1, -1, -1 };
    int shownIdentity_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhonossEditor)
};

} // namespace tezla::phonoss
