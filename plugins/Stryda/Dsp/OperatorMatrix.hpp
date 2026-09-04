// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Six operators, and who modulates whom.
//
// ---------------------------------------------------------------------------
// The one rule that makes an arbitrary matrix computable
// ---------------------------------------------------------------------------
//
// Operators are evaluated **5 -> 0**, in that fixed order, every sample. So
// when operator j is computed:
//
//  - a modulator with a **higher** number has already run this sample, and j
//    reads its *current* output. Instantaneous.
//  - a modulator with a **lower** number has not run yet, and j reads its
//    output from the *previous* sample. One sample late.
//
// That single-sample delay is what turns an algebraic loop into a computable
// one, and it is a design statement rather than a limitation: it is why
// **op 4 -> op 2 and op 2 -> op 4 do not sound alike at the same index**. The
// tooltip says so, because a user who does not know this will hear the
// difference and conclude the matrix is broken.
//
// The same trick is already in Sonitus (`SonitusVoice.hpp`), where the reverse
// PM path is one sample old for exactly this reason.
//
// ---------------------------------------------------------------------------
// The envelope belongs to the modulator, not to the connection
// ---------------------------------------------------------------------------
//
// An operator's envelope scales its **output**, which means it scales the
// modulation depth of everything it feeds as well as its own contribution to
// the mix. That is not a convenience, it is what FM is: the modulator's
// envelope *is* the timbre envelope, and a patch whose modulator decays faster
// than its carrier is a struck sound. A per-connection envelope would let you
// build the same thing, at six times the parameters, and lose the property
// that makes six operators enough.
//
// The ModFM normalisation follows the envelope for the same reason: the
// exponential's peak is the sum of the index magnitudes *as they actually
// arrive*, so a decayed modulator must not leave the carrier scaled by an
// exponential that no longer reaches 1.

#include <algorithm>
#include <array>
#include <cmath>

#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/FmOperator.hpp>

namespace tezla::stryda
{

class OperatorMatrix
{
public:
    static constexpr int kNumOperators = 6;

    void prepare (double sampleRate) noexcept
    {
        for (auto& op : operators_)
            op.prepare (sampleRate);

        reset();
    }

    void reset() noexcept
    {
        for (auto& op : operators_)
            op.reset();

        for (int op = 0; op < kNumOperators; ++op)
        {
            pans_[index (op)] = 2.0;   // out of range, so the next setPan takes
            setPan (op, 0.0);
        }

        outputs_.fill (0.0);
        quadratures_.fill (0.0);
        gains_.fill (0.0);
    }

    // ---- the operators themselves ------------------------------------------

    void setFrequency (int op, double hz) noexcept
    {
        if (inRange (op))
            operators_[index (op)].setFrequency (hz);
    }

    void setCharacter (int op, double character) noexcept
    {
        if (inRange (op))
        {
            characters_[index (op)] = character;
            operators_[index (op)].setCharacter (character);
            operators_[index (op)].setTilt (1.0 - character);
        }
    }

    /// Overrides the `s = 1 - r` pairing, which is what makes the spectrum
    /// asymmetric rather than simply darker.
    void setTilt (int op, double tilt) noexcept
    {
        if (inRange (op))
            operators_[index (op)].setTilt (tilt);
    }

    void setFold (int op, double amount) noexcept
    {
        if (inRange (op))
            operators_[index (op)].setFold (amount);
    }

    void setMode (int op, dsp::FmOperator::Mode mode) noexcept
    {
        if (inRange (op))
            operators_[index (op)].setMode (mode);
    }

    void setFormant (int op, double hz, double depth) noexcept
    {
        if (inRange (op))
        {
            operators_[index (op)].setFormantHz (hz);
            operators_[index (op)].setFormantDepth (depth);
        }
    }

    void setFeedback (int op, double cycles) noexcept
    {
        if (inRange (op))
            operators_[index (op)].setFeedback (cycles * indexScale_);
    }

    void setStartPhase (int op, double cycles) noexcept
    {
        if (inRange (op))
            operators_[index (op)].setPhase (cycles);
    }

    // ---- the connections ---------------------------------------------------

    /// `from` modulates `to`, by `cycles` of peak phase deviation.
    void setIndex (int to, int from, double cycles) noexcept
    {
        if (inRange (to) && inRange (from))
            cells_[index (to)][index (from)] = cycles;
    }

    [[nodiscard]] double getIndex (int to, int from) const noexcept
    {
        return (inRange (to) && inRange (from)) ? cells_[index (to)][index (from)] : 0.0;
    }

    /// The shared noise source into an operator's phase. The grit no amount of
    /// sine-on-sine will give you, and the one modulator with no pitch.
    void setNoiseIndex (int op, double cycles) noexcept
    {
        if (inRange (op))
            noiseCells_[index (op)] = cycles;
    }

    void setOutputLevel (int op, double level) noexcept
    {
        if (inRange (op))
            levels_[index (op)] = level;
    }

    /// Constant power, so moving an operator across the field does not move
    /// the patch's loudness with it. The two transcendentals are computed here,
    /// not per sample: a pan is set once per control chunk and read 32 times.
    void setPan (int op, double pan) noexcept
    {
        if (! inRange (op))
            return;

        const double clamped = std::clamp (pan, -1.0, 1.0);
        if (! (clamped < pans_[index (op)]) && ! (clamped > pans_[index (op)]))
            return;   // guarded no-op; -Wfloat-equal forbids the direct compare

        pans_[index (op)] = clamped;

        const double angle = 0.25 * std::numbers::pi * (clamped + 1.0);

        // The sqrt(2) is folded in here too: constant-power panning multiplies
        // a centred operator by cos(pi/4), so without it "one carrier, level 1"
        // would arrive at 0.707 rather than full scale.
        leftGains_[index (op)] = std::cos (angle) * std::numbers::sqrt2;
        rightGains_[index (op)] = std::sin (angle) * std::numbers::sqrt2;
    }

    /// The index cap's scale, in [0, 1]. **Exactly 1.0 leaves every index
    /// untouched**, which `FmBandwidth::indexScaleFor` guarantees by returning
    /// a literal 1.0 rather than a bisected approximation to it.
    void setIndexScale (double scale) noexcept
    {
        indexScale_ = std::clamp (scale, 0.0, 1.0);
    }

    [[nodiscard]] double getIndexScale() const noexcept { return indexScale_; }

    /// Work out which operators anyone actually reads the quadrature of, and
    /// tell them. Call after the cells and characters are set for the chunk;
    /// it is a 36-entry scan, once per chunk, against a `std::cos` per operator
    /// per sample.
    void refreshQuadratureNeeds() noexcept
    {
        for (int from = 0; from < kNumOperators; ++from)
        {
            bool needed = false;

            for (int to = 0; to < kNumOperators && ! needed; ++to)
                if (to != from
                    && ! dsp::isExactlyZero (cells_[index (to)][index (from)])
                    && characters_[index (to)] > 0.0)
                    needed = true;

            operators_[index (from)].setQuadratureNeeded (needed);
        }
    }

    // ---- one sample --------------------------------------------------------

    /// `gains` is one envelope value per operator; `noise` the shared source.
    void process (const double* gains, double noise, double& left, double& right) noexcept
    {
        double mixLeft = 0.0;
        double mixRight = 0.0;

        for (int j = kNumOperators - 1; j >= 0; --j)
        {
            const auto to = index (j);

            double pm = 0.0;
            double am = 0.0;
            double norm = 0.0;

            for (int i = 0; i < kNumOperators; ++i)
            {
                if (i == j)
                    continue;

                const auto from = index (i);
                const double cell = cells_[to][from];

                if (dsp::isExactlyZero (cell))
                    continue;

                const double scaled = cell * indexScale_;

                // No branch on the direction, and that is the whole trick:
                // `outputs_` holds this sample's value for an operator that has
                // already run and **last sample's** for one that has not,
                // because nothing clears it in between. So reading it
                // unconditionally *is* the ordering rule.
                //
                // A first version kept a separate `previousOutputs_` copy and
                // chose between the two. It produced bit-identical audio --
                // the break-check that removed the choice stayed green, which
                // is how the redundancy was found -- while copying three
                // six-element arrays every sample for nothing.
                pm += scaled * outputs_[from];
                am += scaled * quadratures_[from];
                norm += scaled * gains_[from];
            }

            if (! dsp::isExactlyZero (noiseCells_[to]))
                pm += noiseCells_[to] * indexScale_ * noise;

            const double gain = gains[to];
            const double raw = operators_[to].advance (pm, am, norm);

            outputs_[to] = raw * gain;
            quadratures_[to] = operators_[to].getQuadrature() * gain;
            gains_[to] = gain;

            const double level = levels_[to];
            if (! dsp::isExactlyZero (level))
            {
                mixLeft += outputs_[to] * level * leftGains_[to];
                mixRight += outputs_[to] * level * rightGains_[to];
            }
        }

        left = mixLeft;
        right = mixRight;
    }

    [[nodiscard]] const dsp::FmOperator& getOperator (int op) const noexcept
    {
        return operators_[index (std::clamp (op, 0, kNumOperators - 1))];
    }

private:
    [[nodiscard]] static constexpr bool inRange (int op) noexcept
    {
        return op >= 0 && op < kNumOperators;
    }

    [[nodiscard]] static constexpr std::size_t index (int op) noexcept
    {
        return static_cast<std::size_t> (op);
    }

    std::array<dsp::FmOperator, kNumOperators> operators_ {};
    std::array<std::array<double, kNumOperators>, kNumOperators> cells_ {};
    std::array<double, kNumOperators> noiseCells_ {};
    std::array<double, kNumOperators> characters_ {};
    std::array<double, kNumOperators> levels_ {};
    std::array<double, kNumOperators> pans_ {};
    std::array<double, kNumOperators> leftGains_ {};
    std::array<double, kNumOperators> rightGains_ {};

    std::array<double, kNumOperators> outputs_ {};
    std::array<double, kNumOperators> quadratures_ {};
    std::array<double, kNumOperators> gains_ {};

    double indexScale_ { 1.0 };
};

} // namespace tezla::stryda
