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
//   PartialsView  the hat's six partials on a log-frequency axis with the two
//                 band-passes drawn over them, so Harmonics, Spread and
//                 Colour are one picture.
//   BurstView     the clap's four bursts and its tail against time.
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

    /// A snare-engine pad's value for `own`, or the main snare's for the
    /// same control when the pad is a ghost whose LINK is lit -- the drum
    /// identity the ghost borrows, so the picture shows what will sound.
    [[nodiscard]] double readLinked (const SnareIds& ids, const char* own, const char* main) const;

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
    ModesView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint,
               const SnareIds& ids, PadIndex pad);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    const SnareIds& ids_;
    PadIndex pad_;
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

    /// `snare` names the snare-engine pad's IDs; ignored for the kick.
    EnvelopeView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint, Drum drum,
                  const SnareIds& snare = kSnare1Ids);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    const SnareIds& snare_;

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

class PartialsView final : public DrumDisplay
{
public:
    PartialsView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    static constexpr double kLowHz = 100.0;
    static constexpr double kHighHz = 20000.0;
    static constexpr int kPoints = 180;

    dsp::SvfFilter lowBand_, highBand_, highpass_;
    double partials_[6] {};
    std::vector<float> responseDb_;
    double air_ { 0.0 };
    double sizzle_ { 0.0 };
    double colour_ { 3440.0 };

    /// The plate's modes for the current Tune and Spread -- the same table
    /// the engine rings, from the same static -- and how far the crossfade
    /// has moved towards them.
    double plate_ { 0.0 };
    int plateCount_ { 0 };
    double plateHz_[64] {};
    double plateAmplitude_[64] {};
};

class BurstView final : public DrumDisplay
{
public:
    BurstView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    static constexpr int kPoints = 300;

    std::vector<float> envelope_;   ///< 0..1 over `seconds_`
    std::vector<float> bodyTrace_;  ///< the cavity's ring, same axis
    double onsets_[6] {};           ///< where each burst lands, in seconds
    int bursts_ { 4 };
    double seconds_ { 0.3 };
    double flamSeconds_ { 0.011 };
};

class WiresView final : public DrumDisplay
{
public:
    WiresView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint, const SnareIds& ids);

    void paint (juce::Graphics&) override;

private:
    void gather (std::vector<double>& inputs) override;
    void update() override;

    const SnareIds& ids_;

    static constexpr double kLowHz = 200.0;
    static constexpr double kHighHz = 20000.0;
    static constexpr int kPoints = 160;

    dsp::SvfFilter filter_;
    std::vector<float> responseDb_;
    bool wiresOn_ { true };
};

} // namespace tezla::ictus
