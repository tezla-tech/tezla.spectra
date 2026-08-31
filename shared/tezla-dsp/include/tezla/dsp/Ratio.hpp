// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The nearest simple whole-number ratio to a frequency ratio, and how far off
// it is in cents.
//
// ---------------------------------------------------------------------------
// What this is for
// ---------------------------------------------------------------------------
//
// Two oscillators tuned in octaves, semitones and cents is right for detuning
// and wrong for FM. What an FM patch cares about is the **ratio** between the
// carrier and the modulator, and whether it is simple: 2:1 and 3:2 are
// harmonic and fuse into one instrument, 2.03:1 beats, and 4.76:1 is a bell.
// Nothing on a pitch control says which of those you have set.
//
// So this answers "what ratio am I actually running", in lowest terms, with
// the error stated rather than hidden. It is a readout: no new parameter, and
// nothing here changes a sound.
//
// ---------------------------------------------------------------------------
// Why cents rather than a ratio tolerance
// ---------------------------------------------------------------------------
//
// Nearness between pitches is logarithmic, the same argument `snapSeconds`
// makes about note lengths. A fixed ratio tolerance would be four times as
// forgiving at 4:1 as at 1:1, and the ear is not.
//
// ---------------------------------------------------------------------------
// This is NOT Tuning.hpp's `nearestFraction`, and must not be merged with it
// ---------------------------------------------------------------------------
//
// They look like the same function and answer opposite questions.
// `nearestFraction` recovers p/q only when the double **is** that fraction to
// within a few ulps, and deliberately refuses a tempered interval -- because a
// tuning table printing "442/295" for an equal-tempered degree would be a lie.
// This one asks what simple ratio an interval is *near*, and by how much, which
// is the only useful question about an FM pair and one `nearestFraction`
// returns "not found" to. Both are correct; consolidating them would break one.
//
// ---------------------------------------------------------------------------
// Why a plain search rather than continued fractions
// ---------------------------------------------------------------------------
//
// The classic answer is a Stern-Brocot walk, and it is the right one when the
// terms can be large. Here they cannot: past about 9:8 a ratio has stopped
// being audibly simple, so the whole search space is a hundred-odd pairs and a
// loop over it is exact, obviously correct, and impossible to get subtly
// wrong. Preferring the smaller denominator first falls out of the loop order
// instead of needing an argument.

#include <cmath>

namespace tezla::dsp
{

struct RatioMatch
{
    /// 0 when no p:q within `maxTerm` describes the ratio at all -- which is
    /// not an error but the common case at the ends of the pitch controls:
    /// six octaves apart is 64:1, and no small pair says that. A panel prints
    /// the decimal there, which is the honest reading anyway.
    int numerator { 0 };
    int denominator { 0 };

    /// How far the real ratio sits above (+) or below (-) `numerator` over
    /// `denominator`, in cents.
    double centsError { 0.0 };

    /// Whether that error is inside the tolerance asked for. False does not
    /// mean the numbers are wrong -- they are still the nearest simple ratio
    /// there is -- it means calling the interval by that name would be a lie.
    bool simple { false };
};

/// The simplest p:q with both terms at most `maxTerm` that is within
/// `toleranceCents` of `ratio`; failing that, the nearest such p:q at all.
///
/// "Simplest" is the smallest denominator, then the smallest numerator, which
/// is the order a musician would read them in. A ratio at or below zero, a
/// nonsensical `maxTerm`, or a ratio no pair in range can express gives back
/// numerator 0 rather than an error -- this is a readout, and a panel has to
/// draw something.
///
/// Sixteen is the default ceiling because it covers four octaves, which is
/// where FM ratios stop being thought of as ratios; past it the decimal is
/// what a player reads anyway.
[[nodiscard]] inline RatioMatch nearestRatio (double ratio, int maxTerm = 16,
                                              double toleranceCents = 6.0) noexcept
{
    RatioMatch result;

    if (! (ratio > 0.0) || maxTerm < 1)
        return result;

    const auto centsBetween = [] (double a, double b)
    {
        return 1200.0 * std::log2 (a / b);
    };

    bool found = false;
    double bestError = 0.0;

    for (int denominator = 1; denominator <= maxTerm; ++denominator)
    {
        // The one numerator worth trying for this denominator: any other is
        // further away by construction.
        const int numerator = static_cast<int> (std::lround (ratio * denominator));

        if (numerator < 1 || numerator > maxTerm)
            continue;

        // **Lowest terms by construction, not by division.** A pair that is not
        // coprime is reached at a smaller denominator first -- 6:4 is 3:2, and
        // q = 2 is tried before q = 4 with p = 3 in range -- so the loop can
        // only ever report a reduced pair, and a `gcd` here would be a line
        // that never fires. That was checked by removing it: the whole-grid
        // test stayed green, which is what "dead" means. The invariant is
        // stated instead, and the test asserts it rather than the division.
        const int p = numerator;
        const int q = denominator;

        const double error = centsBetween (ratio, static_cast<double> (p)
                                                    / static_cast<double> (q));

        if (! found || std::abs (error) < std::abs (bestError))
        {
            found = true;
            bestError = error;
            result.numerator = p;
            result.denominator = q;
            result.centsError = error;
        }

        // Within tolerance and the smallest denominator that manages it: stop,
        // because anything later is a more complicated way of saying the same
        // interval. 4:3 beats 8:6 even when both are exact.
        if (std::abs (error) <= toleranceCents)
        {
            result.simple = true;
            return result;
        }
    }

    return result;
}

/// The frequency ratio a pitch offset in octaves, semitones and cents makes.
/// The same arithmetic the voice runs, kept here so a readout and a note
/// cannot disagree about what the controls mean.
[[nodiscard]] inline double ratioFromOffset (double octaves, double semitones,
                                             double cents) noexcept
{
    return std::pow (2.0, octaves + semitones / 12.0 + cents / 1200.0);
}

} // namespace tezla::dsp
