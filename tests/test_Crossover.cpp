// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Crossover.hpp>
#include <tezla/dsp/Decibels.hpp>

using namespace tezla::dsp;

namespace {

/// Amplitude of a steady sine after a process, measured by RMS -- never by peak
/// picking, which under-reads badly near Nyquist.
template <typename Process>
double amplitudeAt (double frequencyHz, double sampleRate, Process&& process)
{
    const double omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const int settle = 40000;
    const int measure = 40000;

    for (int i = 0; i < settle; ++i)
        (void) process (std::sin (omega * static_cast<double> (i)));

    double sumOfSquares = 0.0;
    for (int i = settle; i < settle + measure; ++i)
    {
        const double y = process (std::sin (omega * static_cast<double> (i)));
        sumOfSquares += y * y;
    }

    return std::sqrt (2.0 * sumOfSquares / static_cast<double> (measure));
}

} // namespace

TEZLA_TEST (two_way_crossover_sums_flat)
{
    constexpr double fs = 48000.0;

    LinkwitzRiley4<double> crossover;
    crossover.prepare (fs);
    crossover.setCrossover (1000.0);

    for (const double frequency : { 30.0, 100.0, 500.0, 1000.0, 2000.0, 8000.0, 16000.0 })
    {
        crossover.reset();

        const double summed = amplitudeAt (frequency, fs, [&crossover] (double x)
        {
            double low {}, high {};
            crossover.process (x, low, high);
            return low + high;
        });

        // Flat sum is the entire point of Linkwitz-Riley. 0.1 dB is the bar.
        CHECK_NEAR (gainToDb (summed), 0.0, 0.1);
    }
}

TEZLA_TEST (two_way_crossover_is_minus_six_db_at_the_corner)
{
    constexpr double fs = 48000.0;

    LinkwitzRiley4<double> crossover;
    crossover.prepare (fs);
    crossover.setCrossover (1000.0);

    crossover.reset();
    const double low = amplitudeAt (1000.0, fs, [&crossover] (double x)
    {
        double l {}, h {};
        crossover.process (x, l, h);
        return l;
    });

    crossover.reset();
    const double high = amplitudeAt (1000.0, fs, [&crossover] (double x)
    {
        double l {}, h {};
        crossover.process (x, l, h);
        return h;
    });

    // -6 dB each at the crossover, which is what lets them sum to unity.
    CHECK_NEAR (gainToDb (low),  -6.02, 0.15);
    CHECK_NEAR (gainToDb (high), -6.02, 0.15);
}

TEZLA_TEST (three_band_splitter_sums_flat)
{
    // The one that catches a missing allpass on the low band: without it the
    // sum is still close, but wrong by a decibel or two around the lower
    // crossover, which reads as "multiband mode sounds a bit odd" rather than
    // as an obvious bug.
    constexpr double fs = 48000.0;

    ThreeBandSplitter<double> splitter;
    splitter.prepare (fs);
    splitter.setCrossovers (120.0, 2500.0);

    for (const double frequency : { 25.0, 60.0, 120.0, 300.0, 900.0, 2500.0, 6000.0, 15000.0 })
    {
        splitter.reset();

        const double summed = amplitudeAt (frequency, fs, [&splitter] (double x)
        {
            double low {}, mid {}, high {};
            splitter.process (x, low, mid, high);
            return low + mid + high;
        });

        CHECK_NEAR (gainToDb (summed), 0.0, 0.15);
    }
}

TEZLA_TEST (three_band_splitter_sums_flat_at_every_session_rate)
{
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        ThreeBandSplitter<double> splitter;
        splitter.prepare (fs);
        splitter.setCrossovers (120.0, 2500.0);

        for (const double frequency : { 50.0, 400.0, 3000.0, 12000.0 })
        {
            splitter.reset();

            const double summed = amplitudeAt (frequency, fs, [&splitter] (double x)
            {
                double low {}, mid {}, high {};
                splitter.process (x, low, mid, high);
                return low + mid + high;
            });

            CHECK_NEAR (gainToDb (summed), 0.0, 0.2);
        }
    }
}

TEZLA_TEST (three_band_splitter_actually_separates)
{
    // A flat sum is necessary but not sufficient -- a splitter that put the
    // whole signal in one band and nothing in the others would also sum flat.
    constexpr double fs = 48000.0;

    ThreeBandSplitter<double> splitter;
    splitter.prepare (fs);
    splitter.setCrossovers (120.0, 2500.0);

    const auto bandLevels = [&splitter, fs] (double frequency)
    {
        struct Levels { double low, mid, high; };
        Levels levels {};

        splitter.reset();
        levels.low = amplitudeAt (frequency, fs, [&splitter] (double x)
        { double l{}, m{}, h{}; splitter.process (x, l, m, h); return l; });

        splitter.reset();
        levels.mid = amplitudeAt (frequency, fs, [&splitter] (double x)
        { double l{}, m{}, h{}; splitter.process (x, l, m, h); return m; });

        splitter.reset();
        levels.high = amplitudeAt (frequency, fs, [&splitter] (double x)
        { double l{}, m{}, h{}; splitter.process (x, l, m, h); return h; });

        return levels;
    };

    const auto deep   = bandLevels (40.0);
    const auto middle = bandLevels (700.0);
    const auto top    = bandLevels (10000.0);

    CHECK (gainToDb (deep.low)    > -1.0);
    CHECK (gainToDb (deep.mid)    < -25.0);
    CHECK (gainToDb (deep.high)   < -40.0);

    CHECK (gainToDb (middle.mid)  > -1.0);
    CHECK (gainToDb (middle.low)  < -25.0);
    CHECK (gainToDb (middle.high) < -25.0);

    CHECK (gainToDb (top.high)    > -1.0);
    CHECK (gainToDb (top.mid)     < -25.0);
    CHECK (gainToDb (top.low)     < -40.0);
}

TEZLA_TEST (crossover_survives_being_automated_past_nyquist)
{
    constexpr double fs = 48000.0;

    ThreeBandSplitter<double> splitter;
    splitter.prepare (fs);
    splitter.setCrossovers (80000.0, 200000.0);   // both far past Nyquist

    double low {}, mid {}, high {};
    for (int i = 0; i < 10000; ++i)
        splitter.process (i == 0 ? 1.0 : 0.0, low, mid, high);

    CHECK (std::isfinite (low));
    CHECK (std::isfinite (mid));
    CHECK (std::isfinite (high));

    // And swapped, so a user dragging the high crossover below the low one
    // gets sensible behaviour rather than inverted bands.
    splitter.setCrossovers (5000.0, 200.0);
    for (int i = 0; i < 10000; ++i)
        splitter.process (i == 0 ? 1.0 : 0.0, low, mid, high);

    CHECK (std::isfinite (low));
    CHECK (std::isfinite (mid));
    CHECK (std::isfinite (high));
}

TEZLA_TEST (crossover_single_branch_matches_the_full_split)
{
    // Single-band processing wants one side of the split and throws the other
    // away. The saving is only legitimate if the branch it keeps is bit-for-bit
    // what process() would have produced -- otherwise the plugin quietly sounds
    // different in single-band mode from multiband mode at the same frequency.
    LinkwitzRiley4<double> both;
    LinkwitzRiley4<double> branches;

    both.prepare (48000.0);
    branches.prepare (48000.0);
    both.setCrossover (3000.0);
    branches.setCrossover (3000.0);

    // Something with content on both sides of the corner and a discontinuity,
    // so the filter states are genuinely exercised rather than idling.
    for (int i = 0; i < 4096; ++i)
    {
        const double t = static_cast<double> (i) / 48000.0;
        const double input = std::sin (2.0 * std::numbers::pi * 120.0 * t)
                           + 0.5 * std::sin (2.0 * std::numbers::pi * 9000.0 * t)
                           + (i == 2048 ? 1.0 : 0.0);

        double low = 0.0, high = 0.0;
        both.process (input, low, high);

        CHECK (branches.processLow (input) == low);
        CHECK (branches.processHigh (input) == high);
    }
}
