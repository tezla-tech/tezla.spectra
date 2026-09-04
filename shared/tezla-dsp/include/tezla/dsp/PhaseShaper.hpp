// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Phase distortion: bend the phase ramp, and the waveform bends with it.
//
// ---------------------------------------------------------------------------
// What this is, and what it is *not* a separate engine from
// ---------------------------------------------------------------------------
//
// A digital oscillator reads a table with a linearly rising phase. Make that
// rise non-linear and the output distorts -- sharply where the phase races,
// slowly where it crawls. Casio built a whole synthesiser line on it in 1984,
// and Tom Wiltshire's analysis (Electric Druid, read first-hand) says plainly
// what it amounts to:
//
//     "Phase distortion synthesis is essentially a special case of the more
//      general phase modulation synthesis... a phase distortion function can
//      be seen as the addition of a triangle wave to the base phase
//      accumulator."
//
// So this is **not** a third synthesis technique beside FM and ModFM. It is
// phase modulation whose modulator is a piecewise-linear wave locked to the
// carrier's own cycle. That identity is why it costs a transfer function here
// rather than an operator: spending one of six operators to make its own
// modulator at ratio 1 would work and would be wasteful.
//
// What it buys that a sine modulator does not, per the same source: the
// modulator's *shape* is not a sine and can be changed, and that "extra
// complexity in the modulator makes up in some respects for the simplicity of
// the algorithm". A knee sweeps a sine towards a saw with a hard corner --
// which is a filter-sweep gesture from a single oscillator, and on a bass it
// sits somewhere neither FM nor ModFM reaches.
//
// ---------------------------------------------------------------------------
// The transfer function
// ---------------------------------------------------------------------------
//
// One knee, at a movable point. Phase in [0, 1) maps to phase in [0, 1):
//
//     y = x * (k / p)                    for x < p
//     y = k + (x - p) * ((1 - k) / (1 - p))   for x >= p
//
// with `k` fixed at 0.5 and the knee position `p` swept from 0.5. At p = 0.5
// the map is the identity and the output is untouched **bit for bit**: the two
// branches both reduce to `y = x`, and the multiplier is exactly 1.0 because
// `0.5 / 0.5` is exact in binary. That is the neutral setting CLAUDE.md
// section 7 requires, and it is exact by arithmetic rather than by tolerance.
//
// Moving p towards 0 makes the first half of the cycle race and the second
// crawl; a sine read through it grows a leading edge and turns saw-like. The
// amount control maps 0 -> p = 0.5 and 1 -> p = 0.02, kept off zero because a
// vertical segment is an instantaneous jump and no amount of oversampling
// helps a genuine discontinuity.

#include <algorithm>

namespace tezla::dsp
{

class PhaseShaper
{
public:
    /// The knee never reaches the axis: at p = 0 the first segment is vertical,
    /// which is a step in the output and a step has infinite bandwidth. 0.02
    /// leaves the steepest usable slope at 25x, which measures cleanly inside
    /// an oversampled section.
    static constexpr double kNarrowestKnee = 0.02;

    /// 0 is the identity, exactly. 1 is the sharpest bend.
    void setAmount (double amount) noexcept
    {
        amount_ = std::clamp (amount, 0.0, 1.0);
        knee_ = 0.5 - amount_ * (0.5 - kNarrowestKnee);
    }

    [[nodiscard]] double getAmount() const noexcept { return amount_; }
    [[nodiscard]] double getKnee() const noexcept { return knee_; }

    /// Maps a phase in [0, 1) to a distorted phase in [0, 1).
    ///
    /// At amount 0 this returns its argument bit for bit: `0.5 / 0.5` is
    /// exactly 1.0, and so is `0.5 / 0.5` on the other branch, so both reduce
    /// to `x * 1.0` and `0.5 + (x - 0.5) * 1.0`. Branched anyway, because an
    /// operator that is not folding should not pay for the divide.
    [[nodiscard]] double map (double phase) const noexcept
    {
        if (amount_ <= 0.0)
            return phase;

        return phase < knee_
                 ? phase * (0.5 / knee_)
                 : 0.5 + (phase - knee_) * (0.5 / (1.0 - knee_));
    }

private:
    double amount_ { 0.0 };
    double knee_ { 0.5 };
};

} // namespace tezla::dsp
