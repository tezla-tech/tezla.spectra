// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include <tezla/ui/StateIds.hpp>

namespace tezla::membrana {

namespace
{
/// Diaphragm nickel: a cool teal-cyan, sitting between Capstone's blue and
/// Transpectus's sea green and reading as neither -- the sputtered face of
/// the thing this plugin is named for. The secondary stays the suite's
/// amber so every working figure reads like the rest of the suite.
const ui::Palette kPalette {
    juce::Colour { 0xff141418 },   // background
    juce::Colour { 0xff1d1d23 },   // panel
    juce::Colour { 0xffd8d5cf },   // text
    juce::Colour { 0xff86837e },   // dim text
    juce::Colour { 0xff3fb5b0 },   // accent: nickel teal
    juce::Colour { 0xff7fded6 },   // accent bright
    juce::Colour { 0xffe0a33c },   // secondary: the working amber, as the suite
    juce::Colour { 0xffff7a18 },   // bypass glow, the same in every plugin
    juce::Colour { 0xffe2483d }    // over
};

constexpr int kHeaderHeight = 58;

/// The instrument row. It grew from 148 when the OUT box gave its ground
/// back: the displays are where this plugin explains itself, so the spare
/// space went to them rather than to taller knob boxes.
constexpr int kDisplayHeight = 200;
constexpr int kDisplayLabelHeight = 15;
constexpr int kStatusHeight = 22;

/// The gap between stage boxes, where the arrows live. Order matters here
/// the way it does on Phonoss: the mic exists before the presence ride
/// hears it, and the boxes should say so.
constexpr int kChainGap = 13;

/// A hue per stage along the chain -- the house step (PanelDesign.hpp), the
/// axis being position in the signal path.
[[nodiscard]] juce::Colour stageTint (const ui::Palette& palette, int stage)
{
    return ui::design::tintFor (palette.accent, stage);
}

constexpr int kStageTitleHeight = 26;
constexpr int kLabelHeight = 15;
constexpr int kValueHeight = 15;
constexpr int kCellGap = 10;
constexpr int kIdealCellHeight = kLabelHeight + 70 + kValueHeight + kCellGap;
} // namespace

// ---------------------------------------------------------------------------
// PolarPatternDisplay
// ---------------------------------------------------------------------------

void PolarPatternDisplay::setState (double pattern01, double axisDeg,
                                    double distanceCm, bool engaged)
{
    // Repaint only when the picture would actually change: this arrives
    // thirty times a second and the knobs mostly are not moving.
    if (std::abs (pattern01 - pattern01_) < 1.0e-4
        && std::abs (axisDeg - axisDeg_) < 0.05
        && std::abs (distanceCm - distanceCm_) < 0.05
        && engaged == engaged_)
        return;

    pattern01_ = pattern01;
    axisDeg_ = axisDeg;
    distanceCm_ = distanceCm;
    engaged_ = engaged;
    repaint();
}

void PolarPatternDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.darker (0.15f));
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto centre = bounds.getCentre().translated (0.0f, 4.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 22.0f;

    const auto radiusFor = [radius] (double db)
    {
        const double clamped = std::max (db, kFloorDb);
        return radius * static_cast<float> (1.0 - clamped / kFloorDb);
    };

    // The dB rings, labelled where a polar chart labels them.
    g.setFont (juce::FontOptions (8.0f));

    for (double db : { 0.0, -6.0, -12.0, -18.0 })
    {
        const float r = radiusFor (db);
        g.setColour (palette_.text.withAlpha (std::abs (db) < 0.5 ? 0.16f : 0.07f));
        g.drawEllipse (centre.x - r, centre.y - r, 2.0f * r, 2.0f * r, 1.0f);
    }

    // The axis cross: front is UP.
    g.setColour (palette_.text.withAlpha (0.08f));
    g.fillRect (centre.x - 0.5f, centre.y - radius, 1.0f, 2.0f * radius);
    g.fillRect (centre.x - radius, centre.y - 0.5f, 2.0f * radius, 1.0f);

    g.setColour (palette_.dimText.withAlpha (0.55f));
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    g.drawText ("FRONT", static_cast<int> (centre.x) - 20,
                static_cast<int> (centre.y - radius) - 11, 40, 10,
                juce::Justification::centred);

    // The pattern itself: |a + (1-a) cos(theta)| on the dB rings. The
    // figure-8's rear lobe and the intermediate patterns' null fall out of
    // the same expression that runs in the DSP -- this is MicPattern's
    // level() drawn round the clock, not an illustration.
    const double a = 1.0 - pattern01_;
    const float patternAlpha = engaged_ ? 1.0f : 0.35f;

    juce::Path shape;
    bool started = false;

    for (int step = 0; step <= 240; ++step)
    {
        const double theta = juce::MathConstants<double>::twoPi * step / 240.0;
        const double gain = std::abs (a + (1.0 - a) * std::cos (theta));
        const double db = gain > 1.0e-6 ? 20.0 * std::log10 (gain) : kFloorDb;
        const float r = radiusFor (db);

        const auto point = centre.getPointOnCircumference (
            r, static_cast<float> (theta));

        if (! started)
        {
            shape.startNewSubPath (point);
            started = true;
        }
        else
        {
            shape.lineTo (point);
        }
    }

    shape.closeSubPath();

    g.setColour (tint_.withAlpha (0.16f * patternAlpha));
    g.fillPath (shape);
    g.setColour (tint_.withAlpha (patternAlpha));
    g.strokePath (shape, juce::PathStrokeType (1.6f));

    // The singer: a ray at the off-axis angle, the dot's distance along it
    // the physical distance (log-mapped, 2 cm at the mic to 1 m at the
    // edge). Two different quantities share the ray -- direction is angle,
    // position is metres -- which is exactly how a session is described.
    const auto angle = static_cast<float> (axisDeg_ * juce::MathConstants<double>::pi / 180.0);
    const float distanceT = static_cast<float> (
        std::log (distanceCm_ / 2.0) / std::log (100.0 / 2.0));
    const float singerR = radius * (0.22f + 0.75f * juce::jlimit (0.0f, 1.0f, distanceT));
    const auto singer = centre.getPointOnCircumference (singerR, angle);

    g.setColour (palette_.text.withAlpha (0.18f * patternAlpha));
    {
        const auto rayEnd = centre.getPointOnCircumference (radius, angle);
        g.drawLine (centre.x, centre.y, rayEnd.x, rayEnd.y, 1.0f);
    }

    g.setColour (palette_.accentBright.withAlpha (patternAlpha));
    g.fillEllipse (singer.x - 3.5f, singer.y - 3.5f, 7.0f, 7.0f);
    g.setColour (palette_.background);
    g.fillEllipse (singer.x - 1.2f, singer.y - 1.2f, 2.4f, 2.4f);

    // The sentence under the picture: what the three knobs mean together.
    const double levelDb = [this, a]
    {
        const double gain = a + (1.0 - a)
                              * std::cos (axisDeg_ * juce::MathConstants<double>::pi / 180.0);
        return gain > 1.0e-6 ? 20.0 * std::log10 (gain) : kFloorDb;
    }();

    juce::String caption;

    if (distanceCm_ > 99.5)
        caption << "1 m";
    else if (distanceCm_ < 9.95)
        caption << juce::String (distanceCm_, 1) << " cm";
    else
        caption << juce::roundToInt (distanceCm_) << " cm";

    caption << "  ·  "
            << (axisDeg_ < 0.5 ? juce::String ("on axis")
                               : juce::String (juce::roundToInt (axisDeg_))
                                     + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")))
            << "  ·  "
            << (levelDb > -0.05 ? juce::String ("0.0")
                                : juce::String (levelDb, 1)) << " dB";

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (engaged_ ? palette_.text.withAlpha (0.8f)
                          : palette_.dimText.withAlpha (0.6f));
    g.drawText (engaged_ ? caption : juce::String ("mic model bypassed"),
                getLocalBounds().removeFromBottom (16).reduced (4, 0),
                juce::Justification::centred);
}

// ---------------------------------------------------------------------------
// CapsuleCurveDisplay
// ---------------------------------------------------------------------------

double CapsuleCurveDisplay::hzForX (float x) const
{
    const auto width = static_cast<float> (getWidth());
    const double t = width > 0.0f ? static_cast<double> (x / width) : 0.0;
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float CapsuleCurveDisplay::xForHz (double hz) const
{
    const double t = std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
    return static_cast<float> (t) * static_cast<float> (getWidth());
}

float CapsuleCurveDisplay::yForDb (double db) const
{
    const double t = (kMaxDb - db) / (kMaxDb - kMinDb);
    return static_cast<float> (t) * static_cast<float> (getHeight());
}

void CapsuleCurveDisplay::refreshCurve (bool micEngaged)
{
    engaged_ = micEngaged;

    for (int i = 0; i < kPoints; ++i)
    {
        const double t = static_cast<double> (i) / (kPoints - 1);
        const double hz = kMinHz * std::pow (kMaxHz / kMinHz, t);
        curveDb_[static_cast<std::size_t> (i)] =
            engaged_ ? static_cast<float> (renderedDbAt_ (hz)) : 0.0f;
    }

    repaint();
}

void CapsuleCurveDisplay::setLiveState (double presenceLiftDb, double presenceHz,
                                        bool presenceOn, double detailLiftDb,
                                        double detailHz, bool detailOn)
{
    // Repaint only when the overlay would visibly move.
    if (std::abs (presenceLiftDb - presenceLiftDb_) < 0.05
        && std::abs (detailLiftDb - detailLiftDb_) < 0.05
        && std::abs (presenceHz - presenceHz_) < 1.0
        && std::abs (detailHz - detailHz_) < 1.0
        && presenceOn == presenceOn_ && detailOn == detailOn_)
        return;

    presenceLiftDb_ = presenceLiftDb;
    presenceHz_ = presenceHz;
    presenceOn_ = presenceOn;
    detailLiftDb_ = detailLiftDb;
    detailHz_ = detailHz;
    detailOn_ = detailOn;
    repaint();
}

void CapsuleCurveDisplay::setMarkers (std::vector<Marker> markers)
{
    if (markers.size() == markers_.size())
    {
        bool same = true;

        for (std::size_t i = 0; i < markers.size(); ++i)
            same = same && std::abs (markers[i].hz - markers_[i].hz) < 1.0
                        && markers[i].colour == markers_[i].colour;

        if (same)
            return;
    }

    markers_ = std::move (markers);
    repaint();
}

double CapsuleCurveDisplay::liveShelvesDbAt (double hz) const
{
    double db = 0.0;

    if (presenceOn_ && presenceLiftDb_ > 0.01)
    {
        // The SVF highpass at zero resonance is critically damped (Q 0.5):
        // HP(jx) = -x^2 / (1 + jx)^2, x = f/fc. The drawn shelf must be the
        // one that plays; a prettier Butterworth here would be a small lie.
        const double g = std::pow (10.0, presenceLiftDb_ / 20.0) - 1.0;
        const double x = hz / presenceHz_;
        const std::complex<double> denominator { 1.0 - x * x, 2.0 * x };
        const std::complex<double> hp = -x * x / denominator;
        db += 20.0 * std::log10 (std::abs (1.0 + g * hp));
    }

    if (detailOn_ && detailLiftDb_ > 0.01)
    {
        // The exact one-pole complementary residual: H(jx) = jx / (1 + jx).
        const double g = std::pow (10.0, detailLiftDb_ / 20.0) - 1.0;
        const double x = hz / detailHz_;
        const std::complex<double> h = std::complex<double> (0.0, x)
                                       / std::complex<double> (1.0, x);
        db += 20.0 * std::log10 (std::abs (1.0 + g * h));
    }

    return db;
}

void CapsuleCurveDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.darker (0.15f));
    g.fillRoundedRectangle (bounds, 4.0f);

    // dB grid: the zero line strongest, +/-6 and +/-12 as whispers.
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        g.setColour (palette_.text.withAlpha (std::abs (db) < 0.5 ? 0.22f : 0.07f));
        g.fillRect (0.0f, yForDb (db) - 0.5f, bounds.getWidth(), 1.0f);
    }

    // Frequency grid at the decades.
    g.setFont (juce::FontOptions (9.0f));

    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const float x = xForHz (hz);
        g.setColour (palette_.text.withAlpha (0.07f));
        g.fillRect (x - 0.5f, 0.0f, 1.0f, bounds.getHeight());
        g.setColour (palette_.dimText.withAlpha (0.55f));
        g.drawText (hz < 999.0 ? juce::String (static_cast<int> (hz))
                               : juce::String (static_cast<int> (hz / 1000.0)) + "k",
                    static_cast<int> (x) + 3, static_cast<int> (bounds.getHeight()) - 13,
                    30, 11, juce::Justification::left);
    }

    // The corner markers along the foot: each stage's tuning frequency in
    // its own hue, tying the picture to the knob that owns it.
    for (const auto& marker : markers_)
    {
        const float x = xForHz (marker.hz);
        g.setColour (marker.colour.withAlpha (0.85f));
        g.fillRect (x - 1.0f, bounds.getHeight() - 7.0f, 2.0f, 7.0f);

        juce::Path tip;
        tip.addTriangle (x - 3.5f, bounds.getHeight() - 7.0f,
                         x + 3.5f, bounds.getHeight() - 7.0f,
                         x, bounds.getHeight() - 11.0f);
        g.fillPath (tip);
    }

    // The mic model's curve, filled to zero so boost and cut read as areas.
    juce::Path base;

    for (int i = 0; i < kPoints; ++i)
    {
        const float x = bounds.getWidth() * static_cast<float> (i) / (kPoints - 1);
        const float y = yForDb (curveDb_[static_cast<std::size_t> (i)]);

        if (i == 0)
            base.startNewSubPath (x, y);
        else
            base.lineTo (x, y);
    }

    juce::Path fill (base);
    fill.lineTo (bounds.getWidth(), yForDb (0.0));
    fill.lineTo (0.0f, yForDb (0.0));
    fill.closeSubPath();

    g.setColour (tint_.withAlpha (engaged_ ? 0.14f : 0.05f));
    g.fillPath (fill);

    g.setColour (engaged_ ? tint_ : tint_.withAlpha (0.35f));
    g.strokePath (base, juce::PathStrokeType (engaged_ ? 1.8f : 1.2f));

    // The rides, composed on top: the bright trace is the TOTAL response
    // this instant -- mic model plus the presence and detail shelves at
    // their current smoothed lifts -- so it breathes while the voice plays.
    const bool ridesActive = (presenceOn_ && presenceLiftDb_ > 0.05)
                             || (detailOn_ && detailLiftDb_ > 0.05);

    if (ridesActive)
    {
        juce::Path total;

        for (int i = 0; i < kPoints; ++i)
        {
            const double t = static_cast<double> (i) / (kPoints - 1);
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, t);
            const float x = bounds.getWidth() * static_cast<float> (i) / (kPoints - 1);
            const float y = yForDb (curveDb_[static_cast<std::size_t> (i)]
                                    + liveShelvesDbAt (hz));

            if (i == 0)
                total.startNewSubPath (x, y);
            else
                total.lineTo (x, y);
        }

        g.setColour (palette_.accentBright.withAlpha (0.95f));
        g.strokePath (total, juce::PathStrokeType (1.5f));
    }

    if (! engaged_ && ! ridesActive)
    {
        g.setColour (palette_.dimText.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("flat -- nothing engaged", getLocalBounds(),
                    juce::Justification::centred);
    }

    if (! hovering_)
        return;

    // The exact numbers under the pointer: the total that plays, and the
    // mic model alone when the rides are adding to it.
    const double hz = hzForX (hoverX_);
    const int slot = juce::jlimit (0, kPoints - 1,
        juce::roundToInt ((kPoints - 1)
                          * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz)));
    const double micDb = curveDb_[static_cast<std::size_t> (slot)];
    const double totalDb = micDb + (ridesActive ? liveShelvesDbAt (hz) : 0.0);

    g.setColour (palette_.text.withAlpha (0.35f));
    g.fillRect (hoverX_ - 0.5f, 0.0f, 1.0f, bounds.getHeight());

    const float dotY = yForDb (totalDb);
    g.setColour (palette_.accentBright);
    g.fillEllipse (hoverX_ - 2.5f, dotY - 2.5f, 5.0f, 5.0f);

    const auto formatDb = [] (double db)
    {
        return (db >= 0.05 ? "+" : "") + juce::String (db, 1);
    };

    juce::String readout;
    readout << (hz < 999.5 ? juce::String (juce::roundToInt (hz)) + " Hz"
                           : juce::String (hz / 1000.0, 2) + " kHz")
            << "   " << formatDb (totalDb) << " dB";

    if (ridesActive && std::abs (totalDb - micDb) >= 0.05)
        readout << "  (mic " << formatDb (micDb) << ")";

    const bool left = hoverX_ > bounds.getWidth() * 0.55f;
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (palette_.text);
    g.drawText (readout,
                left ? static_cast<int> (hoverX_) - 190 : static_cast<int> (hoverX_) + 8,
                4, 182, 14,
                left ? juce::Justification::right : juce::Justification::left);
}

void CapsuleCurveDisplay::mouseMove (const juce::MouseEvent& event)
{
    hovering_ = true;
    hoverX_ = event.position.x;
    repaint();
}

void CapsuleCurveDisplay::mouseExit (const juce::MouseEvent&)
{
    hovering_ = false;
    repaint();
}

// ---------------------------------------------------------------------------
// LiftLanesDisplay
// ---------------------------------------------------------------------------

void LiftLanesDisplay::push (const std::array<Tick, kLanes>& ticks)
{
    for (int lane = 0; lane < kLanes; ++lane)
    {
        const auto l = static_cast<std::size_t> (lane);
        const auto w = static_cast<std::size_t> (writeIndex_);
        applied_[l][w] = std::max (0.0f, ticks[l].appliedDb);
        target_[l][w] = std::max (0.0f, ticks[l].targetDb);
    }

    if (++writeIndex_ >= kHistory)
    {
        writeIndex_ = 0;
        filled_ = true;
    }

    repaint();
}

void LiftLanesDisplay::setLaneEnabled (int lane, bool enabled)
{
    if (lane >= 0 && lane < kLanes)
        enabled_[static_cast<std::size_t> (lane)] = enabled;
}

juce::Rectangle<float> LiftLanesDisplay::laneBounds (int lane) const
{
    const auto bounds = getLocalBounds().toFloat();
    const float laneHeight = bounds.getHeight() / kLanes;
    return { 0.0f, laneHeight * static_cast<float> (lane),
             bounds.getWidth(), laneHeight };
}

int LiftLanesDisplay::slotAt (float x) const
{
    const auto width = static_cast<float> (getWidth());

    if (width <= 0.0f)
        return -1;

    const int available = filled_ ? kHistory : writeIndex_;
    const int offset = juce::roundToInt ((1.0f - x / width) * (kHistory - 1));

    if (offset < 0 || offset >= available)
        return -1;

    int slot = writeIndex_ - 1 - offset;

    while (slot < 0)
        slot += kHistory;

    return slot;
}

void LiftLanesDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.darker (0.15f));
    g.fillRoundedRectangle (bounds, 4.0f);

    const int hoverSlot = hoverSlot_;

    for (int lane = 0; lane < kLanes; ++lane)
    {
        auto area = laneBounds (lane).reduced (0.0f, 3.0f);
        const auto l = static_cast<std::size_t> (lane);
        const bool on = enabled_[l];
        const auto tint = tints_[l];
        const float floorY = area.getBottom();

        g.setColour (palette_.text.withAlpha (0.10f));
        g.fillRect (area.getX(), floorY - 0.5f, area.getWidth(), 1.0f);

        if (on)
        {
            const int available = filled_ ? kHistory : writeIndex_;

            const auto xFor = [&area] (int i)
            {
                return area.getWidth() * (1.0f - static_cast<float> (i) / (kHistory - 1));
            };

            const auto yFor = [&area, floorY] (float db)
            {
                return floorY - area.getHeight() * std::min (1.0f, db / kCeilingDb);
            };

            const auto slotFor = [this] (int i)
            {
                int slot = writeIndex_ - 1 - i;

                while (slot < 0)
                    slot += kHistory;

                return static_cast<std::size_t> (slot);
            };

            if (available > 1)
            {
                // What the stage is APPLYING: the filled trace.
                juce::Path appliedPath;

                for (int i = 0; i < available; ++i)
                {
                    const auto point = juce::Point<float> (
                        xFor (i), yFor (applied_[l][slotFor (i)]));

                    if (i == 0)
                        appliedPath.startNewSubPath (point);
                    else
                        appliedPath.lineTo (point);
                }

                juce::Path appliedFill (appliedPath);
                appliedFill.lineTo (xFor (available - 1), floorY);
                appliedFill.lineTo (xFor (0), floorY);
                appliedFill.closeSubPath();

                g.setColour (tint.withAlpha (0.20f));
                g.fillPath (appliedFill);
                g.setColour (tint);
                g.strokePath (appliedPath, juce::PathStrokeType (1.4f));

                // What the curve ASKED for: the thin amber line the applied
                // trace chases. The gap between the two IS the attack and
                // release -- the mechanism, visible.
                juce::Path targetPath;

                for (int i = 0; i < available; ++i)
                {
                    const auto point = juce::Point<float> (
                        xFor (i), yFor (target_[l][slotFor (i)]));

                    if (i == 0)
                        targetPath.startNewSubPath (point);
                    else
                        targetPath.lineTo (point);
                }

                g.setColour (palette_.secondary.withAlpha (0.65f));
                g.strokePath (targetPath, juce::PathStrokeType (1.0f));
            }
        }

        // The lane's name and figures: applied bold, asked dimmed beside it.
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (on ? palette_.dimText.interpolatedWith (tint, 0.5f)
                        : palette_.dimText.withAlpha (0.5f));
        g.drawText (names_[l], area.reduced (6.0f, 1.0f), juce::Justification::topLeft);

        juce::String figure;

        if (! on)
        {
            figure = "off";
        }
        else
        {
            const auto slot = static_cast<std::size_t> (
                hoverSlot >= 0 ? hoverSlot
                               : (writeIndex_ + kHistory - 1) % kHistory);
            figure = "+" + juce::String (applied_[l][slot], 1) + " dB";

            if (target_[l][slot] - applied_[l][slot] > 0.05f
                || applied_[l][slot] - target_[l][slot] > 0.05f)
                figure += "  (asked +" + juce::String (target_[l][slot], 1) + ")";
        }

        g.setColour (on ? palette_.text.withAlpha (0.75f)
                        : palette_.dimText.withAlpha (0.5f));
        g.drawText (figure, area.reduced (6.0f, 1.0f), juce::Justification::topRight);
    }

    if (hoverSlot >= 0)
    {
        g.setColour (palette_.text.withAlpha (0.35f));
        g.fillRect (hoverX_ - 0.5f, 0.0f, 1.0f, bounds.getHeight());
    }
}

void LiftLanesDisplay::mouseMove (const juce::MouseEvent& event)
{
    hoverX_ = event.position.x;
    hoverSlot_ = slotAt (event.position.x);
    repaint();
}

void LiftLanesDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverSlot_ = -1;
    repaint();
}

// ---------------------------------------------------------------------------
// IoMeters
// ---------------------------------------------------------------------------

void IoMeters::advance (Channel& channel, float db)
{
    channel.latestDb = db;

    // Instant attack, steady fall: a peak that only fell with new blocks
    // would freeze whenever the transport stopped.
    channel.shownDb = db > channel.shownDb ? db
                                           : channel.shownDb - kFallDbPerTick;

    if (db > channel.peakDb)
    {
        channel.peakDb = db;
        channel.holdTicks = kHoldTicks;
    }
    else if (channel.holdTicks > 0)
    {
        --channel.holdTicks;
    }
    else
    {
        channel.peakDb = channel.shownDb;
    }
}

void IoMeters::setLevels (float inDb, float outDb)
{
    advance (in_, inDb);
    advance (out_, outDb);
    repaint();
}

void IoMeters::paintBar (juce::Graphics& g, juce::Rectangle<float> area,
                         const Channel& channel, const juce::String& name)
{
    auto nameArea = area.removeFromTop (12.0f);
    auto figureArea = area.removeFromBottom (13.0f);
    auto barArea = area.reduced (5.0f, 2.0f);

    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.setColour (palette_.dimText);
    g.drawText (name, nameArea, juce::Justification::centred);

    g.setColour (palette_.background.brighter (0.06f));
    g.fillRoundedRectangle (barArea, 2.0f);

    const auto heightFor = [&barArea] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
        return barArea.getHeight() * t;
    };

    if (channel.shownDb > kFloorDb)
    {
        const float height = heightFor (channel.shownDb);
        auto fillArea = barArea.withTop (barArea.getBottom() - height);

        // The house meter colours: accent through the working range, the
        // amber above -6, the over-red past 0 -- level meaning the same
        // thing it means everywhere else in the suite.
        g.setColour (palette_.accent);
        g.fillRect (fillArea.withTrimmedTop (
            std::max (0.0f, fillArea.getHeight() - heightFor (-6.0f))));

        if (channel.shownDb > -6.0f)
        {
            auto upper = barArea.withTop (barArea.getBottom() - height);
            upper = upper.withBottom (barArea.getBottom() - heightFor (-6.0f));
            g.setColour (channel.shownDb > 0.0f ? palette_.over : palette_.secondary);
            g.fillRect (upper);
        }
    }

    if (channel.peakDb > kFloorDb)
    {
        g.setColour (palette_.text.withAlpha (0.8f));
        g.fillRect (barArea.getX(),
                    barArea.getBottom() - heightFor (channel.peakDb) - 1.0f,
                    barArea.getWidth(), 1.5f);
    }

    g.setFont (juce::FontOptions (9.0f));
    g.setColour (palette_.dimText);
    g.drawText (channel.latestDb <= -99.0f
                    ? juce::String ("--")
                    : juce::String (channel.latestDb, 1),
                figureArea, juce::Justification::centred);
}

void IoMeters::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.darker (0.15f));
    g.fillRoundedRectangle (bounds, 4.0f);

    auto area = bounds.reduced (2.0f, 4.0f);
    const float half = area.getWidth() * 0.5f;

    paintBar (g, area.removeFromLeft (half), in_, "IN");
    paintBar (g, area, out_, "OUT");
}

// ---------------------------------------------------------------------------
// StagePanel
// ---------------------------------------------------------------------------

StagePanel::StagePanel (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                        juce::Colour tint, juce::String title,
                        const char* enableParameterId, int columns)
    : state_ (state), palette_ (palette), tint_ (tint), title_ (std::move (title)),
      enableId_ (enableParameterId), columns_ (columns)
{
    if (enableId_ == nullptr)
        return;

    enableButton_ = std::make_unique<ui::LampButton> ("ON");
    enableButton_->setClickingTogglesState (true);
    enableButton_->setTooltip (
        "Switches this stage out of the chain. Off is a bit-exact identity -- "
        "the samples pass through untouched rather than nearly untouched.");
    addAndMakeVisible (*enableButton_);

    enableAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, enableId_, *enableButton_);
}

void StagePanel::showActivityFigure()
{
    showsActivity_ = true;
}

void StagePanel::setActivityDb (double db)
{
    if (std::abs (db - activityDb_) < 0.05)
        return;

    activityDb_ = db;
    repaint (getLocalBounds().removeFromTop (kStageTitleHeight + 2));
}

void StagePanel::addKnob (const char* parameterId, const juce::String& name,
                          const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    ui::styleKnob (knob->slider, palette_, tint_, emphasisOf (parameterId));
    ui::resetsToDefault (knob->slider, state_, parameterId);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    ui::styleName (knob->label, palette_, tint_);
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    knobs_.push_back (std::move (knob));
}

void StagePanel::addToggle (const char* parameterId, const juce::String& name,
                            const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle> (name);

    toggle->button.setClickingTogglesState (true);
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->id = parameterId;
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, toggle->button);

    toggles_.push_back (std::move (toggle));
}

void StagePanel::setEmphasis (const char* lead, std::vector<juce::String> trims)
{
    leadId_ = lead != nullptr ? juce::String (lead) : juce::String{};
    trimIds_ = std::move (trims);
}

ui::design::Emphasis StagePanel::emphasisOf (const juce::String& id) const
{
    if (id == leadId_)
        return ui::design::Emphasis::lead;

    for (const auto& trim : trimIds_)
        if (id == trim)
            return ui::design::Emphasis::trim;

    return ui::design::Emphasis::normal;
}

bool StagePanel::isStageEnabled() const
{
    if (enableId_ == nullptr)
        return true;

    if (auto* raw = state_.getRawParameterValue (enableId_))
        return raw->load() > 0.5f;

    return true;
}

void StagePanel::refreshEnablement()
{
    const bool on = isStageEnabled();

    for (auto& knob : knobs_)
    {
        knob->slider.setEnabled (on);

        // Back to the stage's own warmed colour, never plain dimText -- the
        // Malleus/Crossbar restore bug.
        const auto colour = palette_.dimText.interpolatedWith (tint_,
                                                               ui::design::kLabelTint);

        knob->label.setColour (juce::Label::textColourId,
                               on ? colour : colour.withAlpha (0.4f));
        knob->label.repaint();
    }

    for (auto& toggle : toggles_)
        toggle->button.setEnabled (on);
}

void StagePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (palette_.background.brighter (0.10f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const bool on = isStageEnabled();

    {
        juce::Path spine;
        spine.addRoundedRectangle (bounds.getX() + 1.0f, bounds.getY() + 4.0f,
                                   3.0f, bounds.getHeight() - 8.0f,
                                   1.5f, 1.5f, true, false, true, false);

        g.setColour (tint_.withAlpha (on ? 0.85f : 0.25f));
        g.fillPath (spine);
    }

    auto titleArea = bounds.removeFromTop (static_cast<float> (kStageTitleHeight));

    // How hard the stage is working, as light: a ride at +6 dB is a
    // different object from one at +0.2.
    const auto working = on && showsActivity_
                             ? juce::jlimit (0.0f, 1.0f,
                                             static_cast<float> (std::abs (activityDb_)) / 6.0f)
                             : 0.0f;

    g.setColour (on ? tint_.withAlpha (0.16f + 0.34f * working)
                    : palette_.background.withAlpha (0.35f));
    g.fillRoundedRectangle (titleArea.reduced (2.0f, 2.0f), 3.0f);

    g.setColour (on ? palette_.text : palette_.dimText);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (title_, titleArea.reduced (enableButton_ != nullptr ? 52.0f : 6.0f, 0.0f),
                juce::Justification::centred);

    if (! showsActivity_)
        return;

    // The live figure, signed: these stages ADD.
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (on ? palette_.text.withAlpha (0.45f + 0.5f * working)
                    : palette_.dimText.withAlpha (0.35f));

    juce::String figure;

    if (! on)
        figure = "off";
    else if (std::abs (activityDb_) < 0.05)
        figure = "0.0 dB";
    else
        figure = (activityDb_ > 0.0 ? "+" : "") + juce::String (activityDb_, 1) + " dB";

    g.drawText (figure, titleArea.withTrimmedRight (7.0f).withHeight (titleArea.getHeight()),
                juce::Justification::centredRight, false);
}

void StagePanel::resized()
{
    auto bounds = getLocalBounds().reduced (4, 3);
    auto titleArea = bounds.removeFromTop (kStageTitleHeight);

    if (enableButton_ != nullptr)
        enableButton_->setBounds (ui::LampButton::sized (34, 18)
                                    .withCentre (titleArea.removeFromLeft (50).getCentre()));

    if (! toggles_.empty())
    {
        const int rowHeight = 22 + 2 * ui::LampButton::kGlowMargin;

        auto toggleArea = bounds.removeFromBottom (
            static_cast<int> (toggles_.size()) * rowHeight + 4);

        for (auto& toggle : toggles_)
            toggle->button.setBounds (
                ui::LampButton::sized (juce::jmax (54, toggleArea.getWidth() - 8
                                                        - 2 * ui::LampButton::kGlowMargin), 22)
                  .withCentre (toggleArea.removeFromTop (rowHeight).getCentre()));
    }

    if (knobs_.empty())
        return;

    const int rows = (static_cast<int> (knobs_.size()) + columns_ - 1) / columns_;
    const int cellWidth = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (kIdealCellHeight,
                                       bounds.getHeight() / juce::jmax (1, rows));

    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    bounds.getY() + row * cellHeight,
                                    cellWidth, cellHeight };

        auto& knob = *knobs_[i];
        knob.label.setBounds (cell.removeFromTop (kLabelHeight));
        cell.removeFromBottom (kCellGap);
        knob.slider.setBounds (ui::emphasised (cell.reduced (2, 0), emphasisOf (knob.id)));
    }
}

// ---------------------------------------------------------------------------
// MembranaEditor
// ---------------------------------------------------------------------------

MembranaEditor::MembranaEditor (MembranaProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      membrana_ (processorToUse),
      palette_ (kPalette),
      knobLook_ (kPalette)
{
    setLookAndFeel (&knobLook_);

    header_ = std::make_unique<ui::HeaderBar> (
        membrana_.getState(), "MEMBRANA",
        "The microphone stage -- capsule, position, presence, detail",
        ids::bypass, palette_);

    header_->attachSuiteControls (membrana_.getState(), nullptr, ids::output, nullptr);

    header_->onSwapRequested = [this]
    {
        membrana_.getAbCompare().swapSlots();
        header_->setActiveSlot (membrana_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (membrana_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        membrana_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (membrana_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        membrana_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (membrana_.getTooltipsEnabled());
    tooltips_.setEnabled (membrana_.getTooltipsEnabled());

    header_->setActiveSlot (membrana_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (membrana_.getAbCompare().otherSlotFilled());

    addAndMakeVisible (*header_);

    buildStages();

    polar_ = std::make_unique<PolarPatternDisplay> (palette_, stageTint (palette_, position));
    addAndMakeVisible (*polar_);

    polarLabel_.setText ("PATTERN & POSITION", juce::dontSendNotification);
    polarLabel_.setColour (juce::Label::textColourId,
                           palette_.dimText.interpolatedWith (stageTint (palette_, position),
                                                              ui::design::kLabelTint));
    polarLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    polarLabel_.setTooltip (
        "The session from above: the pattern's gain against angle on dB "
        "rings (the rear lobe and the null are the physics, not artwork), "
        "with the singer as the dot -- direction is the off-axis angle, "
        "position along the ray is the distance. The caption gives the "
        "pattern level at that angle.");
    addAndMakeVisible (polarLabel_);

    curve_ = std::make_unique<CapsuleCurveDisplay> (
        palette_, stageTint (palette_, mic),
        [this] (double hz) { return membrana_.capsuleRenderedDbAt (hz); });
    addAndMakeVisible (*curve_);

    curveLabel_.setText ("THE RESPONSE  as it plays", juce::dontSendNotification);
    curveLabel_.setColour (juce::Label::textColourId,
                           palette_.dimText.interpolatedWith (stageTint (palette_, mic),
                                                              ui::design::kLabelTint));
    curveLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    curveLabel_.setTooltip (
        "The whole plugin on one plot. The filled curve is the mic model, "
        "drawn from the SAME coefficients the audio runs through -- the "
        "position section, the LF limit and a DFT of the live FIR taps. The "
        "bright trace adds the two rides at their CURRENT lifts, so it "
        "breathes while the voice plays: that line is what is happening to "
        "the signal this instant. Ticks along the foot mark each stage's "
        "corner in its own hue. Hover for the exact dB at any frequency.");
    addAndMakeVisible (curveLabel_);

    lifts_ = std::make_unique<LiftLanesDisplay> (
        palette_,
        std::array<juce::Colour, 2> { stageTint (palette_, presence),
                                      stageTint (palette_, detail) },
        std::array<juce::String, 2> { "PRESENCE", "DETAIL" });
    addAndMakeVisible (*lifts_);

    liftsLabel_.setText ("THE RIDES", juce::dontSendNotification);
    liftsLabel_.setColour (juce::Label::textColourId,
                           palette_.dimText.interpolatedWith (stageTint (palette_, presence),
                                                              ui::design::kLabelTint));
    liftsLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    liftsLabel_.setTooltip (
        "What the two dynamic stages are DOING, on one clock. The filled "
        "trace is the lift being APPLIED; the thin amber line is what the "
        "curve ASKED for that instant, before the attack and release "
        "smoothing -- the gap between them is the riding hand at work. "
        "Lanes grow UPWARD because these stages only ever add. Hover for "
        "the figures at any instant.");
    addAndMakeVisible (liftsLabel_);

    ioMeters_ = std::make_unique<IoMeters> (palette_);
    addAndMakeVisible (*ioMeters_);

    ioLabel_.setText ("I/O", juce::dontSendNotification);
    ioLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    ioLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    ioLabel_.setTooltip (
        "Peak in and peak out. With Auto Level doing its job the two sit "
        "together whatever Distance does; if OUT runs hot, trim in the "
        "header rather than letting Capstone clean it up.");
    addAndMakeVisible (ioLabel_);

    statusLabel_.setJustificationType (juce::Justification::centredRight);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statusLabel_);

    setResizable (true, true);
    setResizeLimits (980, 540, 2200, 1200);
    setSize (1180, 560);

    updateForSwitches();

    startTimerHz (30);
}

MembranaEditor::~MembranaEditor()
{
    setLookAndFeel (nullptr);
}

void MembranaEditor::buildStages()
{
    auto& state = membrana_.getState();

    // ---- MIC ---------------------------------------------------------------

    stages_[mic] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, mic),
                                                 "MIC", ids::micOn, 3);
    stages_[mic]->setEmphasis (ids::pattern, { ids::grilleHz });
    stages_[mic]->addKnob (ids::pattern, "PATTERN",
        "Omni to figure-8, continuously. This is not a filter: it decides how "
        "much gradient (velocity) capsule is in the mix, which is what carries "
        "proximity -- an omni has none at any distance, a figure-8 has double "
        "a cardioid's. Level, proximity and the off-axis response all follow "
        "from this one number, coupled the way physics couples them.");
    stages_[mic]->addKnob (ids::capsuleMm, "BODY",
        "The diffracting body's diameter. A 50 mm body starts rewriting the "
        "top octaves around 2 kHz; a 25 mm body moves that an octave up. This "
        "is the physical reason large- and small-diaphragm mics sound "
        "different before any electronics are involved.");
    stages_[mic]->addKnob (ids::character, "CHARACTER",
        "How much of the body's raw diffraction survives into the sound -- "
        "the exact rigid-sphere response at the set distance and angle, "
        "relative to 1 m on axis. At the reference position it is 0 dB "
        "everywhere regardless, so the default costs nothing until the mic "
        "moves.");
    stages_[mic]->addKnob (ids::grille, "GRILLE",
        "Depth of the grille-basket resonance, up to 6 dB. Zero removes it "
        "entirely.");
    stages_[mic]->addKnob (ids::grilleHz, "G FREQ",
        "Where the grille rings. Higher and shallower reads as 'air'; lower "
        "and deeper as a coloured basket.");

    // ---- POSITION ----------------------------------------------------------

    // Shares the MIC switch: position is half of the same physical model,
    // and the two boxes light and grey together.
    stages_[position] = std::make_unique<StagePanel> (state, palette_,
                                                      stageTint (palette_, position),
                                                      "POSITION", ids::micOn, 3);
    stages_[position]->setEmphasis (ids::distanceCm, { ids::lowLimitHz });
    stages_[position]->showActivityFigure();
    stages_[position]->addKnob (ids::distanceCm, "DISTANCE",
        "How close the singer stands. Closer is warmer twice over: the "
        "proximity corner rises as 1/distance, and the body's high rise "
        "genuinely collapses at close range (about +2 dB at point-blank "
        "against +6 far away -- measured physics, not a curve drawn to feel "
        "right). 1 m is the reference: bit-exact neutral.");
    stages_[position]->addKnob (ids::axisDeg, "OFF-AXIS",
        "Turning the mic away. Level, proximity and the body's shadow all "
        "move together: a cardioid at 90 degrees is -6 dB with NO proximity "
        "at all, which is how the real thing behaves and why off-axis is a "
        "tone control, not a volume control.");
    stages_[position]->addKnob (ids::lowLimitHz, "LF LIMIT",
        "The simulated diaphragm's own low corner, bounding the proximity "
        "shelf. At 2 cm a cardioid honestly implies +34 dB below 30 Hz; this "
        "is what keeps that survivable under a dubstep sub. Raise it to "
        "tighten the boom, lower it to let the weight through.");
    stages_[position]->addToggle (ids::autoLevel, "AUTO LEVEL",
        "Divides out the loudness change so Distance reads as TONE. Off, the "
        "physical level applies instead (+24 dB max) -- closer really is "
        "louder. On is how you judge the sound; off is how it would have "
        "tracked.");

    // ---- PRESENCE ----------------------------------------------------------

    stages_[presence] = std::make_unique<StagePanel> (state, palette_,
                                                      stageTint (palette_, presence),
                                                      "PRESENCE", ids::presenceOn, 2);
    stages_[presence]->setEmphasis (ids::presence, { ids::presThresh });
    stages_[presence]->showActivityFigure();
    stages_[presence]->addKnob (ids::presence, "PRESENCE",
        "The most the shelf may lift, and with Track up, the amount it leans "
        "in when the singer backs off. Bounded here by construction -- no "
        "transient can overshoot this number.");
    stages_[presence]->addKnob (ids::presHz, "FREQ",
        "Where the shelf starts. 4-5 kHz is the classic vocal presence "
        "region; higher reads as air, lower as edge.");
    stages_[presence]->addKnob (ids::presThresh, "THRESH",
        "The level the ride is anchored to: at or above it the ridden part "
        "of the shelf stands down, and over the 12 dB below it the lift "
        "eases in. Set it around the loud phrases' level.");
    stages_[presence]->addKnob (ids::track, "TRACK",
        "How much of the shelf is ridden versus standing. 0% is a fixed "
        "shelf of exactly the Presence amount; 100% is fully dynamic -- "
        "everything leans in on the quiet lines and nothing brightens the "
        "shout. The riding is slow on purpose: 120 ms to back off, 400 ms "
        "to lean in, an engineer's hand rather than a compressor's.");

    // ---- DETAIL ------------------------------------------------------------

    stages_[detail] = std::make_unique<StagePanel> (state, palette_,
                                                    stageTint (palette_, detail),
                                                    "DETAIL", ids::detailOn, 3);
    stages_[detail]->setEmphasis (ids::detail, { ids::detFloor });
    stages_[detail]->showActivityFigure();
    stages_[detail]->addKnob (ids::detail, "DETAIL",
        "The most the high band may come up. This is the thing a shelf "
        "cannot do: consonants and breath rise RELATIVE to the vowels, "
        "because quiet detail is lifted and loud vowels are left alone. A "
        "vowel on its own reads exactly zero lift -- the detector is four "
        "poles steep -- so the body of the voice never brightens itself.");
    stages_[detail]->addKnob (ids::detHz, "SPLIT",
        "Where the detail band starts. The split is exactly complementary "
        "-- below it passes through arithmetic-untouched, no crossover "
        "phase, no allpass.");
    stages_[detail]->addKnob (ids::detFloor, "FLOOR",
        "Below this, NOTHING lifts. The floor is what separates this from "
        "an exciter: consonants live in the 20 dB above it and come up; "
        "hiss lives at it and stays exactly where it was. Set it just above "
        "the take's noise floor.");

    for (auto& stage : stages_)
        addAndMakeVisible (*stage);
}

void MembranaEditor::updateForSwitches()
{
    bool changed = false;

    const std::array<bool, kNumStages> enabled {
        stages_[mic]->isStageEnabled(),
        stages_[position]->isStageEnabled(),
        stages_[presence]->isStageEnabled(),
        stages_[detail]->isStageEnabled(),
    };

    for (int stage = 0; stage < kNumStages; ++stage)
    {
        const int now = enabled[static_cast<std::size_t> (stage)] ? 1 : 0;

        if (shownEnabled_[static_cast<std::size_t> (stage)] != now)
        {
            shownEnabled_[static_cast<std::size_t> (stage)] = now;
            stages_[static_cast<std::size_t> (stage)]->refreshEnablement();
            changed = true;
        }
    }

    lifts_->setLaneEnabled (0, enabled[presence]);
    lifts_->setLaneEnabled (1, enabled[detail]);

    const int identity = membrana_.isIdentity() ? 1 : 0;

    if (identity != shownIdentity_)
    {
        shownIdentity_ = identity;
        statusLabel_.setText (identity == 1
                                  ? "bit-exact identity at these settings"
                                  : juce::String(),
                              juce::dontSendNotification);
        changed = true;
    }

    if (changed)
        repaint();
}

void MembranaEditor::timerCallback()
{
    const auto& meters = membrana_.getMeterValues();
    const auto& state = membrana_.getState();

    const auto paramValue = [&state] (const char* id)
    {
        auto* raw = state.getRawParameterValue (id);
        return raw != nullptr ? static_cast<double> (raw->load()) : 0.0;
    };

    const auto presenceLift = meters.presenceLiftDb.load (std::memory_order_relaxed);
    const auto presenceTarget = meters.presenceTargetDb.load (std::memory_order_relaxed);
    const auto detailLift = meters.detailLiftDb.load (std::memory_order_relaxed);
    const auto detailTarget = meters.detailTargetDb.load (std::memory_order_relaxed);
    const auto trim = meters.capsuleTrimDb.load (std::memory_order_relaxed);

    stages_[presence]->setActivityDb (presenceLift);
    stages_[detail]->setActivityDb (detailLift);
    stages_[position]->setActivityDb (trim);

    lifts_->push ({ LiftLanesDisplay::Tick { presenceLift, presenceTarget },
                    LiftLanesDisplay::Tick { detailLift, detailTarget } });

    ioMeters_->setLevels (meters.inputDb.load (std::memory_order_relaxed),
                          meters.outputDb.load (std::memory_order_relaxed));

    const bool micEngaged = stages_[mic]->isStageEnabled();
    const bool presenceEngaged = stages_[presence]->isStageEnabled();
    const bool detailEngaged = stages_[detail]->isStageEnabled();

    polar_->setState (paramValue (ids::pattern), paramValue (ids::axisDeg),
                      paramValue (ids::distanceCm), micEngaged);

    curve_->setLiveState (presenceLift, paramValue (ids::presHz), presenceEngaged,
                          detailLift, paramValue (ids::detHz), detailEngaged);

    // The corner markers: each stage's tuning frequency in its hue, only
    // where the stage is doing something at that corner.
    {
        std::vector<CapsuleCurveDisplay::Marker> markers;

        if (micEngaged)
        {
            markers.push_back ({ paramValue (ids::lowLimitHz),
                                 stageTint (palette_, position) });

            if (paramValue (ids::grille) > 0.005)
                markers.push_back ({ paramValue (ids::grilleHz),
                                     stageTint (palette_, mic) });
        }

        if (presenceEngaged)
            markers.push_back ({ paramValue (ids::presHz),
                                 stageTint (palette_, presence) });

        if (detailEngaged)
            markers.push_back ({ paramValue (ids::detHz),
                                 stageTint (palette_, detail) });

        curve_->setMarkers (std::move (markers));
    }

    // The mic curve depends on the mic parameters and nothing else; a cheap
    // weighted sum says when the DFT-backed refresh is worth running.
    double revision = 0.0;
    int weight = 1;

    for (const char* id : { ids::micOn, ids::pattern, ids::capsuleMm, ids::character,
                            ids::grille, ids::grilleHz, ids::distanceCm, ids::axisDeg,
                            ids::autoLevel, ids::lowLimitHz })
    {
        revision += paramValue (id) * weight;
        weight += 17;
    }

    if (std::abs (revision - shownCurveRevision_) > 1.0e-9)
    {
        shownCurveRevision_ = revision;
        curve_->refreshCurve (micEngaged);
    }

    updateForSwitches();
}

void MembranaEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    // The arrows in the chain gaps: the order IS the design.
    if (chainRow_.isEmpty())
        return;

    const int boxWidth = (chainRow_.getWidth() - (kNumStages - 1) * kChainGap) / kNumStages;

    g.setColour (palette_.dimText.withAlpha (0.55f));

    for (int gap = 0; gap < kNumStages - 1; ++gap)
    {
        const int x = chainRow_.getX() + (gap + 1) * boxWidth + gap * kChainGap;
        const auto centreY = static_cast<float> (chainRow_.getCentreY());
        const auto left = static_cast<float> (x) + 2.5f;

        juce::Path arrow;
        arrow.addTriangle (left, centreY - 4.5f, left, centreY + 4.5f,
                           left + static_cast<float> (kChainGap) - 5.0f, centreY);
        g.fillPath (arrow);
    }
}

void MembranaEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (kHeaderHeight));

    auto content = bounds.reduced (10, 6);

    // The instrument row: polar (square), the response (the widest share --
    // it is the identity picture), the rides, the I/O pair.
    auto displayRow = content.removeFromTop (kDisplayHeight + kDisplayLabelHeight);
    auto labelRow = displayRow.removeFromTop (kDisplayLabelHeight);

    const int polarWidth = displayRow.getHeight();
    const int ioWidth = 64;
    const int ridesWidth = juce::jmax (220, (displayRow.getWidth() - polarWidth - ioWidth
                                             - 3 * kChainGap) * 3 / 10);

    auto polarArea = displayRow.removeFromLeft (polarWidth);
    displayRow.removeFromLeft (kChainGap);
    auto ioArea = displayRow.removeFromRight (ioWidth);
    displayRow.removeFromRight (kChainGap);
    auto ridesArea = displayRow.removeFromRight (ridesWidth);
    displayRow.removeFromRight (kChainGap);
    auto curveArea = displayRow;

    polarLabel_.setBounds (labelRow.removeFromLeft (polarWidth).reduced (2, 0));
    labelRow.removeFromLeft (kChainGap);
    ioLabel_.setBounds (labelRow.removeFromRight (ioWidth).reduced (2, 0));
    labelRow.removeFromRight (kChainGap);
    liftsLabel_.setBounds (labelRow.removeFromRight (ridesWidth).reduced (2, 0));
    labelRow.removeFromRight (kChainGap);
    curveLabel_.setBounds (labelRow.reduced (2, 0));

    polar_->setBounds (polarArea);
    curve_->setBounds (curveArea);
    lifts_->setBounds (ridesArea);
    ioMeters_->setBounds (ioArea);

    content.removeFromTop (6);

    auto statusRow = content.removeFromBottom (kStatusHeight);
    statusLabel_.setBounds (statusRow.reduced (2, 0));

    chainRow_ = content;

    const int boxWidth = (content.getWidth() - (kNumStages - 1) * kChainGap) / kNumStages;

    for (int stage = 0; stage < kNumStages; ++stage)
    {
        auto box = content.removeFromLeft (boxWidth);
        stages_[static_cast<std::size_t> (stage)]->setBounds (box);

        if (stage < kNumStages - 1)
            content.removeFromLeft (kChainGap);
    }
}

} // namespace tezla::membrana
