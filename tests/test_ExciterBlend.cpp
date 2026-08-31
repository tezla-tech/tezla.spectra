// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <tuple>
#include <vector>

#include <tezla/dsp/Scales.hpp>

#include "MalleusVoice.hpp"

using namespace tezla::malleus;

namespace
{
constexpr double kRate = 48000.0;

/// 20 ms: the window the strike actually lives in.
///
/// Chosen by measurement rather than by feel. Over 250 ms the same velocity
/// sweep moves the centroid 213 -> 246 Hz and over 20 ms it moves 216 -> 542,
/// because after the first few cycles what is left is the fundamental ringing
/// and the mallet's contribution is buried under it. A measurement that cannot
/// resolve the thing it is about is not a strict one, it is a blunt one.
constexpr std::size_t kStrikeWindow = 960;

/// A voice struck once and rendered, at whatever settings are handed in.
std::vector<double> playNote (const VoiceSettings& settings, double velocity,
                              int samples, int blockSize = 128)
{
    MalleusVoice voice;

    voice.prepare (kRate);
    voice.noteOn (57, 220.0, velocity, 0x9E3779B97F4A7C15ULL, settings,
                  tezla::dsp::scales::twelveToneEqual(), 0);

    std::vector<double> out (static_cast<std::size_t> (samples), 0.0);

    for (int done = 0; done < samples; done += blockSize)
    {
        const int take = std::min (blockSize, samples - done);

        voice.controlTick (take);
        voice.render (out.data() + done, take);
    }

    return out;
}

double rmsOf (const std::vector<double>& x)
{
    double sum = 0.0;

    for (const double sample : x)
        sum += sample * sample;

    return x.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (x.size()));
}

/// Spectral centroid in Hz by direct projection onto a bank of probe
/// frequencies -- the honest measure of "brighter", and the one the hardness
/// claim is about. Hann-windowed, per test_MalleusEngine's note about
/// rectangular skirts masking what is being measured.
double centroidHz (const std::vector<double>& x, std::size_t from, std::size_t to)
{
    double weighted = 0.0;
    double total = 0.0;

    const auto count = static_cast<double> (to - from);

    for (int band = 0; band < 48; ++band)
    {
        // 100 Hz to 16 kHz, log-spaced.
        const double hz = 100.0 * std::pow (160.0, static_cast<double> (band) / 47.0);

        double re = 0.0;
        double im = 0.0;

        for (std::size_t n = from; n < to && n < x.size(); ++n)
        {
            const double along = static_cast<double> (n - from) / count;
            const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * along);
            const double phase = 2.0 * std::numbers::pi * hz
                               * static_cast<double> (n - from) / kRate;

            re += window * x[n] * std::cos (phase);
            im += window * x[n] * std::sin (phase);
        }

        const double power = re * re + im * im;

        weighted += hz * power;
        total += power;
    }

    return total > 0.0 ? weighted / total : 0.0;
}

VoiceSettings baseSettings()
{
    VoiceSettings settings;

    settings.material = 1.2;
    settings.partials = 32;
    settings.decaySeconds = 2.0;
    settings.position = 0.29;
    settings.hardness = 0.5;

    return settings;
}
} // namespace

// ---------------------------------------------------------------------------
// The blend's two ends
// ---------------------------------------------------------------------------

TEZLA_TEST (a_blend_of_zero_is_bit_for_bit_the_first_exciter_alone)
{
    // The compatibility claim. Every Malleus patch ever saved has one exciter
    // and no blend, so blend 0 has to be the voice that shipped -- identical,
    // not merely close. Checked for all four exciters in slot A, against all
    // four in slot B, because a slot B that leaves any trace at weight 0
    // would show up on exactly one of the sixteen pairs.
    // **With the scrape on, and with a roll fast enough to restrike inside
    // it**, which took two break-checks to arrive at and is the only shape
    // that gives this test teeth against the zero-weight skip.
    //
    // Deleting `applyExciter`'s `isExactlyZero (weight)` guard leaves every
    // obvious case green, because every downstream of a zero weight really is
    // a no-op: `excite(mode, 0.0)` adds nothing, `ping(0)` cannot raise the
    // flash, and a bow at zero pressure is silent. The exception is
    // `NoiseBurst::trigger`, which **restarts** rather than adds -- so a
    // zero-weight Roll slot whose clock fires while the real burst is still
    // running replaces it with silence.
    //
    // At the default 90 ms roll start and a 0.5 hardness the burst (4 x the
    // 1.1 ms contact, ~6 ms) is long spent before the first restrike, and the
    // damage is invisible -- a restarted burst that was already silent is
    // still silent. The soft row below is the one that matters: a 0.0 hardness
    // makes the contact 8 ms and the burst 34 ms, and a 20 ms roll start puts
    // a restrike inside it. With that row present, deleting the guard fails
    // **46,080 sample comparisons** across the two tests; without it, none.
    int pairs = 0;

    for (const auto& [noise, hardness, rollStart] :
         { std::tuple { 0.0, 0.5, 0.09 },
           std::tuple { 0.4, 0.5, 0.09 },
           std::tuple { 0.4, 0.0, 0.02 } })
        for (const auto a : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
        {
            auto lone = baseSettings();
            lone.exciter = a;
            lone.noiseAmount = noise;
            lone.hardness = hardness;
            lone.rollStartSeconds = rollStart;
            // ...and no exciterB, no blend: the settings a pre-phase-2 build
            // had, since neither field existed.

            const auto reference = playNote (lone, 0.8, 24000);

            for (const auto b : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
            {
                auto blended = lone;
                blended.exciterB = b;
                blended.exciterBlend = 0.0;

                const auto after = playNote (blended, 0.8, 24000);

                CHECK (reference.size() == after.size());

                for (std::size_t i = 0; i < reference.size(); ++i)
                    CHECK (reference[i] == after[i]);

                ++pairs;
            }
        }

    CHECK (pairs == 48);
}

TEZLA_TEST (a_blend_of_one_is_bit_for_bit_the_second_exciter_alone)
{
    // The other end, and the reason the weights are spelt `1 - t` and `t`
    // rather than `a + t * (b - a)`: the latter is exact only at zero. Here
    // slot A must vanish as completely as slot B did above.
    int pairs = 0;

    for (const auto& [noise, hardness, rollStart] :
         { std::tuple { 0.0, 0.5, 0.09 },
           std::tuple { 0.4, 0.5, 0.09 },
           std::tuple { 0.4, 0.0, 0.02 } })
        for (const auto b : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
        {
            auto lone = baseSettings();
            lone.exciter = b;
            lone.noiseAmount = noise;
            lone.hardness = hardness;
            lone.rollStartSeconds = rollStart;

            const auto reference = playNote (lone, 0.8, 24000);

            for (const auto a : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
            {
                auto blended = lone;
                blended.exciter = a;
                blended.exciterB = b;
                blended.exciterBlend = 1.0;

                const auto after = playNote (blended, 0.8, 24000);

                for (std::size_t i = 0; i < reference.size(); ++i)
                    CHECK (reference[i] == after[i]);

                ++pairs;
            }
        }

    CHECK (pairs == 48);
}

// ---------------------------------------------------------------------------
// What the blend actually does
// ---------------------------------------------------------------------------

TEZLA_TEST (a_mallet_blended_with_a_pluck_lands_between_the_two)
{
    // A pluck's 1/k^2 series is inherently darker than any mallet -- that is
    // the audible difference between them -- so a blend of the two has to sit
    // between their centroids and move monotonically along the control.
    // Measured on the sound, not argued from the weights.
    auto settings = baseSettings();
    settings.exciter = Exciter::Mallet;
    settings.exciterB = Exciter::Pluck;
    settings.hardness = 0.75;   // a firm beater, so the two are far apart

    std::printf ("        [blend]  t    centroid Hz     rms\n");

    double previous = 1.0e9;
    int steps = 0;

    for (const double t : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        settings.exciterBlend = t;

        const auto note = playNote (settings, 0.8, 24000);
        const double centroid = centroidHz (note, 0, kStrikeWindow);

        std::printf ("        [blend] %4.2f   %9.1f   %7.4f\n",
                     t, centroid, rmsOf (note));

        // Strictly darker at every step: the pluck is taking over.
        CHECK (centroid < previous);

        previous = centroid;
        ++steps;
    }

    CHECK (steps == 5);
}

TEZLA_TEST (the_blend_never_makes_a_note_louder_than_either_end)
{
    // A lerp on the amounts, not a sum of two full-strength hits: halfway
    // between two exciters must not be a double strike. Swept across the
    // whole grid rather than sampled, because the roll and the bow add energy
    // over time rather than at contact and are the cases that could break it.
    double worst = 0.0;
    int combinations = 0;

    for (const auto a : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
        for (const auto b : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
        {
            auto settings = baseSettings();
            settings.exciter = a;
            settings.exciterB = b;

            settings.exciterBlend = 0.0;
            const double endA = rmsOf (playNote (settings, 1.0, 24000));

            settings.exciterBlend = 1.0;
            const double endB = rmsOf (playNote (settings, 1.0, 24000));

            const double ceiling = std::max (endA, endB);

            for (const double t : { 0.2, 0.4, 0.5, 0.6, 0.8 })
            {
                settings.exciterBlend = t;

                const double middle = rmsOf (playNote (settings, 1.0, 24000));

                if (ceiling > 0.0)
                    worst = std::max (worst, middle / ceiling);

                ++combinations;
            }
        }

    CHECK (combinations == 80);

    // Measured worst 1.000000 across the grid: the middle of the control is
    // never louder than the louder of its two ends, anywhere.
    //
    // That is not free, and it was 2.4x wrong before the gate fix. Both slots
    // used to ping the gate and trigger the scrape burst themselves, and
    // neither is linear in the amount -- `ping` takes a maximum, `trigger`
    // restarts -- so a half-and-half blend opened the gate half as far as
    // either end did and left a hole in the middle of the control: RMS 0.0016
    // against 0.0038 and 0.0027 on a mallet blended with a pluck. The strike
    // is now split into its modal part, which is linear and per slot, and its
    // contact part, which happens once from the summed weight.
    std::printf ("        [blend] worst middle/end rms ratio %.6f\n", worst);
    CHECK (worst < 1.1);
}

TEZLA_TEST (blending_an_exciter_with_itself_is_that_exciter_at_every_position)
{
    // The invariant that says the weights are a **partition** rather than two
    // independent gains: with the same exciter in both slots, every blend
    // position has to render the note that exciter renders alone, because the
    // two halves sum to the whole.
    //
    // It is the sharpest test here and it caught **two** real bugs, neither of
    // which any of the tests above could see. Both slots used to ping the
    // gate, so mallet-into-mallet at 0.5 opened it half as far and this read
    // 2.4x rather than 1; and both slots used to call `roll_.trigger`, so
    // roll-into-roll restarted the bouncing-ball clock and then rolled at the
    // *second* slot's weight rather than at the sum -- 9.192e-02 of difference
    // on a note whose samples are of order 1e-2, which is to say a different
    // note. The pattern in both is the same: anything not linear in the weight
    // happens once, from the summed weight, not once per slot.
    //
    // Not asserted bit-exact everywhere, and the reason is arithmetic rather
    // than design: `0.5a + 0.5a` is exactly `a`, but `0.25a + 0.75a` need not
    // be, because 0.75a rounds. So the tolerance is relative and tight, and
    // 0.5 is checked bit for bit separately.
    double worst = 0.0;

    for (const auto which : { Exciter::Mallet, Exciter::Pluck, Exciter::Roll, Exciter::Bow })
    {
        auto settings = baseSettings();
        settings.exciter = which;
        settings.exciterB = which;
        settings.exciterBlend = 0.0;

        const auto alone = playNote (settings, 0.9, 24000);
        const double reference = rmsOf (alone);

        CHECK (reference > 0.0);

        for (const double t : { 0.1, 0.25, 0.5, 0.75, 0.9 })
        {
            settings.exciterBlend = t;

            const auto blended = playNote (settings, 0.9, 24000);

            double difference = 0.0;

            for (std::size_t i = 0; i < alone.size(); ++i)
                difference = std::max (difference, std::abs (blended[i] - alone[i]));

            worst = std::max (worst, difference);

            // ...and at the halfway point, exactly: two halves of a power of
            // two add back to the whole with no rounding at all.
            if (t == 0.5)
                for (std::size_t i = 0; i < alone.size(); ++i)
                    CHECK (blended[i] == alone[i]);
        }
    }

    // Measured worst absolute sample difference across all four exciters and
    // five positions: 1.249e-16, against notes whose samples are of order
    // 1e-2. That is the rounding in `0.75a` and nothing else.
    std::printf ("        [blend] self-blend worst sample difference %.3e\n", worst);
    CHECK (worst < 1.0e-14);
}

// ---------------------------------------------------------------------------
// Velocity picks the hardness
// ---------------------------------------------------------------------------

TEZLA_TEST (a_velocity_amount_of_zero_leaves_hardness_exactly_where_the_knob_is)
{
    // `h * (1 - 0) + v * 0` is `h * 1.0 + 0.0`, and both of those are exact --
    // which is the whole reason the law is written as a lerp rather than as
    // `h + amount * (v - h)`. Checked at velocities either side of the knob,
    // since a law that accidentally used velocity would only show up on one.
    for (const double hardness : { 0.0, 0.25, 0.5, 0.9, 1.0 })
        for (const double velocity : { 0.1, 0.5, 1.0 })
        {
            auto reference = baseSettings();
            reference.hardness = hardness;
            // ...and no hardnessVelocity at all.

            auto with = reference;
            with.hardnessVelocity = 0.0;

            const auto before = playNote (reference, velocity, 12000);
            const auto after = playNote (with, velocity, 12000);

            for (std::size_t i = 0; i < before.size(); ++i)
                CHECK (before[i] == after[i]);
        }
}

TEZLA_TEST (the_hardness_law_is_exact_at_both_ends_across_the_whole_midi_grid)
{
    // The law itself, swept, rather than inferred from a rendered note.
    //
    // **Why this test exists**: `h * (1 - t) + v * t` and `h + t * (v - h)`
    // are the same lerp and are *not* the same arithmetic. The second is exact
    // at t = 0 for every input, and at t = 1 only when `v - h` happens to be
    // representable. Substituting it left every other test here green -- a
    // four-million-pair random search over [0, 1) also finds no difference,
    // because random doubles are dense enough that the subtraction is exact.
    //
    // The **quantised** values are where it bites, and those are the ones a
    // keyboard actually sends: over the 128 MIDI velocities against a
    // thousand knob positions, `h + 1.0 * (v - h)` differs from `v` on 20579
    // of 128128 pairs. One ulp each time, and inaudible -- but "the neutral
    // setting is exact" is a claim about arithmetic, and a claim that holds
    // only for the values you happened to test is not the claim.
    //
    // Break-checked: the substitution fails 4141 of these comparisons, and
    // reversing the law's sign instead fails the brightness test below.
    MalleusVoice voice;
    voice.prepare (kRate);

    auto settings = baseSettings();

    int checked = 0;

    for (int knob = 0; knob <= 200; ++knob)
    {
        settings.hardness = static_cast<double> (knob) / 200.0;

        for (int step = 0; step <= 127; ++step)
        {
            const double velocity = static_cast<double> (step) / 127.0;

            // Amount 0: the knob, exactly.
            settings.hardnessVelocity = 0.0;
            voice.noteOn (57, 220.0, velocity, 1, settings,
                          tezla::dsp::scales::twelveToneEqual(), 0);
            CHECK (voice.getStrikeHardness() == settings.hardness);

            // Amount 1: the velocity, exactly.
            settings.hardnessVelocity = 1.0;
            voice.noteOn (57, 220.0, velocity, 1, settings,
                          tezla::dsp::scales::twelveToneEqual(), 0);
            CHECK (voice.getStrikeHardness() == velocity);

            ++checked;
        }
    }

    CHECK (checked == 201 * 128);
}

TEZLA_TEST (velocity_makes_the_strike_brighter_by_the_amount_asked_for)
{
    // The playability claim: a soft hit is felt and a hard hit is stick. At
    // full amount velocity *is* the hardness, so a note at velocity 0.1 has
    // to be the 0.1 mallet and one at 1.0 the 1.0 mallet -- and the way to
    // check that is against the lone-knob notes, not against each other.
    auto settings = baseSettings();
    settings.exciter = Exciter::Mallet;

    std::printf ("        [hard]  vel   amt 0     amt 0.5   amt 1.0   knob=vel\n");

    for (const double velocity : { 0.1, 0.4, 0.7, 1.0 })
    {
        double reading[3] {};
        int column = 0;

        for (const double amount : { 0.0, 0.5, 1.0 })
        {
            settings.hardnessVelocity = amount;
            settings.hardness = 0.5;

            reading[column++] = centroidHz (playNote (settings, velocity, 24000), 0, kStrikeWindow);
        }

        // The same note with the knob set to the velocity and no amount at
        // all: at amount 1 the two must agree, because the law says the
        // velocity has become the knob.
        auto direct = baseSettings();
        direct.exciter = Exciter::Mallet;
        direct.hardness = velocity;
        direct.hardnessVelocity = 0.0;

        const double knobbed = centroidHz (playNote (direct, velocity, 24000), 0, kStrikeWindow);

        std::printf ("        [hard] %4.2f  %8.1f  %8.1f  %8.1f  %8.1f\n",
                     velocity, reading[0], reading[1], reading[2], knobbed);

        // At full amount the velocity is the hardness, exactly.
        CHECK (std::abs (reading[2] - knobbed) < 1.0e-9);

        // ...and half amount sits between the knob and the velocity.
        const double low = std::min (reading[0], reading[2]);
        const double high = std::max (reading[0], reading[2]);

        CHECK (reading[1] >= low - 1.0e-9);
        CHECK (reading[1] <= high + 1.0e-9);
    }
}

TEZLA_TEST (harder_is_brighter_across_the_velocity_range_and_the_soft_end_is_flat)
{
    // The direction, swept rather than sampled. At full amount velocity *is*
    // the hardness, so the strike gets brighter as it gets harder -- 215.8 Hz
    // at velocity 0.1 to 541.8 Hz at 1.0, a factor of 2.51.
    //
    // **It is not monotone at the very bottom, and that is the object rather
    // than a bug.** Velocity 0.1 reads 215.825 and 0.2 reads 215.655 -- a dip
    // of 0.08%. Hardness 0.1 and 0.2 are contact times of 5.8 ms and 4.2 ms,
    // whose first spectral nulls sit at 345 Hz and 476 Hz; on a 220 Hz object
    // those land either side of the second partial, so which partials survive
    // changes in a way the centroid does not read monotonically. The mechanism
    // underneath is strictly monotone and is pinned on the weights themselves
    // in test_Exciters.cpp. This asserts the audible claim: from 0.3 up it
    // climbs at every step, and across the range it more than doubles.
    auto settings = baseSettings();
    settings.exciter = Exciter::Mallet;
    settings.hardnessVelocity = 1.0;

    double first = 0.0;
    double last = 0.0;
    double previous = 0.0;
    int steps = 0;

    for (int v = 1; v <= 10; ++v)
    {
        const double velocity = static_cast<double> (v) / 10.0;
        const double centroid = centroidHz (playNote (settings, velocity, 24000),
                                            0, kStrikeWindow);

        if (v == 1)
            first = centroid;

        if (v >= 3)
            CHECK (centroid > previous);

        previous = centroid;
        last = centroid;
        ++steps;
    }

    CHECK (steps == 10);

    CHECK_NEAR (first, 215.825, 0.01);
    CHECK_NEAR (last, 541.826, 0.01);
    CHECK (last / first > 2.4);
}
