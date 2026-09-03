// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "Displays.h"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/TensionDrop.hpp>

#include "KickEngine.hpp"
#include "SnareEngine.hpp"

namespace tezla::ictus {

namespace
{
constexpr float kCaptionHeight = 13.0f;
constexpr float kInset = 6.0f;

/// A decaying envelope's amplitude at `t` for a T60 -- what a mode does.
[[nodiscard]] double ringAt (double t, double t60) noexcept
{
    return t60 > 0.0 ? std::pow (10.0, -3.0 * t / t60) : 0.0;
}

juce::String hzText (double hz)
{
    return hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz" : juce::String (hz, 1) + " Hz";
}

juce::String msText (double seconds)
{
    return seconds >= 1.0 ? juce::String (seconds, 2) + " s"
                          : juce::String (juce::roundToInt (1000.0 * seconds)) + " ms";
}
} // namespace

// ---------------------------------------------------------------------------
// DrumDisplay
// ---------------------------------------------------------------------------

DrumDisplay::DrumDisplay (IctusProcessor& processor, ui::Palette palette, juce::Colour tint)
    : processor_ (processor), palette_ (palette), tint_ (tint)
{
    // Hover reaches it (the tooltip is the point); clicks pass through --
    // there is nothing here to drag.
    setInterceptsMouseClicks (false, true);
}

double DrumDisplay::read (const char* id) const
{
    if (auto* raw = processor_.getState().getRawParameterValue (id))
        return static_cast<double> (raw->load());

    return 0.0;
}

void DrumDisplay::refresh()
{
    scratch_.clear();
    gather (scratch_);

    if (scratch_ == inputs_)
        return;

    inputs_ = scratch_;
    update();
    repaint();
}

juce::Rectangle<float> DrumDisplay::plotArea() const
{
    auto area = getLocalBounds().toFloat().reduced (kInset, 4.0f);
    area.removeFromBottom (kCaptionHeight);
    return area;
}

void DrumDisplay::paintFrame (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // A recess in the plate: darker than it, with the plate's lit lip
    // inverted -- a window rather than a card.
    g.setColour (palette_.background.brighter (0.06f));
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (bounds.getX() + 4.0f, bounds.getY() + 0.5f, bounds.getRight() - 4.0f, bounds.getY() + 0.5f);

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (bounds.getX() + 4.0f, bounds.getBottom() - 0.5f, bounds.getRight() - 4.0f, bounds.getBottom() - 0.5f);

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.0f));

    const auto captionArea = bounds.reduced (kInset, 3.0f).removeFromBottom (kCaptionHeight);

    if (captionRight_.isNotEmpty())
    {
        const int rightWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), captionRight_) + 2;
        g.drawText (captionRight_, captionArea, juce::Justification::centredRight, false);
        g.drawFittedText (caption_, captionArea.withTrimmedRight (static_cast<float> (rightWidth) + 6.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }
    else
    {
        g.drawFittedText (caption_, captionArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

float DrumDisplay::along (double value, double low, double high, float from, float to, bool logarithmic)
{
    const double clamped = juce::jlimit (juce::jmin (low, high), juce::jmax (low, high), value);
    const double t = logarithmic ? std::log (clamped / low) / std::log (high / low)
                                 : (clamped - low) / (high - low);

    return from + static_cast<float> (t) * (to - from);
}

// ---------------------------------------------------------------------------
// PitchView -- the kick's pitch trajectory
// ---------------------------------------------------------------------------

PitchView::PitchView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint)
    : DrumDisplay (processor, palette, tint)
{
    setTooltip ("The kick's pitch against time, on log axes: 1 ms to 2 s across, and "
                "the notes of the TUNING page's scale as a ruler, with the note the "
                "kick lands on named. Start and Drop are the fast fall on the left, Sigh "
                "the slow one on the right. Drawn from the knobs, so it is right before "
                "the first hit.");
    refresh();
}

void PitchView::gather (std::vector<double>& inputs)
{
    inputs.push_back (read (ids::k1Tune));
    inputs.push_back (read (ids::k1FollowKey));
    inputs.push_back (read (ids::k1NoteSnap));
    inputs.push_back (read (ids::bassMode));
    inputs.push_back (read (ids::k1Start));
    inputs.push_back (read (ids::k1Drop));
    inputs.push_back (read (ids::k1Sigh));
    inputs.push_back (read (ids::k1SighTime));
    inputs.push_back (processor_.getConcertPitch());
    inputs.push_back (static_cast<double> (processor_.getScaleName().hashCode()));
    inputs.push_back (processor_.getPadNote (PadIndex::kick1));
}

void PitchView::update()
{
    const double tune = read (ids::k1Tune);
    keyed_ = read (ids::k1FollowKey) > 0.5 || read (ids::bassMode) > 0.5;
    snapped_ = read (ids::k1NoteSnap) > 0.5;

    landedHz_ = keyed_ ? processor_.previewFrequencyFor (processor_.getPadNote (PadIndex::kick1))
              : snapped_ ? processor_.previewSnappedHz (tune)
                         : tune;

    startCents_ = 100.0 * read (ids::k1Start);
    dropSeconds_ = 0.001 * read (ids::k1Drop);
    sighCents_ = 100.0 * read (ids::k1Sigh);
    sighSeconds_ = 0.001 * read (ids::k1SighTime);

    caption_ = (keyed_ ? "from the key: " : snapped_ ? "snaps to " : "lands on ")
             + hzText (landedHz_) + "  " + processor_.noteNameFor (landedHz_);

    captionRight_ = startCents_ > 0.0
        ? "+" + juce::String (startCents_ / 100.0, 1) + " st / " + msText (dropSeconds_)
        : juce::String ("no drop");
}

void PitchView::paint (juce::Graphics& g)
{
    paintFrame (g);

    const auto plot = plotArea();

    // The axes: time 1 ms to 2 s, frequency around the landed pitch --
    // an octave below it up to a little past where the drop starts.
    constexpr double tLow = 0.001, tHigh = 2.0;
    const double top = landedHz_ * std::exp2 ((juce::jmax (startCents_ + juce::jmax (0.0, sighCents_), 0.0) + 300.0) / 1200.0);
    const double bottom = landedHz_ * std::exp2 ((juce::jmin (0.0, sighCents_) - 1200.0) / 1200.0);

    const auto xOf = [&] (double t) { return along (t, tLow, tHigh, plot.getX(), plot.getRight(), true); };
    const auto yOf = [&] (double hz) { return along (hz, bottom, top, plot.getBottom(), plot.getY(), true); };

    // Time gridlines at 10 ms, 100 ms and 1 s.
    g.setColour (palette_.panel.brighter (0.30f));

    for (const double t : { 0.01, 0.1, 1.0 })
        g.drawVerticalLine (juce::roundToInt (xOf (t)), plot.getY(), plot.getBottom());

    // The tuning's notes as a ruler: every note whose frequency falls in
    // the window, the landed one in blue with its name.
    const auto& tuning = processor_.previewTuning();

    for (int note = 0; note < 128; ++note)
    {
        const double hz = tuning.frequencyFor (note);

        if (hz < bottom || hz > top)
            continue;

        const float y = yOf (hz);
        const bool landed = std::abs (1200.0 * std::log2 (hz / landedHz_)) < 0.5;

        g.setColour (landed ? palette_.secondary.withAlpha (0.9f) : palette_.secondary.withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

        if (landed)
        {
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 3),
                        juce::Rectangle<float> (plot.getX() + 3.0f, y - 12.0f, 40.0f, 11.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // The trajectory: both drops' cents summed, each falling as the
    // TensionDrop does -- e^-5 of the depth left at the stated time.
    juce::Path curve;
    constexpr int points = 160;

    for (int i = 0; i <= points; ++i)
    {
        const double t = tLow * std::pow (tHigh / tLow, static_cast<double> (i) / points);
        const double cents = startCents_ * std::exp (-dsp::TensionDrop::kLandFactor * t / juce::jmax (0.002, dropSeconds_))
                           + sighCents_ * std::exp (-dsp::TensionDrop::kLandFactor * t / juce::jmax (0.1, sighSeconds_));
        const double hz = landedHz_ * std::exp2 (cents / 1200.0);

        const float x = xOf (t);
        const float y = yOf (hz);

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    g.setColour (tint_.withAlpha (0.35f));
    g.strokePath (curve, juce::PathStrokeType (4.0f));
    g.setColour (tint_);
    g.strokePath (curve, juce::PathStrokeType (1.6f));
}

// ---------------------------------------------------------------------------
// ModesView -- the snare's shell
// ---------------------------------------------------------------------------

ModesView::ModesView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint)
    : DrumDisplay (processor, palette, tint)
{
    setTooltip ("The shell's three modes as bars on a log-frequency axis, their height "
                "how hard each is struck (Tone), the faint bars to their right where the "
                "drop starts them. The ticks along the bottom are the TUNING page's "
                "notes, with the fundamental's named.");
    refresh();
}

void ModesView::gather (std::vector<double>& inputs)
{
    inputs.push_back (read (ids::s1Tune));
    inputs.push_back (read (ids::s1FollowKey));
    inputs.push_back (read (ids::s1NoteSnap));
    inputs.push_back (read (ids::s1Spread));
    inputs.push_back (read (ids::s1Tone));
    inputs.push_back (read (ids::s1Start));
    inputs.push_back (processor_.getConcertPitch());
    inputs.push_back (static_cast<double> (processor_.getScaleName().hashCode()));
    inputs.push_back (processor_.getPadNote (PadIndex::snare1));
}

void ModesView::update()
{
    const double tune = read (ids::s1Tune);
    keyed_ = read (ids::s1FollowKey) > 0.5;
    const bool snapped = read (ids::s1NoteSnap) > 0.5;

    fundamental_ = keyed_ ? processor_.previewFrequencyFor (processor_.getPadNote (PadIndex::snare1))
                 : snapped ? processor_.previewSnappedHz (tune)
                           : tune;

    const double spread = 0.01 * read (ids::s1Spread);
    const double tone = 0.01 * read (ids::s1Tone);

    for (int mode = 0; mode < 3; ++mode)
    {
        ratios_[mode] = 1.0 + (SnareEngine::kModeRatios[mode] - 1.0) * spread;
        amounts_[mode] = mode == 0 ? 1.0 : tone;
    }

    startMultiplier_ = std::exp2 (read (ids::s1Start) / 12.0);

    caption_ = hzText (fundamental_) + "  " + processor_.noteNameFor (fundamental_)
             + "  ·  " + juce::String (fundamental_ * ratios_[1], 0) + " / "
             + juce::String (fundamental_ * ratios_[2], 0) + " Hz";
    captionRight_ = read (ids::s1Start) > 0.0 ? "from +" + juce::String (read (ids::s1Start), 1) + " st"
                                              : juce::String ("no drop");
}

void ModesView::paint (juce::Graphics& g)
{
    paintFrame (g);

    const auto plot = plotArea();
    constexpr double low = 40.0, high = 3000.0;

    const auto xOf = [&] (double hz) { return along (hz, low, high, plot.getX(), plot.getRight(), true); };

    // Octave gridlines.
    g.setColour (palette_.panel.brighter (0.30f));

    for (double hz = 62.5; hz < high; hz *= 2.0)
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());

    // The note ruler along the bottom.
    const auto& tuning = processor_.previewTuning();
    const float tick = plot.getHeight() * 0.18f;

    for (int note = 0; note < 128; ++note)
    {
        const double hz = tuning.frequencyFor (note);

        if (hz < low || hz > high)
            continue;

        const bool fundamental = std::abs (1200.0 * std::log2 (hz / fundamental_)) < 0.5;
        g.setColour (palette_.secondary.withAlpha (fundamental ? 0.9f : 0.28f));
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getBottom() - (fundamental ? tick * 1.6f : tick),
                            plot.getBottom());
    }

    // The modes: where they land, and ghosted where the drop starts them.
    for (int mode = 0; mode < 3; ++mode)
    {
        if (mode > 0 && amounts_[mode] <= 0.0)
            continue;

        const double hz = fundamental_ * ratios_[mode];
        const float height = plot.getHeight() * (0.25f + 0.65f * static_cast<float> (amounts_[mode]));

        if (startMultiplier_ > 1.0)
        {
            const float gx = xOf (hz * startMultiplier_);
            g.setColour (tint_.withAlpha (0.22f));
            g.fillRect (juce::Rectangle<float> (gx - 1.0f, plot.getBottom() - height, 2.0f, height));
        }

        const float x = xOf (hz);
        g.setColour (tint_.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x - 2.5f, plot.getBottom() - height, 5.0f, height));
        g.setColour (mode == 0 ? palette_.accentBright : tint_);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, plot.getBottom() - height, 2.0f, height));
    }
}

// ---------------------------------------------------------------------------
// EnvelopeView -- the hit's amplitude against time
// ---------------------------------------------------------------------------

EnvelopeView::EnvelopeView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint, Drum drum)
    : DrumDisplay (processor, palette, tint), drum_ (drum)
{
    setTooltip (drum == Drum::kick
        ? "The kick's amplitude against time: Attack, Hold and Decay under Shape, "
          "with the Tail's longer envelope dashed and the mix of the two filled. "
          "Computed with the engine's own envelope, so what is drawn is what plays."
        : "The snare's amplitude against time: the shell's fundamental ringing "
          "down over Decay (the upper modes fainter, at 0.7 and 0.5 of it) and the "
          "wires' burst under Wires decay. Rattle carries the wires along the "
          "shell's line.");
    refresh();
}

void EnvelopeView::gather (std::vector<double>& inputs)
{
    if (drum_ == Drum::kick)
    {
        for (const char* id : { ids::k1Attack, ids::k1Hold, ids::k1Decay, ids::k1Shape,
                                ids::k1Tail, ids::k1TailTime, ids::k1Drop, ids::k1Gate, ids::k1Release })
            inputs.push_back (read (id));
    }
    else
    {
        for (const char* id : { ids::s1Decay, ids::s1Tone, ids::s1Wires, ids::s1WiresDecay,
                                ids::s1Rattle, ids::s1Body, ids::s1Gate, ids::s1Release })
            inputs.push_back (read (id));
    }
}

void EnvelopeView::update()
{
    traces_.clear();

    if (drum_ == Drum::kick)
    {
        const double attack = 0.001 * read (ids::k1Attack);
        const double hold = 0.001 * read (ids::k1Hold);
        const double decay = 0.001 * read (ids::k1Decay);
        const double shape = 0.01 * read (ids::k1Shape);
        const double tailMix = 0.01 * read (ids::k1Tail);
        const double tailTime = 0.001 * read (ids::k1TailTime);
        const double drop = 0.001 * read (ids::k1Drop);

        seconds_ = juce::jmax (0.05, 1.08 * juce::jmax (attack + hold + decay,
                                                          tailMix > 0.0 ? drop + tailTime : 0.0));

        // The engine's own envelopes, run at a display rate: the shape is the
        // shape, tension and all.
        constexpr double rate = 8000.0;

        dsp::Adsr amp, tail;
        amp.prepare (rate);
        amp.setAttackSeconds (attack);
        amp.setAttackTension (0.0);
        amp.setHoldSeconds (hold);
        amp.setDecaySeconds (decay);
        amp.setDecayTension (1.0 - shape);
        amp.setSustain (0.0);
        amp.noteOn();

        tail.prepare (rate);
        tail.setAttackSeconds (drop);
        tail.setAttackTension (0.0);
        tail.setHoldSeconds (0.0);
        tail.setDecaySeconds (tailTime);
        tail.setDecayTension (1.0 - shape);
        tail.setSustain (0.0);
        tail.noteOn();

        Trace mixed { {}, tint_, 1.0f, true, false };
        Trace ampOnly { {}, tint_, 0.55f, false, true };

        const double step = seconds_ / kPoints;
        double t = 0.0;

        for (int i = 0; i <= kPoints; ++i)
        {
            const int samples = juce::jmax (1, juce::roundToInt (step * rate));
            double a = 0.0, b = 0.0;

            for (int n = 0; n < samples; ++n)
            {
                a = amp.process();
                if (amp.getStage() == dsp::AdsrStage::sustain) amp.kill();
                b = tail.process();
                if (tail.getStage() == dsp::AdsrStage::sustain) tail.kill();
            }

            mixed.points.push_back (static_cast<float> (a * (1.0 - tailMix) + tailMix * b));
            ampOnly.points.push_back (static_cast<float> (a));
            t += step;
        }

        traces_.push_back (mixed);

        if (tailMix > 0.0)
            traces_.push_back (ampOnly);

        caption_ = msText (decay) + " decay" + (hold > 0.0 ? ", hold " + msText (hold) : juce::String())
                 + (tailMix > 0.0 ? ", tail " + juce::String (juce::roundToInt (100.0 * tailMix)) + "% over " + msText (tailTime)
                                  : juce::String());
    }
    else
    {
        const double decay = 0.001 * read (ids::s1Decay);
        const double tone = 0.01 * read (ids::s1Tone);
        const double wires = 0.01 * read (ids::s1Wires);
        const double wiresDecay = 0.001 * read (ids::s1WiresDecay);
        const double rattle = 0.01 * read (ids::s1Rattle);
        const double body = 0.01 * read (ids::s1Body);

        seconds_ = juce::jmax (0.05, 1.1 * juce::jmax (decay, wires > 0.0 ? wiresDecay : 0.0));

        // The shell: the fundamental at Body, the upper modes at Tone x Body
        // on their shorter T60s.
        for (int mode = 0; mode < 3; ++mode)
        {
            const double amount = (mode == 0 ? 1.0 : tone) * body;

            if (amount <= 0.0)
                continue;

            Trace shell { {}, tint_, mode == 0 ? 1.0f : 0.4f, mode == 0, false };

            for (int i = 0; i <= kPoints; ++i)
            {
                const double t = seconds_ * i / kPoints;
                shell.points.push_back (static_cast<float> (amount * ringAt (t, decay * SnareEngine::kModeDecays[mode])));
            }

            traces_.push_back (shell);
        }

        // The wires: their burst, and with Rattle the shell's drive on top.
        if (wires > 0.0)
        {
            constexpr double rate = 8000.0;
            dsp::Adsr env;
            env.prepare (rate);
            env.setAttackSeconds (0.0);
            env.setHoldSeconds (0.0);
            env.setDecaySeconds (wiresDecay);
            env.setDecayTension (1.0);
            env.setSustain (0.0);
            env.noteOn();

            Trace wire { {}, palette_.secondary, 0.9f, false, false };
            const double step = seconds_ / kPoints;

            for (int i = 0; i <= kPoints; ++i)
            {
                const int samples = juce::jmax (1, juce::roundToInt (step * rate));
                double e = 0.0;

                for (int n = 0; n < samples; ++n)
                {
                    e = env.process();
                    if (env.getStage() == dsp::AdsrStage::sustain) env.kill();
                }

                const double t = step * i;
                const double drive = rattle * SnareEngine::kRattleGain * 0.24 * ringAt (t, decay);
                wire.points.push_back (static_cast<float> (wires * (e + drive)));
            }

            traces_.push_back (wire);
        }

        caption_ = "shell " + msText (decay) + (wires > 0.0 ? ", wires " + msText (wiresDecay) : juce::String (", wires off"))
                 + (rattle > 0.0 ? ", rattle " + juce::String (juce::roundToInt (100.0 * rattle)) + "%" : juce::String());
    }

    const bool gate = read (drum_ == Drum::kick ? ids::k1Gate : ids::s1Gate) > 0.5;
    const double release = 0.001 * read (drum_ == Drum::kick ? ids::k1Release : ids::s1Release);
    captionRight_ = gate ? "gate, release " + (release > 0.0 ? msText (release) : juce::String ("1 ms"))
                         : juce::String ("one-shot");
}

void EnvelopeView::paint (juce::Graphics& g)
{
    paintFrame (g);

    const auto plot = plotArea();

    g.setColour (palette_.panel.brighter (0.30f));

    for (const double t : { 0.1, 0.5, 1.0 })
        if (t < seconds_)
            g.drawVerticalLine (juce::roundToInt (along (t, 0.0, seconds_, plot.getX(), plot.getRight(), false)),
                                plot.getY(), plot.getBottom());

    for (const auto& trace : traces_)
    {
        if (trace.points.empty())
            continue;

        juce::Path path;

        for (std::size_t i = 0; i < trace.points.size(); ++i)
        {
            const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (trace.points.size() - 1);
            const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, trace.points[i]);

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        if (trace.filled)
        {
            juce::Path fill (path);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();

            g.setColour (trace.colour.withAlpha (0.16f * trace.alpha));
            g.fillPath (fill);
        }

        g.setColour (trace.colour.withAlpha (trace.alpha));

        if (trace.dashed)
        {
            juce::Path dashed;
            const float lengths[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, path, lengths, 2);
            g.fillPath (dashed);
        }
        else
        {
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }
    }
}

// ---------------------------------------------------------------------------
// WiresView -- the wires' filter
// ---------------------------------------------------------------------------

WiresView::WiresView (IctusProcessor& processor, ui::Palette palette, juce::Colour tint)
    : DrumDisplay (processor, palette, tint)
{
    setTooltip ("The wires' filter response, 200 Hz to 20 kHz: a high-pass above Snappy at "
                "Shape 0, a band-pass at it at Shape 100, with the engine's own filter "
                "drawing its own curve.");

    // The engine's filter at the internal rate Auto gives a 48 kHz session,
    // so the curve is the one the wires actually go through.
    filter_.prepare (192000.0);
    filter_.setMode (dsp::SvfMode::highpass);
    filter_.setResonance (SnareEngine::kWiresResonance);

    refresh();
}

void WiresView::gather (std::vector<double>& inputs)
{
    inputs.push_back (read (ids::s1Snappy));
    inputs.push_back (read (ids::s1Snap));
    inputs.push_back (read (ids::s1Wires));
}

void WiresView::update()
{
    const double snappy = read (ids::s1Snappy);
    const double snap = 0.01 * read (ids::s1Snap);
    wiresOn_ = read (ids::s1Wires) > 0.0;

    filter_.setCutoffHz (snappy);
    filter_.setMorph (-0.5 * snap);

    responseDb_.clear();

    for (int i = 0; i <= kPoints; ++i)
    {
        const double hz = kLowHz * std::pow (kHighHz / kLowHz, static_cast<double> (i) / kPoints);
        responseDb_.push_back (static_cast<float> (20.0 * std::log10 (juce::jmax (1.0e-6, filter_.magnitudeAt (hz)))));
    }

    caption_ = (snap >= 0.75 ? "band-pass at " : snap > 0.25 ? "between high- and band-pass at " : "high-pass from ")
             + hzText (snappy);
    captionRight_ = wiresOn_ ? juce::String() : juce::String ("wires off");
}

void WiresView::paint (juce::Graphics& g)
{
    paintFrame (g);

    const auto plot = plotArea();
    constexpr float lowDb = -36.0f, highDb = 12.0f;

    g.setColour (palette_.panel.brighter (0.30f));

    for (const double hz : { 1000.0, 10000.0 })
        g.drawVerticalLine (juce::roundToInt (along (hz, kLowHz, kHighHz, plot.getX(), plot.getRight(), true)),
                            plot.getY(), plot.getBottom());

    g.drawHorizontalLine (juce::roundToInt (along (0.0, lowDb, highDb, plot.getBottom(), plot.getY(), false)),
                          plot.getX(), plot.getRight());

    if (responseDb_.empty())
        return;

    juce::Path path;

    for (std::size_t i = 0; i < responseDb_.size(); ++i)
    {
        const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (responseDb_.size() - 1);
        const float y = along (responseDb_[i], lowDb, highDb, plot.getBottom(), plot.getY(), false);

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    juce::Path fill (path);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();

    const float alpha = wiresOn_ ? 1.0f : 0.35f;

    g.setColour (tint_.withAlpha (0.18f * alpha));
    g.fillPath (fill);
    g.setColour (tint_.withAlpha (alpha));
    g.strokePath (path, juce::PathStrokeType (1.6f));
}

} // namespace tezla::ictus
