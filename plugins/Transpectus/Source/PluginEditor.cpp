// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::transpectus
{

namespace
{
// Transpectus reads rather than colours, so its accent is a cool instrument
// green against Emberdrive's ember, Halo's gold and Capstone's steel.
const ui::Palette kPalette {
    juce::Colour { 0xff141416 },   // background
    juce::Colour { 0xff1d1d20 },   // panel
    juce::Colour { 0xffd8d5cf },   // text
    juce::Colour { 0xff86837e },   // dim text
    juce::Colour { 0xff5bb98c },   // accent
    juce::Colour { 0xff8fe0b4 },   // accent bright
    juce::Colour { 0xff54c7c0 },   // secondary
    juce::Colour { 0xffff7a18 },   // bypass glow, the same in every plugin
    juce::Colour { 0xffe2483d }    // over
};

constexpr int kMinWidth  = 760;
constexpr int kMinHeight = 520;
constexpr int kMaxWidth  = 1520;
constexpr int kMaxHeight = 1040;

constexpr int kStatusHeight = 42;

/// Below this the spectrum's control strip takes two lines instead of one.
/// 160+62+58+58 of reference controls, 100+100+98+110 of view switches, and the
/// gaps between them.
constexpr int kSpectrumControlsOneRowWidth = 790;
constexpr int kControlHeight = 52;

/// Below this a loudness reading is silence rather than a number.
constexpr double kSilenceThreshold = -150.0;

/// Where PLR stops meaning "punchy" and starts meaning "squashed". Guidance,
/// sourced, and labelled as such -- not a pass/fail.
constexpr double kPlrSquashed = 5.0;
constexpr double kPlrHealthy  = 8.0;
} // namespace

// ---------------------------------------------------------------------------
// Readout
// ---------------------------------------------------------------------------

Readout::Readout (ui::Palette palette, juce::String caption, juce::String unit)
    : palette_ (palette), caption_ (std::move (caption)), unit_ (std::move (unit))
{
}

void Readout::setValue (juce::String text, juce::String note)
{
    if (value_ == text && note_ == note)
        return;

    value_ = std::move (text);
    note_ = std::move (note);
    repaint();
}

void Readout::setWarning (bool shouldWarn)
{
    if (warning_ == shouldWarn)
        return;

    warning_ = shouldWarn;
    repaint();
}

void Readout::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (10.0f, 5.0f);

    // The note gets its own line under the pair; everything else shares one.
    // Stacking the caption above the value cost a whole line per readout and
    // seven readouts made the panel feel like a form to fill in.
    auto noteArea = note_.isEmpty() ? juce::Rectangle<float>{} : area.removeFromBottom (15.0f);

    // Sized to the cell rather than fixed. These panels get resized a long way
    // -- the window goes from 760 to 1520 wide -- and a number that stays 26
    // point floats in the middle of a tall cell looking like a placeholder.
    const float size = juce::jlimit (16.0f, 34.0f, area.getHeight() * 0.80f);
    const auto valueFont = juce::Font (juce::FontOptions (size, juce::Font::bold));
    const auto unitFont  = juce::Font (juce::FontOptions (11.0f));

    // The value and its unit are one right-aligned group: "-14.2 LUFS" reads as
    // one thing, and a unit parked in the far corner reads as two.
    const float valueWidth = juce::GlyphArrangement::getStringWidth (valueFont, value_);
    const float unitWidth  = unit_.isEmpty()
                                 ? 0.0f
                                 : juce::GlyphArrangement::getStringWidth (unitFont, unit_) + 5.0f;

    auto valueArea = area.removeFromRight (juce::jmin (area.getWidth(), valueWidth + unitWidth));

    g.setColour (warning_ ? palette_.over : palette_.text);
    g.setFont (valueFont);
    g.drawText (value_, valueArea.withTrimmedRight (unitWidth), juce::Justification::centredRight);

    if (! unit_.isEmpty())
    {
        g.setColour (palette_.dimText);
        g.setFont (unitFont);
        g.drawText (unit_, valueArea.withLeft (valueArea.getRight() - unitWidth + 5.0f),
                    juce::Justification::centredLeft);
    }

    // Whatever room the number left. Fitted rather than drawn, so a long
    // caption shrinks instead of running under the value.
    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText (caption_, area.withTrimmedRight (8.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.7f);

    if (! note_.isEmpty())
    {
        g.setColour (warning_ ? palette_.over.withAlpha (0.9f) : palette_.accent);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (note_, noteArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

// ---------------------------------------------------------------------------
// CorrelationBar
// ---------------------------------------------------------------------------

void CorrelationBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (8.0f, 6.0f);

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (caption_, area.removeFromTop (13.0f), juce::Justification::centredLeft);

    auto value = area.removeFromRight (52.0f);
    g.setColour (warning_ ? palette_.over : palette_.text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (juce::String (correlation_, 2), value, juce::Justification::centredRight);

    auto track = area.reduced (2.0f, 0.0f).withSizeKeepingCentre (area.getWidth() - 4.0f, 10.0f);

    g.setColour (palette_.background);
    g.fillRoundedRectangle (track, 3.0f);

    // The mono-safe half, marked rather than left to be remembered. Below zero
    // a fold to mono starts costing real level; at -1 it costs all of it.
    auto safe = track.withLeft (track.getCentreX());
    g.setColour (palette_.accent.withAlpha (0.16f));
    g.fillRoundedRectangle (safe, 3.0f);

    g.setColour (palette_.dimText.withAlpha (0.5f));
    g.drawVerticalLine (juce::roundToInt (track.getCentreX()), track.getY(), track.getBottom());

    // The held worst moment, in the hold colour, behind the live needle. A
    // correlation meter is watched for its dips and the dips are exactly what
    // a glance misses; the tick is the dip that already happened.
    if (heldMinimum_ < 0.995f)
    {
        const float held = track.getX()
                         + track.getWidth() * (juce::jlimit (-1.0f, 1.0f, heldMinimum_) + 1.0f) * 0.5f;

        g.setColour (palette_.hold);
        g.fillRoundedRectangle (juce::Rectangle<float> { held - 1.5f, track.getY() - 3.0f,
                                                         3.0f, track.getHeight() + 6.0f }, 1.5f);

        g.setFont (juce::FontOptions (9.5f));
        g.drawText ("worst " + juce::String (heldMinimum_, 2),
                    track.translated (0.0f, 12.0f), juce::Justification::centred);
    }

    // The needle.
    const float position = track.getX()
                         + track.getWidth() * (juce::jlimit (-1.0f, 1.0f, correlation_) + 1.0f) * 0.5f;

    g.setColour (warning_ ? palette_.over : palette_.accentBright);
    g.fillRoundedRectangle (juce::Rectangle<float> { position - 2.0f, track.getY() - 2.0f,
                                                     4.0f, track.getHeight() + 4.0f }, 2.0f);

    g.setColour (palette_.dimText.withAlpha (0.7f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("-1", track.withWidth (16.0f).translated (0.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("0",  track.withSizeKeepingCentre (16.0f, 10.0f).translated (0.0f, 12.0f),
                juce::Justification::centred);
    g.drawText ("+1", track.withLeft (track.getRight() - 18.0f).translated (0.0f, 12.0f),
                juce::Justification::centredRight);
}

// ---------------------------------------------------------------------------
// SpectrumView
// ---------------------------------------------------------------------------

namespace
{
constexpr double kLowHz  = 20.0;
constexpr double kHighHz = 20000.0;

/// Where the octave gridlines go.
constexpr double kGridHz[] { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 };

/// The display's vertical range, in dB.
constexpr float kTopDb   = 0.0f;
constexpr float kFloorDb = -84.0f;

/// Where an untouched peak-hold bin sits -- below the display, so it draws off
/// the bottom rather than as a flat line that reads like a measurement.
constexpr float kHoldFloorDb = -140.0f;
} // namespace

SpectrumView::SpectrumView (ui::Palette palette, dsp::ReferenceCurve& reference,
                            std::vector<float>& peakHold)
    : palette_ (palette), reference_ (reference), peakHold_ (peakHold)
{
    // 2048 points: about 23 Hz of resolution at 48 kHz, which separates the
    // harmonics of anything above a bass note and still redraws comfortably.
    analyser_.prepare (48000.0, 11, TranspectusProcessor::kSpectrumBins, kLowHz, kHighHz);

    // Falls slowly enough to read, holds peaks long enough to see them.
    analyser_.setBallistics (1.6f, 0.28f);

    setTooltip ("Point at it for a crosshair: frequency, the nearest note in cents, the "
                "signal's level there and the level the cursor itself is at. MAX makes that "
                "reading finer by giving the panel more pixels to spread the axis over. "
                "The live spectrum has a peak hold behind it. The dotted line is a pink-noise "
                "slope -- physics, not a genre target. Capture Reference measures a track you "
                "play through the plugin and overlays its balance; Difference then shows yours "
                "minus it, which is the EQ move stated directly.");
}

float SpectrumView::positionFor (double hz) const noexcept
{
    const double clamped = juce::jlimit (kLowHz, kHighHz, hz);
    return static_cast<float> (std::log (clamped / kLowHz) / std::log (kHighHz / kLowHz));
}

double SpectrumView::frequencyAt (float fraction) noexcept
{
    return kLowHz * std::pow (kHighHz / kLowHz, juce::jlimit (0.0f, 1.0f, fraction));
}

juce::Rectangle<float> SpectrumView::plotArea() const noexcept
{
    return getLocalBounds().toFloat().reduced (8.0f, 7.0f).withTrimmedTop (13.0f);
}

float SpectrumView::levelAt (float fraction) const noexcept
{
    const auto& db = analyser_.getMagnitudesDb();

    if (db.size() < 2)
        return kFloorDb;

    // The bins are laid out one per pixel-fraction across the width, and the
    // analyser spaces them logarithmically -- which is why the same fraction
    // serves for both the frequency and the bin index.
    const float position = juce::jlimit (0.0f, 1.0f, fraction)
                         * static_cast<float> (db.size() - 1);

    const auto low = static_cast<std::size_t> (position);
    const auto high = juce::jmin (low + 1, db.size() - 1);
    const float t = position - static_cast<float> (low);

    return db[low] + (db[high] - db[low]) * t;
}

void SpectrumView::mouseMove (const juce::MouseEvent& event)
{
    const auto area = plotArea();
    const auto position = event.position;

    // Only inside the plot: a crosshair over the caption would be reading off
    // an axis that is not there.
    auto next = area.contains (position) ? std::optional<juce::Point<float>> { position }
                                         : std::nullopt;

    if (next != cursor_)
    {
        cursor_ = next;
        repaint();
    }
}

void SpectrumView::mouseDrag (const juce::MouseEvent& event)
{
    mouseMove (event);
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    if (cursor_.has_value())
    {
        cursor_.reset();
        repaint();
    }
}

void SpectrumView::paintCrosshair (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (! cursor_.has_value())
        return;

    const auto point = *cursor_;

    const float xFraction = (point.x - area.getX()) / area.getWidth();
    const float yFraction = (point.y - area.getY()) / area.getHeight();

    const double hz = frequencyAt (xFraction);
    const float cursorDb = kTopDb - yFraction * (kTopDb - kFloorDb);
    const float signalDb = levelAt (xFraction);

    // ---- the hairlines --------------------------------------------------------

    g.setColour (palette_.text.withAlpha (0.35f));
    g.drawVerticalLine (juce::roundToInt (point.x), area.getY(), area.getBottom());
    g.drawHorizontalLine (juce::roundToInt (point.y), area.getX(), area.getRight());

    // A dot where the curve actually is at this frequency, so the signal
    // reading below is anchored to something visible rather than asserted.
    if (signalDb > kFloorDb)
    {
        const float curveY = area.getY() + area.getHeight()
                           * juce::jlimit (0.0f, 1.0f, (kTopDb - signalDb) / (kTopDb - kFloorDb));

        g.setColour (palette_.accentBright);
        g.fillEllipse (point.x - 3.0f, curveY - 3.0f, 6.0f, 6.0f);
    }

    // ---- the readout ----------------------------------------------------------

    // Rounded to an int rather than asking for zero decimal places: JUCE reads
    // a decimal count of 0 as "use the shortest accurate representation", which
    // turned 216 Hz into "215.799 Hz".
    const auto frequencyText = hz >= 1000.0
                                   ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz"
                                   : hz >= 100.0
                                         ? juce::String (juce::roundToInt (hz)) + " Hz"
                                         : juce::String (hz, 1) + " Hz";

    // The nearest note, because half of what a spectrum gets used for on this
    // rig is finding out what note a bass or a resonance is sitting on.
    static const char* const noteNames[] { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };

    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = static_cast<int> (std::lround (midi));
    const int cents = static_cast<int> (std::lround ((midi - nearest) * 100.0));

    juce::String noteText = juce::String (noteNames[((nearest % 12) + 12) % 12])
                          + juce::String (nearest / 12 - 1);

    if (cents != 0)
        noteText += (cents > 0 ? " +" : " ") + juce::String (cents);

    const juce::String lines[] {
        frequencyText + "   " + noteText,
        "signal  " + juce::String (signalDb, 1) + " dB",
        "cursor  " + juce::String (cursorDb, 1) + " dB",
    };

    const auto font = juce::Font (juce::FontOptions (11.0f));

    float widest = 0.0f;

    for (const auto& line : lines)
        widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, line));

    // Two pixels of slack on top of the padding: the text is drawn into an
    // integer rectangle, and rounding the exact measured width down is enough
    // to earn an ellipsis on the longest line.
    const float boxWidth = widest + 20.0f;
    constexpr float boxHeight = 52.0f;

    // Flipped to whichever side of the pointer has room, so the readout never
    // hangs off the panel and never covers the part of the curve being pointed
    // at.
    auto box = juce::Rectangle<float> { point.x + 12.0f, point.y + 12.0f, boxWidth, boxHeight };

    if (box.getRight() > area.getRight())
        box.setX (point.x - 12.0f - boxWidth);

    if (box.getBottom() > area.getBottom())
        box.setY (point.y - 12.0f - boxHeight);

    box = box.constrainedWithin (area);

    g.setColour (palette_.background.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (palette_.text.withAlpha (0.22f));
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    auto text = box.reduced (9.0f, 5.0f);
    g.setFont (font);

    bool first = true;

    for (const auto& line : lines)
    {
        g.setColour (first ? palette_.text : palette_.dimText);
        g.drawText (line, text.removeFromTop (14.0f).toNearestInt(),
                    juce::Justification::centredLeft);
        first = false;
    }
}

void SpectrumView::applyConfiguration (double sampleRate, int resolutionChoice)
{
    const int rateHz = juce::roundToInt (sampleRate > 0.0 ? sampleRate : 48000.0);
    const int resolution = juce::jlimit (0, 2, resolutionChoice);

    if (rateHz == configuredRateHz_ && resolution == configuredResolution_)
        return;

    configuredRateHz_ = rateHz;
    configuredResolution_ = resolution;

    // Fast is the original single 2048-point transform; Balanced doubles it;
    // Fine keeps the responsive short window for the top and adds a
    // 16384-point transform below 500 Hz -- 2.9 Hz of resolution where the
    // short window's 23.4 smeared the whole sub octave into one plateau.
    analyser_.prepare (static_cast<double> (rateHz), resolution == 1 ? 12 : 11,
                       TranspectusProcessor::kSpectrumBins, kLowHz, kHighHz);

    if (resolution == 2)
        analyser_.setBassTransform (14, 500.0);

    analyser_.setBallistics (1.6f, 0.28f);
}

bool SpectrumView::update (const dsp::SpectrumCapture& capture)
{
    if (! analyser_.update (capture))
        return false;

    // The permanent maximum. No decay at all, which is the whole point: the
    // analyser's own peak hold falls away after a second or two, and what a
    // mix decision needs is the worst thing that happened since you last asked.
    const auto& live = analyser_.getMagnitudesDb();

    if (peakHold_.size() == live.size())
        for (std::size_t i = 0; i < live.size(); ++i)
            peakHold_[i] = juce::jmax (peakHold_[i], live[i]);

    return true;
}

void SpectrumView::pushToCapture()
{
    if (reference_.isCapturing())
        reference_.push (analyser_.getMagnitudesDb().data(), analyser_.getNumBins());
}

void SpectrumView::setShowPeakHold (bool shouldShow)
{
    if (showPeakHold_ == shouldShow)
        return;

    showPeakHold_ = shouldShow;
    repaint();
}

void SpectrumView::resetPeakHold()
{
    // The same floor the processor uses, so a cleared bin looks cleared rather
    // than like a very quiet reading.
    std::fill (peakHold_.begin(), peakHold_.end(), kHoldFloorDb);
    repaint();
}

void SpectrumView::setShowPinkSlope (bool shouldShow)
{
    showPinkSlope_ = shouldShow;
    repaint();
}

void SpectrumView::setShowDifference (bool shouldShow)
{
    showDifference_ = shouldShow;
    repaint();
}

void SpectrumView::paintGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setFont (juce::FontOptions (9.0f));

    for (const double hz : kGridHz)
    {
        const float x = area.getX() + area.getWidth() * positionFor (hz);

        g.setColour (palette_.dimText.withAlpha (0.14f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom() - 11.0f);

        g.setColour (palette_.dimText.withAlpha (0.6f));
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k"
                                 : juce::String (juce::roundToInt (hz)),
                    juce::Rectangle<float> { x - 16.0f, area.getBottom() - 11.0f, 32.0f, 11.0f },
                    juce::Justification::centred);
    }

    for (float db = -20.0f; db > kFloorDb; db -= 20.0f)
    {
        const float y = area.getY() + area.getHeight() * (kTopDb - db) / (kTopDb - kFloorDb);

        g.setColour (palette_.dimText.withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }
}

void SpectrumView::paintCurve (juce::Graphics& g, juce::Rectangle<float> area,
                               const std::vector<float>& db, juce::Colour colour,
                               float thickness, bool fill) const
{
    if (db.size() < 2)
        return;

    juce::Path path;

    const auto yFor = [&area] (float value)
    {
        return area.getY() + area.getHeight()
             * juce::jlimit (0.0f, 1.0f, (kTopDb - value) / (kTopDb - kFloorDb));
    };

    for (std::size_t i = 0; i < db.size(); ++i)
    {
        const float x = area.getX() + area.getWidth()
                      * static_cast<float> (i) / static_cast<float> (db.size() - 1);
        const float y = yFor (db[i]);

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    if (fill)
    {
        juce::Path filled = path;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (colour.withAlpha (0.18f));
        g.fillPath (filled);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness));
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (8.0f, 7.0f);

    {
        // The caption stops short of whatever chrome the editor has put over the
        // top-right corner. The grid below still uses the full width.
        //
        // NB this removeFromTop is what plotArea() mirrors; the two must agree,
        // or the crosshair reads its dB off an axis a few pixels from the one
        // that was drawn.
        auto captionArea = area.removeFromTop (13.0f)
                               .withTrimmedRight (static_cast<float> (topRightInset_));

        if (captionArea.getWidth() >= 52.0f)
        {
            g.setColour (palette_.dimText);
            g.setFont (juce::FontOptions (10.5f));
            g.drawFittedText ("SPECTRUM", captionArea.toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
    }

    paintGrid (g, area);

    // A pink slope through the middle of the display: -3 dB per octave, which
    // is what equal energy per octave looks like. A reference for the eye, not
    // a target to hit.
    if (showPinkSlope_)
    {
        const double octaves = std::log2 (kHighHz / kLowHz);

        juce::Path slope;
        slope.startNewSubPath (area.getX(),
                               area.getY() + area.getHeight() * 0.22f);
        slope.lineTo (area.getRight(),
                      area.getY() + area.getHeight()
                          * (0.22f + static_cast<float> (3.0 * octaves) / (kTopDb - kFloorDb)));

        g.setColour (palette_.dimText.withAlpha (0.45f));

        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (slope, slope, dashes, 2);
        g.strokePath (slope, juce::PathStrokeType (1.0f));
    }

    // The peak hold behind the live curve, so a transient stays visible.
    paintCurve (g, area, analyser_.getPeaksDb(), palette_.accent.withAlpha (0.45f), 1.0f, false);

    // The permanent hold under the live curve, in a colour outside the family
    // the live curves use -- it is a different kind of statement and has to
    // read as one. Under rather than over, so it never hides what is happening
    // now.
    if (showPeakHold_ && peakHold_.size() == analyser_.getMagnitudesDb().size())
        paintCurve (g, area, peakHold_, palette_.hold.withAlpha (0.9f), 1.3f, false);

    paintCurve (g, area, analyser_.getMagnitudesDb(), palette_.accentBright, 1.6f, true);

    // The captured reference, drawn at the live curve's own average so the two
    // sit on top of each other rather than at arbitrary heights.
    if (reference_.hasCurve())
    {
        const auto& live = analyser_.getMagnitudesDb();
        const auto& curve = reference_.getCurveDb();

        double sum = 0.0;
        std::size_t counted = 0;

        for (const float value : live)
            if (value > dsp::ReferenceCurve::kFloorDb)
            {
                sum += value;
                ++counted;
            }

        const double mean = counted > 0 ? sum / static_cast<double> (counted) : -40.0;

        std::vector<float> shifted (curve.size());

        for (std::size_t i = 0; i < curve.size(); ++i)
            shifted[i] = static_cast<float> (curve[i] + mean);

        paintCurve (g, area, shifted, palette_.secondary, 1.8f, false);

        if (showDifference_)
        {
            reference_.computeDifference (live.data(), live.size(), difference_);

            // Drawn about the middle of the display: above the line means the
            // mix has more there than the reference does.
            const float centre = area.getCentreY();

            juce::Path path;

            for (std::size_t i = 0; i < difference_.size(); ++i)
            {
                const float x = area.getX() + area.getWidth()
                              * static_cast<float> (i)
                              / static_cast<float> (std::max<std::size_t> (difference_.size() - 1, 1));

                const float y = centre - static_cast<float> (difference_[i])
                              * area.getHeight() / (kTopDb - kFloorDb);

                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, juce::jlimit (area.getY(), area.getBottom(), y));
            }

            g.setColour (palette_.over.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.4f));

            g.setColour (palette_.dimText.withAlpha (0.4f));
            g.drawHorizontalLine (juce::roundToInt (centre), area.getX(), area.getRight());
        }
    }

    // A progress bar while capturing, so thirty seconds is visibly thirty
    // seconds rather than a button that appears to have done nothing.
    if (reference_.isCapturing())
    {
        auto strip = area.removeFromTop (4.0f);

        g.setColour (palette_.background);
        g.fillRect (strip);

        g.setColour (palette_.secondary);
        g.fillRect (strip.withWidth (strip.getWidth()
                                     * static_cast<float> (reference_.getProgress())));
    }

    // Last, so it reads over every curve rather than under one. plotArea()
    // rather than the local `area`, which the progress bar has by now eaten
    // into.
    paintCrosshair (g, plotArea());
}

// ---------------------------------------------------------------------------
// TranspectusEditor
// ---------------------------------------------------------------------------

PanelWindow::PanelWindow (const juce::String& name, juce::Colour background,
                          juce::Component& content, std::function<void()> onClose)
    : juce::DocumentWindow (name, background, juce::DocumentWindow::closeButton),
      onClose_ (std::move (onClose))
{
    setUsingNativeTitleBar (true);

    // Non-owned: the editor owns this component whether it is docked or
    // floating, so there is never a moment with two owners or none.
    setContentNonOwned (&content, false);

    setResizable (true, false);
    setResizeLimits (240, 200, 3000, 2400);
    setVisible (true);
}

void PanelWindow::placeBeside (juce::Rectangle<int> editorArea)
{
    // Beside the editor, not centred on the screen: a window that opens on top
    // of the thing it was detached from looks like the editor broke, and seeing
    // both at once is the entire point of detaching.
    auto area = juce::Rectangle<int> (editorArea.getRight() + 12, editorArea.getY(), 620, 480);

    // Constrained against every display rather than the editor's own, so a
    // second monitor is allowed and the void past the last one is not. The
    // default constrainer only keeps a *corner* on screen, which is how a
    // window ends up mostly off the edge and effectively lost.
    const auto screens = juce::Desktop::getInstance().getDisplays().getTotalBounds (true);

    if (! screens.isEmpty())
    {
        area.setSize (juce::jmin (area.getWidth(), screens.getWidth()),
                      juce::jmin (area.getHeight(), screens.getHeight()));
        area = area.constrainedWithin (screens);
    }

    setBounds (area);
}

void PanelWindow::closeButtonPressed()
{
    if (onClose_ != nullptr)
        onClose_();
}

// ---------------------------------------------------------------------------

TranspectusEditor::~TranspectusEditor()
{
    // Docking first makes teardown deterministic: every component ends up
    // parented where it started, and the windows are gone before anything they
    // referred to is.
    //
    // It is not load-bearing, and the comment that used to be here said it was.
    // ResizableWindow holds its content by weak reference, so a destroyed
    // component simply clears it -- closing the standalone with a panel
    // detached exits 0 with this whole function removed, and exits 0 again with
    // the member declaration order reversed on top of that. Kept because
    // explicit teardown is cheap and does not depend on either of those facts
    // staying true; not kept because it prevents a crash anybody has seen.
    setDetached (Panel::spectrum, false);
    setDetached (Panel::goniometer, false);
}

juce::Component& TranspectusEditor::componentFor (Panel panel) noexcept
{
    return panel == Panel::spectrum ? static_cast<juce::Component&> (*spectrum_)
                                    : static_cast<juce::Component&> (*goniometer_);
}

bool TranspectusEditor::isDetached (Panel panel) const noexcept
{
    return (panel == Panel::spectrum ? spectrumWindow_ : goniometerWindow_) != nullptr;
}

void TranspectusEditor::setMaximised (Panel panel, bool shouldMaximise)
{
    // A detached panel is already as large as its window; maximising it inside
    // the editor would hide everything to make room for something that is not
    // there.
    if (shouldMaximise && isDetached (panel))
        return;

    maximised_ = shouldMaximise ? std::optional<Panel> { panel } : std::nullopt;

    updatePanelChrome();
    resized();
}

void TranspectusEditor::setDetached (Panel panel, bool shouldDetach)
{
    auto& window = panel == Panel::spectrum ? spectrumWindow_ : goniometerWindow_;

    if ((window != nullptr) == shouldDetach)
        return;

    auto& component = componentFor (panel);

    if (shouldDetach)
    {
        // A maximised panel that is then detached would leave the body given
        // over to something no longer in it.
        if (maximised_ == panel)
            maximised_.reset();

        removeChildComponent (&component);

        window = std::make_unique<PanelWindow> (
            panel == Panel::spectrum ? "Transpectus \xe2\x80\x94 Spectrum"
                                     : "Transpectus \xe2\x80\x94 Goniometer",
            palette_.background, component,
            [this, panel] { setDetached (panel, false); });

        window->placeBeside (getScreenBounds());
    }
    else
    {
        // Clear the content first, so the window is not holding a component
        // that is simultaneously being adopted by the editor.
        window->clearContentComponent();
        window.reset();

        addAndMakeVisible (component);
    }

    updatePanelChrome();
    resized();
}

void TranspectusEditor::updatePanelChrome()
{
    const auto label = [] (juce::TextButton& button, const char* text, bool enabled)
    {
        button.setButtonText (text);
        button.setEnabled (enabled);
    };

    label (spectrumMaxButton_, maximised_ == Panel::spectrum ? "MIN" : "MAX",
           ! isDetached (Panel::spectrum));
    label (goniometerMaxButton_, maximised_ == Panel::goniometer ? "MIN" : "MAX",
           ! isDetached (Panel::goniometer));

    // Detached, the POP button stops being corner chrome and becomes the
    // placeholder standing where the panel was -- so it gets room to say what
    // it will bring back, which matters when the floating window has gone
    // behind the DAW and looks lost.
    label (spectrumPopButton_, isDetached (Panel::spectrum) ? "DOCK SPECTRUM" : "POP", true);
    label (goniometerPopButton_, isDetached (Panel::goniometer) ? "DOCK GONIOMETER" : "POP", true);

    // The chrome belongs to the panel, so it goes with it: a MAX button left
    // floating over an empty rectangle in the editor would be a control for
    // something that is not there.
    const bool spectrumHere = ! isDetached (Panel::spectrum)
                           && (! maximised_ || maximised_ == Panel::spectrum);
    const bool goniometerHere = ! isDetached (Panel::goniometer)
                             && (! maximised_ || maximised_ == Panel::goniometer);

    spectrumMaxButton_.setVisible (spectrumHere);
    goniometerMaxButton_.setVisible (goniometerHere);
    resetImageButton_.setVisible (goniometerHere);
    spectrumPopButton_.setVisible (spectrumHere || isDetached (Panel::spectrum));
    goniometerPopButton_.setVisible (goniometerHere || isDetached (Panel::goniometer));

    // Chrome sits *over* its panel, and z-order is insertion order: docking a
    // panel re-adds it, which puts it at the front of the child list, in front
    // of buttons that were added long before it.
    //
    // The symptom is worth writing down because it is not what it looks like.
    // The buttons stayed visible and stayed enabled -- they were simply behind
    // an opaque panel, which from the outside is indistinguishable from having
    // been hidden. Only the panel that was re-added lost its chrome, and
    // reopening the editor "fixed" it by rebuilding in the original order.
    for (auto* button : { &spectrumMaxButton_, &spectrumPopButton_,
                          &goniometerMaxButton_, &goniometerPopButton_,
                          &resetImageButton_ })
        button->toFront (false);
}

void TranspectusEditor::buildPanelChrome()
{
    const auto style = [this] (juce::TextButton& button, const char* tooltip)
    {
        button.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.25f));
        button.setColour (juce::TextButton::textColourOffId, palette_.dimText);
        button.setTooltip (tooltip);
        addAndMakeVisible (button);
    };

    // Stable IDs so the editor can be driven headlessly -- `tezla-render editor
    // <id>...` clicks these, which is how the detach path gets exercised in a
    // container with no window manager.
    spectrumMaxButton_.setComponentID ("spectrum-max");
    spectrumPopButton_.setComponentID ("spectrum-pop");
    goniometerMaxButton_.setComponentID ("goniometer-max");
    goniometerPopButton_.setComponentID ("goniometer-pop");

    style (spectrumMaxButton_, "Gives the spectrum the whole panel. Everything else is hidden "
                               "while it is -- if you want a large spectrum and the numbers at "
                               "the same time, POP it into its own window instead.");
    style (spectrumPopButton_, "Opens the spectrum in its own resizable window, so it can live "
                               "on a second monitor while the meters stay here. Closing that "
                               "window puts it back.");
    style (goniometerMaxButton_, "Gives the goniometer the whole panel.");

    style (resetImageButton_, "Clears the violet image hold -- the outline of the widest the "
                              "stereo picture has been -- and the held worst-correlation ticks, "
                              "and starts collecting again. Same idea as RESET PEAKS on the "
                              "spectrum: make a width move, clear, play the section back, and "
                              "see what the new worst case is.");
    resetImageButton_.setColour (juce::TextButton::textColourOffId, palette_.hold);
    resetImageButton_.setComponentID ("reset-image");
    resetImageButton_.onClick = [this]
    {
        transpectus_.resetImageExcursionHold();
        transpectus_.getEngine().resetCorrelationHold();
    };
    style (goniometerPopButton_, "Opens the goniometer in its own resizable window.");

    spectrumMaxButton_.onClick = [this]
    {
        setMaximised (Panel::spectrum, maximised_ != Panel::spectrum);
    };

    goniometerMaxButton_.onClick = [this]
    {
        setMaximised (Panel::goniometer, maximised_ != Panel::goniometer);
    };

    spectrumPopButton_.onClick = [this]
    {
        setDetached (Panel::spectrum, ! isDetached (Panel::spectrum));
    };

    goniometerPopButton_.onClick = [this]
    {
        setDetached (Panel::goniometer, ! isDetached (Panel::goniometer));
    };

    updatePanelChrome();
}

// ---------------------------------------------------------------------------

TranspectusEditor::TranspectusEditor (TranspectusProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      transpectus_ (processorToUse),
      palette_ (kPalette)
{
    header_ = std::make_unique<ui::HeaderBar> (
        transpectus_.getState(), "TRANSPECTUS",
        "Loudness, true peak and stereo analysis", ids::bypass, palette_);

    header_->onSwapRequested = [this]
    {
        transpectus_.getAbCompare().swapSlots();
        header_->setActiveSlot (transpectus_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (transpectus_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        transpectus_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (transpectus_.getAbCompare().otherSlotFilled());
    };

    // The TIPS button. The flag lives on the processor so it survives the
    // window being closed, and the header is told the current value rather
    // than assuming its own default -- reopening a panel whose tips were off
    // must not turn them back on.
    header_->onTooltipsToggled = [this] (bool enabled)
    {
        transpectus_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (transpectus_.getTooltipsEnabled());
    tooltips_.setEnabled (transpectus_.getTooltipsEnabled());

    addAndMakeVisible (*header_);

    buildControls();
    buildPanelChrome();

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (980, 700);

    startTimerHz (20);
}

void TranspectusEditor::buildControls()
{
    const auto makeReadout = [this] (const char* caption, const char* unit, const char* tooltip)
    {
        auto readout = std::make_unique<Readout> (palette_, caption, unit);
        readout->setTooltip (tooltip);
        addAndMakeVisible (*readout);
        return readout;
    };

    integrated_ = makeReadout ("INTEGRATED", "LUFS",
        "The whole programme, gated to ITU-R BS.1770-5: quiet passages below -70 LUFS, and "
        "anything more than 10 LU under the average, are thrown out before the mean is taken. "
        "This is the number a streaming platform measures.");

    shortTerm_ = makeReadout ("SHORT TERM", "LUFS",
        "Three seconds, ungated. How loud this section is, which is the reading to mix "
        "against.");

    momentary_ = makeReadout ("MOMENTARY", "LUFS",
        "Four hundred milliseconds, ungated. Moves with the bar.");

    truePeak_ = makeReadout ("TRUE PEAK", "dBTP",
        "The highest the waveform reaches between the samples, held until you reset. Samples "
        "all under full scale still reconstruct above it -- measured on this rig at +1.5 dB on "
        "dense content and +3.0 dB on a tone at a quarter of the sample rate, which is why "
        "delivery specs ask for -1 dBTP rather than 0.");

    plr_ = makeReadout ("PLR", "dB",
        "True peak minus integrated loudness: how much transient survived the whole "
        "programme. Roughly 15-20 dB is classical, 8-12 a pop master, 5-8 a loud EDM master. "
        "Below about 5 the transients are gone -- the kick has no click left. Guidance, not a "
        "rule.");

    psr_ = makeReadout ("PSR", "dB",
        "The same subtraction against short-term loudness, so it moves bar to bar. This is "
        "the one to watch while working: it shows the punch of what is playing rather than "
        "the average of everything.");

    delta_ = makeReadout ("VS TARGET", "dB",
        "What the selected platform will do to this master. Several of them only ever turn a "
        "loud master down -- a quiet one on YouTube simply plays quiet -- so this reads zero "
        "there rather than promising a boost that will not happen.");

    inputMeter_ = std::make_unique<ui::LevelMeter> (palette_);
    inputMeter_->setReferenceDb (static_cast<float> (kDeliveryTruePeakDb));
    inputMeter_->setScaleVisible (true);
    inputMeter_->setTooltip ("Sample peak, with the hold marked against -1 dBFS -- the delivery "
                             "ceiling every platform in the list expects. Click to clear.");
    addAndMakeVisible (*inputMeter_);

    fullCorrelation_ = std::make_unique<CorrelationBar> (palette_);
    fullCorrelation_->setCaption ("CORRELATION  full band");
    fullCorrelation_->setTooltip ("+1 the channels agree, 0 uncorrelated, -1 polarity inverted. "
                                  "A full-band reading is mostly mid and top, so it can look "
                                  "healthy while the sub underneath cancels -- which is what the "
                                  "reading below it is for.");
    addAndMakeVisible (*fullCorrelation_);

    lowCorrelation_ = std::make_unique<CorrelationBar> (palette_);
    lowCorrelation_->setCaption ("CORRELATION  sub");
    lowCorrelation_->setTooltip ("The one that matters on a club system. Below the Mono Check "
                                 "frequency this should sit near +1: anything lower and the bass "
                                 "loses level when the rig sums to mono, and headphones will "
                                 "never show it because nothing sums there.");
    addAndMakeVisible (*lowCorrelation_);

    goniometer_ = std::make_unique<ui::Goniometer> (palette_);

    // The excursion hold lives on the processor so it survives this window;
    // sized here because the sector count is the goniometer's business.
    {
        auto& hold = transpectus_.getImageExcursionHold();

        if (hold.size() != static_cast<std::size_t> (ui::Goniometer::kHoldSectors))
            hold.assign (static_cast<std::size_t> (ui::Goniometer::kHoldSectors), 0.0f);

        goniometer_->attachExcursionHold (&hold);
    }
    goniometer_->setTooltip ("The sample pairs the correlation numbers summarise, rotated so mono "
                             "is vertical. A tall narrow shape is a mono-ish mix; a wide one is a "
                             "wide mix; a horizontal one is out of phase. A shape leaning left or "
                             "right is a lopsided mix, which no correlation number will tell you. "
                             "Fifty milliseconds of history at every sample rate. The violet "
                             "outline is the widest the image has ever been since the last RESET "
                             "IMAGE -- it survives closing the window, like every held reading "
                             "here -- and the violet ticks on the correlation bars hold the worst "
                             "moment each has seen.");
    addAndMakeVisible (*goniometer_);

    // ---- controls ------------------------------------------------------------

    targetBox_.addItemList (choices::targetNames(), 1);
    targetBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    targetBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    targetBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    targetBox_.setTooltip ("Which platform the VS TARGET readout is computed against. The "
                           "figures were verified in August 2026 and are stored with that date, "
                           "because platforms change them.");
    addAndMakeVisible (targetBox_);

    targetAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        transpectus_.getState(), ids::target, targetBox_);

    truePeakBox_.addItemList (choices::truePeak, 1);
    truePeakBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    truePeakBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    truePeakBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    truePeakBox_.setTooltip ("Off is sample peak and reads up to 3 dB low. Standard is the ITU's "
                             "own filter and agrees with every other dBTP meter. Strict "
                             "interpolates 16x and is the one to believe, at about four times "
                             "the CPU.");
    addAndMakeVisible (truePeakBox_);

    truePeakAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        transpectus_.getState(), ids::truePeak, truePeakBox_);

    for (auto* label : { &targetLabel_, &truePeakLabel_ })
    {
        label->setJustificationType (juce::Justification::centredLeft);
        label->setColour (juce::Label::textColourId, palette_.dimText);
        label->setFont (juce::FontOptions (10.5f));
        addAndMakeVisible (label);
    }

    resetButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    resetButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    resetButton_.setTooltip ("Clears the integrated reading, the true-peak hold and the level "
                             "meter's hold, and starts measuring again. The filters are not "
                             "disturbed, so the momentary reading keeps running.");
    resetButton_.onClick = [this]
    {
        transpectus_.resetMeasurement();
        inputMeter_->resetHold();

        // Everything held, including the spectrum's and the stereo image's.
        // "Restart measurement" that left one held reading standing would be
        // the surprising one.
        spectrum_->resetPeakHold();
        transpectus_.resetImageExcursionHold();
    };
    addAndMakeVisible (resetButton_);

    spectrum_ = std::make_unique<SpectrumView> (palette_, transpectus_.getReferenceCurve(),
                                                transpectus_.getSpectrumPeakHold());
    spectrum_->setComponentID ("spectrum");
    addAndMakeVisible (*spectrum_);

    captureButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    captureButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    captureButton_.setTooltip ("Plays a track through the plugin for thirty seconds and stores "
                               "its tonal balance. Thirty rather than two: a short capture is a "
                               "snapshot of one chord and says nothing about balance. The curve "
                               "is normalised, so a quiet reference and a loud one compare on "
                               "shape rather than volume, and it saves with the project.");
    captureButton_.onClick = [this]
    {
        auto& reference = transpectus_.getReferenceCurve();

        if (reference.isCapturing())
            reference.cancelCapture();
        else
            reference.beginCapture (dsp::ReferenceCurve::kDefaultSeconds, 20.0);
    };
    addAndMakeVisible (captureButton_);

    clearReferenceButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    clearReferenceButton_.setColour (juce::TextButton::textColourOffId, palette_.dimText);
    clearReferenceButton_.setTooltip ("Throws the stored reference away.");
    clearReferenceButton_.onClick = [this]
    {
        transpectus_.getReferenceCurve().clear();
        differenceButton_.setToggleState (false, juce::sendNotification);
    };
    addAndMakeVisible (clearReferenceButton_);

    // A reference stored only in the project travels with that project and
    // nowhere else, which is the wrong shape for the thing it is: a reference
    // is something you keep and reuse across everything you make. So it lives
    // in both places -- in the state, and in a file you can name, copy and
    // read. The file is plain text on purpose; a curve you cannot diff or
    // inspect is a curve you have to trust.
    saveReferenceButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    saveReferenceButton_.setColour (juce::TextButton::textColourOffId, palette_.dimText);
    saveReferenceButton_.setTooltip ("Writes the stored curve to a .tzref file, so the same "
                                     "reference can be used in every project rather than only "
                                     "in this one. Plain text: 96 numbers and a header.");
    saveReferenceButton_.onClick = [this] { saveReference(); };
    addAndMakeVisible (saveReferenceButton_);

    loadReferenceButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    loadReferenceButton_.setColour (juce::TextButton::textColourOffId, palette_.dimText);
    loadReferenceButton_.setTooltip ("Reads a .tzref file back. It replaces whatever is stored "
                                     "now, and is then saved with the project like any other "
                                     "capture.");
    loadReferenceButton_.onClick = [this] { loadReference(); };
    addAndMakeVisible (loadReferenceButton_);

    pinkButton_.setColour (juce::ToggleButton::textColourId, palette_.text);
    pinkButton_.setColour (juce::ToggleButton::tickColourId, palette_.accent);
    pinkButton_.setToggleState (true, juce::dontSendNotification);
    pinkButton_.setTooltip ("A -3 dB per octave slope: what equal energy per octave looks like. "
                            "Physics rather than a genre target, and the only reference curve "
                            "this plugin ships with.");
    pinkButton_.onClick = [this] { spectrum_->setShowPinkSlope (pinkButton_.getToggleState()); };
    addAndMakeVisible (pinkButton_);

    differenceButton_.setColour (juce::ToggleButton::textColourId, palette_.text);
    differenceButton_.setColour (juce::ToggleButton::tickColourId, palette_.over);
    differenceButton_.setTooltip ("Draws your spectrum minus the captured reference about the "
                                  "centre line. Above it you have more than the reference does; "
                                  "below it, less. Needs a captured reference.");
    differenceButton_.onClick = [this]
    {
        spectrum_->setShowDifference (differenceButton_.getToggleState());
    };
    addAndMakeVisible (differenceButton_);

    peakHoldButton_.setColour (juce::ToggleButton::textColourId, palette_.text);
    peakHoldButton_.setColour (juce::ToggleButton::tickColourId, palette_.hold);
    peakHoldButton_.setToggleState (true, juce::dontSendNotification);
    peakHoldButton_.setTooltip ("A permanent maximum, in violet, of the loudest each frequency "
                                "has been since the last reset. It never decays -- the faint "
                                "green trace behind the live curve is the analyser's own hold "
                                "and falls away in about a second, which is a different question. "
                                "This one answers \"what is the worst this mix does\", and it "
                                "survives closing the window.");
    peakHoldButton_.setComponentID ("peak-hold");
    peakHoldButton_.onClick = [this]
    {
        spectrum_->setShowPeakHold (peakHoldButton_.getToggleState());
    };
    addAndMakeVisible (peakHoldButton_);

    resetPeaksButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    resetPeaksButton_.setColour (juce::TextButton::textColourOffId, palette_.hold);
    resetPeaksButton_.setTooltip ("Throws the violet maximum away and starts collecting again. "
                                  "This is how the feature gets used: make an EQ move, clear, "
                                  "play the section again, and see what the new worst case is.");
    resetPeaksButton_.setComponentID ("reset-peaks");
    resetPeaksButton_.onClick = [this] { spectrum_->resetPeakHold(); };
    addAndMakeVisible (resetPeaksButton_);

    resolutionBox_.addItemList (choices::resolution, 1);
    resolutionBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    resolutionBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    resolutionBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    resolutionBox_.setTooltip ("How finely the transform resolves, and what each setting costs "
                               "in time. Fast is one 2048-point window: 43 ms of audio, 23 Hz "
                               "of resolution at 48 kHz -- it cannot separate a whole tone "
                               "below 770 Hz, and the sub octave reads as one plateau. "
                               "Balanced doubles the window. Fine keeps the fast window for "
                               "the top and adds a 16384-point one below 500 Hz: 2.9 Hz of "
                               "resolution down there, so a bass line moving a fourth shows "
                               "as two peaks 15 dB apart where Fast showed a flat line. The "
                               "price is honesty about time -- the long window is a third of "
                               "a second, so the bass region breathes at bass-note speed "
                               "rather than transient speed.");
    resolutionAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        transpectus_.getState(), ids::resolution, resolutionBox_);
    addAndMakeVisible (resolutionBox_);

    statusLabel_.setJustificationType (juce::Justification::centred);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (statusLabel_);
}

juce::String TranspectusEditor::formatLufs (double lufs)
{
    return lufs <= kSilenceThreshold ? juce::String::fromUTF8 ("-\xe2\x88\x9e")
                                     : juce::String (lufs, 1);
}

void TranspectusEditor::timerCallback()
{
    auto& engine = transpectus_.getEngine();

    const double integrated = engine.getIntegratedLufs();

    integrated_->setValue (formatLufs (integrated));
    shortTerm_->setValue (formatLufs (engine.getShortTermLufs()));
    momentary_->setValue (formatLufs (engine.getMomentaryLufs()));

    const double peak = engine.getTruePeakDb();
    truePeak_->setValue (peak <= kSilenceThreshold ? juce::String::fromUTF8 ("-\xe2\x88\x9e")
                                                   : juce::String (peak, 2));
    truePeak_->setWarning (peak > kDeliveryTruePeakDb);

    // PLR, with the interpretation the number needs to be worth reading.
    const double plr = engine.getPlr();

    if (plr <= 0.0)
    {
        plr_->setValue ("--");
        plr_->setWarning (false);
    }
    else
    {
        plr_->setValue (juce::String (plr, 1),
                        plr < kPlrSquashed ? "transients gone"
                      : plr < kPlrHealthy  ? "loud master"
                                           : "dynamics intact");
        plr_->setWarning (plr < kPlrSquashed);
    }

    const double psr = engine.getPsr();
    psr_->setValue (psr <= 0.0 ? "--" : juce::String (psr, 1));
    psr_->setWarning (psr > 0.0 && psr < kPlrSquashed);

    // What the platform does, in words rather than a signed number nobody
    // wants to interpret twice.
    const auto& target = engine.getTarget();
    const double delta = engine.getTargetDeltaDb();

    if (integrated <= kSilenceThreshold)
    {
        delta_->setValue ("--");
        delta_->setWarning (false);
    }
    else if (std::abs (delta) < 0.05)
    {
        delta_->setValue ("0.0", target.boostsQuietMaterial
                                     ? juce::String ("at ") + target.name
                                     : juce::String ("unchanged on ") + target.name);
        delta_->setWarning (false);
    }
    else
    {
        delta_->setValue (juce::String (std::abs (delta), 1),
                          delta > 0.0 ? juce::String ("turned down on ") + target.name
                                      : juce::String ("turned up on ") + target.name);
        delta_->setWarning (false);
    }

    fullCorrelation_->setValue (static_cast<float> (engine.getCorrelation()), false);
    fullCorrelation_->setHeldMinimum (static_cast<float> (engine.getMinCorrelation()));

    const double low = engine.getBandCorrelation (dsp::StereoAnalyser::low);
    lowCorrelation_->setValue (static_cast<float> (low), ! engine.isLowBandMonoSafe());
    lowCorrelation_->setHeldMinimum (static_cast<float> (engine.getMinLowCorrelation()));

    fullCorrelation_->repaint();
    lowCorrelation_->repaint();

    goniometer_->update (engine.getStereoScope());
    goniometer_->repaint();

    // Track the host rate and the Resolution choice every tick -- a no-op
    // when nothing changed, a rebuild when it did. Before this, the analyser
    // stayed at its construction-time 48 kHz forever, and a 96 kHz session
    // read every frequency at half its true value.
    spectrum_->applyConfiguration (
        transpectus_.getSampleRate(),
        juce::roundToInt (transpectus_.getState()
                              .getRawParameterValue (ids::resolution)->load()));

    // Fold the latest window onto the display bins, then feed the capture if
    // one is running -- in that order, so the capture sees the same frame the
    // user is looking at.
    if (spectrum_->update (engine.getSpectrumCapture()))
    {
        spectrum_->pushToCapture();
        spectrum_->repaint();
    }

    auto& reference = transpectus_.getReferenceCurve();

    captureButton_.setButtonText (reference.isCapturing()
        ? "CAPTURING  " + juce::String (juce::roundToInt (reference.getProgress() * 100.0)) + "%"
        : "CAPTURE REFERENCE");

    clearReferenceButton_.setEnabled (reference.hasCurve());
    saveReferenceButton_.setEnabled (reference.hasCurve());
    loadReferenceButton_.setEnabled (! reference.isCapturing());
    differenceButton_.setEnabled (reference.hasCurve());

    inputMeter_->setValues (static_cast<float> (peak), static_cast<float> (peak));
    inputMeter_->repaint();

    header_->setActiveSlot (transpectus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (transpectus_.getAbCompare().otherSlotFilled());

    // The line that says what is and is not being measured.
    juce::String status;

    if (! transpectus_.isPrepared())
    {
        status = "Waiting for the host to start audio.";
    }
    else if (transpectus_.getState().getRawParameterValue (ids::bypass)->load() > 0.5f)
    {
        status = "Bypassed \xe2\x80\x94 measurement is paused. The audio is untouched either way; "
                 "this plugin never changes it.";
    }
    else
    {
        status << "Measuring at " << juce::String (engine.getSampleRate() / 1000.0, 1)
               << " kHz \xe2\x80\xa2 true peak x"
               << dsp::truePeakFactorFor (
                      static_cast<dsp::TruePeakMode> (juce::jlimit (0, 2, static_cast<int> (
                          transpectus_.getState().getRawParameterValue (ids::truePeak)->load()))),
                      engine.getSampleRate())
               << " \xe2\x80\xa2 mono check below "
               << juce::String (juce::roundToInt (
                      transpectus_.getState().getRawParameterValue (ids::monoCheckHz)->load()))
               << " Hz \xe2\x80\xa2 latency 0";
    }

    // A notice from SAVE or LOAD holds the line for a few seconds; the running
    // report resumes on its own afterwards.
    if (juce::Time::getMillisecondCounter() >= noticeUntilMs_)
        statusLabel_.setText (status, juce::dontSendNotification);
}

void TranspectusEditor::showNotice (juce::String text)
{
    statusLabel_.setText (text, juce::dontSendNotification);
    noticeUntilMs_ = juce::Time::getMillisecondCounter() + 4000;
}

void TranspectusEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

juce::File TranspectusEditor::referenceFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

void TranspectusEditor::saveReference()
{
    auto& reference = transpectus_.getReferenceCurve();

    if (! reference.hasCurve())
        return;

    chooser_ = std::make_unique<juce::FileChooser> (
        "Save this reference curve", referenceFolder().getChildFile ("reference.tzref"), "*.tzref");

    chooser_->launchAsync (juce::FileBrowserComponent::saveMode
                               | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& result)
        {
            auto file = result.getResult();

            if (file == juce::File {})
                return;

            // The extension is how the plugin finds these again. A user who
            // types a bare name gets one anyway rather than a file the LOAD
            // browser then hides from them.
            if (! file.hasFileExtension ("tzref"))
                file = file.withFileExtension ("tzref");

            const auto text = transpectus_.getReferenceCurve().toText();

            if (file.replaceWithText (juce::String (text)))
                showNotice ("Saved " + file.getFileName());
            else
                showNotice ("Could not write " + file.getFullPathName());
        });
}

void TranspectusEditor::loadReference()
{
    chooser_ = std::make_unique<juce::FileChooser> (
        "Load a reference curve", referenceFolder(), "*.tzref");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& result)
        {
            const auto file = result.getResult();

            if (file == juce::File {} || ! file.existsAsFile())
                return;

            // fromText() validates and returns false rather than half-loading,
            // so a truncated or foreign file leaves the current curve alone.
            if (transpectus_.getReferenceCurve().fromText (file.loadFileAsString().toStdString()))
            {
                showNotice ("Loaded " + file.getFileName());
                spectrum_->repaint();
            }
            else
            {
                showNotice (file.getFileName() + " is not a reference curve");
            }
        });
}

void TranspectusEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));
    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 4));

    // Controls along the bottom, above the status line.
    {
        auto controls = bounds.removeFromBottom (kControlHeight).reduced (10, 4);

        auto target = controls.removeFromLeft (juce::jmin (280, controls.getWidth() / 2));
        targetLabel_.setBounds (target.removeFromTop (13));
        targetBox_.setBounds (target.reduced (0, 1));

        controls.removeFromLeft (12);

        auto truePeak = controls.removeFromLeft (150);
        truePeakLabel_.setBounds (truePeak.removeFromTop (13));
        truePeakBox_.setBounds (truePeak.reduced (0, 1));

        resetButton_.setBounds (controls.removeFromRight (juce::jmin (190, controls.getWidth()))
                                    .withSizeKeepingCentre (
                                        juce::jmin (190, controls.getWidth()), 26));
    }

    auto body = bounds.reduced (10, 6);

    // The level meter down the right-hand side, as in every other plugin here.
    // It stays whatever else the body is doing: it is the one reading you want
    // in peripheral vision rather than looked at.
    auto meterColumn = body.removeFromRight (ui::LevelMeter::kMinimumWidth
                                             + ui::LevelMeter::kScaleWidth + 8).reduced (4, 0);
    inputMeter_->setBounds (meterColumn);

    body.removeFromRight (8);

    // Two small buttons over a panel's top-right corner, in the strip its own
    // caption occupies.
    // Two small buttons over a panel's top-right corner, in the strip its own
    // caption occupies -- and the panel is told how much room they take, so the
    // caption gets out of the way rather than being drawn underneath them.
    constexpr int kChromeWidth = 42 + 4 + 38 + 6;

    const auto placeChrome = [] (juce::Rectangle<int> panel,
                                 juce::TextButton& maximise, juce::TextButton& detach)
    {
        auto strip = panel.reduced (6, 5).removeFromTop (16);

        detach.setBounds (strip.removeFromRight (42));
        strip.removeFromRight (4);
        maximise.setBounds (strip.removeFromRight (38));
    };

    spectrum_->setTopRightInset (kChromeWidth);
    goniometer_->setTopRightInset (kChromeWidth);

    const auto readoutsVisible = ! maximised_.has_value();

    for (auto* readout : { integrated_.get(), shortTerm_.get(), momentary_.get(),
                           truePeak_.get(), plr_.get(), psr_.get(), delta_.get() })
        readout->setVisible (readoutsVisible);

    // Everything below belongs to one panel or the other; hidden here rather
    // than moved off-screen, so a hidden panel costs no paint at all.
    const bool spectrumInBody   = ! isDetached (Panel::spectrum)
                               && (! maximised_ || maximised_ == Panel::spectrum);
    const bool goniometerInBody = ! isDetached (Panel::goniometer)
                               && (! maximised_ || maximised_ == Panel::goniometer);
    const bool positionRow      = ! maximised_.has_value();

    const std::initializer_list<juce::Component*> spectrumControlList {
        &captureButton_, &clearReferenceButton_, &saveReferenceButton_,
        &loadReferenceButton_, &pinkButton_, &differenceButton_,
        &peakHoldButton_, &resetPeaksButton_ };

    for (auto* c : spectrumControlList)
        c->setVisible (spectrumInBody);

    fullCorrelation_->setVisible (positionRow);
    lowCorrelation_->setVisible (positionRow);

    // The panels themselves, and not only their controls. Leaving this out is
    // not a layout nuisance: a hidden-by-omission panel keeps its old bounds
    // and keeps painting, so maximising the goniometer drew the spectrum
    // straight across the middle of it.
    //
    // A *detached* panel stays visible -- it is showing inside its own window,
    // and "not in the editor's body" is not the same statement as "not on
    // screen". Getting that wrong opens an empty window.
    spectrum_->setVisible (isDetached (Panel::spectrum) || spectrumInBody);
    goniometer_->setVisible (isDetached (Panel::goniometer) || goniometerInBody);

    // ---- maximised: one panel, the whole body ---------------------------------

    if (maximised_ == Panel::goniometer)
    {
        goniometer_->setBounds (body.reduced (4, 2));
        placeChrome (goniometer_->getBounds(), goniometerMaxButton_, goniometerPopButton_);
        return;
    }

    if (maximised_ == Panel::spectrum)
    {
        auto spectrumControls = body.removeFromBottom (spectrumControlsHeight (body.getWidth()))
                                    .reduced (4, 2);
        layOutSpectrumControls (spectrumControls);

        spectrum_->setBounds (body.reduced (4, 2));
        placeChrome (spectrum_->getBounds(), spectrumMaxButton_, spectrumPopButton_);
        return;
    }

    // ---- the shared body ------------------------------------------------------

    // Seven readouts on two rows. The caption sits beside its number rather than
    // above it, which is what lets a row be 44 pixels instead of 76 -- and the
    // 64 that buys goes to the spectrum, which is where the eye spends its time.
    const int readoutHeight = juce::jlimit (38, 50, body.getHeight() / 11);

    {
        auto top = body.removeFromTop (readoutHeight);
        const int width = top.getWidth() / 4;

        integrated_->setBounds (top.removeFromLeft (width).reduced (3, 2));
        shortTerm_->setBounds  (top.removeFromLeft (width).reduced (3, 2));
        momentary_->setBounds  (top.removeFromLeft (width).reduced (3, 2));
        truePeak_->setBounds   (top.reduced (3, 2));
    }

    {
        // PLR and PSR are one number each; VS TARGET carries a sentence, so it
        // takes the room the other two do not need.
        auto second = body.removeFromTop (readoutHeight + 12);
        const int narrow = second.getWidth() / 5;

        plr_->setBounds   (second.removeFromLeft (narrow).reduced (3, 2));
        psr_->setBounds   (second.removeFromLeft (narrow).reduced (3, 2));
        delta_->setBounds (second.reduced (3, 2));
    }

    // Position across the bottom: the goniometer square on the left with the two
    // correlation bars stacked beside it, so the picture and the numbers that
    // summarise it are read together. The spectrum fills whatever is left.
    {
        // Two fifths rather than a third: the goniometer is square, so its
        // height is also its width, and a strip sized for two bars leaves a
        // circle too small to read a lean off.
        const int positionHeight = juce::jlimit (120, 200, body.getHeight() * 2 / 5);
        auto position = body.removeFromBottom (positionHeight);

        if (goniometerInBody)
        {
            // Square, taken from the height, so it stays a circle at every
            // window width.
            goniometer_->setBounds (position.removeFromLeft (position.getHeight()).reduced (4, 4));
            placeChrome (goniometer_->getBounds(), goniometerMaxButton_, goniometerPopButton_);

            resetImageButton_.setBounds (goniometer_->getBounds()
                                             .reduced (6, 5)
                                             .removeFromBottom (16)
                                             .removeFromLeft (86));
        }
        else
        {
            // Detached: the bars take the whole strip rather than leaving a
            // square hole where the picture used to be.
            goniometerPopButton_.setBounds (position.removeFromLeft (152)
                                                .withSizeKeepingCentre (140, 26));
        }

        const int barHeight = position.getHeight() / 2;
        fullCorrelation_->setBounds (position.removeFromTop (barHeight).reduced (4, 4));
        lowCorrelation_->setBounds (position.reduced (4, 4));
    }

    if (spectrumInBody)
    {
        auto spectrumControls = body.removeFromBottom (spectrumControlsHeight (body.getWidth()))
                                    .reduced (4, 2);
        layOutSpectrumControls (spectrumControls);

        spectrum_->setBounds (body.reduced (4, 2));
        placeChrome (spectrum_->getBounds(), spectrumMaxButton_, spectrumPopButton_);
    }
    else
    {
        // Detached: a single button where the panel was, so there is somewhere
        // obvious to click to get it back if the window is lost behind the DAW.
        spectrumPopButton_.setBounds (body.reduced (4, 2)
                                          .withSizeKeepingCentre (150, 26));
    }
}

int TranspectusEditor::spectrumControlsHeight (int width) noexcept
{
    // Everything on one line needs this much; below it the strip takes two.
    // Wrapping rather than shrinking, because the alternative at the minimum
    // window width is controls that overlap, and a control you cannot read is
    // worse than one that costs a few pixels of spectrum.
    return width >= kSpectrumControlsOneRowWidth ? 28 : 54;
}

void TranspectusEditor::layOutSpectrumControls (juce::Rectangle<int> row)
{
    const auto reference = [this] (juce::Rectangle<int> strip)
    {
        captureButton_.setBounds (strip.removeFromLeft (160));
        strip.removeFromLeft (6);
        clearReferenceButton_.setBounds (strip.removeFromLeft (62));
        strip.removeFromLeft (6);
        saveReferenceButton_.setBounds (strip.removeFromLeft (58));
        strip.removeFromLeft (6);
        loadReferenceButton_.setBounds (strip.removeFromLeft (58));
    };

    const auto view = [this] (juce::Rectangle<int> strip)
    {
        pinkButton_.setBounds (strip.removeFromLeft (100));
        strip.removeFromLeft (4);
        differenceButton_.setBounds (strip.removeFromLeft (100));
        strip.removeFromLeft (4);
        peakHoldButton_.setBounds (strip.removeFromLeft (98));
        strip.removeFromLeft (6);
        resetPeaksButton_.setBounds (strip.removeFromLeft (110).withSizeKeepingCentre (110, 22));
        strip.removeFromLeft (6);
        resolutionBox_.setBounds (strip.removeFromLeft (96).withSizeKeepingCentre (96, 22));
    };

    if (row.getHeight() >= 40)
    {
        // Two lines: the reference controls above, the view switches below.
        reference (row.removeFromTop (row.getHeight() / 2).withTrimmedBottom (2));
        view (row);
        return;
    }

    reference (row);
    row.removeFromLeft (160 + 6 + 62 + 6 + 58 + 6 + 58 + 14);
    view (row);
}

} // namespace tezla::transpectus
