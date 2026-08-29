// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include <tezla/ui/Goniometer.hpp>

namespace tezla::ui
{

void Goniometer::update (const dsp::StereoScope& scope)
{
    const auto capacity = static_cast<int> (scope.getCapacity());

    if (capacity <= 0)
    {
        filled_ = 0;
        return;
    }

    // Stride across the whole window rather than taking its tail, so the
    // picture spans the same slice of time whatever the host is running at --
    // at 192 kHz that is a stride of 4 where 48 kHz takes every sample.
    const int stride = juce::jmax (1, capacity / kPoints);
    const int points = juce::jmin (kPoints, capacity / stride);

    std::array<double, kPoints> left {}, right {};

    if (scope.readLatest (left.data(), right.data(), points, stride) != points)
    {
        filled_ = 0;
        return;
    }

    // The 45-degree rotation. Divided by sqrt(2) twice over: once for the
    // rotation itself and once so a full-scale mono signal reaches the edge
    // rather than overshooting it by 3 dB.
    constexpr double scale = 0.5;   // 1 / (sqrt(2) * sqrt(2))

    for (int i = 0; i < points; ++i)
    {
        const auto index = static_cast<std::size_t> (i);

        x_[index] = static_cast<float> ((right[index] - left[index]) * scale);
        y_[index] = static_cast<float> (-(left[index] + right[index]) * scale);
    }

    filled_ = points;

    // Fold the same points into the excursion hold, radius clamped to the
    // drawn square exactly as the trail is, so the outline can never sit
    // outside what the scatter could have shown.
    if (excursionHold_ != nullptr
        && excursionHold_->size() == static_cast<std::size_t> (kHoldSectors))
    {
        auto& hold = *excursionHold_;

        for (int i = 0; i < points; ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            const float px = juce::jlimit (-1.0f, 1.0f, x_[index]);
            const float py = juce::jlimit (-1.0f, 1.0f, y_[index]);
            const float r = std::sqrt (px * px + py * py);

            if (r < 1.0e-4f)
                continue;

            const double angle = std::atan2 (static_cast<double> (py),
                                             static_cast<double> (px))
                                   + juce::MathConstants<double>::pi;

            const int sector = juce::jlimit (0, kHoldSectors - 1,
                static_cast<int> (angle / juce::MathConstants<double>::twoPi
                                  * kHoldSectors));

            hold[static_cast<std::size_t> (sector)] =
                juce::jmax (hold[static_cast<std::size_t> (sector)],
                            juce::jmin (r, 1.0f));
        }
    }
}

void Goniometer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (8.0f);

    auto captionArea = area.removeFromTop (13.0f)
                           .withTrimmedRight (static_cast<float> (topRightInset_));

    // 58 px is about where "GONIOMETER" stops being readable at 10.5 point even
    // squeezed. Below that the label is dropped rather than clipped: a panel
    // captioned "GONIOMET" looks broken, and an uncaptioned one does not.
    if (captionArea.getWidth() >= 58.0f)
    {
        g.setColour (palette_.dimText);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText ("GONIOMETER", captionArea.toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    // Square, so a circle is a circle and the diagonals really are 45 degrees.
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    auto square = area.withSizeKeepingCentre (side, side);

    const auto centre = square.getCentre();
    const float radius = side * 0.5f;

    // ---- the frame -----------------------------------------------------------

    g.setColour (palette_.background);
    g.fillEllipse (square);

    g.setColour (palette_.dimText.withAlpha (0.22f));
    g.drawEllipse (square, 1.0f);
    g.drawEllipse (square.withSizeKeepingCentre (side * 0.5f, side * 0.5f), 1.0f);

    // Mono is vertical and side is horizontal, which is the whole reason for
    // the rotation: a tilt off vertical is much easier to see than a tilt off
    // a diagonal.
    //
    // Both stop short of the rim so they do not run through their own labels --
    // a vertical line through a centred "S" draws a dollar sign.
    const float labelGap = side >= 90.0f ? 15.0f : 0.0f;

    g.setColour (palette_.dimText.withAlpha (0.30f));
    g.drawLine (centre.x, square.getY() + labelGap, centre.x, square.getBottom() - labelGap, 1.0f);
    g.setColour (palette_.dimText.withAlpha (0.16f));
    g.drawLine (square.getX() + labelGap, centre.y, square.getRight() - labelGap, centre.y, 1.0f);

    // The hard-panned diagonals, which is where a single channel on its own
    // lands. Dashed, so they read as guides rather than as signal.
    {
        const float d = radius * 0.7071f;
        const float dashes[] { 3.0f, 4.0f };

        g.setColour (palette_.dimText.withAlpha (0.20f));

        juce::Line<float> leftAxis  { centre.x, centre.y, centre.x - d, centre.y - d };
        juce::Line<float> rightAxis { centre.x, centre.y, centre.x + d, centre.y + d };

        g.drawDashedLine (leftAxis,  dashes, 2, 1.0f);
        g.drawDashedLine (rightAxis, dashes, 2, 1.0f);
        g.drawDashedLine ({ centre.x, centre.y, centre.x + d, centre.y - d }, dashes, 2, 1.0f);
        g.drawDashedLine ({ centre.x, centre.y, centre.x - d, centre.y + d }, dashes, 2, 1.0f);
    }

    // Below about 90 pixels the labels are bigger than the space between the
    // rings and read as clutter over the trace. The shapes are legible without
    // them; four letters crowding a thumbnail are not.
    if (side >= 90.0f)
    {
        g.setColour (palette_.dimText.withAlpha (0.8f));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText ("L", square.withWidth (16.0f).withHeight (14.0f).translated (2.0f, 2.0f),
                    juce::Justification::centred);
        g.drawText ("R", square.withWidth (16.0f).withHeight (14.0f)
                              .translated (square.getWidth() - 18.0f, 2.0f),
                    juce::Justification::centred);
        g.drawText ("M", square.withSizeKeepingCentre (16.0f, 12.0f)
                              .withY (square.getY() + 1.0f),
                    juce::Justification::centred);
        g.drawText ("S", square.withSizeKeepingCentre (16.0f, 12.0f)
                              .withY (square.getBottom() - 13.0f),
                    juce::Justification::centred);
    }

    // ---- the excursion hold --------------------------------------------------
    //
    // The widest the image has ever been, per direction, until cleared. Drawn
    // before the trail so the live motion sits on top of its own history.
    if (excursionHold_ != nullptr
        && excursionHold_->size() == static_cast<std::size_t> (kHoldSectors))
    {
        const auto& hold = *excursionHold_;

        juce::Path outline;
        bool open = false;

        // Segments rather than one closed polygon: sectors nothing has
        // reached yet stay blank instead of snapping the outline to the
        // centre, which matters in the first seconds after a reset.
        for (int sector = 0; sector <= kHoldSectors; ++sector)
        {
            const auto wrapped = static_cast<std::size_t> (sector % kHoldSectors);
            const float r = hold[wrapped];

            if (r <= 0.02f)
            {
                open = false;
                continue;
            }

            const double angle = (static_cast<double> (sector % kHoldSectors) + 0.5)
                                   / kHoldSectors * juce::MathConstants<double>::twoPi
                               - juce::MathConstants<double>::pi;

            const juce::Point<float> at {
                centre.x + static_cast<float> (std::cos (angle)) * r * radius,
                centre.y + static_cast<float> (std::sin (angle)) * r * radius };

            if (open)
                outline.lineTo (at);
            else
                outline.startNewSubPath (at);

            open = true;
        }

        g.setColour (palette_.hold.withAlpha (0.55f));
        g.strokePath (outline, juce::PathStrokeType (1.2f));
    }

    if (filled_ < 2)
        return;

    // ---- the trail -----------------------------------------------------------
    //
    // Drawn oldest-first in chunks of rising opacity. One alpha per point would
    // be a path per point; one alpha for the lot would be a static smear. Ten
    // chunks is enough for the eye to read which end is now.

    const auto toScreen = [&] (int index)
    {
        const auto i = static_cast<std::size_t> (index);
        return juce::Point<float> { centre.x + juce::jlimit (-1.0f, 1.0f, x_[i]) * radius,
                                    centre.y + juce::jlimit (-1.0f, 1.0f, y_[i]) * radius };
    };

    const int perChunk = juce::jmax (2, filled_ / kChunks);

    for (int start = 0; start < filled_ - 1; start += perChunk)
    {
        const int end = juce::jmin (filled_ - 1, start + perChunk);

        juce::Path path;
        path.startNewSubPath (toScreen (start));

        for (int i = start + 1; i <= end; ++i)
            path.lineTo (toScreen (i));

        const float age = static_cast<float> (start) / static_cast<float> (juce::jmax (1, filled_));
        const float alpha = 0.10f + 0.55f * age;

        g.setColour (palette_.accentBright.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

} // namespace tezla::ui
