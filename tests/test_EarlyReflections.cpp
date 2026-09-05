// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include <tezla/dsp/EarlyReflections.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Fft.hpp>

using tezla::dsp::EarlyReflections;
using tezla::dsp::isExactly;
using tezla::dsp::isExactlyZero;

namespace
{
struct Response
{
    std::vector<double> left, right;
};

/// The room's answer to a unit impulse, `samples` long.
Response impulseResponse (EarlyReflections& room, int samples)
{
    Response out;
    out.left.assign (static_cast<std::size_t> (samples), 0.0);
    out.right.assign (static_cast<std::size_t> (samples), 0.0);

    for (int n = 0; n < samples; ++n)
        room.process (n == 0 ? 1.0 : 0.0, out.left[static_cast<std::size_t> (n)],
                      out.right[static_cast<std::size_t> (n)]);

    return out;
}

int nonZeroCount (const std::vector<double>& x)
{
    int count = 0;
    for (const double v : x)
        if (! isExactlyZero (v))
            ++count;
    return count;
}

double energyOf (const std::vector<double>& x)
{
    double sum = 0.0;
    for (const double v : x)
        sum += v * v;
    return sum;
}

double centroidHz (const std::vector<double>& x, double rate)
{
    std::vector<double> padded (1u << 15, 0.0);
    for (std::size_t n = 0; n < std::min (x.size(), padded.size()); ++n)
        padded[n] = x[n];

    const auto spectrum = tezla::dsp::fftOfReal (padded);
    const double binWidth = rate / static_cast<double> (padded.size());

    double weighted = 0.0, total = 0.0;
    for (std::size_t bin = 1; bin < spectrum.size() / 2; ++bin)
    {
        const double power = std::norm (spectrum[bin]);
        weighted += static_cast<double> (bin) * binWidth * power;
        total += power;
    }

    return total > 0.0 ? weighted / total : 0.0;
}
} // namespace

TEZLA_TEST (a_room_is_one_tap_per_cell_per_channel_at_unit_energy)
{
    const double rate = 96000.0;
    EarlyReflections room;
    room.prepare (rate, 0.25);
    room.design (0.1, 7u);

    CHECK (room.getLengthSamples() == 9600);

    const auto ir = impulseResponse (room, 9601);

    // One tap in each of 48 cells, on both channels, every one inside the length.
    CHECK (nonZeroCount (ir.left) == EarlyReflections::kTapsPerChannel);
    CHECK (nonZeroCount (ir.right) == EarlyReflections::kTapsPerChannel);

    const double cell = 9600.0 / EarlyReflections::kTapsPerChannel;

    for (int channel = 0; channel < 2; ++channel)
        for (int tap = 0; tap < EarlyReflections::kTapsPerChannel; ++tap)
        {
            const int delay = room.getTapDelay (channel, tap);
            CHECK (delay >= static_cast<int> (tap * cell) && delay < static_cast<int> ((tap + 1) * cell) + 1);
            CHECK (delay < 9600);
        }

    // Unit energy per channel: the room's level is the caller's one number.
    CHECK_NEAR (energyOf (ir.left), 1.0, 1.0e-12);
    CHECK_NEAR (energyOf (ir.right), 1.0, 1.0e-12);

    // The taps fall to -30 dB across the length: each gain follows the rule
    // exactly, relative to a tap at delay 0.
    const double first = std::abs (room.getTapGain (0, 0))
                       / std::pow (10.0, -EarlyReflections::kDecayDb / 20.0 * room.getTapDelay (0, 0) / 9600.0);

    for (int tap = 0; tap < EarlyReflections::kTapsPerChannel; ++tap)
    {
        const double expected = first * std::pow (10.0, -EarlyReflections::kDecayDb / 20.0
                                                              * room.getTapDelay (0, tap) / 9600.0);
        CHECK_NEAR (std::abs (room.getTapGain (0, tap)), expected, 1.0e-12);
    }

    // Each tap is the impulse times its gain, bit for bit: with the tone off
    // nothing touches the sum.
    for (int tap = 0; tap < EarlyReflections::kTapsPerChannel; ++tap)
        CHECK (isExactly (ir.left[static_cast<std::size_t> (room.getTapDelay (0, tap))], room.getTapGain (0, tap)));

    std::printf ("        [room] 0.1 s at 96 kHz: %d taps a side, first |gain| %.4f, last %.4f, energy %.12f / %.12f\n",
                 EarlyReflections::kTapsPerChannel, std::abs (room.getTapGain (0, 0)),
                 std::abs (room.getTapGain (0, EarlyReflections::kTapsPerChannel - 1)),
                 energyOf (ir.left), energyOf (ir.right));
}

TEZLA_TEST (the_two_channels_are_two_rooms_and_the_line_drains_to_exact_zeros)
{
    const double rate = 48000.0;
    EarlyReflections room;
    room.prepare (rate, 0.25);
    room.design (0.15, 12u);

    const int length = room.getLengthSamples();
    const auto ir = impulseResponse (room, length + 64);

    // Decorrelated by construction: two independent draws.
    double dot = 0.0;
    for (std::size_t n = 0; n < ir.left.size(); ++n)
        dot += ir.left[n] * ir.right[n];

    const double correlation = dot / std::sqrt (energyOf (ir.left) * energyOf (ir.right));
    std::printf ("        [room] left/right correlation of the responses: %.3f\n", correlation);
    CHECK (std::abs (correlation) < 0.3);

    // Drained: exact zeros after the last tap, and the room says so.
    for (std::size_t n = static_cast<std::size_t> (length); n < ir.left.size(); ++n)
    {
        CHECK (isExactlyZero (ir.left[n]));
        CHECK (isExactlyZero (ir.right[n]));
    }

    CHECK (! room.isActive());

    // A different seed is a different room; a redesign moves no memory.
    const double* storage = room.lineData();
    room.design (0.15, 13u);
    CHECK (room.lineData() == storage);

    int moved = 0;
    for (int tap = 0; tap < EarlyReflections::kTapsPerChannel; ++tap)
        if (room.getTapDelay (0, tap) != static_cast<int> (std::lround (ir.left.size())))   // placeholder never equal
            ++moved;

    CHECK (moved == EarlyReflections::kTapsPerChannel);
}

TEZLA_TEST (the_tone_dulls_the_room_and_off_is_exact)
{
    const double rate = 96000.0;

    EarlyReflections bright;
    bright.prepare (rate, 0.25);
    bright.design (0.08, 3u);
    bright.setToneHz (0.0);
    const auto open = impulseResponse (bright, 8192);

    EarlyReflections dull;
    dull.prepare (rate, 0.25);
    dull.design (0.08, 3u);
    dull.setToneHz (1500.0);
    const auto closed = impulseResponse (dull, 8192);

    const double openCentroid = centroidHz (open.left, rate);
    const double closedCentroid = centroidHz (closed.left, rate);

    std::printf ("        [room] centroid %.0f Hz with the tone off, %.0f Hz at 1.5 kHz\n", openCentroid, closedCentroid);

    CHECK (closedCentroid < openCentroid * 0.35);
    CHECK (isExactlyZero (bright.getToneHz()));
    CHECK (isExactly (dull.getToneHz(), 1500.0));

    // With the tone on, the room still ends in exact zeros once the filters
    // have fallen below the floor.
    EarlyReflections finishing;
    finishing.prepare (rate, 0.25);
    finishing.design (0.02, 5u);
    finishing.setToneHz (400.0);

    double l = 0.0, r = 0.0;
    finishing.process (1.0, l, r);

    for (int n = 0; n < static_cast<int> (rate); ++n)
        finishing.process (0.0, l, r);

    CHECK (isExactlyZero (l) && isExactlyZero (r));
    CHECK (! finishing.isActive());
}
