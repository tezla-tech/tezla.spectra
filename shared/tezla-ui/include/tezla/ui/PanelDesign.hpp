// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The house panel design: one set of numbers, shared by every plugin.
//
// ---------------------------------------------------------------------------
// Where these came from
// ---------------------------------------------------------------------------
//
// Eight variants of the Sonitus panel were built as real editors -- not
// mockups -- selected at construction from an environment variable and
// photographed through `tezla-render editor shot:`. A drawing cannot tell you
// that six columns of knobs do not fit at 880 px, that a group heading collides
// with its own rule at three words, or that a tinted ring stops clearing 4.5:1
// two hues round the wheel. The real components at the real sizes can.
//
// The chosen one was **Instrument**: the restrained member of a family that
// also contained a louder one (Console, with knocked-out heading bars and
// labels at 85% of their group's colour) and a busier one (Hardware, with
// screws at the plate corners). What survives from all three:
//
//  - **A hue per group**, 18 degrees apart, carried by the heading, a spine
//    down the plate's left edge, the control names, the knob tracks and the
//    dropdowns. Eighteen rather than twenty-four because at twenty-four the
//    fourth group landed on a salmon close enough to a warning red to be worth
//    a second look, and five groups still span 72 degrees, which is plainly
//    five colours.
//  - **A size hierarchy**, so each group has something the eye lands on first.
//  - **Knobs in relief**, because the panel that shipped drew a knob body and
//    the plate behind it within a few points of the same lightness, and the
//    control read as a smudge. Geometry separates them, not hue.
//  - **Bigger values**, because the number under a knob is what is actually
//    read, and 11.5 pt was chosen when the cell was 172 px wide.
//  - **The switch is red**, always, whatever the group is wearing -- see
//    `LampButton.hpp` for why that is a deliberate disagreement with the rest
//    of the scheme rather than an oversight.
//
// **This is not configuration.** There is no variant enum any more and no
// environment variable: an editor whose shape depends on the environment is a
// thing to decide *with*, not a thing to ship on a control surface. The numbers
// are here, once, so that a change lands on every plugin at the same time.

#include <juce_gui_basics/juce_gui_basics.h>

namespace tezla::ui::design
{

/// How much of the panel a control deserves.
///
/// Not every knob on a page is equally important, and drawing them the same
/// size says they are. Cutoff and Level are what a player reaches for; Drift is
/// something set once and forgotten. A panel that gives them identical
/// footprints gives the eye nothing to land on.
///
/// The table that decides which is which belongs to the plugin -- these are its
/// own parameter names -- so it lives beside its editor, not here.
enum class Emphasis
{
    lead,        ///< the control the group is *about*
    normal,
    trim         ///< set once, then ignored
};

// ---------------------------------------------------------------------------
// The grid
// ---------------------------------------------------------------------------

/// Cells stop widening past this, and the grid centres instead. A four-column
/// group on a wide window used to stretch each cell to two hundred pixels of
/// which thirty-nine were the knob; capping the width is most of the answer to
/// "too much space used".
inline constexpr int kCellWidthMax = 118;

/// The narrowest a cell may be squeezed to while filling a row.
///
/// **Let a group fill its row rather than centring in a sea of metal.** The
/// column counts at the call sites were chosen for a panel whose cells were
/// 172 px wide. Narrow the cell without touching them and a six-column group of
/// eleven controls occupies 670 px of an 1800 px plate and centres itself,
/// which is worse than the spacing it replaced -- the knobs are closer together
/// and the *page* is emptier. So the layout may raise the column count until
/// the row is full, down to this width.
inline constexpr int kCellWidthMin = 86;

inline constexpr int kCellHeightMin = 86;
inline constexpr int kCellHeightMax = 112;
inline constexpr int kGroupGap = 9;

/// Multipliers on a rotary's diameter, by emphasis.
inline constexpr float kLeadScale = 1.32f;
inline constexpr float kTrimScale = 0.74f;

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

/// Degrees of hue between one group and the next.
inline constexpr float kGroupHueStep = 18.0f;

/// How much of its group's colour a control's name takes, 0 to 1.
///
/// Not the tint at full: a page of saturated labels is a page where the loudest
/// thing is the words, and the words are the part you already know. Mixed
/// toward the dim grey the labels use, so a group reads as *warm* or *cool*
/// rather than as coloured.
inline constexpr float kLabelTint = 0.40f;

/// How much of its group's colour a dropdown takes. Higher than a label's,
/// because a dropdown is a box with an edge and a fill to carry it, and because
/// a choice is the control most easily mistaken for a caption.
inline constexpr float kBoxTint = 0.62f;

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------

inline constexpr float kLabelSize = 10.0f;

/// The value under a knob, in points, and its row's height.
inline constexpr float kValueSize = 14.0f;
inline constexpr float kValueSizeLead = 16.5f;
inline constexpr int   kValueHeight = 19;

// ---------------------------------------------------------------------------
// A group's own colour, rotated off the page accent
// ---------------------------------------------------------------------------

/// Rotated in **hue only**, holding saturation and brightness, because those
/// two are what each plugin's contrast measurement was made against: the
/// accents were placed at one lightness and their own chroma limit, and a
/// rotation that also moved lightness would walk one of the groups under 4.5:1
/// without anything saying so.
[[nodiscard]] inline juce::Colour tintFor (juce::Colour accent, int groupIndex) noexcept
{
    if (groupIndex <= 0)
        return accent;

    return accent.withRotatedHue (kGroupHueStep * static_cast<float> (groupIndex) / 360.0f);
}

} // namespace tezla::ui::design
