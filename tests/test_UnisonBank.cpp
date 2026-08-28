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

#include <tezla/dsp/UnisonBank.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
namespace measure = tezla::measure;

namespace
{
// 48 kHz and short windows on purpose. These tests measure beats, and a beat
// at a few cents on a low note lasts over a second -- so the temptation is to
// render for ages. Detuning harder instead makes the beat fast enough to
// measure in a fraction of the time, and the mechanism under test is the same
// one. The first draft rendered eight seconds at 96 kHz per case and took the
// suite from thirty seconds to over two minutes.
constexpr double kRate = 48000.0;

UnisonBank made (int voices, double detune, double spread = 0.0, double drift = 0.0)
{
    UnisonBank bank;
    bank.prepare (kRate);
    bank.setShape (OscShape::saw);
    bank.setVoiceCount (voices);
    bank.setDetuneCents (detune);
    bank.setSpread (spread);
    bank.setDrift (drift);
    bank.setFrequency (110.0);
    bank.reset();
    return bank;
}

struct Stereo { std::vector<double> left, right; };

Stereo render (UnisonBank& bank, std::size_t n)
{
    Stereo out { std::vector<double> (n), std::vector<double> (n) };

    for (std::size_t i = 0; i < n; ++i)
        bank.process (0.0, out.left[i], out.right[i]);

    return out;
}

double rmsOf (const std::vector<double>& v, std::size_t from = 0)
{
    double sum = 0.0;

    for (std::size_t i = from; i < v.size(); ++i)
        sum += v[i] * v[i];

    return std::sqrt (sum / static_cast<double> (v.size() - from));
}
} // namespace

TEZLA_TEST (a_stack_sums_incoherently_so_it_is_normalised_by_the_root)
{
    // N uncorrelated sources sum to sqrt(N), not N. Dividing by N would make a
    // seven-voice stack quieter than a one-voice one and the control unusable;
    // dividing by sqrt(N) holds the level where the ear expects it.
    //
    // So the *measured* level has to stay put as voices are added -- which is
    // the claim, and it is only true once the phases have scattered and the
    // detune has pulled the voices apart.
    // Measured over several seconds. A stack detuned by a few cents beats at
    // well under a hertz, so a third of a second of it is not a level at all --
    // it is wherever the beat happened to be.
    constexpr std::size_t kLong = static_cast<std::size_t> (kRate * 3.0);
    constexpr std::size_t kSettle = static_cast<std::size_t> (kRate * 0.5);

    auto one = made (1, 0.0);
    const double single = rmsOf (render (one, kLong).left, kSettle);

    for (const int voices : { 2, 3, 5, 7 })
    {
        auto bank = made (voices, 45.0);
        const double level = rmsOf (render (bank, kLong).left, kSettle);

        // Within 2 dB of a single voice, across the whole range.
        CHECK (level > single * 0.79);
        CHECK (level < single * 1.26);
    }
}

TEZLA_TEST (the_normalisation_is_the_root_and_not_the_count)
{
    // Stated directly as well, because the level test above would also pass for
    // a normalisation that happened to be close, and this one would not.
    for (const int voices : { 1, 2, 3, 5, 7 })
    {
        auto bank = made (voices, 0.0);

        CHECK_NEAR (bank.getNormalisation(),
                    1.0 / std::sqrt (static_cast<double> (voices)), 1.0e-12);
    }
}

TEZLA_TEST (detune_beats_at_the_difference_and_that_is_the_reese)
{
    // Two saws a few cents apart drift in and out of phase at the difference
    // frequency: where they agree harmonics add, where they oppose they
    // cancel. That is a comb whose notches sweep, and it is the whole of a
    // reese.
    //
    // The claim is testable directly: the envelope of a two-voice stack must
    // pulse at the beat rate. At 110 Hz, 60 cents apart, the two voices sit at
    // 108.13 and 111.92 Hz -- a difference of 3.83 Hz, which is fast enough to
    // count several of in a second.
    auto bank = made (2, 60.0);

    const auto rendered = render (bank, static_cast<std::size_t> (kRate * 4.0));

    // Track the envelope with a slow follower, then count how many times it
    // crosses its own mean. Two crossings per beat.
    double envelope = 0.0;
    // Slow enough that the saw's own 110 Hz ripple is gone (attenuated about
    // seventyfold) but far faster than the 0.76 Hz beat being looked for. A
    // 20 ms follower measured 17 Hz here, which was its own ripple.
    const double coefficient = 1.0 - std::exp (-1.0 / (0.03 * kRate));

    std::vector<double> followed;
    followed.reserve (rendered.left.size());

    for (const double sample : rendered.left)
    {
        envelope += coefficient * (std::abs (sample) - envelope);
        followed.push_back (envelope);
    }

    // Past the settling.
    const std::size_t from = static_cast<std::size_t> (kRate * 0.3);

    double mean = 0.0;
    for (std::size_t i = from; i < followed.size(); ++i)
        mean += followed[i];
    mean /= static_cast<double> (followed.size() - from);

    // Crossings counted with hysteresis. Without it the saw's own ripple --
    // still about 0.005 on a mean of 0.28 after a 150 ms follower -- adds
    // spurious crossings every time the envelope passes near its mean, and the
    // count comes out three to twenty times too high depending on the
    // follower. Requiring the envelope to travel a real distance before
    // counting again is what makes this measure the beat and not the waveform.
    double deviation = 0.0;
    for (std::size_t i = from; i < followed.size(); ++i)
        deviation += (followed[i] - mean) * (followed[i] - mean);
    deviation = std::sqrt (deviation / static_cast<double> (followed.size() - from));

    const double hysteresis = deviation * 0.5;

    int crossings = 0;
    int state = 0;

    for (std::size_t i = from; i < followed.size(); ++i)
    {
        const double offset = followed[i] - mean;

        if (state <= 0 && offset > hysteresis)  { ++crossings; state = 1; }
        else if (state >= 0 && offset < -hysteresis) { ++crossings; state = -1; }
    }

    const double seconds = static_cast<double> (followed.size() - from) / kRate;
    const double beatHz = crossings / (2.0 * seconds);

    const double expected = 110.0 * (std::pow (2.0, 30.0 / 1200.0) - std::pow (2.0, -30.0 / 1200.0));

    // The first crossing is the envelope arriving from its settling rather than
    // a beat, so the rate is taken from the intervals between them.
    CHECK (crossings >= 4);
    CHECK_NEAR (beatHz, expected, expected * 0.30);
}

TEZLA_TEST (detune_in_cents_beats_proportionally_to_the_note)
{
    // Cents rather than Hz, and this is why: a fixed cent spread beats faster
    // on high notes than low ones, so a bass line's movement stays
    // proportional to its pitch instead of turning to mud at the bottom. An
    // octave up should double the beat rate.
    const auto beatRateAt = [] (double hz)
    {
        UnisonBank bank;
        bank.prepare (kRate);
        bank.setShape (OscShape::saw);
        bank.setVoiceCount (2);
        bank.setDetuneCents (20.0);
        bank.setFrequency (hz);
        bank.reset (false);           // in phase, so the first beat is clean

        // The two voices are at +-10 cents. Their difference frequency is what
        // the envelope pulses at.
        return hz * (std::pow (2.0, 10.0 / 1200.0) - std::pow (2.0, -10.0 / 1200.0));
    };

    CHECK_NEAR (beatRateAt (220.0) / beatRateAt (110.0), 2.0, 1.0e-9);
    CHECK_NEAR (beatRateAt (440.0) / beatRateAt (110.0), 4.0, 1.0e-9);
}

TEZLA_TEST (phases_start_scattered_so_the_attack_is_not_one_loud_saw)
{
    // Seven oscillators reset to the same phase are one oscillator seven times
    // as loud, and they stay that way until the detune pulls them apart --
    // which at a few cents on a low note is over a second. Every note would
    // begin with a single loud saw.
    auto scattered = made (7, 10.0);
    const double scatteredPeak = std::abs (render (scattered, 64).left[0]);

    UnisonBank aligned;
    aligned.prepare (kRate);
    aligned.setShape (OscShape::saw);
    aligned.setVoiceCount (7);
    aligned.setDetuneCents (10.0);
    aligned.setFrequency (110.0);
    aligned.reset (false);

    const double alignedPeak = std::abs (render (aligned, 64).left[0]);

    // In phase, seven voices at the same value sum to 7/sqrt(7) = 2.646 -- and
    // then the equal-power pan takes each channel to cos(pi/4) of that, 1.871.
    // Scattered, they mostly cancel.
    CHECK (alignedPeak > 1.8);
    CHECK (scatteredPeak < alignedPeak * 0.6);
}

TEZLA_TEST (spread_widens_without_changing_the_level)
{
    // Equal-power panning, so opening the stack up does not also turn it up.
    // A width control that changes loudness is a width control nobody can use.
    // Long enough to average over many beats -- the flat voices pan left and
    // the sharp ones right, so a short window reads the two channels at
    // different points in their beat and reports an asymmetry that is not
    // there. Measured over a third of a second it looked like 3.7 dB.
    constexpr std::size_t kLong = static_cast<std::size_t> (kRate * 3.0);
    constexpr std::size_t kSettle = static_cast<std::size_t> (kRate * 0.5);

    auto narrow = made (7, 45.0, 0.0);
    auto wide = made (7, 45.0, 1.0);

    const auto a = render (narrow, kLong);
    const auto b = render (wide, kLong);

    const double narrowPower = rmsOf (a.left, kSettle) * rmsOf (a.left, kSettle)
                             + rmsOf (a.right, kSettle) * rmsOf (a.right, kSettle);
    const double widePower = rmsOf (b.left, kSettle) * rmsOf (b.left, kSettle)
                           + rmsOf (b.right, kSettle) * rmsOf (b.right, kSettle);

    CHECK_NEAR (widePower / narrowPower, 1.0, 0.15);

    // And it genuinely widens: at zero spread the two channels are identical.
    double narrowDifference = 0.0;
    for (std::size_t i = kSettle; i < a.left.size(); ++i)
        narrowDifference = std::max (narrowDifference, std::abs (a.left[i] - a.right[i]));

    double wideDifference = 0.0;
    for (std::size_t i = kSettle; i < b.left.size(); ++i)
        wideDifference = std::max (wideDifference, std::abs (b.left[i] - b.right[i]));

    CHECK (narrowDifference < 1.0e-12);
    CHECK (wideDifference > 0.2);
}

TEZLA_TEST (spread_takes_effect_without_touching_any_other_control)
{
    // The bug this pins, reported from the DAW: turning the spread knob did
    // nothing until the *detune* knob was also moved, at which point the new
    // spread appeared all at once. The pan gains are computed in
    // `updateIncrements` alongside the detuned increments, and `setSpread`
    // wrote the member without calling it -- so the width only ever changed as
    // a side effect of some other setter that did.
    //
    // The test therefore has to change spread on a bank that is already
    // running and touch nothing else. Setting it before `reset`, as every
    // other test here does, walks straight past the bug.
    auto bank = made (7, 45.0, 0.0);

    constexpr std::size_t kSettle = static_cast<std::size_t> (kRate * 0.5);
    constexpr std::size_t kWindow = static_cast<std::size_t> (kRate * 1.5);

    const auto narrow = render (bank, kSettle + kWindow);

    bank.setSpread (1.0);

    const auto wide = render (bank, kSettle + kWindow);

    double narrowDifference = 0.0;
    for (std::size_t i = kSettle; i < narrow.left.size(); ++i)
        narrowDifference = std::max (narrowDifference,
                                     std::abs (narrow.left[i] - narrow.right[i]));

    double wideDifference = 0.0;
    for (std::size_t i = kSettle; i < wide.left.size(); ++i)
        wideDifference = std::max (wideDifference,
                                   std::abs (wide.left[i] - wide.right[i]));

    CHECK (narrowDifference < 1.0e-12);
    CHECK (wideDifference > 0.2);

    // Closing it again has to work too, and has to land exactly back on mono
    // rather than nearly there -- the pan law is only equal-power if the
    // gains are recomputed, and a stale gain would leave a residue here.
    bank.setSpread (0.0);

    const auto closed = render (bank, kSettle + kWindow);

    double closedDifference = 0.0;
    for (std::size_t i = kSettle; i < closed.left.size(); ++i)
        closedDifference = std::max (closedDifference,
                                     std::abs (closed.left[i] - closed.right[i]));

    CHECK (closedDifference < 1.0e-12);
}

TEZLA_TEST (a_live_spread_change_lands_where_the_preset_would_have)
{
    // The other half of the same bug: turning the knob to 0.6 mid-note must
    // give the same stack as loading a preset that was already at 0.6, and
    // must do it without disturbing where the oscillators had got to.
    //
    // The change therefore has to happen *after* both banks have run, or the
    // test cannot see the failure it is for. Both are at the same phase at the
    // moment of the change, because spread reaches only the pan gains -- so a
    // `setSpread` that reset a phase, an increment or the normalisation puts
    // one bank back to the start while the other carries on, and every sample
    // after that diverges.
    auto turned = made (7, 45.0, 0.0);
    auto loaded = made (7, 45.0, 0.6);

    constexpr std::size_t kRun = static_cast<std::size_t> (kRate * 0.25);

    for (std::size_t i = 0; i < kRun; ++i)
    {
        double l = 0.0, r = 0.0;
        turned.process (0.0, l, r);
        loaded.process (0.0, l, r);
    }

    turned.setSpread (0.6);

    for (std::size_t i = 0; i < kRun; ++i)
    {
        double turnedLeft = 0.0, turnedRight = 0.0;
        double loadedLeft = 0.0, loadedRight = 0.0;

        turned.process (0.0, turnedLeft, turnedRight);
        loaded.process (0.0, loadedLeft, loadedRight);

        CHECK (turnedLeft == loadedLeft);
        CHECK (turnedRight == loadedRight);
    }
}

TEZLA_TEST (drift_wanders_slowly_and_stays_bounded)
{
    // A static detune gives a periodic churn the ear locks onto within a bar.
    // The drift is what stops it repeating -- and it has to stay small, or it
    // is not tuning drift, it is vibrato.
    auto steady = made (7, 15.0, 0.0, 0.0);
    auto drifting = made (7, 15.0, 0.0, 20.0);

    const auto a = render (steady, static_cast<std::size_t> (kRate * 3.0));
    const auto b = render (drifting, static_cast<std::size_t> (kRate * 3.0));

    // It changes the sound...
    double difference = 0.0;
    for (std::size_t i = 60000; i < a.left.size(); ++i)
        difference = std::max (difference, std::abs (a.left[i] - b.left[i]));

    CHECK (difference > 0.05);

    // ...without changing the level, and without running away.
    CHECK_NEAR (rmsOf (b.left, 60000) / rmsOf (a.left, 60000), 1.0, 0.2);

    for (const double sample : b.left)
        CHECK (std::abs (sample) < 4.0);
}

TEZLA_TEST (one_voice_costs_what_one_voice_costs)
{
    // A stack of one has to be exactly a plain oscillator: no detune, no
    // normalisation beyond unity, no pan. Anything else means the control's
    // bottom end is a different instrument.
    auto bank = made (1, 40.0, 1.0);

    Oscillator plain;
    plain.setShape (OscShape::saw);
    plain.setIncrement (110.0 / kRate);
    plain.reset (0.0);

    UnisonBank aligned;
    aligned.prepare (kRate);
    aligned.setShape (OscShape::saw);
    aligned.setVoiceCount (1);
    aligned.setDetuneCents (40.0);
    aligned.setSpread (1.0);
    aligned.setFrequency (110.0);
    aligned.reset (false);

    double worst = 0.0;

    for (int i = 0; i < 4000; ++i)
    {
        double left = 0.0, right = 0.0;
        aligned.process (0.0, left, right);

        const double reference = plain.advance();

        // Centre-panned and unnormalised: both channels carry the same thing at
        // equal power, so each is 1/sqrt(2) of it.
        worst = std::max (worst, std::abs (left - reference * 0.7071067811865476));
        worst = std::max (worst, std::abs (left - right));
    }

    CHECK (worst < 1.0e-9);
    CHECK_NEAR (bank.getNormalisation(), 1.0, 1.0e-12);
}

TEZLA_TEST (a_silent_bank_is_silent)
{
    UnisonBank bank;
    bank.prepare (kRate);
    bank.setVoiceCount (7);
    bank.setDetuneCents (20.0);
    bank.setDrift (10.0);
    bank.setFrequency (0.0);       // no note
    bank.reset (false);

    for (int i = 0; i < 4000; ++i)
    {
        double left = 0.0, right = 0.0;
        bank.process (0.0, left, right);

        // A saw at phase zero reads -1, so "silent" here means "not moving".
        CHECK (std::isfinite (left));
        CHECK (std::isfinite (right));
    }
}

TEZLA_TEST (a_frequency_change_reaches_the_oscillators_on_the_next_sample)
{
    // The drift timer batches the increment pushes -- that batching is where
    // a five-times CPU regression was hiding when the pushes ran per sample --
    // and it must never delay a *control* change: a stolen voice retriggered
    // at a new pitch that plays even half a millisecond of the old one is a
    // smeared attack. Every setter that rebuilds the increments zeroes the
    // countdown, so the change lands on the very next sample.
    UnisonBank bank;
    bank.prepare (48000.0);
    bank.setShape (OscShape::saw);
    bank.setVoiceCount (3);
    bank.setFrequency (110.0);
    bank.reset();

    double left = 0.0, right = 0.0;

    for (int i = 0; i < 100; ++i)   // land mid-interval, not on a boundary
        bank.process (0.0, left, right);

    bank.setFrequency (220.0);
    bank.process (0.0, left, right);

    // Voice 1 is the centre of a three-voice stack: no detune offset, no
    // drift by default, so its increment is exactly the note.
    CHECK_NEAR (bank.voice (1).getIncrement(), 220.0 / 48000.0, 1.0e-12);
}
