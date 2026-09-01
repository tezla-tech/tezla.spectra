// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The house on/off control: a moulded cap in a recessed bezel that lights red.
//
// ---------------------------------------------------------------------------
// Never a tick box
// ---------------------------------------------------------------------------
//
// A tick box says "an option in a list". An on/off control on an instrument is
// a *switch*, and it reads faster as one -- which matters on a panel of sixty
// controls where the eye is looking for what is currently doing something. The
// pill-with-a-travelling-dot that this replaced was closer to a tick box than
// to a switch, which is why it went.
//
// So: the label lives inside the cap, the whole cap lights, it *moves* when
// pressed, and lit it throws a glow the dark state does not. Four cues at once
// -- colour, travel, legend contrast, glow -- because on a dense panel any
// single cue is one somebody's eye slides past.
//
// ---------------------------------------------------------------------------
// Why it is red on every group
// ---------------------------------------------------------------------------
//
// Everything else on these panels takes its group's hue: headings, spines,
// names, knob tracks, dropdowns. The switch deliberately does not. A power
// switch is red on every box in a rack precisely so that it can be *read*
// without first being identified, and a switch that changed colour by which
// group it landed in would be the one element on the panel whose meaning
// depended on where it was. It is a disagreement with the rest of the scheme,
// and it is the point.
//
// ---------------------------------------------------------------------------
// The glow needs room, and this is why the button is bigger than the switch
// ---------------------------------------------------------------------------
//
// JUCE clips a component's painting to its own bounds. The first version of
// this drew its halo at `getLocalBounds().expanded (3.5f)` -- entirely outside
// the clip, so all that ever reached the screen was the one-pixel sliver
// between the bezel and the component edge, and the switch looked barely lit.
// Nothing errors, nothing warns; the paint call simply goes nowhere.
//
// The fix is geometric rather than clever: the *button* is `kGlowMargin`
// larger on every side than the switch drawn inside it, so the halo has
// somewhere to land. The visible cap is unchanged; the hit area grows, which
// on a control this size is an improvement rather than a cost.

#include <juce_gui_basics/juce_gui_basics.h>

namespace tezla::ui
{

class LampButton final : public juce::Button
{
public:
    /// How much bigger the button is than the switch inside it, per side, so
    /// the lit halo is not clipped away. Callers size the button with this
    /// added -- `sized()` does the arithmetic.
    static constexpr int kGlowMargin = 7;

    /// The switch's colour, lit. Not a parameter: see the header comment.
    static constexpr juce::uint32 kLitArgb = 0xffe8342a;

    explicit LampButton (const juce::String& name)
        : juce::Button (name)
    {
        setButtonText (name.toUpperCase());
    }

    /// The button bounds that draw a switch of the given size.
    [[nodiscard]] static juce::Rectangle<int> sized (int switchWidth, int switchHeight) noexcept
    {
        return { switchWidth + 2 * kGlowMargin, switchHeight + 2 * kGlowMargin };
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool on = getToggleState();
        const bool enabled = isEnabled();
        const auto lit = juce::Colour { kLitArgb };

        // Four passes and no images:
        //
        //  1. The bezel: a dark, hard-edged recess cut into the plate, with its
        //     own inner shadow along the top and a lit lip along the bottom.
        //     This is the hole the switch lives in and it is what makes the cap
        //     look like it is *in* the panel rather than on it.
        //  2. The cap: inset from the bezel, with a vertical gradient. Off, the
        //     gradient runs light-at-the-top -- a surface lit from above,
        //     standing proud. On, it inverts and the cap drops two pixels,
        //     which is what a pressed switch does and is the cue the eye reads
        //     fastest.
        //  3. The chamfer: a bright line along whichever edge is catching the
        //     light and a dark one opposite. Two lines, and they are the whole
        //     of the 3D -- a gradient alone reads as a painted rectangle.
        //  4. The legend, engraved into the cap rather than printed on it.
        const auto full = getLocalBounds().toFloat().reduced (static_cast<float> (kGlowMargin));
        const auto bezel = full.reduced (1.0f);
        const float bezelRadius = 4.0f;

        // -- the halo, outside the bezel, before anything else ---------------
        //
        // Painted outermost-first with the alpha rising as the rings close in,
        // so five flat fills accumulate into something that falls off smoothly.
        // The cap covers the innermost, so what is actually seen is the ramp.
        if (on && enabled)
        {
            static constexpr float kSpread[] { 7.0f, 5.5f, 4.0f, 2.5f, 1.0f };
            static constexpr float kAlpha[]  { 0.06f, 0.09f, 0.13f, 0.18f, 0.26f };

            for (int i = 0; i < 5; ++i)
            {
                g.setColour (lit.withAlpha (kAlpha[i]));
                g.fillRoundedRectangle (bezel.expanded (kSpread[i]), bezelRadius + kSpread[i]);
            }
        }

        // -- 1. the recess ---------------------------------------------------
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (bezel, bezelRadius);

        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawLine (bezel.getX() + bezelRadius, bezel.getY() + 1.0f,
                    bezel.getRight() - bezelRadius, bezel.getY() + 1.0f, 1.6f);

        g.setColour (juce::Colours::white.withAlpha (0.11f));
        g.drawLine (bezel.getX() + bezelRadius, bezel.getBottom() - 0.75f,
                    bezel.getRight() - bezelRadius, bezel.getBottom() - 0.75f, 1.5f);

        // The bezel's own rim, catching the lamp. A real illuminated switch
        // leaks light around its cap into the surround, and this one line is
        // most of why the lit state reads as *illuminated* rather than as
        // *painted red*.
        if (on && enabled)
        {
            g.setColour (lit.withAlpha (0.55f));
            g.drawRoundedRectangle (bezel.reduced (0.5f), bezelRadius, 1.4f);
        }

        // -- 2. the cap ------------------------------------------------------
        //
        // Pressed, it sits lower in its hole and loses a pixel of height: the
        // travel is what says "this moved", and a switch that lights without
        // moving is a lamp with a legend on it.
        const float travel = on ? 2.0f : 0.0f;

        const auto cap = bezel.reduced (3.0f, 3.0f)
                              .withTrimmedTop (travel)
                              .translated (0.0f, on ? 0.5f : -0.5f);

        const auto capBase = on ? (enabled ? lit.withAlpha (0.94f) : lit.withAlpha (0.25f))
                                : juce::Colour { 0xff4c5055 }.withAlpha (enabled ? 1.0f : 0.5f);

        const float capRadius = 2.5f;

        g.setGradientFill (on
            // Pressed: lit from below, because the face has tipped away from
            // the light. This inversion is most of the "it went in" reading.
            ? juce::ColourGradient (capBase.darker (0.30f), cap.getCentreX(), cap.getY(),
                                    capBase.brighter (0.18f), cap.getCentreX(), cap.getBottom(), false)
            : juce::ColourGradient (capBase.brighter (0.55f), cap.getCentreX(), cap.getY(),
                                    capBase.darker (0.42f), cap.getCentreX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, capRadius);

        // The filament behind the lens: a radial hot spot, brightest just below
        // centre where a lamp inside a moulded cap actually sits. Laid over the
        // vertical gradient rather than replacing it, so the cap still reads as
        // a curved surface and not as a disc.
        if (on && enabled)
        {
            const auto centre = cap.getCentre().translated (0.0f, cap.getHeight() * 0.08f);
            const float reach = juce::jmax (cap.getWidth(), cap.getHeight()) * 0.62f;

            juce::ColourGradient core { lit.brighter (0.85f).withAlpha (0.55f), centre.x, centre.y,
                                        lit.withAlpha (0.0f), centre.x + reach, centre.y, true };
            core.addColour (0.45, lit.brighter (0.35f).withAlpha (0.22f));

            g.setGradientFill (core);
            g.fillRoundedRectangle (cap, capRadius);
        }

        // -- 3. the chamfer --------------------------------------------------
        g.setColour (juce::Colours::white.withAlpha (on ? 0.14f : (enabled ? 0.32f : 0.12f)));
        g.drawLine (cap.getX() + capRadius, cap.getY() + 0.75f,
                    cap.getRight() - capRadius, cap.getY() + 0.75f, 1.5f);

        g.setColour (juce::Colours::black.withAlpha (on ? 0.20f : 0.42f));
        g.drawLine (cap.getX() + capRadius, cap.getBottom() - 0.75f,
                    cap.getRight() - capRadius, cap.getBottom() - 0.75f, 1.5f);

        // The sides, half as strong: a moulded cap is rounded across its width
        // too.
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawRoundedRectangle (cap.reduced (0.5f), capRadius, 1.0f);

        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.07f));
            g.fillRoundedRectangle (cap, capRadius);
        }

        // -- 4. the legend ---------------------------------------------------
        //
        // Cut into the cap rather than printed on it: a dark legend on a lit
        // cap and a light one on a dead cap, each with the faintest opposite
        // shadow offset by a pixel, which is what engraving looks like from a
        // metre away.
        //
        // Centred on the *cap*, which already carries the travel, so the legend
        // moves with the switch for free. Doing it from `getLocalBounds` needs
        // `translated` and not `withY` -- `withY` takes an absolute coordinate
        // and `getY()` is where this button sits in its parent, which pushed
        // the legend clean off the cap and made the switch a blank red slab.
        const auto legend = cap.toNearestInt();

        g.setFont (juce::FontOptions (juce::jmin (11.5f, cap.getHeight() * 0.55f),
                                      juce::Font::bold));

        g.setColour (on ? juce::Colours::white.withAlpha (enabled ? 0.24f : 0.08f)
                        : juce::Colours::black.withAlpha (enabled ? 0.45f : 0.15f));
        g.drawText (getButtonText(), legend.translated (0, 1), juce::Justification::centred, false);

        g.setColour (on ? juce::Colours::black.withAlpha (enabled ? 0.82f : 0.30f)
                        : juce::Colour { 0xffb9bdc2 }.withAlpha (enabled ? 1.0f : 0.35f));
        g.drawText (getButtonText(), legend, juce::Justification::centred, false);
    }
};

} // namespace tezla::ui
