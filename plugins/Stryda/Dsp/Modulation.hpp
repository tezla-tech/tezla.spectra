// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// What can move what, and by how much.
//
// ---------------------------------------------------------------------------
// The destination list is FROZEN, and that is not a style preference
// ---------------------------------------------------------------------------
//
// A modulation slot stores its destination as an **index** into `dest::names`.
// Insert an entry anywhere but the end and every saved modulation in every
// project points one control to the left: the plugin still loads, still runs,
// and quietly modulates the wrong thing (CLAUDE.md section 8). New entries go
// on the end, always, even when the order reads badly -- the UI can sort what
// it displays, the stored index cannot be sorted.
//
// **The list holds continuous controls only**, and is built that way by
// construction. A choice or a switch reconfigures rather than adjusts:
// modulating `oversampling` means rebuilding a filter graph per chunk, and
// modulating an operator's Mode means a crossfade nobody asked for. They are
// parameters like any other and they are still excluded.

#include <array>
#include <cstddef>

#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Lfo.hpp>
#include <tezla/dsp/MultiEnvelope.hpp>

namespace tezla::stryda {

namespace dsp = tezla::dsp;

/// Where a slot's value comes from. **Append-only**, same reason as `dest::`.
namespace source {

enum Index
{
    none = 0,
    env1, env2,
    lfo1, lfo2,
    velocity,
    key,
    macro1, macro2, macro3, macro4,

    count
};

inline constexpr const char* names[] {
    "Off",
    "ADV 1", "ADV 2",
    "LFO 1", "LFO 2",
    "Velocity",
    "Key",
    "Macro 1", "Macro 2", "Macro 3", "Macro 4"
};

static_assert (std::size (names) == static_cast<std::size_t> (count),
               "source::names must have one entry per source::Index");

} // namespace source

/// What a slot can move. **Append-only.** The `static_assert` below is what
/// stops the list and the parameter ids drifting apart.
namespace dest {

enum Index
{
    off = 0,

    // Per operator, which is where most of the sound lives. Six of each, in
    // operator order, so the arithmetic that maps an index to an operator is
    // a division rather than a table.
    op1Ratio, op2Ratio, op3Ratio, op4Ratio, op5Ratio, op6Ratio,
    op1Character, op2Character, op3Character, op4Character, op5Character, op6Character,
    op1Level, op2Level, op3Level, op4Level, op5Level, op6Level,
    op1Feedback, op2Feedback, op3Feedback, op4Feedback, op5Feedback, op6Feedback,
    op1Fold, op2Fold, op3Fold, op4Fold, op5Fold, op6Fold,

    // The matrix, as one control: every live cell scaled together. Modulating
    // thirty cells individually would need thirty destinations and nobody
    // would use them; modulating the braid's depth is the gesture people
    // actually want.
    matrixDepth,

    // The voice's own stages.
    filterCutoff, filterResonance, filterMorph, filterDrive, filterSing,
    subLevel,

    count
};

inline constexpr const char* names[] {
    "Off",
    "Op 1 ratio", "Op 2 ratio", "Op 3 ratio", "Op 4 ratio", "Op 5 ratio", "Op 6 ratio",
    "Op 1 character", "Op 2 character", "Op 3 character", "Op 4 character",
    "Op 5 character", "Op 6 character",
    "Op 1 level", "Op 2 level", "Op 3 level", "Op 4 level", "Op 5 level", "Op 6 level",
    "Op 1 feedback", "Op 2 feedback", "Op 3 feedback", "Op 4 feedback",
    "Op 5 feedback", "Op 6 feedback",
    "Op 1 fold", "Op 2 fold", "Op 3 fold", "Op 4 fold", "Op 5 fold", "Op 6 fold",
    "Matrix depth",
    "Filter cutoff", "Filter resonance", "Filter morph", "Filter drive", "Filter sing",
    "Sub level"
};

static_assert (std::size (names) == static_cast<std::size_t> (count),
               "dest::names must have one entry per dest::Index");

/// The per-operator groups are contiguous and in operator order, which several
/// places rely on to turn an index into an operator number by subtraction.
static_assert (op6Ratio - op1Ratio == 5 && op6Character - op1Character == 5
                 && op6Level - op1Level == 5 && op6Feedback - op1Feedback == 5
                 && op6Fold - op1Fold == 5,
               "the per-operator destination groups must stay contiguous and in order");

} // namespace dest

/// One assignment.
struct ModulationSlot
{
    int source { source::none };
    int destination { dest::off };

    /// Bipolar, in the destination's own units. Zero is inert, and inert here
    /// means the destination is not even read.
    double amount { 0.0 };
};

/// A shared ADV envelope's shape. Two of these exist rather than one per
/// operator: six would be 288 parameters for a feature used on two operators
/// at a time, and each operator *chooses* its source instead.
struct AdvSettings
{
    std::array<dsp::MultiEnvelope::Point, dsp::MultiEnvelope::kMaxPoints> points {};
    int pointCount { 4 };

    /// Zero-based here, 1-based on the panel. Without one an ADV envelope is a
    /// one-shot, and a held bass note is what this instrument is for.
    int sustain { 2 };
    int loopStart { 0 };
    bool loop { false };
};

struct LfoSettings
{
    int wave { 0 };
    double rateHz { 2.0 };
    bool synced { false };
    int division { 6 };
    double smooth { 0.0 };
    double phaseOffset { 0.0 };
    bool retrigger { true };
};

inline constexpr int kNumSlots = 8;
inline constexpr int kNumMacros = 4;

/// Everything the modulation layer is told, once per control chunk.
struct ModulationSettings
{
    std::array<AdvSettings, 2> envelopes {};
    std::array<LfoSettings, 2> lfos {};
    std::array<double, kNumMacros> macros {};
    std::array<ModulationSlot, kNumSlots> slots {};

    /// True when at least one slot has a source, a destination and an amount.
    /// The whole layer is skipped when it is false, so a patch that uses none
    /// of it is bit-identical to a build without it.
    [[nodiscard]] bool anyActive() const noexcept
    {
        for (const auto& slot : slots)
            if (slot.source != source::none && slot.destination != dest::off
                && ! dsp::isExactlyZero (slot.amount))
                return true;

        return false;
    }
};

} // namespace tezla::stryda
