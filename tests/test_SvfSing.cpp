// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// Sing -- the filter's own limit cycle.
//
// The claim being defended is CLAUDE.md section 6's: the same patch must sound
// the same at 44.1, 48, 96 and 192 kHz. A self-oscillating filter is the worst
// case for it, because the loop's *own* pitch and level are the output, so any
// rate dependence in the mechanism is heard directly rather than as a shading.
//
// Two designs were measured and rejected before this one, and both failures
// are pinned here as numbers so the tests cannot quietly slide back to them:
//
//   1. **Bounded by the rail alone.** Growth per cycle is rate-independent but
//      the rail compresses once per *sample*, so a higher rate compresses more
//      often per cycle: amplitude 1.17 to 1.69 across the grid, and 45 cents
//      flat, because a state past the rail's knee is not the state the prewarp
//      was computed for.
//   2. **Level-dependent damping driven by `|s1|`.** The bandpass state swings
//      to zero and back within every cycle, so the damping is modulated at
//      twice the oscillation frequency -- a parametric term whose per-sample
//      effect scales with g. Pitch ran +0.75 % sharp at 2 kHz / 44.1 kHz
//      against +0.06 % at 2 kHz / 192 kHz, and +2.34 % against +0.47 % at
//      6 kHz: 32 cents between two session rates on one patch.
//
// What ships uses the quadrature amplitude, which has no ripple to modulate
// anything, and walks k to exactly zero so the equilibrium does not move with
// resonance. See the header of SvfFilter.hpp for the derivation.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/SvfFilter.hpp>

using namespace tezla::dsp;

namespace
{

struct Cycle
{
    double amplitude { 0.0 };   ///< RMS * sqrt(2), never a sampled peak
    double crest     { 0.0 };
    double frequency { 0.0 };
};

/// Runs a singing filter until it has settled, then measures the second half.
///
/// **Amplitude is RMS-derived on purpose.** CLAUDE.md section 10's worked
/// example is this exact trap: at 6 kHz on a 48 kHz grid there are eight
/// samples per cycle and the sampled peak under-reads by up to 4 %, which
/// looks precisely like an amplitude that varies with the sample rate. The RMS
/// of a sine is A/sqrt(2) however it is sampled.
Cycle sing (double rate, double cutoff, double resonance = 1.0,
            double singAmount = 1.0, double seconds = 4.0)
{
    SvfFilter filter;

    filter.prepare (rate);
    filter.setMode (SvfMode::bandpass);
    filter.setCutoffHz (cutoff);
    filter.setResonance (resonance);
    filter.setSing (singAmount);
    filter.seedIfSilent();

    const int total = static_cast<int> (rate * seconds);
    const int settle = total / 2;

    std::vector<double> rendered (static_cast<std::size_t> (total));

    for (int i = 0; i < total; ++i)
        rendered[static_cast<std::size_t> (i)] = filter.process (0.0);

    double peak = 0.0;
    double sum = 0.0;
    int counted = 0;
    double firstCrossing = -1.0;
    double lastCrossing = -1.0;
    int crossings = 0;

    for (int i = settle; i < total; ++i)
    {
        const double value = rendered[static_cast<std::size_t> (i)];

        peak = std::max (peak, std::abs (value));
        sum += value * value;
        ++counted;

        // Upward zero crossings, linearly interpolated. Over a two-second
        // window that is thousands of cycles, so the estimate is not limited
        // by the sample grid the way a peak reading is.
        const double previous = rendered[static_cast<std::size_t> (i - 1)];

        if (i > settle && previous <= 0.0 && value > 0.0)
        {
            const double where = static_cast<double> (i - 1) + (-previous / (value - previous));

            if (firstCrossing < 0.0)
                firstCrossing = where;
            else
            {
                lastCrossing = where;
                ++crossings;
            }
        }
    }

    const double rms = std::sqrt (sum / static_cast<double> (counted));
    const double frequency = crossings > 0 ? rate * crossings / (lastCrossing - firstCrossing) : 0.0;

    return { rms * std::sqrt (2.0), rms > 0.0 ? peak / rms : 0.0, frequency };
}

constexpr double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

} // namespace

// The section 6 test, and the reason the other two designs were thrown away:
// one patch, four session rates, and the sound must not move.
TEZLA_TEST (a_singing_filter_lands_on_the_same_note_at_every_sample_rate)
{
    ScopedNoDenormals guard;

    for (const double cutoff : { 110.0, 440.0, 2000.0, 6000.0 })
    {
        for (const double rate : kRates)
        {
            const auto measured = sing (rate, cutoff);

            // Measured 0.800000 in every cell, six figures. A tolerance of
            // 1e-5 is 1e-4 dB and still fails the |s1| design, which drifted
            // by 0.008 dB, and the rail design, which drifted by 3.2 dB.
            CHECK_NEAR (measured.amplitude, SvfFilter::kSingAmplitude, 1.0e-5);

            // 0.02 % is 0.35 cents. The |s1| design read +0.75 % at
            // 2 kHz / 44.1 kHz and +2.34 % at 6 kHz / 44.1 kHz.
            CHECK_NEAR (measured.frequency, cutoff, cutoff * 2.0e-4);
        }
    }
}

// The tone is a sine, not a squared-off one -- which is what says the rail is
// not what is bounding the loop.
//
// Only the rates that resolve a peak are asserted: crest is peak-derived by
// definition, so at 6 kHz on 48 kHz it reads 1.383 for a perfect sine and
// asserting sqrt(2) there would be asserting the sample grid.
TEZLA_TEST (a_singing_filter_is_a_sine_where_the_grid_can_show_it)
{
    ScopedNoDenormals guard;

    for (const double cutoff : { 110.0, 440.0, 2000.0 })
    {
        for (const double rate : kRates)
        {
            if (rate / cutoff < 64.0)
                continue;

            const auto measured = sing (rate, cutoff);

            CHECK_NEAR (measured.crest, std::sqrt (2.0), 1.0e-3);
        }
    }
}

// The equilibrium is `kSingAmplitude` whatever the resonance, which the first
// level-dependent attempt got wrong: it settled where k reached 1/Q rather
// than zero, so a resonance of 0 sang 19 dB quieter than a resonance of 1.
TEZLA_TEST (a_singing_filter_lands_on_the_same_level_at_every_resonance)
{
    ScopedNoDenormals guard;

    for (const double resonance : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        const auto measured = sing (48000.0, 440.0, resonance, 1.0);

        CHECK_NEAR (measured.amplitude, SvfFilter::kSingAmplitude, 1.0e-5);
        CHECK_NEAR (measured.frequency, 440.0, 0.1);
    }
}

// CLAUDE.md section 7: a feedback path must not be able to self-start from
// nothing. A filter with negative damping is the obvious way to break that
// rule, so it is asserted at the setting most likely to break it -- Sing at
// full travel, resonance at full, silence in -- and asserted **bit for bit**,
// because "inaudibly small" is what a runaway looks like in its first second.
TEZLA_TEST (a_singing_filter_never_starts_itself_from_silence)
{
    ScopedNoDenormals guard;

    SvfFilter filter;

    filter.prepare (48000.0);
    filter.setMode (SvfMode::bandpass);
    filter.setCutoffHz (440.0);
    filter.setResonance (1.0);
    filter.setSing (1.0);
    // deliberately no seedIfSilent()

    for (int i = 0; i < 96000; ++i)
        CHECK (filter.process (0.0) == 0.0);
}

// Sing has to cancel the damping the resonance left before it can go past it,
// so where it bites depends on the resonance. Below the crossing the loop is
// still a damped one and a seed dies in it; above, it arrives at the same
// amplitude as everywhere else.
//
// A decaying loop does not reach exactly zero -- at k = 0.053 the seed is down
// to about 1e-127 after two seconds, which prints as 0.000000 and is not the
// same claim -- so what is asserted is that it is *ruinously* below the
// singing amplitude rather than that it is silent.
TEZLA_TEST (sing_bites_later_when_the_resonance_left_more_damping_to_undo)
{
    ScopedNoDenormals guard;

    // k = 1/Q at resonance 0.25 is 0.3557, so the crossing is at
    // 0.3557 / (0.3557 + 0.25) = 0.587 of Sing's travel.
    const auto below = sing (48000.0, 440.0, 0.25, 0.5);
    const auto above = sing (48000.0, 440.0, 0.25, 0.75);

    CHECK (below.amplitude < 1.0e-30);
    CHECK_NEAR (above.amplitude, SvfFilter::kSingAmplitude, 1.0e-5);

    // And at full resonance the crossing is at 0.008 of the travel, so the
    // same Sing setting that could not start the quarter-resonance filter
    // starts this one.
    CHECK_NEAR (sing (48000.0, 440.0, 1.0, 0.5).amplitude, SvfFilter::kSingAmplitude, 1.0e-5);
}

// Sing 0 is not "almost the old filter", it is the old filter -- CLAUDE.md
// section 7's bit-exact neutral. What guarantees it is a single invariant:
// **`k_` is 1/Q and so is strictly positive unless Sing has been asked for**,
// which is why `if (k < 0.0)` is a sound test for "is this filter singing" and
// why a patch that never touches the control runs the arithmetic it always
// ran, having paid one predictable compare.
//
// So that invariant is what is asserted, across the whole resonance travel and
// across a round trip of the control.
TEZLA_TEST (sing_at_zero_leaves_the_damping_positive_so_the_loop_is_untouched)
{
    for (int step = 0; step <= 20; ++step)
    {
        const double resonance = static_cast<double> (step) / 20.0;

        SvfFilter filter;

        filter.prepare (48000.0);
        filter.setMode (SvfMode::lowpass);
        filter.setCutoffHz (1200.0);
        filter.setResonance (resonance);

        CHECK (filter.getDamping() > 0.0);

        // Full Sing sweeps the whole distance from k to -kSingCeiling, so it
        // lands on exactly -kSingCeiling whatever damping it started from --
        // which is what makes "how loud" and "how fast" separate controls.
        filter.setSing (1.0);
        CHECK (filter.getDamping() == -SvfFilter::kSingCeiling);

        filter.setSing (0.0);
        CHECK (filter.getDamping() > 0.0);   // and comes all the way back
    }
}

// The control round-trips to the same numbers, not merely to the same
// neighbourhood: 0 after a visit to 0.7 is the 0 it started at.
//
// **This cannot catch a change to the shared sample loop** and does not claim
// to -- both filters here run whatever that loop currently is, so a break in
// it perturbs the two sides equally and cancels. (That mistake was made once
// on this branch already, in the H1 neutrality check, which is why it is
// written down.) What covers the loop is the invariant above, plus the
// whole-plugin render hash taken against the previous commit at the end of
// every phase.
TEZLA_TEST (the_sing_control_round_trips_to_the_same_filter_bit_for_bit)
{
    ScopedNoDenormals guard;

    for (const double resonance : { 0.0, 0.5, 1.0 })
    {
        SvfFilter untouched;
        SvfFilter roundTripped;

        for (SvfFilter* filter : { &untouched, &roundTripped })
        {
            filter->prepare (48000.0);
            filter->setMode (SvfMode::lowpass);
            filter->setCutoffHz (1200.0);
            filter->setResonance (resonance);
        }

        roundTripped.setSing (0.7);
        roundTripped.setSing (0.0);

        CHECK (untouched.getDamping() == roundTripped.getDamping());

        double phase = 0.0;

        for (int i = 0; i < 4096; ++i)
        {
            const double input = 0.4 * std::sin (phase);
            phase += 2.0 * 3.14159265358979323846 * 220.0 / 48000.0;

            CHECK (untouched.process (input) == roundTripped.process (input));
        }
    }
}

// A silent loop stays silent however negative k is, so a singing filter needs
// something to grow from. The seed fires once and only into silence.
TEZLA_TEST (the_sing_seed_fires_only_into_a_silent_filter)
{
    ScopedNoDenormals guard;

    SvfFilter filter;

    filter.prepare (48000.0);
    filter.setMode (SvfMode::bandpass);
    filter.setCutoffHz (440.0);
    filter.setResonance (1.0);
    filter.setSing (1.0);

    CHECK (filter.seedIfSilent());        // silent, so it takes
    CHECK (! filter.seedIfSilent());      // no longer silent, so it does not

    filter.reset();

    CHECK (filter.seedIfSilent());        // silent again
}

// Above fc/Fs = 0.2048 the integrator states pass kRailKnee and the rail
// begins to shave the loop. This is the honest edge of the claim above, not a
// separate mechanism: it is bounded, small, and it is asserted rather than
// left to be discovered.
TEZLA_TEST (a_singing_filter_above_the_rail_corner_is_shaved_but_only_just)
{
    ScopedNoDenormals guard;

    const auto measured = sing (44100.0, 12000.0);   // fc/Fs = 0.272

    // Measured 0.794879 against 0.800000, which is -0.056 dB.
    CHECK (measured.amplitude < SvfFilter::kSingAmplitude);
    CHECK_NEAR (measured.amplitude, 0.794879, 1.0e-4);

    // And the pitch survives it: -0.0003 %, which is 0.005 cents.
    CHECK_NEAR (measured.frequency, 12000.0, 12000.0 * 1.0e-5);
}
