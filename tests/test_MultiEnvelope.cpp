// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/MultiEnvelope.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

MultiEnvelope made()
{
    MultiEnvelope envelope;
    envelope.prepare (kRate);
    return envelope;
}
} // namespace

// ---------------------------------------------------------------------------
// Segment exactness
// ---------------------------------------------------------------------------

TEZLA_TEST (each_segment_arrives_at_its_level_in_exactly_its_stated_time)
{
    auto envelope = made();

    envelope.setPointCount (4);
    envelope.setSustainIndex (2);
    envelope.setPoint (0, 0.010, 1.0, 0.5);
    envelope.setPoint (1, 0.050, 0.3, -0.4);
    envelope.setPoint (2, 0.020, 0.6, 0.0);
    envelope.setPoint (3, 0.100, 0.0, 0.3);

    envelope.noteOn();

    const auto samplesFor = [] (double seconds)
    {
        return static_cast<int> (std::llround (seconds * kRate));
    };

    // Segment 1: after exactly 10 ms the level is exactly 1.0.
    double value = 0.0;
    for (int i = 0; i < samplesFor (0.010); ++i)
        value = envelope.process();
    CHECK (value == 1.0);

    // Segment 2: exactly 0.3 after exactly 50 ms more.
    for (int i = 0; i < samplesFor (0.050); ++i)
        value = envelope.process();
    CHECK (value == 0.3);

    // Segment 3: to the sustain point, and it parks there.
    for (int i = 0; i < samplesFor (0.020); ++i)
        value = envelope.process();
    CHECK (value == 0.6);

    for (int i = 0; i < 4800; ++i)
        value = envelope.process();
    CHECK (value == 0.6);

    // Release: the post-sustain chain, exactly 100 ms to zero, then finished.
    envelope.noteOff();

    for (int i = 0; i < samplesFor (0.100); ++i)
        value = envelope.process();

    CHECK (value == 0.0);
    CHECK (envelope.isFinished());
}

TEZLA_TEST (the_curve_is_adsrs_curve_for_the_same_tension)
{
    // Shared statics, not a copied formula -- so a two-point envelope rising
    // over the same time with the same tension must match the Adsr's attack
    // sample for sample.
    for (const double tension : { -0.8, 0.0, 0.35, 0.9 })
    {
        Adsr adsr;
        adsr.prepare (kRate);
        adsr.setAttackSeconds (0.2);
        adsr.setAttackTension (tension);
        adsr.setHoldSeconds (10.0);   // park after the attack
        adsr.noteOn();

        auto multi = made();
        multi.setPointCount (2);
        multi.setSustainIndex (1);
        multi.setPoint (0, 0.2, 1.0, tension);
        multi.setPoint (1, 10.0, 1.0, 0.0);
        multi.noteOn();

        for (int i = 0; i < static_cast<int> (0.2 * kRate) - 1; ++i)
            CHECK_NEAR (adsr.process(), multi.process(), 1.0e-9);
    }
}

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

TEZLA_TEST (the_loop_period_is_exactly_the_sum_of_its_legs)
{
    auto envelope = made();

    envelope.setPointCount (4);
    envelope.setSustainIndex (2);
    envelope.setLoopStart (1);
    envelope.setLoop (true);
    envelope.setPoint (0, 0.005, 1.0, 0.0);
    envelope.setPoint (1, 0.030, 0.2, 0.0);   // the return leg's time too
    envelope.setPoint (2, 0.050, 0.9, 0.0);
    envelope.setPoint (3, 0.040, 0.0, 0.0);

    envelope.noteOn();

    // Collect two seconds and find every arrival at the sustain level's peak.
    std::vector<int> peaks;
    double previous = 0.0;
    bool rising = false;

    for (int i = 0; i < static_cast<int> (2.0 * kRate); ++i)
    {
        const double value = envelope.process();

        if (value > previous)
            rising = true;
        else if (rising && value <= previous)
        {
            peaks.push_back (i);
            rising = false;
        }

        previous = value;
    }

    CHECK (peaks.size() >= 10);

    // Period = return leg (0.030) + forward leg (0.050) = 80 ms = 3840 samples.
    for (std::size_t i = 2; i < peaks.size(); ++i)
        CHECK (std::abs ((peaks[i] - peaks[i - 1]) - 3840) <= 1);
}

TEZLA_TEST (release_from_mid_loop_leaves_without_a_step)
{
    auto envelope = made();

    envelope.setPointCount (3);
    envelope.setSustainIndex (1);
    envelope.setLoopStart (0);
    envelope.setLoop (true);
    envelope.setPoint (0, 0.020, 1.0, 0.4);
    envelope.setPoint (1, 0.020, 0.1, 0.4);
    envelope.setPoint (2, 0.200, 0.0, 0.0);

    envelope.noteOn();

    for (int i = 0; i < 3000; ++i)
        (void) envelope.process();

    const double atRelease = envelope.getValue();

    envelope.noteOff();

    // The first release sample continues from exactly where the loop was.
    const double first = envelope.process();

    CHECK (std::abs (first - atRelease) < 0.01);

    // And it drains to zero.
    double value = first;
    for (int i = 0; i < static_cast<int> (0.3 * kRate); ++i)
        value = envelope.process();

    CHECK (value == 0.0);
    CHECK (envelope.isFinished());
}

// ---------------------------------------------------------------------------
// Edges
// ---------------------------------------------------------------------------

TEZLA_TEST (retrigger_resumes_from_the_current_level_without_a_jump)
{
    auto envelope = made();

    envelope.setPointCount (2);
    envelope.setSustainIndex (1);
    envelope.setPoint (0, 0.050, 1.0, 0.0);
    envelope.setPoint (1, 0.050, 0.5, 0.0);

    envelope.noteOn();

    for (int i = 0; i < 1200; ++i)
        (void) envelope.process();

    const double before = envelope.getValue();

    envelope.noteOn();   // retrigger mid-attack

    const double after = envelope.process();

    CHECK (std::abs (after - before) < 0.01);
}

TEZLA_TEST (sustain_on_the_last_point_releases_to_zero)
{
    auto envelope = made();

    envelope.setPointCount (2);
    envelope.setSustainIndex (1);
    envelope.setPoint (0, 0.010, 0.8, 0.0);
    envelope.setPoint (1, 0.020, 0.6, 0.0);

    envelope.noteOn();

    for (int i = 0; i < 4800; ++i)
        (void) envelope.process();

    CHECK (envelope.isSustaining());
    CHECK (envelope.getValue() == 0.6);

    envelope.noteOff();

    double value = 1.0;
    for (int i = 0; i < static_cast<int> (0.05 * kRate); ++i)
        value = envelope.process();

    CHECK (value == 0.0);
    CHECK (envelope.isFinished());
}

TEZLA_TEST (silence_and_a_finished_envelope_stay_put)
{
    auto envelope = made();

    for (int i = 0; i < 4800; ++i)
        CHECK (envelope.process() == 0.0);

    CHECK (envelope.isFinished());
}

// ---------------------------------------------------------------------------
// Sixteen points
//
// The ceiling moved from eight to sixteen. Sixteen is a bar of sixteenths, so
// the interesting cases are a full-length envelope whose every leg still lands
// where it says it does, a loop that spans the whole bar, and the guarantee
// that the eight points nobody asked for cannot reach the output.
// ---------------------------------------------------------------------------

TEZLA_TEST (the_ceiling_is_sixteen_and_a_count_past_it_clamps)
{
    auto envelope = made();

    CHECK (MultiEnvelope::kMaxPoints == 16);

    envelope.setPointCount (17);
    CHECK (envelope.getPointCount() == 16);

    envelope.setPointCount (0);
    CHECK (envelope.getPointCount() == 2);

    envelope.setPointCount (16);
    CHECK (envelope.getPointCount() == 16);
}

TEZLA_TEST (all_sixteen_legs_arrive_at_their_level_in_exactly_their_stated_time)
{
    auto envelope = made();

    // Sixteen legs, none the same length, none the same level, so a leg that
    // landed on the wrong point could not pass by accident.
    constexpr double seconds[16] { 0.004, 0.011, 0.006, 0.019, 0.008, 0.013, 0.005, 0.021,
                                   0.009, 0.014, 0.007, 0.017, 0.010, 0.012, 0.015, 0.020 };
    constexpr double levels[16]  { 1.00, 0.12, 0.87, 0.31, 0.74, 0.19, 0.95, 0.44,
                                   0.62, 0.08, 0.81, 0.27, 0.55, 0.36, 0.99, 0.03 };
    constexpr double tensions[16] { 0.0, 0.4, -0.4, 0.7, -0.7, 0.2, -0.2, 0.9,
                                    -0.9, 0.35, -0.35, 0.5, -0.5, 0.1, -0.1, 0.0 };

    envelope.setPointCount (16);
    envelope.setSustainIndex (15);

    for (int i = 0; i < 16; ++i)
        envelope.setPoint (i, seconds[i], levels[i], tensions[i]);

    envelope.noteOn();

    // Every arrival is exact -- the segment snaps to its destination on the
    // sample its stated time runs out. That is the design, not a tolerance.
    long elapsed = 0;
    double value = 0.0;

    for (int point = 0; point < 16; ++point)
    {
        const long samples = std::lround (seconds[point] * kRate);

        for (long i = 0; i < samples; ++i)
            value = envelope.process();

        elapsed += samples;

        CHECK (value == levels[point]);
    }

    CHECK (elapsed == 9168);   // 0.191 s of envelope at 48 kHz, measured
    CHECK (envelope.isSustaining());
}

TEZLA_TEST (a_sixteen_point_loop_is_a_whole_bar_of_sixteenths)
{
    auto envelope = made();

    // 120 BPM: a sixteenth is 0.125 s, sixteen of them a bar of 2 s. This is
    // the reason sixteen points exists rather than eight.
    constexpr double sixteenth = 0.125;

    envelope.setPointCount (16);
    envelope.setSustainIndex (15);
    envelope.setLoopStart (0);
    envelope.setLoop (true);

    for (int i = 0; i < 16; ++i)
        envelope.setPoint (i, sixteenth, i == 0 ? 1.0 : (i % 2 == 0 ? 0.5 : 0.2), 0.0);

    envelope.noteOn();

    // Point 0 is the only one at 1.0, and a leg only *reaches* its destination
    // on the arrival sample, so an exact 1.0 marks one lap of the loop.
    std::vector<long> laps;

    for (long i = 0; i < static_cast<long> (7.0 * kRate); ++i)
        if (envelope.process() == 1.0)
            laps.push_back (i);

    CHECK (laps.size() >= 3);

    // 2.0 s at 48 kHz. The bar is exact because the legs are counted in
    // samples, not detected by threshold.
    for (std::size_t i = 1; i < laps.size(); ++i)
        CHECK (laps[i] - laps[i - 1] == 96000);
}

TEZLA_TEST (points_past_the_count_cannot_reach_the_output)
{
    // The eight points the sixteen-point ceiling added are, for a short
    // envelope, eight parameters that must do exactly nothing -- and the case
    // that reaches them is **not** the tidy one. Points, Sustain and Loop from
    // are three independent parameters with three independent ranges, so
    // "4 points, sustain at 11, loop from 9" is a setting the panel can be put
    // in, and raising the ceiling to sixteen made eight more values of it
    // reachable. Every index the envelope walks is clamped against the count
    // for exactly this reason; the poisoned tail is how that is checked, bit
    // for bit rather than by ear.
    const auto run = [] (int count, int sustain, int loopStart, bool loop, bool poisonTheTail)
    {
        auto envelope = made();

        envelope.setPointCount (count);
        envelope.setSustainIndex (sustain);
        envelope.setLoopStart (loopStart);
        envelope.setLoop (loop);
        envelope.setPoint (0, 0.010, 1.0, 0.35);
        envelope.setPoint (1, 0.040, 0.3, -0.5);
        envelope.setPoint (2, 0.025, 0.8, 0.2);
        envelope.setPoint (3, 0.120, 0.0, 0.0);

        for (int i = 4; i < MultiEnvelope::kMaxPoints; ++i)
            envelope.setPoint (i, 0.100, 0.0, 0.0);

        // Poisoned from the count upward, so the two runs differ in exactly
        // the points this envelope is not using -- and in no others. (Poisoning
        // from a fixed index instead was this test's own first bug: at count 5
        // it corrupted a point the envelope legitimately plays, and the sweep
        // duly reported a difference that was mine, not the code's.)
        if (poisonTheTail)
            for (int i = count; i < MultiEnvelope::kMaxPoints; ++i)
                envelope.setPoint (i, 0.030, 0.95, -0.9);

        std::vector<double> out;
        out.reserve (12000);

        envelope.noteOn();

        for (int i = 0; i < 9000; ++i)
            out.push_back (envelope.process());

        envelope.noteOff();

        for (int i = 0; i < 3000; ++i)
            out.push_back (envelope.process());

        return out;
    };

    // The whole reachable grid of the three, not a sample of it: 4 counts x
    // 16 sustain indices x 16 loop starts x loop on and off.
    int compared = 0;
    long mismatches = 0;
    double worst = 0.0;

    for (int count = 2; count <= 5; ++count)
        for (int sustain = 0; sustain < MultiEnvelope::kMaxPoints; ++sustain)
            for (int loopStart = 0; loopStart < MultiEnvelope::kMaxPoints; ++loopStart)
                for (const bool loop : { false, true })
                {
                    const auto clean = run (count, sustain, loopStart, loop, false);
                    const auto poisoned = run (count, sustain, loopStart, loop, true);

                    if (clean.size() != poisoned.size())
                    {
                        ++mismatches;
                        continue;
                    }

                    for (std::size_t i = 0; i < clean.size(); ++i)
                        if (clean[i] != poisoned[i])
                        {
                            ++mismatches;
                            worst = std::max (worst, std::abs (clean[i] - poisoned[i]));
                        }

                    ++compared;
                }

    // Counted rather than asserted per sample, so a regression reports how far
    // out it is instead of 24 million identical lines.
    //
    // Seen red, and the interesting part is what it took: **two** clamps had to
    // go at once (`aimAt`'s ceiling and `loopPoint`'s) before a tail point
    // reached the output -- then 4 040 033 samples of 24 576 000 differed,
    // worst difference 0.9500, which is the poisoned level itself. Removing either one alone leaves the
    // other holding, because every index the envelope walks is derived from
    // the count twice over. That is worth knowing rather than glossing: the
    // sweep is a backstop against the *combination*, and `aimAt`'s clamp is
    // the one load-bearing line.
    CHECK (compared == 2048);
    CHECK (mismatches == 0);
    CHECK (worst == 0.0);
}
