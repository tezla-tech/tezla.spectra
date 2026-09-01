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

    /// **The exact number under the pointer.**
    ///
    /// A trace tells you the shape and lies about the value: whether an /s/
    /// crossed the line by 1 dB or by 9 is the difference between a de-esser
    /// that works and one that lisps, and no amount of looking at a curve
    /// settles it. Hovering reads the history back.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr int kHistory = 240;
    static constexpr double kFloorDb = -48.0;
    static constexpr double kCeilingDb = 24.0;

    [[nodiscard]] float yFor (double db) const;

    /// Which history slot sits under an x, or -1 for none.
    [[nodiscard]] int slotAt (float x) const;

    ui::Palette palette_;
    std::array<float, kHistory> sibilance_ {};
    std::array<float, kHistory> reduction_ {};
    int writeIndex_ { 0 };
    bool filled_ { false };
    double thresholdDb_ { -6.0 };
    bool enabled_ { true };
    int hoverSlot_ { -1 };
    float hoverX_ { 0.0f };
};

/// **Which stage is doing the work, and are they fighting each other.**
///
/// Four gain-reduction meters cannot answer that, however honest each one is:
/// they share no clock, so a de-esser ducking 40 ms before the leveller does
/// looks exactly like the two of them ducking together. One time axis with the
/// four stages on it *does* answer it, and the answer is usually the diagnosis
/// -- a compressor chewing on an /s/ the de-esser was about to remove is the
/// classic vocal-chain fault and it is invisible in isolation.
///
/// Laid out as four **lanes** rather than four overlaid traces. Overlaid, the
/// only thing telling them apart is colour, and the house hue step is 18
/// degrees -- deliberately subtle, because it was chosen for plates sitting
/// side by side rather than curves crossing each other. Lanes need no colour
/// to be read, and the colour then reinforces which box each lane belongs to
/// instead of carrying the whole burden.
class ChainReductionDisplay final : public juce::Component
{
public:
    static constexpr int kLanes = 4;

    ChainReductionDisplay (ui::Palette palette, std::array<juce::Colour, kLanes> tints,
                           std::array<juce::String, kLanes> names)
        : palette_ (palette), tints_ (tints), names_ (names) {}

    /// One tick of history: the four stages' reduction, at or below zero.
    void push (const std::array<float, kLanes>& reductionDb);

    /// A lane whose stage is switched out reads "off" rather than "0.0 dB" --
    /// a stage doing nothing and a stage not in circuit are different facts.
    void setLaneEnabled (int lane, bool enabled);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr int kHistory = 240;

    /// The bottom of a lane. Deep enough for gating, which is the one stage
    /// here that legitimately removes 40 dB -- but a lane scaled to 40 would
    /// show a compressor's 3 dB as nothing at all, so the gate's lane is
    /// scaled separately from the other three.
    [[nodiscard]] float laneFloorDb (int lane) const;

    [[nodiscard]] juce::Rectangle<float> laneBounds (int lane) const;
    [[nodiscard]] int slotAt (float x) const;

    ui::Palette palette_;
    std::array<juce::Colour, kLanes> tints_;
    std::array<juce::String, kLanes> names_;
    std::array<std::array<float, kHistory>, kLanes> history_ {};
    std::array<bool, kLanes> enabled_ { true, true, true, true };
    int writeIndex_ { 0 };
    bool filled_ { false };
    int hoverSlot_ { -1 };
    float hoverX_ { 0.0f };
};

/// One box in the chain: a title, an optional in/out switch, a grid of knobs,
/// and an optional gain-reduction bar along its foot.
class StagePanel final : public juce::Component
{
public:
    StagePanel (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                juce::Colour tint, juce::String title, const char* enableParameterId,
                int columns);

    /// The stage's live gain reduction, for the title bar.
    ///
    /// **Which stage is doing the work has to be readable without looking for
    /// it.** A number in the title, and the title lighting when the stage is
    /// actually reducing, puts that in peripheral vision -- which is where it
    /// is useful, because you are looking at a knob at the time.
    void setReductionDb (double db);

    /// Which control the stage is *about*, drawn larger than its neighbours,
    /// and which are set once and then ignored, drawn smaller.
    void setEmphasis (const char* lead, std::vector<juce::String> trims);

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

    /// A control's size, from the two lists this stage was given.
    [[nodiscard]] ui::design::Emphasis emphasisOf (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    juce::Colour tint_;
    juce::String title_;
    const char* enableId_ { nullptr };
    int columns_;

    juce::String leadId_;
    std::vector<juce::String> trimIds_;
    double reductionDb_ { 0.0 };

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
    std::unique_ptr<ChainReductionDisplay> reduction_;
    juce::Label sibilanceLabel_;
    juce::Label reductionLabel_;
    juce::Label statusLabel_;

    /// The de-esser's Listen, lifted out of its stage box to sit beside the
    /// display it is used with: it is the control you reach for *while looking
    /// at that graph*, and it was at the bottom of a column three boxes away.
    std::unique_ptr<ui::LampButton> listenButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment_;

    /// What the panel is currently dressed for, so the greying is not
    /// recomputed every tick. Deliberately impossible starting values, so the
    /// first tick always applies the state.
    /// Where the row of stage boxes ended up, so `paint` can put an arrow in
    /// each gap without recomputing the layout it was already given.
    juce::Rectangle<int> chainRow_;

    std::array<int, kNumStages> shownEnabled_ { -1, -1, -1, -1, -1, -1 };
    int shownIdentity_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhonossEditor)
};

} // namespace tezla::phonoss
