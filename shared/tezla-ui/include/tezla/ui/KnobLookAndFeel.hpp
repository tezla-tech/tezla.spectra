// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The house look: thin arcs, a pointer, and no chrome.
//
// JUCE's stock rotary is a thick filled pie with a text box in a box, and it
// stops being readable somewhere around fifty pixels across -- which is exactly
// where a synth with sixty controls needs its knobs to live. Everything here
// exists to make a **small** control legible:
//
//  - The value is an **arc**, not a pie. A stroke of constant thickness reads at
//    any diameter; a filled wedge turns into a blob as the radius shrinks.
//  - The arc starts from the parameter's **anchor**, not from its minimum. A
//    bipolar control -- a detune, a tilt, a modulation depth -- is a departure
//    from centre in one direction or the other, and drawing it as a bar growing
//    from the far left says the opposite. The anchor is found from the range
//    itself, so nothing has to be declared: if the range straddles zero the
//    anchor is where zero falls, skew included.
//  - A **pointer**, because an arc alone has no fine reading. The eye takes the
//    coarse value from the arc's length and the exact one from the line.
//  - The text box has no border and no background. A rectangle around every
//    number is sixty rectangles on a page, and none of them carry information.
//
// The palette comes in by construction, so this carries a plugin's identity
// without knowing which plugin it is in -- the same contract as every other
// component in this directory.

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.hpp"

namespace tezla::ui
{

class KnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit KnobLookAndFeel (Palette palette) : palette_ (palette)
    {
        setColour (juce::PopupMenu::backgroundColourId, palette_.panel.brighter (0.10f));
        setColour (juce::PopupMenu::textColourId, palette_.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette_.accent.withAlpha (0.45f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, palette_.background.brighter (0.06f));
        setColour (juce::TooltipWindow::textColourId, palette_.text);
        setColour (juce::TooltipWindow::outlineColourId, palette_.accent.withAlpha (0.35f));
        setColour (juce::Label::textColourId, palette_.text);
    }

    // -----------------------------------------------------------------------
    // Rotaries
    // -----------------------------------------------------------------------

    /// Where a knob's arc grows **from**, as a proportion of its travel.
    ///
    /// Zero if the range straddles it, so a bipolar control reads as a
    /// departure from centre; otherwise the minimum, so a unipolar one reads as
    /// a quantity. `valueToProportionOfLength` rather than arithmetic on the
    /// endpoints, because the ranges here are skewed and the midpoint of a
    /// skewed range is not halfway along it.
    /// Non-const, because `valueToProportionOfLength` is: it goes through the
    /// slider's `NormalisableRange`, which JUCE does not mark const.
    [[nodiscard]] static double anchorProportion (juce::Slider& slider)
    {
        if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
            return juce::jlimit (0.0, 1.0, slider.valueToProportionOfLength (0.0));

        return 0.0;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> { x, y, width, height }.toFloat().reduced (2.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();

        const float thickness = juce::jmax (2.5f, radius * 0.20f);
        const float trackRadius = radius - thickness * 0.5f;

        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float anchor = startAngle
                           + static_cast<float> (anchorProportion (slider)) * (endAngle - startAngle);

        const bool on = slider.isEnabled();

        // **A knob may carry its own colour and its own relief.**
        //
        // Component properties rather than `Slider::rotarySliderFillColourId`,
        // because that id already has a stock default and "has the caller set
        // it" is then unanswerable -- a knob nobody tinted and a knob tinted to
        // the default look identical from in here. A property is absent until
        // somebody puts it there, which is the question actually being asked.
        const auto tint = [&]
        {
            const auto& value = slider.getProperties()["tezlaTint"];

            return value.isVoid() ? palette_.accent
                                  : juce::Colour (static_cast<juce::uint32> (
                                        static_cast<juce::int64> (value)));
        }();

        const auto tintBright = tint == palette_.accent ? palette_.accentBright
                                                        : tint.brighter (0.45f);

        const bool relief = static_cast<bool> (slider.getProperties().getWithDefault (
            "tezlaRelief", false));

        const auto fill = (on ? tint : palette_.dimText.withAlpha (0.30f));

        const auto arc = [&] (float from, float to, juce::Colour colour, float width_)
        {
            juce::Path path;
            path.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                                juce::jmin (from, to), juce::jmax (from, to), true);
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (width_, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        };

        // The unfilled travel, so the knob has a shape even at zero. Struck from
        // the *text* colour rather than the panel: a knob sits on a group panel
        // which is itself a lightened panel, and a track defined relative to
        // `panel` disappears against it -- which it did, and a page of knobs at
        // their minimum read as a page of bare pointers.
        arc (startAngle, endAngle, palette_.dimText.withAlpha (on ? 0.26f : 0.12f), thickness);

        // The body: a shallow gradient, dark at the bottom. Enough to lift the
        // knob off the panel without pretending to be a photograph of one.
        const float bodyRadius = trackRadius - thickness * 0.75f;

        if (bodyRadius > 3.0f)
        {
            const auto body = juce::Rectangle<float> { bodyRadius * 2.0f, bodyRadius * 2.0f }
                                .withCentre (centre);

            if (relief)
            {
                // **A socket, then a cap.**
                //
                // The flat body below is a dark circle on a dark plate, and
                // the two are within a few points of the same lightness -- so
                // the knob reads as a shade of the panel rather than as a thing
                // bolted to it. A real panel separates them with geometry: the
                // knob sits in a countersunk well, and the well's shadow is
                // what the eye actually reads as an edge.
                //
                // Three passes, no images: the well, the shadow it casts on its
                // own upper lip, and a cap whose gradient runs the other way
                // from the well's so the two cannot be confused.
                const auto well = body.expanded (juce::jmax (2.0f, bodyRadius * 0.16f));

                g.setColour (juce::Colours::black.withAlpha (0.42f));
                g.fillEllipse (well.translated (0.0f, 1.0f));

                g.setColour (juce::Colours::black.withAlpha (0.30f));
                g.fillEllipse (well);

                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawEllipse (well.reduced (0.5f), 1.0f);

                // The cap, lit from above and **clearly** lighter than the
                // plate rather than a step darker than it.
                //
                // Literal greys rather than `panel.brighter()`, because
                // `brighter` works in HSB and its steps are not perceptually
                // even -- the first attempt asked for 0.62 of brighter and
                // landed the cap at RGB 93 against a plate at 57, which is a
                // difference you have to go looking for. These are measured
                // against the group plate at 0x34373a: the cap runs 0x86 down
                // to 0x4a, so its lit half is half again as light as the plate
                // and its shaded half still clears it.
                g.setGradientFill (juce::ColourGradient (
                    juce::Colour { 0xff86898d }, body.getCentreX(), body.getY(),
                    juce::Colour { 0xff4a4d51 }, body.getCentreX(), body.getBottom(), false));
                g.fillEllipse (body);

                // A specular sliver across the top third. One ellipse, and it
                // is what stops a flat gradient reading as a disc of paint.
                g.setColour (juce::Colours::white.withAlpha (on ? 0.10f : 0.04f));
                g.fillEllipse (body.reduced (body.getWidth() * 0.18f)
                                   .withHeight (body.getHeight() * 0.34f)
                                   .translated (0.0f, body.getHeight() * 0.10f));

                g.setColour (juce::Colours::white.withAlpha (on ? 0.22f : 0.08f));
                g.drawEllipse (body.reduced (0.5f), 1.0f);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (palette_.panel.brighter (0.22f), body.getCentreX(),
                                                         body.getY(),
                                                         palette_.background, body.getCentreX(),
                                                         body.getBottom(), false));
                g.fillEllipse (body);

                // A hairline round the body, brighter at the top. The same trick as
                // the panel highlight: it is what gives a dark circle an edge.
                g.setColour (juce::Colours::white.withAlpha (on ? 0.07f : 0.03f));
                g.drawEllipse (body.reduced (0.5f), 1.0f);
            }
        }

        // The value, from the anchor -- with a wide, very transparent stroke
        // behind it.
        //
        // That halo is the whole difference between a panel that looks drawn and
        // one that looks lit. A real LED ring scatters in the diffuser around
        // it, and two strokes of the same colour at different widths and alphas
        // is the cheapest honest imitation: no blur, no image, one extra path.
        // Skipped when the control is disabled, so a greyed knob stays flat and
        // reads as inert at a glance.
        if (std::abs (angle - anchor) > 1.0e-4f)
        {
            if (on)
                arc (anchor, angle, tint.withAlpha (0.18f), thickness * 2.4f);

            arc (anchor, angle, fill, thickness);
        }

        // The pointer.
        {
            const float inner = juce::jmax (1.0f, bodyRadius * 0.30f);
            const float outer = trackRadius - thickness * 0.55f;
            const juce::Point<float> from { centre.x + inner * std::sin (angle),
                                            centre.y - inner * std::cos (angle) };
            const juce::Point<float> to { centre.x + outer * std::sin (angle),
                                          centre.y - outer * std::cos (angle) };

            if (on)
            {
                g.setColour (tintBright.withAlpha (0.22f));
                g.drawLine ({ from, to }, juce::jmax (1.6f, radius * 0.085f) * 2.6f);
            }

            // On a relief cap the pointer sits on a light grey, where a dim
            // grey line disappears -- so the disabled pointer goes dark rather
            // than pale, and the lit one keeps its bright tint.
            g.setColour (on ? tintBright
                            : (relief ? juce::Colours::black.withAlpha (0.45f)
                                      : palette_.dimText.withAlpha (0.35f)));
            g.drawLine ({ from, to }, juce::jmax (1.6f, radius * 0.085f));
        }

        // The anchor mark on a bipolar control. Without it "near the middle" and
        // "at the middle" look the same, and on a depth knob those are the
        // difference between a modulation and none.
        if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        {
            const float outer = radius;
            const float inner = radius - thickness * 0.9f;

            g.setColour (palette_.dimText.withAlpha (on ? 0.75f : 0.30f));
            g.drawLine ({ { centre.x + inner * std::sin (anchor), centre.y - inner * std::cos (anchor) },
                          { centre.x + outer * std::sin (anchor), centre.y - outer * std::cos (anchor) } },
                        1.0f);
        }
    }

    /// The text box under a knob, sized so the knob keeps the rest.
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);

        // **Only when the caller asked for the value underneath.** Applying
        // this to every rotary took a header knob with its value set to the
        // *right* and stacked it anyway, leaving the knob a third of the height
        // it had been given and the number in the wrong place. The stock layout
        // already handles the other positions correctly.
        if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow
            && (slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
                || slider.getSliderStyle() == juce::Slider::Rotary))
        {
            auto bounds = slider.getLocalBounds();
            const int textHeight = slider.getTextBoxHeight();

            layout.textBoxBounds = bounds.removeFromBottom (textHeight);
            layout.sliderBounds = bounds;
        }

        return layout;
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);

        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineWhenEditingColourId, palette_.accent);
        label->setColour (juce::Label::backgroundWhenEditingColourId, palette_.background);
        label->setColour (juce::Label::textWhenEditingColourId, palette_.text);
        label->setFont (juce::FontOptions (11.5f));
        label->setJustificationType (juce::Justification::centred);

        return label;
    }

    // -----------------------------------------------------------------------
    // Bars -- the step sequencer's faders
    // -----------------------------------------------------------------------

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (! slider.isBar())
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto bounds = juce::Rectangle<int> { x, y, width, height }.toFloat();
        const bool vertical = style == juce::Slider::LinearBarVertical;

        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (bounds, 2.0f);

        // Anchored, like the rotaries: a bipolar step at nothing must draw as
        // nothing, not as a bar half a screen tall.
        const float anchorPos = vertical
            ? bounds.getBottom() - static_cast<float> (anchorProportion (slider)) * bounds.getHeight()
            : bounds.getX() + static_cast<float> (anchorProportion (slider)) * bounds.getWidth();

        // The zero line, drawn per bar rather than as one rule across the strip
        // behind them. Children paint over their parent, so a line the parent
        // draws is a line sixteen faders cover up -- and a bipolar fader with no
        // visible zero is one whose neutral position has to be guessed.
        g.setColour (palette_.dimText.withAlpha (0.28f));

        if (vertical)
            g.fillRect (bounds.getX(), anchorPos - 0.5f, bounds.getWidth(), 1.0f);
        else
            g.fillRect (anchorPos - 0.5f, bounds.getY(), 1.0f, bounds.getHeight());

        const auto filled = vertical
            ? juce::Rectangle<float> { bounds.getX(), juce::jmin (sliderPos, anchorPos),
                                       bounds.getWidth(), std::abs (sliderPos - anchorPos) }
            : juce::Rectangle<float> { juce::jmin (sliderPos, anchorPos), bounds.getY(),
                                       std::abs (sliderPos - anchorPos), bounds.getHeight() };

        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (filled, 2.0f);

        // A cap on the moving end, so a bar too short to see is still visible --
        // but only once it has left the anchor. Drawn unconditionally it lands
        // exactly on the zero line, and a pattern of sixteen untouched steps
        // reads as sixteen bright marks rather than as the flat line it is.
        if (std::abs (sliderPos - anchorPos) > 1.0f)
        {
            g.setColour (palette_.accentBright.withAlpha (0.85f));

            if (vertical)
                g.fillRect (bounds.getX() + 1.0f, sliderPos - 0.75f, bounds.getWidth() - 2.0f, 1.5f);
            else
                g.fillRect (sliderPos - 0.75f, bounds.getY() + 1.0f, 1.5f, bounds.getHeight() - 2.0f);
        }
    }

    // -----------------------------------------------------------------------
    // Boxes, buttons and furniture
    // -----------------------------------------------------------------------

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float> { 0.0f, 0.0f,
                                                     static_cast<float> (width),
                                                     static_cast<float> (height) }.reduced (0.5f);

        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 3.0f);

        g.setColour (box.isEnabled() ? palette_.accent.withAlpha (box.isMouseOver() ? 0.7f : 0.35f)
                                     : palette_.panel.brighter (0.15f));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        // A chevron rather than a filled triangle: the same reason the knobs
        // are arcs.
        const float cx = bounds.getRight() - 11.0f;
        const float cy = bounds.getCentreY();

        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 1.5f);

        g.setColour (box.isEnabled() ? palette_.dimText : palette_.dimText.withAlpha (0.4f));
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (12.0f));
    }

    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (13.0f)); }

    /// A pill with a travelling dot. A tick box says "an option in a list"; an
    /// on/off control on a synth panel is a switch, and reads faster as one.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool) override
    {
        const auto full = button.getLocalBounds().toFloat();
        const float height = juce::jmin (full.getHeight(), 17.0f);
        const auto bounds = juce::Rectangle<float> { juce::jmin (full.getWidth(), height * 2.0f),
                                                     height }.withCentre (full.getCentre());

        const bool on = button.getToggleState();
        const bool enabled = button.isEnabled();

        g.setColour (on ? palette_.accent.withAlpha (enabled ? 0.75f : 0.25f)
                        : palette_.panel.darker (enabled ? 0.35f : 0.15f));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

        g.setColour (enabled ? palette_.dimText.withAlpha (shouldDrawButtonAsHighlighted ? 0.9f : 0.45f)
                             : palette_.dimText.withAlpha (0.2f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);

        const float dot = bounds.getHeight() - 5.0f;
        const auto knob = juce::Rectangle<float> { dot, dot }.withCentre (
            { on ? bounds.getRight() - dot * 0.5f - 2.5f : bounds.getX() + dot * 0.5f + 2.5f,
              bounds.getCentreY() });

        g.setColour (on ? (enabled ? palette_.accentBright : palette_.dimText)
                        : palette_.dimText.withAlpha (enabled ? 0.85f : 0.35f));
        g.fillEllipse (knob);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

        auto fill = backgroundColour;

        if (shouldDrawButtonAsDown)
            fill = fill.brighter (0.25f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.12f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 3.0f);

        g.setColour (palette_.panel.brighter (0.28f));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return juce::Font (juce::FontOptions (12.0f, juce::Font::bold));
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize, bool isMouseOver,
                        bool isMouseDown) override
    {
        juce::ignoreUnused (bar);

        if (thumbSize <= 0)
            return;

        // A visible rail, and a thumb wide enough to aim at. The first
        // version was a 4 px sliver with no track, and the user's report was
        // exact: the pages scroll and nothing on screen says so. The rail is
        // what announces "there is more below"; the thumb fills most of the
        // bar's width so the whole strip is one obvious handle.
        const auto rail = juce::Rectangle<int> { x, y, width, height };

        g.setColour (palette_.panel.brighter (0.10f));
        g.fillRoundedRectangle (rail.toFloat(), 4.0f);

        const int girth = (isVertical ? width : height) - 4;
        const auto thumb = isVertical
            ? juce::Rectangle<int> { x + 2, thumbStart, girth, std::max (thumbSize, 28) }
            : juce::Rectangle<int> { thumbStart, y + 2, std::max (thumbSize, 28), girth };

        g.setColour (palette_.accent.withAlpha (isMouseDown ? 0.90f : (isMouseOver ? 0.70f : 0.45f)));
        g.fillRoundedRectangle (thumb.toFloat(), static_cast<float> (girth) * 0.5f);
    }

    /// Sized with the same font `drawTooltip` paints with, and wider than the
    /// stock four hundred pixels. The tooltips in this workshop are whole
    /// paragraphs -- they are how the plugins document themselves -- and a
    /// sizing pass that disagrees with the drawing pass clips the last line
    /// off every one of them.
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override
    {
        juce::AttributedString attributed;
        attributed.setJustification (juce::Justification::topLeft);
        attributed.append (tipText, juce::Font (juce::FontOptions (13.0f)));

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (attributed, 460.0f);

        const int width = juce::roundToInt (layout.getWidth()) + 18;
        const int height = juce::roundToInt (layout.getHeight()) + 14;

        return juce::Rectangle<int> (
            screenPos.x > parentArea.getCentreX() ? screenPos.x - (width + 12) : screenPos.x + 12,
            screenPos.y > parentArea.getCentreY() ? screenPos.y - (height + 6) : screenPos.y + 6,
            width, height).constrainedWithin (parentArea);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        const auto bounds = juce::Rectangle<float> { static_cast<float> (width),
                                                     static_cast<float> (height) }.reduced (0.5f);

        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        juce::AttributedString attributed;
        attributed.setJustification (juce::Justification::topLeft);
        attributed.append (text, juce::Font (juce::FontOptions (13.0f)),
                           findColour (juce::TooltipWindow::textColourId));

        attributed.draw (g, bounds.reduced (8.0f, 6.0f));
    }

private:
    Palette palette_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobLookAndFeel)
};

} // namespace tezla::ui
