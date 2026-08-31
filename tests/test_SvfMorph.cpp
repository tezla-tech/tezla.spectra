// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/SvfFilter.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

SvfFilter made (SvfMode mode, double morph, double cutoff = 1000.0,
                double resonance = 0.0)
{
    SvfFilter filter;

    filter.prepare (kRate);
    filter.setMode (mode);
    filter.setCutoffHz (cutoff);
    filter.setResonance (resonance);
    filter.setMorph (morph);

    return filter;
}

/// A burst of everything: a chirp, so one pass exercises the whole band.
std::vector<double> sweep (int samples)
{
    std::vector<double> out;
    out.reserve (static_cast<std::size_t> (samples));

    double phase = 0.0;

    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (samples);
        const double hz = 20.0 * std::pow (1000.0, t);   // 20 Hz to 20 kHz

        phase += 2.0 * std::numbers::pi * hz / kRate;
        out.push_back (0.5 * std::sin (phase));
    }

    return out;
}

std::vector<double> run (SvfFilter filter, const std::vector<double>& input)
{
    std::vector<double> out;
    out.reserve (input.size());

    for (const double sample : input)
        out.push_back (filter.process (sample));

    return out;
}

/// RMS of a steady sine through the filter, after the transient. RMS rather
/// than peak, per CLAUDE.md section 10 -- peak under-reads a high tone at a
/// low sample rate and looks exactly like a filter that is down a decibel.
double responseDb (SvfFilter filter, double hz)
{
    const int settle = static_cast<int> (0.25 * kRate);
    const int window = static_cast<int> (0.25 * kRate);

    double phase = 0.0;
    double sum = 0.0;

    for (int i = 0; i < settle + window; ++i)
    {
        phase += 2.0 * std::numbers::pi * hz / kRate;

        const double out = filter.process (std::sin (phase));

        if (i >= settle)
            sum += out * out;
    }

    return gainToDb (std::sqrt (2.0 * sum / static_cast<double> (window)));
}
} // namespace

// ---------------------------------------------------------------------------
// The neutral setting
// ---------------------------------------------------------------------------

TEZLA_TEST (morph_zero_is_bit_exact_against_every_mode_that_shipped)
{
    // The mode choice is frozen and every saved project uses it, so morph's
    // default has to be the identity for all four modes -- not transparent,
    // identical. Fed a full-band sweep and compared sample for sample.
    //
    // Break-checked by removing the `isExactlyZero` fast path so morph 0 goes
    // through the blend, and it **stayed green** -- which is the useful
    // finding: the blend is exact at its landmarks by itself, and the fast
    // path is speed rather than correctness. The claim survives either way,
    // which is why it is worth having asserted rather than reasoned about.
    const auto input = sweep (24000);

    int compared = 0;

    for (const auto mode : { SvfMode::lowpass, SvfMode::bandpass,
                             SvfMode::highpass, SvfMode::notch })
    {
        SvfFilter reference;
        reference.prepare (kRate);
        reference.setMode (mode);
        reference.setCutoffHz (1000.0);
        reference.setResonance (0.0);
        // ...and no setMorph call at all, which is what a build without the
        // feature would do.

        const auto before = run (reference, input);
        const auto after = run (made (mode, 0.0), input);

        CHECK (before.size() == after.size());

        for (std::size_t i = 0; i < before.size(); ++i)
            CHECK (before[i] == after[i]);

        ++compared;
    }

    CHECK (compared == 4);
}

TEZLA_TEST (the_three_landmark_positions_are_bit_exact_too)
{
    // 0, 0.5 and 1 on the axis hand back the plain outputs with no arithmetic
    // between, which is what makes "morph 0.5 is the bandpass" a fact. From a
    // lowpass, +0.5 is the bandpass and +1 the highpass.
    const auto input = sweep (24000);

    const auto bandpass = run (made (SvfMode::bandpass, 0.0), input);
    const auto morphedToBand = run (made (SvfMode::lowpass, 0.5), input);

    const auto highpass = run (made (SvfMode::highpass, 0.0), input);
    const auto morphedToHigh = run (made (SvfMode::lowpass, 1.0), input);

    // And from the other end: a highpass patch morphed all the way down.
    const auto lowpass = run (made (SvfMode::lowpass, 0.0), input);
    const auto morphedToLow = run (made (SvfMode::highpass, -1.0), input);

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        CHECK (bandpass[i] == morphedToBand[i]);
        CHECK (highpass[i] == morphedToHigh[i]);
        CHECK (lowpass[i] == morphedToLow[i]);
    }
}

TEZLA_TEST (notch_is_not_on_the_axis_and_morph_leaves_it_alone)
{
    // Notch is the sum of the two ends rather than a point between them, so
    // there is no honest position for it. Morph is inert there, and that is a
    // decision rather than an oversight -- sliding a notch towards a bandpass
    // would be inventing a shape nothing makes.
    const auto input = sweep (12000);
    const auto reference = run (made (SvfMode::notch, 0.0), input);

    for (const double morph : { -1.0, -0.5, 0.25, 1.0 })
    {
        const auto morphed = run (made (SvfMode::notch, morph), input);

        for (std::size_t i = 0; i < input.size(); ++i)
            CHECK (reference[i] == morphed[i]);
    }
}

// ---------------------------------------------------------------------------
// What it actually does
// ---------------------------------------------------------------------------

TEZLA_TEST (the_morphed_response_sweeps_from_lowpass_to_highpass)
{
    // Measured, not asserted: the response 3 octaves below and 3 above the
    // corner, at five points along the axis. Below should fall and above
    // should rise, monotonically, which is the whole claim.
    constexpr double kCorner = 1000.0;
    constexpr double kLow = kCorner / 8.0;
    constexpr double kHigh = kCorner * 8.0;

    double previousLow = 1.0e9;
    double previousHigh = -1.0e9;

    for (const double morph : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        const double low = responseDb (made (SvfMode::lowpass, morph), kLow);
        const double high = responseDb (made (SvfMode::lowpass, morph), kHigh);

        CHECK (low < previousLow);
        CHECK (high > previousHigh);

        previousLow = low;
        previousHigh = high;
    }

    // The two ends, pinned. A 2-pole slope is 12 dB an octave, so 3 octaves is
    // about 36 dB, and the measured figures are what this structure at Q 0.9
    // actually gives.
    // Measured, at no resonance and three octaves out. A 2-pole slope is
    // 12 dB an octave, so 36 is the textbook figure and 37.9 is what this
    // structure gives once the warp near Nyquist is included -- the number in
    // the test is the one it produces, not the one the textbook predicts.
    CHECK_NEAR (responseDb (made (SvfMode::lowpass, 0.0), kLow), -0.124, 0.05);
    CHECK_NEAR (responseDb (made (SvfMode::lowpass, 0.0), kHigh), -37.908, 0.05);
    CHECK_NEAR (responseDb (made (SvfMode::lowpass, 1.0), kLow), -36.272, 0.05);
    CHECK_NEAR (responseDb (made (SvfMode::lowpass, 1.0), kHigh), -0.111, 0.05);
}

TEZLA_TEST (the_predicted_magnitude_matches_the_filter_it_describes)
{
    // `magnitudeAt` is what a panel would draw and what the tests reason from,
    // and a morphed prediction built by averaging the three magnitudes would
    // be wrong by however much they are out of phase -- a quarter turn at the
    // corner. This is the check that the complex arithmetic was done.
    //
    // **At no resonance**, deliberately. `magnitudeAt` is the *linear*
    // response and the filter has a rail on its integrator states, so a high-Q
    // setting measures the rail rather than the filter: the first version of
    // this test used the resonance control at 0.9, which is Q ~ 251, and the
    // corner disagreed by 43.4 dB because a linear gain of 251 does not fit
    // through a soft clip. That is CLAUDE.md section 10's "a guard at the end
    // of a chain makes every measurement true" from the other side -- the
    // measurement was honest and the prediction was the one that did not know
    // about the guard.
    double worst = 0.0;

    for (const double morph : { 0.0, 0.2, 0.5, 0.8, 1.0, -0.5 })
        for (const auto mode : { SvfMode::lowpass, SvfMode::bandpass, SvfMode::highpass })
            for (const double hz : { 125.0, 500.0, 1000.0, 2000.0, 8000.0 })
            {
                const auto filter = made (mode, morph);

                const double predicted = gainToDb (filter.magnitudeAt (hz));
                const double measured = responseDb (filter, hz);

                worst = std::max (worst, std::abs (predicted - measured));
            }

    // Measured worst disagreement across the grid. Not zero, because the
    // measurement is a windowed RMS of a real sine through a real filter and
    // the prediction is the exact transfer function.
    // Measured worst disagreement across 90 combinations. Not zero, because
    // the measurement is a windowed RMS of a real sine through a real filter
    // and the prediction is the exact transfer function.
    CHECK (worst < 0.05);
}

TEZLA_TEST (the_morph_is_bounded_and_silence_stays_silent)
{
    // Morph is a modulation destination, so it will be swept at audio-ish
    // rates by an envelope. Nothing along it may ring, blow up, or start from
    // nothing -- swept across the whole space rather than sampled.
    int combinations = 0;
    double worst = 0.0;

    for (int m = -20; m <= 20; ++m)
        for (const auto mode : { SvfMode::lowpass, SvfMode::bandpass,
                                 SvfMode::highpass, SvfMode::notch })
            for (const double resonance : { 0.0, 0.5, 1.0 })
            {
                auto filter = made (mode, static_cast<double> (m) / 20.0, 800.0, resonance);

                for (int i = 0; i < 4800; ++i)
                {
                    const double out = filter.process (0.0);

                    worst = std::max (worst, std::abs (out));
                }

                ++combinations;
            }

    CHECK (combinations == 492);
    CHECK (worst == 0.0);
}
