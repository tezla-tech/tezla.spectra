// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <array>
#include <functional>
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

namespace tezla::membrana
{

/// **The curve that plays, drawn from the coefficients that play.**
///
/// The whole mic model composes into one response, and this draws it by
/// asking the processor's engine for `capsuleRenderedDbAt` -- the digital
/// position section, the LF limit and a DFT of the live FIR taps -- rather
/// than re-deriving a picture from the knobs. What is shown is therefore
/// what sounds, including the fit error (0.02 dB, but honestly there).
///
/// Recomputed only when a mic parameter actually moved: the DFT behind each
/// point is 96-256 taps of trig, and thirty times a second of that on the
/// message thread would be pure waste on a static curve.
class CapsuleCurveDisplay final : public juce::Component
{
public:
    CapsuleCurveDisplay (ui::Palette palette, juce::Colour tint,
                         std::function<double (double)> renderedDbAt)
        : palette_ (palette), tint_ (tint), renderedDbAt_ (std::move (renderedDbAt)) {}

    /// Re-samples the curve from the engine. Call when a mic parameter
    /// changed (the editor's timer compares a revision), not per frame.
    void refreshCurve (bool micEngaged);

    void paint (juce::Graphics&) override;
    void resized() override { refreshCurve (engaged_); }

    /// The exact number under the pointer: frequency and dB. A curve tells
    /// you the shape and lies about the value.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr int kPoints = 140;
    static constexpr double kMinHz = 30.0, kMaxHz = 20000.0;
    static constexpr double kMinDb = -18.0, kMaxDb = 18.0;

    [[nodiscard]] double hzForX (float x) const;
    [[nodiscard]] float xForHz (double hz) const;
    [[nodiscard]] float yForDb (double db) const;

    ui::Palette palette_;
    juce::Colour tint_;
    std::function<double (double)> renderedDbAt_;

    std::array<float, kPoints> curveDb_ {};
    bool engaged_ { false };
    bool hovering_ { false };
    float hoverX_ { 0.0f };
};

/// The two rides on one time axis, growing UP -- these stages add, never
/// remove, and a lane that grew downward would say "reduction" to anyone
/// who has seen the rest of the suite. Presence and detail on the same
/// clock also answers the question two separate meters cannot: whether the
/// shelf and the expander are leaning on the same syllables.
class LiftLanesDisplay final : public juce::Component
{
public:
    static constexpr int kLanes = 2;

    LiftLanesDisplay (ui::Palette palette, std::array<juce::Colour, kLanes> tints,
                      std::array<juce::String, kLanes> names)
        : palette_ (palette), tints_ (tints), names_ (names) {}

    /// One tick of history: the lifts, at or above zero.
    void push (const std::array<float, kLanes>& liftDb);

    /// A lane whose stage is off reads "off", not "0.0 dB".
    void setLaneEnabled (int lane, bool enabled);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr int kHistory = 240;

    /// Presence is bounded at +9 and detail at +12; one shared ceiling
    /// keeps the two lanes comparable at a glance.
    static constexpr float kCeilingDb = 12.0f;

    [[nodiscard]] juce::Rectangle<float> laneBounds (int lane) const;
    [[nodiscard]] int slotAt (float x) const;

    ui::Palette palette_;
    std::array<juce::Colour, kLanes> tints_;
    std::array<juce::String, kLanes> names_;
    std::array<std::array<float, kHistory>, kLanes> history_ {};
    std::array<bool, kLanes> enabled_ { true, true };
    int writeIndex_ { 0 };
    bool filled_ { false };
    int hoverSlot_ { -1 };
    float hoverX_ { 0.0f };
};

/// One box in the chain: a title (optionally carrying the stage's live
/// figure), an optional red switch, and a grid of knobs. The Phonoss
/// StagePanel, adapted: Membrana's stages LIFT rather than reduce, so the
/// title figure is signed and the title brightens with |activity|.
class StagePanel final : public juce::Component
{
public:
    StagePanel (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                juce::Colour tint, juce::String title, const char* enableParameterId,
                int columns);

    /// Enables the live figure in the title bar (e.g. "+3.2 dB").
    void showActivityFigure();
    void setActivityDb (double db);

    void setEmphasis (const char* lead, std::vector<juce::String> trims);

    void addKnob (const char* parameterId, const juce::String& name,
                  const juce::String& tooltip);
    void addToggle (const char* parameterId, const juce::String& name,
                    const juce::String& tooltip);

    void addReadout (const juce::String& tooltip);
    void setReadout (int index, const juce::String& text);

    [[nodiscard]] bool isStageEnabled() const;

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

    /// Never a tick box (see ui/LampButton.hpp).
    struct Toggle
    {
        explicit Toggle (const juce::String& name) : button (name) {}

        juce::String    id;
        ui::LampButton  button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    [[nodiscard]] ui::design::Emphasis emphasisOf (const juce::String& id) const;

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    juce::Colour tint_;
    juce::String title_;
    const char* enableId_ { nullptr };
    int columns_;

    juce::String leadId_;
    std::vector<juce::String> trimIds_;
    bool showsActivity_ { false };
    double activityDb_ { 0.0 };

    std::unique_ptr<ui::LampButton> enableButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment_;

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    std::vector<std::unique_ptr<juce::Label>> readouts_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagePanel)
};

/// The panel, laid out as the chain: MIC | POSITION | PRESENCE | DETAIL |
/// OUT with the signal's arrows in the gaps, and the two displays above --
/// the composed curve on the left, the two rides on the right.
class MembranaEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit MembranaEditor (MembranaProcessor& processorToUse);
    ~MembranaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildStages();
    void updateForSwitches();

    MembranaProcessor& membrana_;

    ui::Palette palette_;
    ui::KnobLookAndFeel knobLook_;
    ui::TooltipHost tooltips_ { *this };

    std::unique_ptr<ui::HeaderBar> header_;

    enum Stage { mic, position, presence, detail, out, kNumStages };
    std::array<std::unique_ptr<StagePanel>, kNumStages> stages_;

    std::unique_ptr<CapsuleCurveDisplay> curve_;
    std::unique_ptr<LiftLanesDisplay> lifts_;
    juce::Label curveLabel_;
    juce::Label liftsLabel_;
    juce::Label statusLabel_;

    juce::Rectangle<int> chainRow_;

    /// A cheap revision of everything the curve depends on, compared each
    /// tick so the DFT-backed refresh runs only when a knob actually moved.
    double shownCurveRevision_ { -1.0e300 };

    std::array<int, kNumStages> shownEnabled_ { -1, -1, -1, -1, -1 };
    int shownIdentity_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MembranaEditor)
};

} // namespace tezla::membrana
