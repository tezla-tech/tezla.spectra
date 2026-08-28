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
#include <random>
#include <vector>

#include <tezla/dsp/Correlation.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

/// Runs a stereo signal through the analyser in host-sized blocks.
void feed (StereoAnalyser& analyser, const std::vector<double>& left,
           const std::vector<double>& right, int blockSize = 256)
{
    const auto total = static_cast<int> (left.size());

    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int span = std::min (blockSize, total - offset);
        const double* pointers[2] { left.data() + offset, right.data() + offset };
        analyser.process (pointers, 2, span);
    }
}

std::vector<double> noise (int length, double amplitude, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> dist (-amplitude, amplitude);

    std::vector<double> x (static_cast<std::size_t> (length));

    for (auto& v : x)
        v = dist (rng);

    return x;
}
} // namespace

TEZLA_TEST (correlation_reads_the_three_textbook_cases)
{
    // +1 identical, -1 inverted, ~0 uncorrelated. If a meter gets these wrong
    // nothing else about it matters.
    constexpr int length = 48000;
    const auto a = noise (length, 0.5, 1u);
    const auto b = noise (length, 0.5, 2u);

    {
        StereoAnalyser analyser;
        analyser.prepare (kRate);
        feed (analyser, a, a);
        CHECK (std::abs (analyser.getCorrelation() - 1.0) < 1.0e-9);
    }

    {
        StereoAnalyser analyser;
        analyser.prepare (kRate);

        std::vector<double> inverted (a.size());
        for (std::size_t i = 0; i < a.size(); ++i)
            inverted[i] = -a[i];

        feed (analyser, a, inverted);
        CHECK (std::abs (analyser.getCorrelation() + 1.0) < 1.0e-9);
    }

    {
        StereoAnalyser analyser;
        analyser.prepare (kRate);
        feed (analyser, a, b);

        // Two independent noise streams over a 400 ms window: near zero, but
        // not exactly, and pretending otherwise would be a tolerance chosen to
        // flatter the test.
        CHECK (std::abs (analyser.getCorrelation()) < 0.1);
    }
}

TEZLA_TEST (correlation_ignores_level_which_is_what_makes_it_a_correlation)
{
    // A hard level difference between the channels is not a phase problem, and
    // a meter that read it as one would flag every panned mix. Scaling either
    // channel must not move the reading at all.
    const auto a = noise (48000, 0.5, 3u);

    std::vector<double> quiet (a.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        quiet[i] = a[i] * 0.05;

    StereoAnalyser analyser;
    analyser.prepare (kRate);
    feed (analyser, a, quiet);

    CHECK (std::abs (analyser.getCorrelation() - 1.0) < 1.0e-9);
}

TEZLA_TEST (correlation_reads_unity_on_silence_rather_than_swinging_to_zero)
{
    // Two channels that are both nothing are not disagreeing. A meter that
    // swung to the middle every time the music stopped would be one nobody
    // watches -- and the ratio is 0/0 there, so this is also the guard against
    // a NaN reaching the panel.
    StereoAnalyser analyser;
    analyser.prepare (kRate);

    const std::vector<double> quiet (48000, 0.0);
    feed (analyser, quiet, quiet);

    CHECK (analyser.getCorrelation() == 1.0);
}

TEZLA_TEST (correlation_finds_a_sub_that_is_out_of_phase_when_the_full_band_does_not)
{
    // The reason the per-band reading exists, and the failure it is for.
    //
    // A mix with an out-of-phase sub under it. The sub cancels completely in
    // mono -- the bass simply disappears on a club rig or a phone -- and
    // headphones will never show it, because each ear gets its own channel and
    // nothing sums.
    //
    // The full-band meter reads **+0.94**, which any engineer would glance at
    // and call healthy. That is the trap, and it is measured rather than
    // asserted: the sub is deliberately quiet, because a quiet out-of-phase sub
    // is both the commonest version of this mistake and the one a full-band
    // meter hides best.
    constexpr int length = 48000 * 2;

    std::vector<double> left (length), right (length);

    for (int i = 0; i < length; ++i)
    {
        const double t = static_cast<double> (i) / kRate;

        // A correlated mix, loud, well above the crossover.
        const double mix = 0.5 * std::sin (2.0 * std::numbers::pi * 1200.0 * t)
                         + 0.3 * std::sin (2.0 * std::numbers::pi * 3500.0 * t);

        // And a sub at a third of that, with the channels inverted.
        const double sub = 0.10 * std::sin (2.0 * std::numbers::pi * 45.0 * t);

        left[static_cast<std::size_t> (i)]  = mix + sub;
        right[static_cast<std::size_t> (i)] = mix - sub;
    }

    StereoAnalyser analyser;
    analyser.prepare (kRate);
    feed (analyser, left, right);

    const double full = analyser.getCorrelation();
    const double low  = analyser.getBandCorrelation (StereoAnalyser::low);

    // Looks fine. Measured at +0.9429.
    CHECK (full > 0.90);

    // Is not fine. Measured at exactly -1.0000: total cancellation in mono.
    CHECK (low < -0.999);
    CHECK (! analyser.isLowBandMonoSafe());

    // The gap is the whole point of splitting the reading up.
    CHECK (full - low > 1.9);

    // And the bands above are still reported healthy, so the display points at
    // the sub rather than vaguely at the mix.
    CHECK (analyser.getBandCorrelation (StereoAnalyser::mid) > 0.99);
    CHECK (analyser.getBandCorrelation (StereoAnalyser::high) > 0.99);
}

TEZLA_TEST (correlation_calls_a_mono_sub_safe)
{
    // The other half of the check above: the common, correct case must not
    // produce a warning, or the warning gets ignored when it matters.
    constexpr int length = 48000 * 2;

    std::vector<double> left (length), right (length);

    for (int i = 0; i < length; ++i)
    {
        const double t = static_cast<double> (i) / kRate;

        // Wide on top, mono underneath -- how a dubstep mix should be built.
        const double sub = 0.4 * std::sin (2.0 * std::numbers::pi * 45.0 * t);

        left[static_cast<std::size_t> (i)]  = sub + 0.3 * std::sin (2.0 * std::numbers::pi * 2400.0 * t);
        right[static_cast<std::size_t> (i)] = sub + 0.3 * std::sin (2.0 * std::numbers::pi * 2650.0 * t);
    }

    StereoAnalyser analyser;
    analyser.prepare (kRate);
    feed (analyser, left, right);

    CHECK (analyser.getBandCorrelation (StereoAnalyser::low) > 0.9);
    CHECK (analyser.isLowBandMonoSafe());
}

TEZLA_TEST (correlation_is_independent_of_the_host_block_size)
{
    const auto a = noise (48000, 0.5, 7u);
    const auto b = noise (48000, 0.5, 8u);

    double first = 0.0;
    bool same = true;

    for (const int blockSize : { 1, 64, 512, 4096 })
    {
        StereoAnalyser analyser;
        analyser.prepare (kRate);
        feed (analyser, a, b, blockSize);

        const double reading = analyser.getCorrelation();

        if (blockSize == 1)
            first = reading;
        else if (std::abs (reading - first) > 1.0e-12)
            same = false;
    }

    CHECK (same);
}

TEZLA_TEST (correlation_running_sums_do_not_drift)
{
    // The sums are updated incrementally rather than recomputed, which is the
    // random walk BoxStackSmoother documents -- and a correlation is a ratio of
    // three of them, so drift would show as a reading that slowly wandered off
    // +1 on a signal that had not changed.
    //
    // Long enough to cross several resync intervals.
    StereoAnalyser analyser;
    analyser.prepare (kRate);

    const auto a = noise (48000 * 20, 0.7, 11u);
    feed (analyser, a, a, 1024);

    CHECK (std::abs (analyser.getCorrelation() - 1.0) < 1.0e-9);
}

// ---------------------------------------------------------------------------
// StereoScope
// ---------------------------------------------------------------------------

namespace
{
/// Pushes a stereo signal into a scope in host-sized blocks.
void feedScope (StereoScope& scope, const std::vector<double>& left,
                const std::vector<double>& right, int blockSize = 256)
{
    const auto total = static_cast<int> (left.size());

    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int span = std::min (blockSize, total - offset);
        const double* pointers[2] { left.data() + offset, right.data() + offset };
        scope.push (pointers, 2, span);
    }
}
} // namespace

TEZLA_TEST (scope_holds_the_same_slice_of_time_at_every_rate)
{
    // Sized in seconds, so the picture spans the same slice of time whatever
    // the host is running at. A scope sized in samples would show four times
    // less music at 192 kHz than at 48, and the display would silently change
    // character with the session.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        StereoScope scope;
        scope.prepare (rate, 0.050);

        const double heldSeconds = static_cast<double> (scope.getCapacity()) / rate;

        // The capacity rounds up to a power of two, so it holds at least the
        // window asked for and less than twice it.
        CHECK (heldSeconds >= 0.050);
        CHECK (heldSeconds < 0.100);
    }
}

TEZLA_TEST (scope_returns_the_most_recent_pairs_in_order)
{
    StereoScope scope;
    scope.prepare (48000.0, 0.050);

    // A ramp, so every sample identifies itself.
    const int total = 4096;
    std::vector<double> left (static_cast<std::size_t> (total));
    std::vector<double> right (static_cast<std::size_t> (total));

    for (int i = 0; i < total; ++i)
    {
        left[static_cast<std::size_t> (i)]  = static_cast<double> (i);
        right[static_cast<std::size_t> (i)] = static_cast<double> (-i);
    }

    feedScope (scope, left, right, 300);

    std::vector<double> outLeft (256), outRight (256);
    CHECK (scope.readLatest (outLeft.data(), outRight.data(), 256) == 256);

    bool correct = true;

    for (int i = 0; i < 256; ++i)
    {
        const double expected = static_cast<double> (total - 256 + i);

        if (outLeft[static_cast<std::size_t> (i)] != expected
            || outRight[static_cast<std::size_t> (i)] != -expected)
            correct = false;
    }

    CHECK (correct);
}

TEZLA_TEST (scope_pairs_never_tear_across_a_block_boundary)
{
    // The reason there is one buffer and one write index rather than two of
    // each. A pair read from two independently-indexed rings can come from two
    // different moments, and a goniometer fed L from now and R from a
    // millisecond ago draws a rotation that is not in the audio.
    //
    // Here right = -left exactly, so any tear shows as a pair that does not.
    StereoScope scope;
    scope.prepare (48000.0, 0.050);

    const auto x = noise (48000, 0.8, 21u);
    std::vector<double> inverted (x.size());

    for (std::size_t i = 0; i < x.size(); ++i)
        inverted[i] = -x[i];

    // Odd block sizes, so pushes land at every alignment against the ring.
    for (const int blockSize : { 1, 7, 61, 257, 1024 })
    {
        StereoScope fresh;
        fresh.prepare (48000.0, 0.050);
        feedScope (fresh, x, inverted, blockSize);

        std::vector<double> outLeft (512), outRight (512);
        CHECK (fresh.readLatest (outLeft.data(), outRight.data(), 512) == 512);

        double worst = 0.0;

        for (std::size_t i = 0; i < outLeft.size(); ++i)
            worst = std::max (worst, std::abs (outLeft[i] + outRight[i]));

        CHECK (worst == 0.0);
    }
}

TEZLA_TEST (scope_striding_covers_the_whole_window)
{
    // How the display keeps a fixed point count at every rate: ask for the same
    // number of points and stride by the ratio. The first point must still come
    // from the start of the window, or the picture would only show its tail.
    StereoScope scope;
    scope.prepare (48000.0, 0.050);

    const std::size_t capacity = scope.getCapacity();

    std::vector<double> left (capacity), right (capacity);

    for (std::size_t i = 0; i < capacity; ++i)
    {
        left[i]  = static_cast<double> (i);
        right[i] = static_cast<double> (i);
    }

    feedScope (scope, left, right, 64);

    const int points = 256;
    const int stride = static_cast<int> (capacity) / points;

    std::vector<double> outLeft (static_cast<std::size_t> (points));
    std::vector<double> outRight (static_cast<std::size_t> (points));

    CHECK (scope.readLatest (outLeft.data(), outRight.data(), points, stride) == points);

    // First sample of the window, and the last point one stride from the end.
    CHECK_NEAR (outLeft.front(), 0.0, 1.0e-12);
    CHECK_NEAR (outLeft.back(), static_cast<double> (capacity - static_cast<std::size_t> (stride)), 1.0e-12);
}

TEZLA_TEST (scope_refuses_a_request_it_cannot_fill)
{
    // Silently returning fewer points than asked for would leave a caller
    // drawing whatever was in its buffer from last time.
    StereoScope scope;
    scope.prepare (48000.0, 0.050);

    const auto capacity = static_cast<int> (scope.getCapacity());

    std::vector<double> left (static_cast<std::size_t> (capacity) + 16);
    std::vector<double> right (left.size());

    CHECK (scope.readLatest (left.data(), right.data(), capacity + 1) == 0);
    CHECK (scope.readLatest (left.data(), right.data(), capacity / 2, 4) == 0);
    CHECK (scope.readLatest (left.data(), right.data(), capacity, 1) == capacity);

    StereoScope unprepared;
    CHECK (unprepared.readLatest (left.data(), right.data(), 16) == 0);
    CHECK (unprepared.getCapacity() == 0);
}

TEZLA_TEST (scope_duplicates_a_mono_input)
{
    // A mono signal is the 45-degree line, and drawing it needs the right
    // channel to exist. Reading uninitialised zeros instead would draw a
    // horizontal line, which on a goniometer means "hard left".
    StereoScope scope;
    scope.prepare (48000.0, 0.050);

    const auto x = noise (2048, 0.5, 31u);

    const double* mono[1] { x.data() };
    scope.push (mono, 1, static_cast<int> (x.size()));

    std::vector<double> outLeft (256), outRight (256);
    CHECK (scope.readLatest (outLeft.data(), outRight.data(), 256) == 256);

    double worst = 0.0;

    for (std::size_t i = 0; i < outLeft.size(); ++i)
        worst = std::max (worst, std::abs (outLeft[i] - outRight[i]));

    CHECK (worst == 0.0);
}

TEZLA_TEST (scope_reset_clears_the_picture)
{
    StereoScope scope;
    scope.prepare (48000.0, 0.050);

    const auto x = noise (4096, 0.9, 41u);
    feedScope (scope, x, x, 256);
    scope.reset();

    std::vector<double> outLeft (256), outRight (256);
    CHECK (scope.readLatest (outLeft.data(), outRight.data(), 256) == 256);

    double worst = 0.0;

    for (std::size_t i = 0; i < outLeft.size(); ++i)
        worst = std::max ({ worst, std::abs (outLeft[i]), std::abs (outRight[i]) });

    CHECK (worst == 0.0);
}
