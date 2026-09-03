// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The pictures on the drum pages: what the knobs are doing, drawn from the
// same numbers the engine will use at the next hit. None of them reads the
// audio -- a hit is a pure function of its settings, so the settings ARE the
// picture, and it is right before the first note is played.
//
//   PitchView     the kick's pitch trajectory on a log-time / log-frequency
//                 axis, with the tuning's notes as a ruler and the landed
//                 note named -- the thing to look at while tuning to a bass.
//   ModesView     the snare's three modes as bars on a log-frequency axis,
//                 where the drop starts them, and the note ruler.
//   EnvelopeView  the amplitude of the hit against time: the kick's AHD and
//                 tail, or the snare's shell modes and wires.
//   WiresView     the wires' filter response.
//
// Every view refreshes from the timer at 15 Hz and repaints only when an
// input has moved.

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::ictus {

/// What every drum display shares: the frame, the caption, the log axes,
/// and the "repaint only when something moved" discipline.
class DrumDisplay : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    DrumDisplay (IctusProcessor& processor, ui::Palette palette, juce::Colour tint);

    /// Re-reads the parameters it draws from and repaints if any moved.
    void refresh();

protected:
    /// The inputs this view draws from, in a fixed order; a change in any
    /// is a repaint.
    virtual void gather (std::vector<double>& inputs) = 0;

    /// Called after the inputs changed, before the repaint: derive whatever
    /// the paint needs (a caption, a curve).
    virtual void update() {}

    [[nodiscard]] double read (const char* id) const;

    /// The plot area inside the frame, and the caption's line under it.
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    void paintFrame (juce::Graphics& g);

    [[nodiscard]] static float along (double value, double low, double high,
                                      float from, float to, bool logarithmic);

    IctusProcessor& processor_;
    ui::Palette palette_;
    juce::Colour tint_;
    juce::String caption_;
    juce::String captionRight_;

private:
    std::vector<double> inputs_;
    std::vector<double> scratch_;
};

class PitchView final : public DrumDisplay
{
public:
    PitchView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    double landedHz_ { 50.0 };
    double startCents_ { 0.0 };
    double dropSeconds_ { 0.03 };
    double sighCents_ { 0.0 };
    double sighSeconds_ { 0.5 };
    bool keyed_ { false };
    bool snapped_ { false };
};

class ModesView final : public DrumDisplay
{
public:
    ModesView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    double fundamental_ { 180.0 };
    double ratios_[3] { 1.0, 1.6, 2.2 };
    double amounts_[3] { 1.0, 0.6, 0.6 };
    double startMultiplier_ { 1.0 };
    bool keyed_ { false };
};

class EnvelopeView final : public DrumDisplay
{
public:
    enum class Drum { kick, snare };

    EnvelopeView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint, Drum drum);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    struct Trace
    {
        std::vector<float> points;   ///< 0..1, `kPoints` of them over `seconds_`
        juce::Colour colour;
        float alpha { 1.0f };
        bool filled { false };
        bool dashed { false };
    };

    static constexpr int kPoints = 240;

    Drum drum_;
    double seconds_ { 0.5 };
    std::vector<Trace> traces_;
};

class WiresView final : public DrumDisplay
{
public:
    WiresView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    static constexpr double kLowHz = 200.0;
    static constexpr double kHighHz = 20000.0;
    static constexpr int kPoints = 160;

    dsp::SvfFilter filter_;
    std::vector<float> responseDb_;
    bool wiresOn_ { true };
};

} // namespace tezla::ictus
