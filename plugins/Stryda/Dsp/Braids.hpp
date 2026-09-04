// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Six topologies, with our own names.
//
// ---------------------------------------------------------------------------
// What these are, and what they deliberately are not
// ---------------------------------------------------------------------------
//
// The eighties workstations shipped a fixed list of numbered algorithms and you
// picked one. A full 6x6 matrix contains every one of them as a special case,
// which is strictly more capable and strictly harder to start from -- thirty-six
// zeroes is not a sound.
//
// So a braid is a **starting point that stays editable**: pressing one writes
// the matrix and the operator levels, and then every cell is still a knob. It
// is not a mode, nothing is locked, and there is no numbered list to memorise.
//
// The names are ours (CLAUDE.md section 2.1). They describe the shape of the
// connection graph, which is a fact about the graph rather than anybody's
// intellectual property, and no product's algorithm numbering is reproduced,
// referenced or implied.

#include <array>

#include "OperatorMatrix.hpp"

namespace tezla::stryda {

/// A braid's effect on the patch: which cells carry an index, and which
/// operators reach the output.
///
/// Only the matrix and the levels. Ratios, envelopes, Character and everything
/// else are left exactly as the player set them, so trying a different topology
/// on a sound you like does not throw the sound away.
struct Braid
{
    const char* name;
    const char* description;

    /// `cells[to][from]`, in cycles. Zero means no connection.
    std::array<std::array<double, OperatorMatrix::kNumOperators>,
               OperatorMatrix::kNumOperators> cells;

    /// Per-operator output level.
    std::array<double, OperatorMatrix::kNumOperators> levels;
};

namespace braids {

inline constexpr int kCount = 6;

/// **Append-only**, like every other choice list here: a preset or an
/// automation lane can store the index.
[[nodiscard]] inline const std::array<Braid, kCount>& table()
{
    static const std::array<Braid, kCount> list {{
        // 6 -> 5 -> 4 -> 3 -> 2 -> 1, one carrier.
        { "Stack",
          "One chain, six deep. The most modulation per operator and the least "
          "control over it: each stage widens a signal that is already wide, so "
          "the bandwidth runs away fast. This is where a growl comes from.",
          { { { 0, 1.2, 0, 0, 0, 0 },
              { 0, 0, 0.7, 0, 0, 0 },
              { 0, 0, 0, 0.5, 0, 0 },
              { 0, 0, 0, 0, 0.4, 0 },
              { 0, 0, 0, 0, 0, 0.3 },
              { 0, 0, 0, 0, 0, 0 } } },
          { 1.0, 0, 0, 0, 0, 0 } },

        // Two independent pairs plus a spare pair, all three carrying.
        { "Twin",
          "Three two-operator pairs side by side, each one a carrier and its own "
          "modulator. Three simple, predictable timbres summed -- the easiest "
          "topology to tune by ear, because nothing you change to one pair moves "
          "another.",
          { { { 0, 1.0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 1.0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 1.0 },
              { 0, 0, 0, 0, 0, 0 } } },
          { 1.0, 0, 0.7, 0, 0.5, 0 } },

        // 2, 3, 4, 5 and 6 all modulating 1.
        { "Fan",
          "Five modulators on one carrier. Every ratio you add puts its own set "
          "of sidebands around the same fundamental, so the spectrum fills in "
          "rather than spreading out -- the way to a dense, static tone that "
          "still sits on one pitch.",
          { { { 0, 0.8, 0.6, 0.4, 0.3, 0.2 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 } } },
          { 1.0, 0, 0, 0, 0, 0 } },

        // A three-cycle: 1 -> 2 -> 3 -> 1, with 1 carrying.
        { "Ring",
          "A closed loop of three. Operator 1 modulates 2, 2 modulates 3, and 3 "
          "comes back round to 1 -- which is computable only because a cell above "
          "the diagonal is one sample old. Unstable-sounding by design, and the "
          "index cap earns its keep here.",
          { { { 0, 0, 0.6, 0, 0, 0 },
              { 0.6, 0, 0, 0, 0, 0 },
              { 0, 0.6, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 } } },
          { 1.0, 0, 0, 0, 0, 0 } },

        // Two stacks of three, both carrying.
        { "Pairs",
          "Two chains of three, summed. Enough depth in each for a growl and "
          "enough separation between them to detune or ratio one against the "
          "other -- the reese topology before unison is even involved.",
          { { { 0, 1.0, 0, 0, 0, 0 },
              { 0, 0, 0.6, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 1.0, 0 },
              { 0, 0, 0, 0, 0, 0.6 },
              { 0, 0, 0, 0, 0, 0 } } },
          { 1.0, 0, 0, 0.8, 0, 0 } },

        // Nothing but operator 1, with its own feedback on the diagonal.
        { "Solo",
          "One operator, feeding back on itself. No modulators at all -- the "
          "feedback walks the sine towards a sawtooth on its own, and the whole "
          "instrument is one oscillator and one knob. The place to start when a "
          "patch has got away from you.",
          { { { 0.35, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 },
              { 0, 0, 0, 0, 0, 0 } } },
          { 1.0, 0, 0, 0, 0, 0 } },
    }};

    return list;
}

} // namespace braids
} // namespace tezla::stryda
