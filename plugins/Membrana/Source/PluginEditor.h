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

/// **The session, seen from above**: the polar diagram every mic manual
/// opens with, drawn live. The pattern's gain-vs-angle shape (dB rings, the
/// rear lobe and the null appearing exactly where the physics puts them),
/// with the singer as a dot on the angle ray at the set distance. One
/// glance answers what three knobs are doing together -- which is the whole
/// point of the POSITION stage's coupling.
class PolarPatternDisplay final : public juce::Component
{
public:
    PolarPatternDisplay (ui::Palette palette, juce::Colour tint)
        : palette_ (palette), tint_ (tint) {}

    /// pattern01: 0 omni, 0.5 cardioid, 1 figure-8 (the parameter's own
    /// convention). Distance in cm, angle in degrees off axis.
    void setState (double pattern01, double axisDeg, double distanceCm, bool engaged);

    void paint (juce::Graphics&) override;

private:
    static constexpr double kFloorDb = -30.0;   // the rings' centre

    ui::Palette palette_;
    juce::Colour tint_;

    double pattern01_ { 0.5 };
    double axisDeg_ { 0.0 };
    double distanceCm_ { 100.0 };
    bool engaged_ { true };
};

/// **The curve that plays, drawn from the coefficients that play** -- and
/// now the whole of it: the mic model's response (a DFT of the live FIR
/// taps plus the analytic sections, recomputed only when a mic knob moves)
/// with the two rides' CURRENT realised shelves composed on top, so the
/// bright trace breathes as the presence and detail stages work. Corner
/// markers along the foot tie the picture back to the knobs that own them,
/// each in its stage's hue.
class CapsuleCurveDisplay final : public juce::Component
{
public:
    struct Marker
    {
        double hz;
        juce::Colour colour;
    };

    CapsuleCurveDisplay (ui::Palette palette, juce::Colour tint,
                         std::function<double (double)> renderedDbAt)
        : palette_ (palette), tint_ (tint), renderedDbAt_ (std::move (renderedDbAt)) {}

    /// Re-samples the mic curve from the engine. Call when a mic parameter
    /// changed (the editor's timer compares a revision), not per frame.
    void refreshCurve (bool micEngaged);

    /// The rides' live state, fed each tick: the smoothed lifts and the
    /// corners. Cheap analytic curves -- no DFT -- so per-frame is fine.
    void setLiveState (double presenceLiftDb, double presenceHz, bool presenceOn,
                       double detailLiftDb, double detailHz, bool detailOn);

    void setMarkers (std::vector<Marker> markers);

    void paint (juce::Graphics&) override;
    void resized() override { refreshCurve (engaged_); }

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr int kPoints = 140;
    static constexpr double kMinHz = 30.0, kMaxHz = 20000.0;
    static constexpr double kMinDb = -18.0, kMaxDb = 18.0;

    [[nodiscard]] double hzForX (float x) const;
    [[nodiscard]] float xForHz (double hz) const;
    [[nodiscard]] float yForDb (double db) const;

    /// The two rides' realised curves at the current lifts, in dB at hz.
    /// Presence: |1 + g HP(jw)| with the SVF's critically damped highpass
    /// (Q = 0.5 -- the tracker runs the SvfFilter at zero resonance, and the
    /// drawn shelf must be the one that plays, not a prettier one).
    /// Detail: |1 + g H(jw)| with the exact one-pole complementary residual.
    [[nodiscard]] double liveShelvesDbAt (double hz) const;

    ui::Palette palette_;
    juce::Colour tint_;
    std::function<double (double)> renderedDbAt_;

    std::array<float, kPoints> curveDb_ {};
    std::vector<Marker> markers_;

    double presenceLiftDb_ { 0.0 }, presenceHz_ { 4500.0 };
    double detailLiftDb_ { 0.0 }, detailHz_ { 3000.0 };
    bool presenceOn_ { true }, detailOn_ { true };

    bool engaged_ { false };
    bool hovering_ { false };
    float hoverX_ { 0.0f };
};

/// The two rides on one time axis, growing UP -- and now showing the
/// MECHANISM, not just the activity: for each lane, the filled trace is
/// what the stage is APPLYING (the smoothed lift) and the thin amber line
/// is what its curve ASKED for that instant, before the attack and release
/// smoothing. The gap between them is the riding hand at work: the ask
/// snaps with the syllables, the applied lift follows at 120/400 ms
/// (presence) or 2/80 ms (detail).
class LiftLanesDisplay final : public juce::Component
{
public:
    static constexpr int kLanes = 2;

    struct Tick
    {
        float appliedDb;
        float targetDb;
    };

    LiftLanesDisplay (ui::Palette palette, std::array<juce::Colour, kLanes> tints,
                      std::array<juce::String, kLanes> names)
        : palette_ (palette), tints_ (tints), names_ (names) {}

    void push (const std::array<Tick, kLanes>& ticks);

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
    std::array<std::array<float, kHistory>, kLanes> applied_ {};
    std::array<std::array<float, kHistory>, kLanes> target_ {};
    std::array<bool, kLanes> enabled_ { true, true };
    int writeIndex_ { 0 };
    bool filled_ { false };
    int hoverSlot_ { -1 };
    float hoverX_ { 0.0f };
};

/// In and out, as bars rather than two lines of text: instant-attack,
/// ~24 dB/s fall, a peak-hold pip, and the figure beneath each. The pair
/// answers the only level question this plugin raises -- is Auto Level
/// keeping its promise -- by looking.
class IoMeters final : public juce::Component
{
public:
    explicit IoMeters (ui::Palette palette) : palette_ (palette) {}

    /// Per timer tick: the block peaks the processor measured.
    void setLevels (float inDb, float outDb);

    void paint (juce::Graphics&) override;

private:
    static constexpr float kFloorDb = -60.0f;
    static constexpr float kFallDbPerTick = 0.8f;   // ~24 dB/s at 30 Hz
    static constexpr int kHoldTicks = 60;

    struct Channel
    {
        float shownDb { -100.0f };
        float peakDb { -100.0f };
        float latestDb { -100.0f };
        int holdTicks { 0 };
    };

    void advance (Channel& channel, float db);
    void paintBar (juce::Graphics&, juce::Rectangle<float> area,
                   const Channel&, const juce::String& name);

    ui::Palette palette_;
    Channel in_, out_;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagePanel)
};

/// The panel: a full-width instrument row up top -- polar diagram, the
/// live composed response, the rides, the I/O pair -- and the chain below
/// it as four boxes, MIC | POSITION | PRESENCE | DETAIL, with the signal's
/// arrows in the gaps. The output trim lives in the header, where the
/// suite keeps it; it does not get a box of its own.
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

    enum Stage { mic, position, presence, detail, kNumStages };
    std::array<std::unique_ptr<StagePanel>, kNumStages> stages_;

    std::unique_ptr<PolarPatternDisplay> polar_;
    std::unique_ptr<CapsuleCurveDisplay> curve_;
    std::unique_ptr<LiftLanesDisplay> lifts_;
    std::unique_ptr<IoMeters> ioMeters_;
    juce::Label polarLabel_;
    juce::Label curveLabel_;
    juce::Label liftsLabel_;
    juce::Label ioLabel_;
    juce::Label statusLabel_;

    juce::Rectangle<int> chainRow_;

    /// A cheap revision of everything the mic curve depends on, compared
    /// each tick so the DFT-backed refresh runs only when a knob moved.
    double shownCurveRevision_ { -1.0e300 };

    std::array<int, kNumStages> shownEnabled_ { -1, -1, -1, -1 };
    int shownIdentity_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MembranaEditor)
};

} // namespace tezla::membrana
