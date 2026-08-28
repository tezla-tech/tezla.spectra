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
