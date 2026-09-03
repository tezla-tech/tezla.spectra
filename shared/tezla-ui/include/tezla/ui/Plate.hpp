// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A control plate: the dark raised panel a group of controls sits on, with a
// spine of the group's own colour down its left edge and a heading with a
// rule running out to the plate's right edge.
//
// The design was chosen in the Sonitus editor among eight photographed
// variants (PanelDesign.hpp, "Where these came from"), and Sonitus draws it
// with two local functions. Shared here, unchanged in geometry, so every
// plugin that groups its controls draws the same plate: the plate's own
// colour is the caller's, because a plate on a dark chassis (Ictus) and one
// on brushed metal (Sonitus) want different greys under the same shading.

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.hpp"

namespace tezla::ui
{

/// The heading row's height, inside the plate's top edge.
inline constexpr int kPlateHeadingHeight = 16;

/// Paints the plate: a drop shadow, a shallow vertical gradient, a lit top
/// lip and a shaded bottom one, and the spine.
///
/// Two things beyond a filled rectangle, and both are what makes a dark
/// panel look lit rather than flat: the gradient, brighter at the top, and
/// the one-pixel highlight along the top edge -- how every piece of real
/// hardware catches the light in a photograph.
inline void paintPlate (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour plate,
                        juce::Colour spine = juce::Colours::transparentBlack)
{
    const auto area = bounds.toFloat();

    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f).expanded (1.0f, 0.5f), 6.0f);

    g.setGradientFill (juce::ColourGradient (plate.brighter (0.09f), area.getX(), area.getY(),
                                             plate.darker (0.16f), area.getX(), area.getBottom(),
                                             false));
    g.fillRoundedRectangle (area, 5.0f);

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (area.getX() + 5.0f, area.getY() + 0.5f,
                area.getRight() - 5.0f, area.getY() + 0.5f, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawLine (area.getX() + 5.0f, area.getBottom() - 0.5f,
                area.getRight() - 5.0f, area.getBottom() - 0.5f, 1.0f);

    // The spine: a bar of the group's colour down the left edge. A coloured
    // heading tells you which group you are reading once you are reading
    // it; a spine tells you from the far side of the window, in the one
    // place a scanning eye already goes.
    if (! spine.isTransparent())
    {
        juce::Path bar;
        bar.addRoundedRectangle (area.getX() + 1.0f, area.getY() + 4.0f,
                                 3.0f, area.getHeight() - 8.0f,
                                 1.5f, 1.5f, true, false, true, false);

        g.setColour (spine.withAlpha (0.30f));
        g.fillPath (bar, juce::AffineTransform::scale (2.6f, 1.0f, area.getX() + 1.0f,
                                                       area.getCentreY()));

        g.setColour (spine.withAlpha (0.85f));
        g.fillPath (bar);
    }
}

/// Paints a plate's heading: its name in the group's colour, an optional
/// explanation in dim text, and a rule out to the right edge.
inline void paintPlateHeading (juce::Graphics& g, Palette palette, juce::Rectangle<int> row,
                               const juce::String& name, const juce::String& detail,
                               juce::Colour tint = juce::Colours::transparentBlack)
{
    const auto accent = tint.isTransparent() ? palette.accent : tint;
    const auto text = row.reduced (8, 0);

    g.setColour (accent);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));

    const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name);
    g.drawText (name, text, juce::Justification::centredLeft, false);

    int x = text.getX() + nameWidth + 8;

    if (detail.isNotEmpty())
    {
        g.setColour (palette.dimText);
        g.setFont (juce::FontOptions (10.5f));

        const auto detailWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), detail);

        g.drawText (detail, text.withX (x).withWidth (juce::jmax (0, text.getRight() - x)),
                    juce::Justification::centredLeft, false);

        x += juce::jmin (detailWidth, juce::jmax (0, text.getRight() - x)) + 8;
    }

    if (x < text.getRight())
    {
        g.setColour (accent.withAlpha (0.16f));
        g.drawHorizontalLine (row.getCentreY(), static_cast<float> (x),
                              static_cast<float> (text.getRight()));
    }
}

} // namespace tezla::ui
