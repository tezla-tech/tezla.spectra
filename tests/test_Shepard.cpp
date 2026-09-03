// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include <tezla/dsp/Shepard.hpp>
#include <tezla/dsp/UnisonBank.hpp>

using namespace tezla::dsp;

namespace
{
constexpr int kSteps = 2048;

struct Ranks
{
    std::array<double, UnisonBank::kMaxVoices> cents {};
    std::array<double, UnisonBank::kMaxVoices> gains {};
};

Ranks at (double phase, int count)
{
    Ranks r;
    shepardRanks (phase, count, r.cents.data(), r.gains.data());
    return r;
}

/// The summed power at one phase -- what the ear hears the level of, since the
/// copies are at different frequencies and add incoherently.
double powerAt (double phase, int count)
{
    const auto r = at (phase, count);

    double sum = 0.0;

    for (int k = 0; k < count; ++k)
        sum += r.gains[static_cast<std::size_t> (k)] * r.gains[static_cast<std::size_t> (k)];

    return sum;
}
} // namespace

TEZLA_TEST (shepard_summed_power_is_flat_for_three_copies_or_more)
{
    // The claim the whole illusion rests on: sum(gain^2) = 0.375 * N for N >= 3,
    // so the glissando has no tremolo in it. The identity is that sum(cos t_k)
    // is zero for N >= 2 and sum(cos 2 t_k) is zero for N >= 3, both being sums
    // of roots of unity -- so two copies beat at the glissando rate and three
    // do not, which is a hard edge rather than a gradual improvement.
    //
    // Figures computed over these same 2048 steps before the header was
    // written; the ripple column is the arithmetic floor, not a tolerance.
    //
    //   N | sum gain^2 | ripple
    //   1 | 0 .. 1     | 1.000e+00
    //   2 | 0.5 .. 1.0 | 5.000e-01
    //   3 | 1.125      | 1.776e-15
    //   7 | 2.625      | 3.553e-15
    for (int count = 3; count <= UnisonBank::kMaxVoices; ++count)
    {
        double lowest = 1.0e300;
        double highest = -1.0e300;

        for (int i = 0; i < kSteps; ++i)
        {
            const double power = powerAt (static_cast<double> (i) / kSteps, count);

            lowest = std::min (lowest, power);
            highest = std::max (highest, power);
        }

        const double ripple = highest - lowest;

        std::printf ("    shepard N=%d  sum gain^2 %.12f  ripple %.3e\n",
                     count, lowest, ripple);

        CHECK_NEAR (lowest, shepardWindowPower (count), 1.0e-12);
        CHECK (ripple < 1.0e-14);
    }

    // And the edge, stated as a measurement rather than left implied. Two
    // copies ripple by half their own power -- that is the tremolo the tooltip
    // warns about, and it is why `shepardWindowPower` refuses to answer below
    // three rather than returning a figure that is only true on average.
    double twoLow = 1.0e300;
    double twoHigh = -1.0e300;

    for (int i = 0; i < kSteps; ++i)
    {
        const double power = powerAt (static_cast<double> (i) / kSteps, 2);

        twoLow = std::min (twoLow, power);
        twoHigh = std::max (twoHigh, power);
    }

    std::printf ("    shepard N=2   sum gain^2 %.6f .. %.6f  ripple %.3e\n",
                 twoLow, twoHigh, twoHigh - twoLow);

    CHECK_NEAR (twoHigh - twoLow, 0.5, 1.0e-12);
}

TEZLA_TEST (the_shepard_seam_is_at_exactly_zero_gain)
{
    // A copy's frequency jumps by N octaves when its place on the ramp wraps.
    // The whole design is that the window is *exactly* 0.0 there -- not small,
    // zero -- so the jump happens in silence and the oscillator's own phase
    // never has to be touched.
    for (int count = 1; count <= UnisonBank::kMaxVoices; ++count)
    {
        // Copy 0 is at u = 0 when the phase is 0.
        const auto r = at (0.0, count);

        CHECK (r.gains[0] == 0.0);

        // ...and that is exactly where its offset sits at the bottom of the
        // span, having just fallen the whole N octaves from the top.
        CHECK_NEAR (r.cents[0], -600.0 * count, 1.0e-9);
    }

    // A negative phase -- a falling glissando that has been running a while --
    // wraps the same way rather than reflecting or clamping.
    CHECK (at (-1.0, 5).gains[0] == 0.0);
    CHECK (at (-13.0, 5).gains[0] == 0.0);
    CHECK_NEAR (at (-0.5, 5).gains[0], at (0.5, 5).gains[0], 1.0e-15);
}

TEZLA_TEST (shepard_copies_are_exactly_an_octave_apart)
{
    // Which is what makes it a Shepard tone rather than a chord: the components
    // have to be octaves or the ear hears them as separate notes rather than as
    // one moving pitch.
    for (int count = 2; count <= UnisonBank::kMaxVoices; ++count)
        for (const double phase : { 0.0, 0.137, 0.5, 0.9 })
        {
            const auto r = at (phase, count);

            for (int k = 0; k + 1 < count; ++k)
            {
                double gap = r.cents[static_cast<std::size_t> (k + 1)]
                           - r.cents[static_cast<std::size_t> (k)];

                // One of the gaps is the wrap, which is the whole span back.
                if (gap < 0.0)
                    gap += 1200.0 * count;

                CHECK_NEAR (gap, 1200.0, 1.0e-9);
            }
        }
}

TEZLA_TEST (one_turn_of_the_shepard_phase_moves_every_copy_one_octave)
{
    // The rate control reads in octaves per second, and this is why: one full
    // turn of the phase advances the set by exactly one octave, so the illusion
    // repeats once per octave and the label means what it says.
    for (const int count : { 3, 5, 7 })
    {
        const auto start = at (0.31, count);
        const auto later = at (0.31 + 1.0 / count, count);

        // After 1/N of a turn every copy has taken the place of the next one
        // up, one octave higher -- which is the same set of frequencies, and
        // that is exactly why the rise never gets anywhere.
        for (int k = 0; k + 1 < count; ++k)
        {
            CHECK_NEAR (later.cents[static_cast<std::size_t> (k)],
                        start.cents[static_cast<std::size_t> (k + 1)], 1.0e-9);
            CHECK_NEAR (later.gains[static_cast<std::size_t> (k)],
                        start.gains[static_cast<std::size_t> (k + 1)], 1.0e-12);
        }
    }
}

TEZLA_TEST (a_shepard_stack_holds_its_level_through_the_bank)
{
    // Two claims, and the second was nearly missed. Sliding the window keeps
    // the level *flat*, which is the window identity above seen end to end --
    // but a flat level at the wrong place is still wrong, and the bank's
    // generalised normalisation is what puts a windowed stack where an
    // unwindowed one sits. The first draft asserted only the swing, and a
    // break-check reverting the normalisation to 1/sqrt(N) left it passing:
    // sum(gain^2) is constant, so that revert is a constant 4.26 dB and not a
    // swing at all. The absolute comparison below is the assertion with teeth.
    constexpr double kRate = 48000.0;
    constexpr int kCount = 7;

    std::array<double, UnisonBank::kMaxVoices> cents {};
    std::array<double, UnisonBank::kMaxVoices> gains {};
    const std::array<double, UnisonBank::kMaxVoices> flat { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

    auto fresh = []
    {
        UnisonBank bank;
        bank.prepare (kRate);
        bank.setShape (OscShape::sine);
        bank.setVoiceCount (kCount);
        bank.setFrequency (220.0);
        bank.reset();
        return bank;
    };

    // ---- flat: the same seven octaves, held still, with no window ----------
    shepardRanks (0.0, kCount, cents.data(), nullptr);

    auto reference = fresh();
    reference.setRankOffsets (cents.data(), flat.data(), kCount);

    constexpr int kWindow = static_cast<int> (kRate * 0.05);
    constexpr int kWindows = 40;

    double referenceSum = 0.0;

    for (int i = 0; i < kWindow * kWindows; ++i)
    {
        double left = 0.0, right = 0.0;
        reference.process (0.0, left, right);
        referenceSum += left * left;
    }

    const double flatRms = std::sqrt (referenceSum / (kWindow * kWindows));

    // ---- windowed, and sliding: one octave per second ---------------------
    auto bank = fresh();

    constexpr double kOctavesPerSecond = 1.0;

    double lowest = 1.0e300;
    double highest = -1.0e300;
    double total = 0.0;

    for (int w = 0; w < kWindows; ++w)
    {
        double sum = 0.0;

        for (int i = 0; i < kWindow; ++i)
        {
            const double seconds = static_cast<double> (w * kWindow + i) / kRate;

            shepardRanks (kOctavesPerSecond * seconds, kCount, cents.data(), gains.data());
            bank.setRankOffsets (cents.data(), gains.data(), kCount);

            double left = 0.0, right = 0.0;
            bank.process (0.0, left, right);

            sum += left * left;
        }

        total += sum;

        const double rms = std::sqrt (sum / kWindow);

        lowest = std::min (lowest, rms);
        highest = std::max (highest, rms);
    }

    const double slidingRms = std::sqrt (total / (kWindow * kWindows));
    const double swingDb = 20.0 * std::log10 (highest / lowest);
    const double againstFlatDb = 20.0 * std::log10 (slidingRms / flatRms);

    std::printf ("    shepard through the bank: rms %.4f .. %.4f, swing %.3f dB;"
                 " %+.3f dB against the same stack unwindowed (%.4f)\n",
                 lowest, highest, swingDb, againstFlatDb, flatRms);

    // Flat as it slides. What is left is the copies' own phase relationships
    // beating, not the window.
    CHECK (swingDb < 1.5);

    // ...and at the level an unwindowed stack of the same size sits at. Without
    // the generalised normalisation this reads -4.26 dB, which is the window's
    // own 1/sqrt(0.375) put back.
    CHECK (std::abs (againstFlatDb) < 1.0);
}
