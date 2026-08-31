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

/// What an on/off control looks like.
enum class Switch
{
    pill,        ///< a travelling dot -- what shipped, and closest to a tick box
    lamp,        ///< a flat plate that lights
    bevel        ///< a moulded cap in a recessed bezel: hardware
};

/// Where a group's heading lives.
enum class Heading
{
    rule,        ///< coloured text with a rule running out to the right
    bar          ///< a filled block of the group's colour, name knocked out
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

    /// Which of the three an on/off control actually is.
    Switch switchStyle { Switch::pill };

    /// **A switch that is always the same red, whatever the group is wearing.**
    ///
    /// The chromatic scheme tints everything by group, and for a *switch* that
    /// is arguably wrong: a power switch is red on every piece of equipment in
    /// a rack precisely so that it does not have to be identified before it can
    /// be read. Offered as a variant rather than assumed, because it is a
    /// legitimate disagreement with the rest of the scheme.
    bool industrialRedSwitch { false };

    /// How much of its group's colour a control's name takes, 0 to 1.
    ///
    /// Not the tint at full: a page of saturated labels is a page where the
    /// loudest thing is the words, and the words are the part you already know.
    /// Mixed toward the dim grey the labels use today, so a group reads as
    /// *warm* or *cool* rather than as coloured.
    float labelTint { 0.0f };

    /// The value under a knob, in points, and its row's height.
    float valueSize { 11.5f };
    float valueSizeLead { 11.5f };
    int valueHeight { 14 };

    /// Bold values. Off by default, and that default is doing real work: the
    /// baseline exists to be the *before* picture, and a weight change applied
    /// to every variant made it drift -- caught by diffing variant 0 against
    /// the panel photographed before any of this started, which is what that
    /// check is for.
    bool valueBold { false };

    Heading heading { Heading::rule };

    /// A machined skirt of tick marks around each knob. Costs one path and it
    /// is most of what makes a control look like a component rather than a
    /// circle.
    bool knobSkirt { false };

    /// The *unfilled* part of a knob's travel takes the group's colour too, at
    /// a low alpha -- so a group hums one hue even with every control at zero.
    bool tintTrack { false };

    /// Screws at the plate corners. Pure decoration, and the honest question a
    /// variant exists to answer is whether it reads as built or as kitsch.
    bool plateScrews { false };

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

        // 1 -- COMPACT. Metrics only: a group fills its row, the cell narrows
        // and grows taller, so the knobs come out bigger AND closer together.
        Variant {
            .name = "Compact", .note = "one full row per group, bigger knobs, closer together",
            .cellWidthMax = 118, .cellHeightMin = 78, .cellHeightMax = 104, .groupGap = 8,
            .lampToggles = true, .switchStyle = Switch::lamp,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 86
        },

        // 2 -- TIERED. Compact plus a size hierarchy, so each group has
        // something the eye lands on first.
        Variant {
            .name = "Tiered", .note = "compact, plus lead and trim sizes",
            .cellWidthMax = 118, .cellHeightMin = 78, .cellHeightMax = 104, .groupGap = 8,
            .leadScale = 1.30f, .trimScale = 0.72f,
            .lampToggles = true, .switchStyle = Switch::lamp,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 86
        },

        // 3 -- CHROMATIC. **The chosen base.** Tiered plus a hue per group at
        // 24 degrees a step, and a spine down each plate.
        Variant {
            .name = "Chromatic", .note = "tiered, plus a hue and a spine per group",
            .cellWidthMax = 118, .cellHeightMin = 78, .cellHeightMax = 104, .groupGap = 8,
            .leadScale = 1.30f, .trimScale = 0.72f,
            .grouping = Grouping::tinted, .groupHueStep = 24.0f, .groupSpine = true,
            .lampToggles = true, .switchStyle = Switch::lamp,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 86
        },

        // 4 -- MIXER. Bars instead of knobs, value inside the bar.
        Variant {
            .name = "Mixer", .note = "bars with inline values, twice the density",
            .cellWidthMax = 190, .cellHeightMin = 40, .cellHeightMax = 50, .groupGap = 7,
            .control = Control::bar,
            .grouping = Grouping::tinted, .groupHueStep = 24.0f, .groupSpine = true,
            .lampToggles = true, .switchStyle = Switch::lamp, .labelSize = 9.5f,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 150
        },

        // -------------------------------------------------------------------
        // The revisions, all on 3.
        //
        // Hue step down from 24 to 18 across all three: at 24 the fourth group
        // landed on a salmon close enough to a warning red to be worth a second
        // look, and five groups still span 72 degrees, which is plainly five
        // colours.
        // -------------------------------------------------------------------

        // 5 -- HARDWARE. The literal reading of "fat industrial switch": a
        // moulded cap in a recessed bezel, and it is **red on every group**,
        // because a power switch is red on every box in a rack precisely so
        // that it can be read without being identified first. Knobs get a
        // machined skirt and the plates get screws.
        Variant {
            .name = "Hardware", .note = "moulded switches in bezels, machined skirts, screws",
            .cellWidthMax = 118, .cellHeightMin = 84, .cellHeightMax = 110, .groupGap = 8,
            .leadScale = 1.30f, .trimScale = 0.72f,
            .grouping = Grouping::tinted, .groupHueStep = 18.0f, .groupSpine = true,
            .lampToggles = true, .switchStyle = Switch::bevel, .industrialRedSwitch = true,
            .labelTint = 0.55f, .valueSize = 13.0f, .valueSizeLead = 15.0f, .valueHeight = 17, .valueBold = true,
            .knobSkirt = true, .plateScrews = true,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 86
        },

        // 6 -- CONSOLE. Group identity carried as hard as it can be: the
        // heading becomes a filled block of the group's colour with its name
        // knocked out of it, the labels take most of the tint, and the knob's
        // unfilled travel is tinted too -- so a group hums one hue even with
        // every control at zero. Navigation first.
        Variant {
            .name = "Console", .note = "filled heading bars, strongly tinted labels, tinted tracks",
            .cellWidthMax = 118, .cellHeightMin = 84, .cellHeightMax = 110, .groupGap = 8,
            .leadScale = 1.30f, .trimScale = 0.72f,
            .grouping = Grouping::tinted, .groupHueStep = 18.0f, .groupSpine = true,
            .lampToggles = true, .switchStyle = Switch::bevel,
            .labelTint = 0.85f, .valueSize = 13.5f, .valueSizeLead = 16.0f, .valueHeight = 18, .valueBold = true,
            .heading = Heading::bar, .tintTrack = true,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 86
        },

        // 7 -- INSTRUMENT. The restrained one. Everything the other two do, at
        // the volume a thing you look at for six hours wants: labels warmed
        // rather than coloured, the biggest values of the three because the
        // number is what you actually read, and a skirt but no screws.
        Variant {
            .name = "Instrument", .note = "restrained tint, the largest values, skirts without screws",
            .cellWidthMax = 118, .cellHeightMin = 86, .cellHeightMax = 112, .groupGap = 9,
            .leadScale = 1.32f, .trimScale = 0.74f,
            .grouping = Grouping::tinted, .groupHueStep = 18.0f, .groupSpine = true,
            .lampToggles = true, .switchStyle = Switch::bevel,
            .labelTint = 0.40f, .valueSize = 14.0f, .valueSizeLead = 16.5f, .valueHeight = 19, .valueBold = true,
            .knobSkirt = true, .tintTrack = true,
            .knobRelief = true, .fillRow = true, .cellWidthMin = 86
        }
    };

    constexpr int count = static_cast<int> (std::size (kVariants));

    return kVariants[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

[[nodiscard]] inline int variantCount() noexcept { return 8; }

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
