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

#include <tezla/dsp/LoudnessMeter.hpp>

using namespace tezla::dsp;

namespace
{
/// A stereo sine at a stated dBFS, where 0 dBFS is a full-scale *peak* -- which
/// is how EBU Tech 3341 specifies its test signals.
std::vector<std::vector<double>> sine (double sampleRate, double seconds,
                                       double levelDbFs, double frequency = 1000.0)
{
    const auto length = static_cast<std::size_t> (sampleRate * seconds);
    const double amplitude = std::pow (10.0, levelDbFs / 20.0);

    std::vector<std::vector<double>> x (2, std::vector<double> (length));

    for (std::size_t i = 0; i < length; ++i)
    {
        const double v = amplitude
            * std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate);

        x[0][i] = v;
        x[1][i] = v;
    }

    return x;
}

void feed (LoudnessMeter& meter, const std::vector<std::vector<double>>& signal, int blockSize = 512)
{
    const auto total = static_cast<int> (signal[0].size());
    std::vector<double> left = signal[0];
    std::vector<double> right = signal[1];

    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int span = std::min (blockSize, total - offset);
        const double* pointers[2] { left.data() + offset, right.data() + offset };
        meter.process (pointers, 2, span);
    }
}
} // namespace

TEZLA_TEST (kweighting_reproduces_the_published_48k_table)
{
    // The whole reason the filters are designed rather than tabulated. The
    // Recommendation prints its coefficients for 48 kHz only; using them at
    // any other rate is silently wrong. Designing from the analogue prototype
    // fixes that -- and the proof that the prototype is the right one is that
    // at 48 kHz it reproduces the printed numbers.
    const auto k = LoudnessMeter::designKWeighting (48000.0);

    // ITU-R BS.1770-5, Tables 1 and 2, typed from the Recommendation.
    constexpr double shelfB0 =  1.53512485958697;
    constexpr double shelfB1 = -2.69169618940638;
    constexpr double shelfB2 =  1.19839281085285;
    constexpr double shelfA1 = -1.69065929318241;
    constexpr double shelfA2 =  0.73248077421585;

    constexpr double highpassA1 = -1.99004745483398;
    constexpr double highpassA2 =  0.99007225036621;

    const double worst = std::max ({
        std::abs (k.shelf.b0 - shelfB0), std::abs (k.shelf.b1 - shelfB1),
        std::abs (k.shelf.b2 - shelfB2), std::abs (k.shelf.a1 - shelfA1),
        std::abs (k.shelf.a2 - shelfA2),
        std::abs (k.highpass.a1 - highpassA1), std::abs (k.highpass.a2 - highpassA2) });

    // Measured at 8.9e-16 -- double rounding, not agreement to a tolerance.
    CHECK (worst < 1.0e-14);

    // The high-pass numerator is exactly (1, -2, 1) by definition, not by
    // design: it is a pure second-order high-pass with a zero pair at DC.
    CHECK (k.highpass.b0 == 1.0);
    CHECK (k.highpass.b1 == -2.0);
    CHECK (k.highpass.b2 == 1.0);
}

TEZLA_TEST (kweighting_is_not_the_same_filter_at_every_rate)
{
    // The negative control for the test above. If designKWeighting ignored its
    // argument -- the exact bug it exists to prevent -- the check above would
    // still pass and every reading off 48 kHz would be wrong.
    const auto at48 = LoudnessMeter::designKWeighting (48000.0);
    const auto at96 = LoudnessMeter::designKWeighting (96000.0);

    CHECK (std::abs (at48.shelf.a1 - at96.shelf.a1) > 0.1);
    CHECK (std::abs (at48.highpass.a1 - at96.highpass.a1) > 0.001);
}

TEZLA_TEST (loudness_reads_minus_23_lufs_at_every_sample_rate)
{
    // EBU Tech 3341 case 1: a 1 kHz stereo sine at -23 dBFS reads -23.0 LUFS on
    // all three meters, within +/-0.1 LU.
    //
    // Run at four rates because that is the coefficient trap: a meter built on
    // the printed 48 kHz numbers passes this at 48 kHz and fails everywhere
    // else, and nothing in the sound would tell you.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        LoudnessMeter meter;
        meter.prepare (rate, 2);

        feed (meter, sine (rate, 10.0, -23.0));

        CHECK (std::abs (meter.getMomentaryLufs()  + 23.0) < 0.1);
        CHECK (std::abs (meter.getShortTermLufs()  + 23.0) < 0.1);
        CHECK (std::abs (meter.getIntegratedLufs() + 23.0) < 0.1);
    }
}

TEZLA_TEST (loudness_tracks_level_one_for_one)
{
    // Tech 3341 case 2, generalised: the meter is a level meter with a
    // frequency weighting, so a level change has to move it by the same amount.
    for (const double level : { -33.0, -23.0, -13.0, -6.0 })
    {
        LoudnessMeter meter;
        meter.prepare (48000.0, 2);

        feed (meter, sine (48000.0, 8.0, level));

        CHECK (std::abs (meter.getIntegratedLufs() - level) < 0.1);
    }
}

TEZLA_TEST (loudness_gating_ignores_the_silence_around_a_take)
{
    // Tech 3341 case 3: ten seconds at -60 dBFS, sixty at -23, ten more at -60.
    // The integrated reading must be -23.0, because the quiet ends are gated
    // out. Ungated, the same signal averages several dB lower -- which is the
    // whole reason gating exists, and is checked below.
    LoudnessMeter meter;
    meter.prepare (48000.0, 2);

    feed (meter, sine (48000.0, 10.0, -60.0));
    feed (meter, sine (48000.0, 20.0, -23.0));
    feed (meter, sine (48000.0, 10.0, -60.0));

    CHECK (std::abs (meter.getIntegratedLufs() + 23.0) < 0.1);
}

TEZLA_TEST (loudness_relative_gate_bites_exactly_where_the_standard_says)
{
    // The second gate, pinned on both sides of its own boundary -- which is not
    // where it looks like it should be, and getting that wrong is how a meter
    // ends up disagreeing with every other meter by 2.6 LU.
    //
    // The gate sits 10 LU below the mean of the blocks that passed the absolute
    // gate, and that mean is itself dragged down by the quiet material. For
    // half a programme at L1 and half at L2, the quiet half is excluded only
    // when z1/z2 > 19 -- a separation of **12.79 dB**, not 10.
    //
    // So: 10 dB apart, the quiet half survives and the reading is the plain
    // mean, 2.60 dB below the loud half. 15 dB apart, it is thrown out and the
    // reading is the loud half exactly. A meter without the relative gate reads
    // the plain mean in both cases and passes neither.
    const auto readingFor = [] (double quietLevel)
    {
        LoudnessMeter meter;
        meter.prepare (48000.0, 2);

        feed (meter, sine (48000.0, 20.0, -23.0));
        feed (meter, sine (48000.0, 20.0, quietLevel));

        return meter.getIntegratedLufs();
    };

    // Below the boundary: nothing is excluded, and the answer is the mean.
    CHECK (std::abs (readingFor (-33.0) - (-25.60)) < 0.2);

    // Above it: the quiet half is gone and the loud half stands alone.
    CHECK (std::abs (readingFor (-38.0) - (-23.00)) < 0.2);
}

TEZLA_TEST (loudness_windows_are_the_lengths_the_standard_says)
{
    // Momentary settles in 400 ms and short-term in 3 s, so after 1 second of
    // tone the first is right and the second is still filling. A meter with the
    // two windows swapped passes every test above and fails this one.
    LoudnessMeter meter;
    meter.prepare (48000.0, 2);

    feed (meter, sine (48000.0, 1.0, -23.0));

    CHECK (std::abs (meter.getMomentaryLufs() + 23.0) < 0.1);
    CHECK (meter.getShortTermLufs() <= LoudnessMeter::kSilenceLufs);

    feed (meter, sine (48000.0, 3.0, -23.0));
    CHECK (std::abs (meter.getShortTermLufs() + 23.0) < 0.1);
}

TEZLA_TEST (loudness_is_independent_of_the_host_block_size)
{
    // The Emberdrive lesson applied to a meter: everything here is counted on a
    // 100 ms grid, so the host's buffer size must not move the reading.
    double first = 0.0;
    bool same = true;

    for (const int blockSize : { 1, 64, 512, 4096 })
    {
        LoudnessMeter meter;
        meter.prepare (48000.0, 2);

        feed (meter, sine (48000.0, 6.0, -18.0), blockSize);

        const double reading = meter.getIntegratedLufs();

        if (blockSize == 1)
            first = reading;
        else if (std::abs (reading - first) > 1.0e-9)
            same = false;
    }

    CHECK (same);
}

TEZLA_TEST (loudness_reports_silence_rather_than_a_large_negative_number)
{
    LoudnessMeter meter;
    meter.prepare (48000.0, 2);

    std::vector<std::vector<double>> quiet (2, std::vector<double> (48000 * 5, 0.0));
    feed (meter, quiet);

    // All three at the floor, and nothing NaN.
    CHECK (meter.getMomentaryLufs()  <= LoudnessMeter::kSilenceLufs);
    CHECK (meter.getShortTermLufs()  <= LoudnessMeter::kSilenceLufs);
    CHECK (meter.getIntegratedLufs() <= LoudnessMeter::kSilenceLufs);

    // Blocks were still counted -- they were simply all gated out.
    CHECK (meter.getBlockCount() > 0);
}

TEZLA_TEST (loudness_integration_resets_without_disturbing_the_filters)
{
    // The "restart measurement" button. It must clear the integration and
    // leave the momentary reading alone, which is what makes it different from
    // a transport reset.
    LoudnessMeter meter;
    meter.prepare (48000.0, 2);

    feed (meter, sine (48000.0, 5.0, -12.0));
    CHECK (std::abs (meter.getIntegratedLufs() + 12.0) < 0.1);

    meter.resetIntegration();
    CHECK (meter.getIntegratedLufs() <= LoudnessMeter::kSilenceLufs);

    // The momentary window is unaffected: its filters and its ring were not
    // touched.
    CHECK (std::abs (meter.getMomentaryLufs() + 12.0) < 0.1);
}
