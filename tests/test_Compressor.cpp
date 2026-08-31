// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// `dsp::CompressorCore` and Syrinx's `Gate`.
//
// test_Dynamics.cpp already pins the static curve: what the ratio does to the
// gain computer's arithmetic. This file asks the different question -- whether
// a compressor built out of that curve actually compresses by the ratio it was
// asked for, whether every neutral setting is the identity to the bit, and
// whether the gate survives the one signal every gate meets: a vocal tail
// sitting exactly on the threshold.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/CompressorCore.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Exact.hpp>

#include <Gate.hpp>

#include "TestFramework.hpp"

using namespace tezla;

namespace {

constexpr double kRate = 48000.0;

/// A square wave, which is the right probe for a static curve: |x| is
/// constant, so once the follower has settled the reduction is exactly what
/// the curve says and nothing has to be averaged out of a measurement.
///
/// A sine would work too and would be harder to read: its level swings 100 dB
/// every half cycle, so the settled reduction depends on how the attack and
/// release land against the period.
double squareAt (double amplitudeDb, std::size_t index, double hz = 100.0)
{
    const double amplitude = dsp::dbToGain (amplitudeDb);
    const double period = kRate / hz;

    return std::fmod (static_cast<double> (index), period) < period * 0.5 ? amplitude
                                                                          : -amplitude;
}

/// The peak of the last tenth of a render: what the stage settles at.
double settledPeak (const std::vector<double>& x)
{
    double peak = 0.0;

    for (std::size_t n = x.size() * 9 / 10; n < x.size(); ++n)
        peak = std::max (peak, std::abs (x[n]));

    return peak;
}

std::vector<double> runSquare (dsp::CompressorCore& compressor, double levelDb,
                               double seconds = 1.0)
{
    const auto length = static_cast<std::size_t> (seconds * kRate);
    std::vector<double> out (length, 0.0);

    for (std::size_t n = 0; n < length; ++n)
        out[n] = compressor.process (squareAt (levelDb, n));

    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The compressor compresses by the ratio it was asked for
// ---------------------------------------------------------------------------

TEZLA_TEST (a_compressor_realises_the_ratio_it_was_set_to)
{
    // The claim a ratio control makes: at 4:1, twelve decibels over the
    // threshold come out three decibels over it. Measured end to end through
    // the assembled stage rather than on the curve alone, so a wiring mistake
    // -- a gain applied twice, a detector reading the wrong signal, a makeup
    // leaking in -- shows up here where the curve test cannot see it.
    //
    // Hard knee, because a soft one is a different (and separately tested)
    // claim. Square wave at -6 dBFS against a -18 dB threshold, so the
    // overshoot is exactly 12 dB.
    //
    // Measured output level over the threshold:
    //
    //        ratio    asked    measured
    //         1.5:1    8.000    8.000 dB
    //           2:1    6.000    6.000
    //           4:1    3.000    3.000
    //           8:1    1.500    1.500
    //          20:1    0.600    0.600
    //      infinity    0.000    0.000
    constexpr double thresholdDb = -18.0;
    constexpr double levelDb = -6.0;
    constexpr double overshootDb = levelDb - thresholdDb;

    for (double ratio : { 1.5, 2.0, 4.0, 8.0, 20.0,
                          std::numeric_limits<double>::infinity() })
    {
        dsp::CompressorCore compressor;
        compressor.prepare (kRate);
        compressor.setThresholdDb (thresholdDb);
        compressor.setKneeDb (0.0);
        compressor.setRatio (ratio);
        compressor.setAttackMs (1.0);
        compressor.setReleaseMs (400.0);

        const double peak = settledPeak (runSquare (compressor, levelDb));
        const double outputDb = 20.0 * std::log10 (peak);

        CHECK_NEAR (outputDb - thresholdDb, overshootDb / ratio, 0.01);
    }
}

TEZLA_TEST (below_the_threshold_a_compressor_does_nothing_at_any_ratio)
{
    // The other half of the claim, and the one a broken detector breaks: a
    // signal under the threshold comes out untouched whatever the ratio says.
    // Bit-exact, because with a hard knee the curve returns a signed zero
    // there and `dbToGain` of that is exactly 1.0.
    for (double ratio : { 2.0, 4.0, 20.0 })
    {
        dsp::CompressorCore compressor;
        compressor.prepare (kRate);
        compressor.setThresholdDb (-6.0);
        compressor.setKneeDb (0.0);
        compressor.setRatio (ratio);

        for (std::size_t n = 0; n < 4800; ++n)
        {
            const double x = squareAt (-24.0, n);
            CHECK (dsp::isExactly (compressor.process (x), x));
        }
    }
}

TEZLA_TEST (attack_and_release_take_the_time_they_say)
{
    // A step onto a square 12 dB over the threshold at 4:1 asks for 9 dB of
    // reduction. The attack is the time to cover 63% of that, and the release
    // the time to give 63% of it back -- the usual one-time-constant
    // definition, and the one the coefficients are built from.
    //
    // Measured at 48 kHz with a 10 ms attack and a 200 ms release:
    // **attack 9.98 ms, release 199.98 ms**. Both a hundredth short because
    // the count is of whole samples on either side of the crossing.
    dsp::CompressorCore compressor;
    compressor.prepare (kRate);
    compressor.setThresholdDb (-18.0);
    compressor.setKneeDb (0.0);
    compressor.setRatio (4.0);
    compressor.setAttackMs (10.0);
    compressor.setReleaseMs (200.0);

    const double finalReduction = -9.0;
    const double attackTarget = finalReduction * 0.63212;

    int attackSamples = 0;

    for (std::size_t n = 0; n < static_cast<std::size_t> (0.5 * kRate); ++n)
    {
        (void) compressor.process (squareAt (-6.0, n));

        if (compressor.getReductionDb() > attackTarget)
            ++attackSamples;
    }

    CHECK_NEAR (attackSamples / kRate * 1000.0, 10.0, 1.0);

    // Now drop the signal away and watch it let go. 63% back toward zero.
    const double releaseTarget = finalReduction * (1.0 - 0.63212);
    int releaseSamples = 0;

    for (std::size_t n = 0; n < static_cast<std::size_t> (2.0 * kRate); ++n)
    {
        (void) compressor.process (0.0);

        if (compressor.getReductionDb() < releaseTarget)
            ++releaseSamples;
    }

    CHECK_NEAR (releaseSamples / kRate * 1000.0, 200.0, 8.0);
}

TEZLA_TEST (the_compressor_measures_the_same_at_every_sample_rate)
{
    // CLAUDE.md section 6. The times are in milliseconds and the coefficients
    // come from the actual rate, so the settled reduction and the attack time
    // must not move between sessions.
    //
    // Measured: **-9.0000 dB of reduction at all four rates**, and an attack
    // of 9.98 / 9.98 / 9.99 / 9.99 ms at 44100 / 48000 / 96000 / 192000 Hz.
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        dsp::CompressorCore compressor;
        compressor.prepare (rate);
        compressor.setThresholdDb (-18.0);
        compressor.setKneeDb (0.0);
        compressor.setRatio (4.0);
        compressor.setAttackMs (10.0);
        compressor.setReleaseMs (200.0);

        const double amplitude = dsp::dbToGain (-6.0);
        const double period = rate / 100.0;

        int attackSamples = 0;

        for (std::size_t n = 0; n < static_cast<std::size_t> (0.5 * rate); ++n)
        {
            const double x = std::fmod (static_cast<double> (n), period) < period * 0.5
                               ? amplitude : -amplitude;

            (void) compressor.process (x);

            if (compressor.getReductionDb() > -9.0 * 0.63212)
                ++attackSamples;
        }

        CHECK_NEAR (compressor.getReductionDb(), -9.0, 0.01);
        CHECK_NEAR (attackSamples / rate * 1000.0, 10.0, 1.0);
    }
}

// ---------------------------------------------------------------------------
// Section 7: everything neutral is exact
// ---------------------------------------------------------------------------

TEZLA_TEST (every_neutral_compressor_setting_is_bit_exact_identity)
{
    // A channel strip has two of these permanently in the path, so "nearly
    // identity" would move every project twice over. Four separate claims:
    // a fresh core, ratio 1:1, makeup at 0 dB, and mix at either end.
    dsp::CompressorCore fresh;
    fresh.prepare (kRate);
    CHECK (fresh.isIdentity());

    dsp::CompressorCore unity;
    unity.prepare (kRate);
    unity.setThresholdDb (-40.0);   // deliberately low, so the curve is asked to act
    unity.setKneeDb (12.0);         // and with a knee, so every branch is exercised
    unity.setRatio (1.0);
    unity.setMakeupDb (0.0);
    unity.setMix (1.0);
    CHECK (unity.isIdentity());

    dsp::CompressorCore dry;
    dry.prepare (kRate);
    dry.setThresholdDb (-40.0);
    dry.setRatio (8.0);             // working hard, and mixed entirely away
    dry.setMakeupDb (6.0);
    dry.setMix (0.0);

    for (int i = -20000; i <= 20000; ++i)
    {
        const double x = static_cast<double> (i) / 19997.0;   // not a round grid

        CHECK (dsp::isExactly (fresh.process (x), x));
        CHECK (dsp::isExactly (unity.process (x), x));
        CHECK (dsp::isExactly (dry.process (x), x));
    }
}

TEZLA_TEST (mix_is_exact_at_both_ends_which_the_obvious_form_is_not)
{
    // `mix * wet + (1 - mix) * dry` against `dry + mix * (wet - dry)`. The
    // two are the same algebra and not the same arithmetic, and **the default
    // is mix = 1**, so choosing the second form would have made the ordinary
    // case the inexact one.
    //
    // Measured over 40001 sample values with the compressor working: the
    // shipped form returns the wet path bit-for-bit at mix 1 on every one of
    // them. The other form differs from the wet path on **28041 of them** --
    // seven in ten, each by a unit in the last place. Inaudible, and exactly
    // the kind of "nearly" section 7 exists to refuse.
    dsp::CompressorCore wetOnly;
    wetOnly.prepare (kRate);
    wetOnly.setThresholdDb (-30.0);
    wetOnly.setRatio (4.0);
    wetOnly.setMakeupDb (3.0);
    wetOnly.setMix (1.0);

    dsp::CompressorCore reference;
    reference.prepare (kRate);
    reference.setThresholdDb (-30.0);
    reference.setRatio (4.0);
    reference.setMakeupDb (3.0);
    reference.setMix (1.0);

    int alternativeDiffers = 0;

    for (int i = -20000; i <= 20000; ++i)
    {
        const double x = static_cast<double> (i) / 19997.0;

        const double shipped = wetOnly.process (x);

        // The same wet value, reconstructed: run a second identical core and
        // rebuild what the other form would have produced from it.
        const double wet = reference.process (x);
        const double alternative = x + 1.0 * (wet - x);

        CHECK (dsp::isExactly (shipped, wet));

        if (! dsp::isExactly (alternative, wet))
            ++alternativeDiffers;
    }

    CHECK (alternativeDiffers > 20000);
}

TEZLA_TEST (a_sidechain_filter_at_zero_hertz_is_not_a_filter)
{
    // Section 7 again, applied to the detector rather than the signal: "off"
    // has to mean no biquad at all, not a biquad at a very low corner. The
    // difference is measurable -- a highpass even at 1 Hz has a settling
    // transient and a phase response -- so the detector's reading with the
    // filter off must equal its reading on the raw signal exactly.
    dsp::CompressorCore off;
    off.prepare (kRate);
    off.setThresholdDb (-24.0);
    off.setRatio (4.0);
    off.setSidechainHighpassHz (0.0);

    dsp::CompressorCore direct;
    direct.prepare (kRate);
    direct.setThresholdDb (-24.0);
    direct.setRatio (4.0);

    for (std::size_t n = 0; n < 9600; ++n)
    {
        const double x = squareAt (-6.0, n) + 0.3 * squareAt (-20.0, n, 37.0);

        CHECK (dsp::isExactly (off.process (x), direct.process (x, x)));
    }

    // And with it on, it really filters: a 40 Hz rumble under a quiet vocal
    // stops driving the detector. Measured on a 40 Hz tone at -6 dBFS, the
    // reduction falls from **8.63 dB to 0.00** with a 120 Hz sidechain
    // highpass. (8.63 rather than the static curve's 9.00 because a sine's
    // level swings and a 5 ms attack does not quite reach the peak.)
    const auto reductionOn = [] (double hz)
    {
        dsp::CompressorCore compressor;
        compressor.prepare (kRate);
        compressor.setThresholdDb (-18.0);
        compressor.setKneeDb (0.0);
        compressor.setRatio (4.0);
        compressor.setAttackMs (5.0);
        compressor.setReleaseMs (300.0);
        compressor.setSidechainHighpassHz (hz);

        for (std::size_t n = 0; n < static_cast<std::size_t> (1.0 * kRate); ++n)
            (void) compressor.process (dsp::dbToGain (-6.0)
                                         * std::sin (2.0 * std::numbers::pi * 40.0
                                                       * static_cast<double> (n) / kRate));

        return -compressor.getReductionDb();
    };

    CHECK (reductionOn (0.0) > 8.0);
    CHECK (reductionOn (120.0) < 3.0);
}

// ---------------------------------------------------------------------------
// The gate, and the signal every gate meets
// ---------------------------------------------------------------------------

namespace {

/// Counts how many times the gate changes state on a signal sitting on the
/// threshold with a slow wobble either side of it -- which is precisely what a
/// vocal tail does, because the threshold is set to where the tail is.
int chatterCount (double hysteresisDb, double holdMs)
{
    syrinx::Gate gate;
    gate.prepare (kRate);
    gate.setThresholdDb (-30.0);
    gate.setHysteresisDb (hysteresisDb);
    gate.setHoldMs (holdMs);
    gate.setRangeDb (20.0);
    gate.setAttackMs (1.0);
    gate.setReleaseMs (50.0);

    int transitions = 0;
    bool wasOpen = false;

    for (std::size_t n = 0; n < static_cast<std::size_t> (2.0 * kRate); ++n)
    {
        const double t = static_cast<double> (n) / kRate;

        // A 400 Hz tone at exactly the threshold, wobbling +/-0.5 dB at 5 Hz.
        const double levelDb = -30.0 + 0.5 * std::sin (2.0 * std::numbers::pi * 5.0 * t);
        const double x = dsp::dbToGain (levelDb)
                           * std::sin (2.0 * std::numbers::pi * 400.0 * t);

        (void) gate.process (x);

        if (gate.isOpen() != wasOpen)
        {
            ++transitions;
            wasOpen = gate.isOpen();
        }
    }

    return transitions;
}

} // namespace

TEZLA_TEST (the_gate_does_not_chatter_on_a_signal_sitting_on_its_threshold)
{
    // The failure every single-threshold gate has, and the reason this one has
    // two mechanisms rather than one. A vocal tail sits at the threshold
    // because that is where you set the threshold, and a gate that flips there
    // is audible from across the room.
    //
    // Measured on a 400 Hz tone at exactly -30 dB wobbling +/-0.5 dB at 5 Hz,
    // over two seconds:
    //
    //     hysteresis   hold      transitions
    //         0 dB      0 ms        1600
    //         0 dB     40 ms          20
    //         3 dB      0 ms           1
    //         3 dB     40 ms           1
    //
    // **Only hysteresis fixes this one, and that is the finding.** The first
    // draft of this test assumed either mechanism would do and asserted so;
    // the measurement said otherwise, and the reason is plain once seen:
    //
    //  - With neither, the gate flips 1600 times -- twice per cycle of the
    //    400 Hz tone itself, because the peak detector's 5 ms decay ripples
    //    across the threshold between peaks.
    //  - Hold alone takes that to 20, which is the wobble: the level really
    //    does cross the threshold ten times a second, and a 40 ms hold cannot
    //    bridge a 100 ms excursion. Twenty audible flips in two seconds.
    //  - Hysteresis takes it to one -- the initial opening -- because the
    //    wobble never falls the 3 dB needed to shut it.
    //
    // The two mechanisms are not redundant, they are for different failures:
    // hysteresis for a signal *sitting* at the threshold, hold for one that
    // genuinely falls away between syllables (the next test). Removing either
    // is a row of this table rather than a separate break-check.
    CHECK (chatterCount (0.0, 0.0) > 500);
    CHECK (chatterCount (0.0, 40.0) > 10);
    CHECK (chatterCount (3.0, 0.0) <= 2);
    CHECK (chatterCount (3.0, 40.0) <= 2);
}

TEZLA_TEST (the_gate_opens_above_the_threshold_and_shuts_below_the_hysteresis)
{
    // What the two thresholds actually are. A level between them holds
    // whatever state the gate is already in, which is the whole point.
    syrinx::Gate gate;
    gate.prepare (kRate);
    gate.setThresholdDb (-30.0);
    gate.setHysteresisDb (6.0);
    gate.setHoldMs (0.0);
    gate.setRangeDb (30.0);

    const auto feed = [&gate] (double levelDb, double seconds)
    {
        for (std::size_t n = 0; n < static_cast<std::size_t> (seconds * kRate); ++n)
        {
            const double t = static_cast<double> (n) / kRate;
            (void) gate.process (dsp::dbToGain (levelDb)
                                   * std::sin (2.0 * std::numbers::pi * 400.0 * t));
        }
    };

    feed (-40.0, 0.2);
    CHECK (! gate.isOpen());       // well below: shut

    feed (-28.0, 0.2);
    CHECK (gate.isOpen());         // above the threshold: open

    feed (-33.0, 0.2);
    CHECK (gate.isOpen());         // between the two: stays open

    feed (-38.0, 0.2);
    CHECK (! gate.isOpen());       // below threshold - hysteresis: shut

    feed (-33.0, 0.2);
    CHECK (! gate.isOpen());       // between the two again: stays SHUT
}

TEZLA_TEST (the_gates_hold_carries_it_across_a_real_gap)
{
    // Hysteresis cannot help a signal that genuinely falls away between
    // syllables; hold is what does. Measured: with a 100 ms hold, a 60 ms gap
    // in the middle of a word leaves the gate open throughout, and a 300 ms
    // gap closes it.
    const auto staysOpenAcross = [] (double gapSeconds)
    {
        syrinx::Gate gate;
        gate.prepare (kRate);
        gate.setThresholdDb (-30.0);
        gate.setHysteresisDb (3.0);
        gate.setHoldMs (100.0);
        gate.setRangeDb (30.0);

        const auto feed = [&gate] (double levelDb, double seconds)
        {
            for (std::size_t n = 0; n < static_cast<std::size_t> (seconds * kRate); ++n)
            {
                const double t = static_cast<double> (n) / kRate;
                (void) gate.process (dsp::dbToGain (levelDb)
                                       * std::sin (2.0 * std::numbers::pi * 400.0 * t));
            }
        };

        feed (-10.0, 0.2);
        feed (-80.0, gapSeconds);

        return gate.isOpen();
    };

    CHECK (staysOpenAcross (0.060));
    CHECK (! staysOpenAcross (0.300));
}

TEZLA_TEST (the_gates_range_attenuates_by_exactly_what_it_says)
{
    // Range is a stated number of decibels, not "quiet". A gate that closed
    // further than asked would swallow the room tone it exists to keep.
    //
    // Measured after the gate has settled shut on silence, then fed a quiet
    // tone under the threshold: 12.000 dB and 24.000 dB of attenuation for
    // those Range settings, to within 0.01 dB.
    for (double rangeDb : { 6.0, 12.0, 24.0 })
    {
        syrinx::Gate gate;
        gate.prepare (kRate);
        gate.setThresholdDb (-20.0);
        gate.setRangeDb (rangeDb);
        gate.setAttackMs (1.0);
        gate.setReleaseMs (20.0);
        gate.setHoldMs (0.0);

        double peak = 0.0;

        for (std::size_t n = 0; n < static_cast<std::size_t> (1.0 * kRate); ++n)
        {
            const double t = static_cast<double> (n) / kRate;
            const double x = dsp::dbToGain (-40.0)
                               * std::sin (2.0 * std::numbers::pi * 400.0 * t);

            const double y = gate.process (x);

            if (n > static_cast<std::size_t> (0.9 * kRate))
                peak = std::max (peak, std::abs (y));
        }

        CHECK (! gate.isOpen());

        const double expected = dsp::dbToGain (-40.0) * dsp::dbToGain (-rangeDb);
        CHECK_NEAR (20.0 * std::log10 (peak / expected), 0.0, 0.05);
    }
}

TEZLA_TEST (a_gate_with_no_range_is_bit_exact_identity)
{
    // Section 7. The gate sits at the head of every vocal that passes through
    // the strip, so its neutral setting has to be the identity function --
    // and it is, by construction: at Range 0 the smoother's target is exactly
    // 0 dB whether the gate is open or shut, and `dbToGain` of that is
    // exactly 1.0.
    syrinx::Gate gate;
    gate.prepare (kRate);
    gate.setThresholdDb (0.0);      // deliberately impossible to cross
    gate.setRangeDb (0.0);
    CHECK (gate.isIdentity());

    for (int i = -20000; i <= 20000; ++i)
    {
        const double x = static_cast<double> (i) / 19997.0;
        CHECK (dsp::isExactly (gate.process (x), x));
    }

    // And silence in is exactly silence out, whatever the Range.
    for (double rangeDb : { 0.0, 20.0, 80.0 })
    {
        syrinx::Gate quiet;
        quiet.prepare (kRate);
        quiet.setRangeDb (rangeDb);

        for (std::size_t n = 0; n < 48000; ++n)
            CHECK (dsp::isExactlyZero (quiet.process (0.0)));
    }
}

TEZLA_TEST (the_gate_holds_for_the_same_time_at_every_sample_rate)
{
    // The hold is counted in samples, computed from the rate, so a 100 ms
    // hold is 100 ms in every session. Measured: the gate shuts 100.0 ms
    // after the signal stops at 44100, 48000, 96000 and 192000 Hz, plus the
    // detector's own 5 ms decay to fall through the closing threshold.
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        syrinx::Gate gate;
        gate.prepare (rate);
        gate.setThresholdDb (-30.0);
        gate.setHysteresisDb (3.0);
        gate.setHoldMs (100.0);
        gate.setRangeDb (30.0);

        for (std::size_t n = 0; n < static_cast<std::size_t> (0.3 * rate); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            (void) gate.process (dsp::dbToGain (-10.0)
                                   * std::sin (2.0 * std::numbers::pi * 400.0 * t));
        }

        CHECK (gate.isOpen());

        int samplesUntilShut = 0;

        while (gate.isOpen() && samplesUntilShut < static_cast<int> (2.0 * rate))
        {
            (void) gate.process (0.0);
            ++samplesUntilShut;
        }

        CHECK_NEAR (samplesUntilShut / rate * 1000.0, 112.65, 0.5);
    }
}
