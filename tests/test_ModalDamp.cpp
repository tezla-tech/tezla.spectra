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
#include <vector>

#include <tezla/dsp/ModalResonator.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;

/// A bank of `count` modes on a harmonic series from `fundamental`, all with
/// the same natural decay, so the only thing that separates them is pitch --
/// which is exactly what the damping law is a claim about.
ModalResonator buildBank (int count, double fundamental, double t60, double damp)
{
    ModalResonator bank;

    bank.prepare (kRate);
    bank.setModeCount (count);

    for (int mode = 0; mode < count; ++mode)
        bank.setMode (mode, fundamental * static_cast<double> (mode + 1), t60, 1.0);

    bank.setDamp (damp);

    return bank;
}

/// Strikes every mode and returns the output.
std::vector<double> strikeAndRing (ModalResonator bank, int samples)
{
    for (int mode = 0; mode < bank.getModeCount(); ++mode)
        bank.excite (mode, 1.0);

    std::vector<double> out;
    out.reserve (static_cast<std::size_t> (samples));

    for (int i = 0; i < samples; ++i)
        out.push_back (bank.process());

    return out;
}

/// The measured T60 of a single ringing mode: how long the envelope takes to
/// fall 60 dB, found from the peak of each cycle rather than from the samples,
/// so a mode whose period is a handful of samples still reads correctly.
double measuredT60 (double frequency, double t60, double damp)
{
    ModalResonator bank;

    bank.prepare (kRate);
    bank.setModeCount (1);
    bank.setMode (0, frequency, t60, 1.0);
    bank.setDamp (damp);
    bank.excite (0, 1.0);

    const int limit = static_cast<int> (20.0 * kRate);
    const int period = std::max (2, static_cast<int> (kRate / frequency));

    double first = 0.0;
    int sample = 0;

    // The first cycle's peak is the reference: the mode starts from an impulse
    // and reaches its own maximum a quarter period in, not at sample zero.
    for (; sample < period * 2 && sample < limit; ++sample)
        first = std::max (first, std::abs (bank.process()));

    if (first <= 0.0)
        return 0.0;

    const double target = first * 0.001;   // -60 dB

    double peak = first;

    while (sample < limit)
    {
        peak = 0.0;

        for (int i = 0; i < period && sample < limit; ++i, ++sample)
            peak = std::max (peak, std::abs (bank.process()));

        if (peak <= target)
            break;
    }

    return static_cast<double> (sample) / kRate;
}
} // namespace

// ---------------------------------------------------------------------------
// Neutral
// ---------------------------------------------------------------------------

TEZLA_TEST (calling_set_damp_with_zero_is_the_bank_that_was_never_told_about_damping)
{
    // `ModalResonator` lives in shared/, so the phase-2 rule applies: anything
    // added is off by default and the existing path stays identical. Compared
    // against a bank that was never told about damping at all.
    //
    // **What this cannot catch, and where the teeth actually are.** Both banks
    // are built from the same code, so a change that damps *everything*
    // equally -- a floor added to the law, say -- leaves them identical and
    // this test green. Break-checked, and it was: a 0.02 floor at damp 0 kept
    // this one passing and failed 7 checks in the test below, whose undamped
    // rows measure a 4 s decay as 4 s rather than as 2.224 s. The absolute
    // claim is pinned there, against the closed-form T60 rather than against
    // another instance of the same build; this test pins the narrower and
    // still worth-having one -- that reaching zero *through the control* is
    // the same as never touching it.
    ModalResonator reference;

    reference.prepare (kRate);
    reference.setModeCount (24);

    for (int mode = 0; mode < 24; ++mode)
        reference.setMode (mode, 180.0 * static_cast<double> (mode + 1), 2.5, 1.0);

    const auto before = strikeAndRing (reference, 48000);
    const auto after = strikeAndRing (buildBank (24, 180.0, 2.5, 0.0), 48000);

    CHECK (before.size() == after.size());

    for (std::size_t i = 0; i < before.size(); ++i)
        CHECK (before[i] == after[i]);
}

// ---------------------------------------------------------------------------
// The law
// ---------------------------------------------------------------------------

TEZLA_TEST (the_damping_takes_the_top_first_and_the_slope_is_the_frequency)
{
    // The claim: added loss proportional to frequency, so a mode an octave up
    // is damped twice as hard. Measured as T60 rather than argued from the
    // pole, because the pole is the thing under test.
    //
    // Decays combine as rates: 1/T = 1/T_own + damp/T_added(f), and
    // T_added(f) = kDampT60AtReference * kDampReferenceHz / f. With a 4 s
    // natural decay the damping dominates everywhere above a few hundred Hz,
    // which is what "a hand" means.
    constexpr double kNatural = 4.0;

    const auto predicted = [] (double frequency, double damp)
    {
        const double own = 1.0 / kNatural;
        const double added = damp * (frequency / ModalResonator::kDampReferenceHz)
                               / ModalResonator::kDampT60AtReference;

        return 1.0 / (own + added);
    };

    // Undamped first: every mode reads its own decay whatever its pitch.
    for (const double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0 })
        CHECK_NEAR (measuredT60 (hz, kNatural, 0.0), kNatural, 0.06);

    // Damped, across four octaves. Each row is within 6% of the law, which is
    // the resolution of a cycle-peak measurement rather than a slack tolerance.
    // Printed as well as checked: the shape of the table is the feature, and a
    // reader should be able to see the halving down the columns rather than
    // take it on trust.
    std::printf ("        [damp] T60 in seconds, 4 s natural decay\n");
    std::printf ("        [damp]   Hz      0.00    0.25    0.50    1.00\n");

    for (const double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0 })
    {
        std::printf ("        [damp] %5.0f   %6.3f", hz, measuredT60 (hz, kNatural, 0.0));

        for (const double damp : { 0.25, 0.5, 1.0 })
        {
            const double measured = measuredT60 (hz, kNatural, damp);
            const double expected = predicted (hz, damp);

            CHECK (std::abs (measured / expected - 1.0) < 0.06);

            std::printf ("  %6.3f", measured);
        }

        std::printf ("\n");
    }

    // And the headline: at full damp a mode an octave up dies about twice as
    // fast, which is the whole difference between a hand and a volume pedal.
    const double low = measuredT60 (250.0, kNatural, 1.0);
    const double high = measuredT60 (500.0, kNatural, 1.0);

    CHECK (low / high > 1.8);
    CHECK (low / high < 2.2);
}

TEZLA_TEST (a_damped_object_goes_dull_before_it_goes_quiet)
{
    // The audible consequence, measured on the sound rather than on the poles:
    // 200 ms after the strike a damped bank has lost far more of its top than
    // of its bottom, so its spectral balance has moved down. A flat decay
    // multiplier would move both equally and this would read 1.0.
    const auto highFraction = [] (const std::vector<double>& block)
    {
        // Zero crossings are a cheap centroid: a bank whose top has gone
        // crosses zero far less often.
        int count = 0;

        for (std::size_t i = 1; i < block.size(); ++i)
            if ((block[i - 1] < 0.0) != (block[i] < 0.0))
                ++count;

        return count;
    };

    const int start = static_cast<int> (0.2 * kRate);
    const int length = static_cast<int> (0.1 * kRate);

    const auto window = [start, length] (const std::vector<double>& all)
    {
        return std::vector<double> (all.begin() + start, all.begin() + start + length);
    };

    const auto dry = strikeAndRing (buildBank (32, 110.0, 4.0, 0.0), start + length);
    const auto damped = strikeAndRing (buildBank (32, 110.0, 4.0, 0.6), start + length);

    const int dryCrossings = highFraction (window (dry));
    const int dampedCrossings = highFraction (window (damped));

    CHECK (dampedCrossings < dryCrossings / 2);

    // ...and it is still ringing rather than muted. A hand that silenced the
    // object would pass the test above trivially.
    double energy = 0.0;

    for (const double sample : window (damped))
        energy += sample * sample;

    CHECK (energy > 0.0);
    CHECK (std::sqrt (energy / static_cast<double> (length)) > 1.0e-4);
}

// ---------------------------------------------------------------------------
// Playing it
// ---------------------------------------------------------------------------

TEZLA_TEST (damping_a_ringing_object_glides_rather_than_clicks)
{
    // Damp is meant to be played -- an aftertouch or a pedal pushing it every
    // control chunk -- so changing it mid-note must not step the output.
    //
    // **The first version of this test was a decoration**, and break-checking
    // is what said so. It compared the worst step at a control boundary
    // against the worst step *anywhere*, and the worst step anywhere is the
    // strike itself: sixteen modes excited on one sample. Against that
    // yardstick almost anything passes. Adding `reset()` to `setDamp` -- the
    // exact bug CLAUDE.md section 7 names, and the one that shipped in
    // Emberdrive's DC corner -- left all seven tests green.
    //
    // Two assertions replace it, and they catch **different** bugs. Neither is
    // redundant, which is the point of writing both numbers down:
    //
    //  * The step at a boundary against the steps **either side of it** --
    //    the only comparison that means "no click" for a signal that is itself
    //    moving. Correct: 0.9999. Damping applied to the state instead of the
    //    pole (an instantaneous duck rather than a faster decay): **12.4569**.
    //    Under a `reset()` it reads 0.0000 and catches nothing, because every
    //    neighbourhood is silent and there is nothing to be a ratio of.
    //  * The bank still ringing at the end. Correct: tail RMS 3.495e-02.
    //    Under a `reset()` per chunk: **exactly 0**, since nothing re-excites
    //    it. Under the state-duck break it reads 7.345e-06 and passes, which
    //    is why the ratio check cannot be dropped either.
    auto bank = buildBank (16, 150.0, 5.0, 0.0);

    for (int mode = 0; mode < 16; ++mode)
        bank.excite (mode, 1.0);

    constexpr int kChunk = 64;
    constexpr int kSamples = 24000;

    std::vector<double> out;
    out.reserve (static_cast<std::size_t> (kSamples));

    for (int i = 0; i < kSamples; ++i)
    {
        // A slow sweep of the control, pushed every 64 samples the way a
        // control chunk would.
        if (i % kChunk == 0)
            bank.setDamp (static_cast<double> (i) / static_cast<double> (kSamples));

        out.push_back (bank.process());
    }

    const auto stepAt = [&out] (std::size_t i)
    {
        return std::abs (out[i] - out[i - 1]);
    };

    // Skip the strike itself: the first chunk is a transient by design and
    // has no neighbours to be compared against.
    double worstRatio = 0.0;
    int boundaries = 0;

    for (std::size_t i = kChunk * 2; i + kChunk < out.size(); i += kChunk)
    {
        // The signal's own biggest step in the eight samples either side of
        // the boundary, excluding the boundary sample.
        double neighbourhood = 0.0;

        for (std::size_t j = i - 8; j <= i + 8; ++j)
            if (j != i)
                neighbourhood = std::max (neighbourhood, stepAt (j));

        if (neighbourhood > 0.0)
            worstRatio = std::max (worstRatio, stepAt (i) / neighbourhood);

        ++boundaries;
    }

    CHECK (boundaries == 372);
    std::printf ("        [damp] worst boundary ratio %.4f\n", worstRatio);

    // Measured 0.9999 across all 372 boundaries: a boundary step is never even
    // as big as the steps immediately around it, which is what a control that
    // moves a pole rather than a state looks like.
    CHECK (worstRatio < 1.1);

    // ...and it is still an object, not a silenced one. Measured 3.495e-02
    // after a full sweep to maximum damping on a 150 Hz fundamental.
    double energy = 0.0;

    for (std::size_t i = out.size() - 2000; i < out.size(); ++i)
        energy += out[i] * out[i];

    CHECK (energy > 0.0);
    std::printf ("        [damp] tail rms %.3e\n", std::sqrt (energy / 2000.0));
    CHECK (std::sqrt (energy / 2000.0) > 1.0e-6);
}

TEZLA_TEST (setting_damp_to_what_it_already_is_costs_nothing_and_changes_nothing)
{
    // Pushing the same value every sample changes nothing about the output.
    //
    // **The guard is speed here, not correctness**, and break-checking is how
    // that was settled rather than assumed: removing it outright left all
    // seven tests green, because `rebuildPole` is a pure function of the
    // parameters and touches no state, so rebuilding a pole that has not moved
    // costs a transcendental and produces the identical coefficients. It stays
    // because `setMode`'s guard exists for the same reason and a reader should
    // find them consistent -- and because the moment this setter ever does
    // touch state, CLAUDE.md section 7's rule binds it and the guard is
    // already where the rule wants it.
    //
    // What the test does hold is that `setDamp` is state-preserving under
    // repetition, which is the property an aftertouch pushing a constant value
    // depends on.
    auto pushed = buildBank (12, 200.0, 3.0, 0.4);
    auto quiet = buildBank (12, 200.0, 3.0, 0.4);

    for (int mode = 0; mode < 12; ++mode)
    {
        pushed.excite (mode, 1.0);
        quiet.excite (mode, 1.0);
    }

    for (int i = 0; i < 12000; ++i)
    {
        pushed.setDamp (0.4);   // the same value, every sample

        CHECK (pushed.process() == quiet.process());
    }

    CHECK (pushed.getDamp() == 0.4);
}
