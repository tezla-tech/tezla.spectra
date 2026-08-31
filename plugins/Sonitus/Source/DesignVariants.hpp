// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Panel design variants, so a layout question can be *looked at* rather than
// argued about.
//
// ---------------------------------------------------------------------------
// Why this exists as code rather than as a mockup
// ---------------------------------------------------------------------------
//
// A drawing of a panel is a drawing. It cannot tell you that six columns of
// knobs do not fit at 880 px, that a group heading collides with its own rule
// at three words, or that a tinted ring stops clearing 4.5:1 two hues round the
// wheel. Every one of those is a thing this workshop has already been bitten by
// and every one of them is invisible until the real components lay themselves
// out at the real sizes.
//
// So the variants are the real editor with different numbers, selected at
// construction from `TEZLA_DESIGN`, and photographed through
// `tezla-render editor shot:`. Nothing here changes what the plugin does; a
// variant is metrics, colour and which component a cell holds.
//
// **This is scaffolding for a decision.** Once one is chosen the rest go, and
// the winner's numbers become the panel's numbers -- an enum switch is not a
// thing to ship on a control surface.

#include <cstdlib>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/ui/Palette.hpp>

namespace tezla::sonitus::design
{

/// How much of the panel a control deserves.
///
/// Not every knob on a page is equally important, and drawing them the same
/// size says they are. Cutoff and Level are what a player reaches for; Drift is
/// something set once and forgotten. The current panel gives them identical
/// footprints, so the eye has nothing to land on.
enum class Emphasis
{
    lead,        ///< the control the group is *about*
    normal,
    trim         ///< set once, then ignored
};

/// The cell's control, which is a design question rather than a technical one.
enum class Control
{
    rotary,      ///< a knob
    bar          ///< a horizontal bar with the value inside it
};

/// How a group is told apart from its neighbours.
enum class Grouping
{
    uniform,     ///< every group in the page accent -- what ships today
    tinted       ///< each group its own hue, rotated off the page accent
};

struct Variant
{
    const char* name { "Current" };
    const char* note { "what ships today" };

    // -- the grid ------------------------------------------------------------
    int cellWidthMax { 172 };
    int cellHeightMin { 62 };
    int cellHeightMax { 80 };
    int groupGap { 10 };

    /// How much of the cell the control fills, before emphasis.
    float controlScale { 1.0f };

    /// Multipliers on the rotary's diameter, by emphasis.
    float leadScale { 1.0f };
    float trimScale { 1.0f };

    // -- the look ------------------------------------------------------------
    Control control { Control::rotary };
    Grouping grouping { Grouping::uniform };

    /// Degrees of hue between one group and the next, when tinted.
    float groupHueStep { 0.0f };

    /// A coloured spine down the left edge of each group's plate. Cheap, and
    /// it survives being glanced at in a way a heading colour does not.
    bool groupSpine { false };

    /// Lamp buttons instead of the pill switch. The brief is explicit:
    /// **never a tick box**, and a pill with a travelling dot is closer to one
    /// than to a lit button.
    bool lampToggles { false };

    /// Label size, in points. Denser layouts want a smaller one.
    float labelSize { 10.0f };

    /// **The knob as a bolted-on object rather than a shade of the plate.**
    ///
    /// The panel that ships draws a knob body from `panel.brighter(0.22)` down
    /// to `background`, on a group plate that is itself a lightened panel --
    /// so the knob's cap and the plate behind it land within a few points of
    /// the same lightness and the control reads as a smudge. Relief puts it in
    /// a countersunk well and lights the cap from above, which is how a real
    /// panel separates the two: with geometry, not with hue.
    bool knobRelief { false };

    /// **Let a group fill its row rather than centring in a sea of metal.**
    ///
    /// The column counts at the call sites were chosen for a panel whose cells
    /// were 172 px wide. Narrow the cell without touching them and a six-column
    /// group of eleven controls occupies 670 px of an 1800 px plate and centres
    /// itself, which is worse than the spacing it replaced -- the knobs are
    /// closer together and the *page* is emptier.
    ///
    /// So the design may raise the column count until the row is full, down to
    /// `cellWidthMin`. A group asking for six gets eleven when eleven fit, and
    /// its two ragged rows become one full one.
    bool fillRow { false };

    /// The narrowest a cell may be squeezed to while filling a row.
    int cellWidthMin { 96 };
};

/// The variants, in order of how far they move from what ships.
///
/// Each is the one before it plus one idea, so the screenshots read as a
/// progression and a choice can land between two of them rather than only on
/// one.
[[nodiscard]] inline const Variant& variantAt (int index) noexcept
{
    static const Variant kVariants[] {
        // 0 -- the baseline, unchanged, so a comparison has something to be
        // against in the same run rather than against a memory.
        Variant {},

        // 1 -- COMPACT. Nothing but metrics: the cell loses 60 px of width and
        // the knob keeps its size, so the air between knobs goes and the page
        // fits its window instead of leaving a third of it empty.
        Variant {
            "Compact", "one full row per group, bigger knobs, closer together",
            118, 78, 104, 8,
            1.0f, 1.0f, 1.0f,
            Control::rotary, Grouping::uniform, 0.0f, false,
            true, 10.0f, true, true, 86
        },

        // 2 -- TIERED. Compact plus a size hierarchy, so each group has
        // something the eye lands on first.
        Variant {
            "Tiered", "compact, plus lead and trim sizes",
            118, 78, 104, 8,
            1.0f, 1.30f, 0.72f,
            Control::rotary, Grouping::uniform, 0.0f, false,
            true, 10.0f, true, true, 86
        },

        // 3 -- CHROMATIC. Tiered plus a hue per group. 24 degrees a step keeps
        // the family related -- five groups span 96 degrees, which is a quarter
        // of the wheel and reads as one instrument rather than as a paintbox.
        Variant {
            "Chromatic", "tiered, plus a hue and a spine per group",
            118, 78, 104, 8,
            1.0f, 1.30f, 0.72f,
            Control::rotary, Grouping::tinted, 24.0f, true,
            true, 10.0f, true, true, 86
        },

        // 4 -- MIXER. The radical one: bars instead of knobs, value inside the
        // bar, cell height nearly halved. Twice the controls in a window and
        // every value readable without hovering -- at the cost of the gesture a
        // knob gives you, which on a synth is not nothing.
        Variant {
            "Mixer", "bars with inline values, twice the density",
            190, 40, 50, 7,
            1.0f, 1.0f, 1.0f,
            Control::bar, Grouping::tinted, 24.0f, true,
            true, 9.5f, true, true, 150
        }
    };

    constexpr int count = static_cast<int> (std::size (kVariants));

    return kVariants[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

[[nodiscard]] inline int variantCount() noexcept { return 5; }

/// The variant this editor was built with.
///
/// Read once from the environment, because a panel that changed shape halfway
/// through a session would be a worse thing to photograph than either of the
/// designs it changed between.
[[nodiscard]] inline const Variant& current() noexcept
{
    static const int chosen = []
    {
        if (const char* text = std::getenv ("TEZLA_DESIGN"))
            return juce::String (text).getIntValue();

        return 0;
    }();

    return variantAt (chosen);
}

/// Which controls carry a group.
///
/// A table rather than an argument on `addKnob`, and deliberately so **while
/// this is scaffolding**: an argument would put an editorial decision at two
/// hundred call sites before anyone has agreed the decision is worth making. If
/// a tiered design wins, this moves to the call sites, where the group that
/// knows what it is about can say so.
[[nodiscard]] inline Emphasis emphasisOf (const juce::String& id) noexcept
{
    // The control each group is *about*: turn this one and the group changes
    // character; turn any other and it adjusts.
    static const char* leads[] {
        "levelA", "levelB", "cutoff", "resonance", "filterDrive",
        "subLevel", "pmDepth", "combMix", "foldAmount", "ringAmount",
        "ampAttack", "ampRelease", "kargyraaAmount", "output"
    };

    // Set once per patch, if ever. Drift, spread and humanise are texture
    // rather than shape.
    static const char* trims[] {
        "driftA", "driftB", "spreadA", "spreadB", "fineA", "fineB",
        "combSpread", "combDamp", "octaveA", "octaveB", "subOctave"
    };

    for (const char* lead : leads)
        if (id == lead)
            return Emphasis::lead;

    for (const char* trim : trims)
        if (id == trim)
            return Emphasis::trim;

    return Emphasis::normal;
}

/// A group's own colour, rotated off the page accent.
///
/// Rotated in **hue only**, holding saturation and brightness, because those
/// two are what the contrast measurement in `PluginEditor.cpp` was made
/// against: the accents were placed at one lightness and their own chroma
/// limit, and a rotation that also moved lightness would walk one of the five
/// groups under 4.5:1 without anything saying so.
[[nodiscard]] inline juce::Colour tintFor (juce::Colour accent, int groupIndex,
                                           float step) noexcept
{
    if (step <= 0.0f || groupIndex <= 0)
        return accent;

    const float rotation = step * static_cast<float> (groupIndex) / 360.0f;

    return accent.withRotatedHue (rotation);
}

} // namespace tezla::sonitus::design
