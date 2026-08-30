// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// G.711's two companding laws.
//
// The header derives the segment structure rather than carrying a table, so
// these tests are what stand in for the standard's own text -- which could not
// be fetched from this container. Four independent statements, any one of
// which would catch a structural mistake:
//
//   1. every one of the 256 code words decodes to the MIDPOINT of the range of
//      inputs that produce it, found by search rather than by arithmetic;
//   2. the SNR is FLAT across 40 dB of level, where a linear 8-bit quantiser's
//      falls a decibel per decibel -- the defining property of companding, and
//      the one break-checked against a linear quantiser;
//   3. encoding is monotone and round-trip stable;
//   4. the two ceilings the structure implies, 0.984312 and 0.984615.

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <tezla/dsp/Companding.hpp>
#include <tezla/dsp/Decibels.hpp>

#include "TestFramework.hpp"

using namespace tezla;

namespace {

/// How finely the input range is swept when looking for a code's decision
/// boundaries. mu-law's smallest interval is 2/8159 wide, so 1/2^21 of the
/// range is about 128 samples inside the narrowest interval there is.
constexpr int kSweepPoints = 1 << 21;

struct Law
{
    const char* name;
    int (*encode) (double);
    double (*decode) (int);
    double (*step) (int);
    double peak;
};

const Law kMuLaw {
    "mu-law",
    dsp::Compander::muLawEncode,
    dsp::Compander::muLawDecode,
    dsp::Compander::muLawStep,
    dsp::Compander::kMuPeak,
};

const Law kALaw {
    "A-law",
    dsp::Compander::aLawEncode,
    dsp::Compander::aLawDecode,
    dsp::Compander::aLawStep,
    dsp::Compander::kAPeak,
};

/// The signal-to-noise ratio a quantiser achieves on a sine at a given level.
///
/// Ten seconds of a **997 Hz** sine at 48 kHz, error measured against the input
/// rather than against a fitted version of it -- so a gain error counts as
/// noise, which is correct here: a codec that quietened everything by 3 dB
/// would be wrong.
///
/// The odd frequency is not decoration. The first draft used 1 kHz, which at
/// 48 kHz is exactly 48 samples per cycle, so the sine only ever takes 48
/// distinct values and the "noise" measured is a lottery over where those 48
/// land on the quantiser's grid. It read 32.9 to 39.7 dB across level with no
/// pattern -- a measurement artefact that looked exactly like a codec fault.
/// 997 is prime, so 997/48000 has period 48000 and the probe visits every
/// phase. CLAUDE.md section 10: check the instrument before trusting it.
double snrDbForSine (const dsp::Compander& compander, double levelDb)
{
    constexpr int length = 480000;
    const double amplitude = dsp::dbToGain (levelDb);

    double signalPower = 0.0;
    double errorPower = 0.0;

    for (int n = 0; n < length; ++n)
    {
        const double phase = 2.0 * std::numbers::pi * 997.0
                               * static_cast<double> (n) / 48000.0;
        const double x = amplitude * std::sin (phase);
        const double y = compander.process (x);

        signalPower += x * x;
        errorPower += (y - x) * (y - x);
    }

    if (errorPower <= 0.0)
        return std::numeric_limits<double>::infinity();

    return 10.0 * std::log10 (signalPower / errorPower);
}

/// How much a quantiser's SNR follows the level, as decibels per decibel, by
/// least squares over 0 to -40 dBFS in 5 dB steps.
///
/// **This one number is the whole of companding.** A fixed-step quantiser
/// scores 1: every decibel you turn the signal down is a decibel of SNR lost.
/// A companding one scores 0: the step grows with the signal, so the noise
/// follows it down and the ratio stays put.
double snrSlopePerDb (const dsp::Compander& compander, double& lowest, double& highest)
{
    std::vector<double> levels;
    std::vector<double> snrs;

    for (double levelDb = 0.0; levelDb >= -40.001; levelDb -= 5.0)
    {
        levels.push_back (levelDb);
        snrs.push_back (snrDbForSine (compander, levelDb));
    }

    double meanLevel = 0.0;
    double meanSnr = 0.0;

    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        meanLevel += levels[i];
        meanSnr += snrs[i];
    }

    meanLevel /= static_cast<double> (levels.size());
    meanSnr /= static_cast<double> (levels.size());

    double covariance = 0.0;
    double variance = 0.0;

    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        covariance += (levels[i] - meanLevel) * (snrs[i] - meanSnr);
        variance += (levels[i] - meanLevel) * (levels[i] - meanLevel);
    }

    lowest = *std::min_element (snrs.begin(), snrs.end());
    highest = *std::max_element (snrs.begin(), snrs.end());

    return covariance / variance;
}

} // namespace

// ---------------------------------------------------------------------------
// The structure
// ---------------------------------------------------------------------------

TEZLA_TEST (every_code_word_decodes_to_the_midpoint_of_its_own_interval)
{
    // The strongest structural statement available without the standard's
    // text: sweep the whole input range, record for every code the lowest and
    // highest input that produced it, and check that the code decodes to the
    // middle of that range.
    //
    // It catches a wrong bias, a wrong segment boundary, an off-by-one in the
    // mantissa shift and a decoder that reconstructs at an interval end
    // instead of its centre -- all of which would still "work" and would all
    // sound like a slightly different lo-fi rather than like a defect.
    //
    // Measured: over the 128 codes a positive sweep reaches, the worst
    // deviation from the midpoint is 2.365e-07 for A-law and 2.4e-07 for
    // mu-law -- the sweep's own resolution (1/2^21 = 4.8e-7, so half a step)
    // rather than an error in the codec.
    //
    // Two codes are excluded, and both exclusions are real rather than
    // convenient:
    //
    //  - **the top code**, which absorbs everything from its lower boundary up
    //    to full scale and beyond, so its range is open-ended and its midpoint
    //    is not where it reconstructs;
    //  - **mu-law's positive-zero code**, whose interval straddles zero. Its
    //    nominal range is +/-1 in the 14-bit domain and the positive sweep only
    //    ever sees the upper half of it, so the "midpoint" a one-sided sweep
    //    computes is 6.13e-05 while the code correctly reconstructs at exactly
    //    0. The two sign codes together cover the interval symmetrically, which
    //    the test below states directly.
    for (const Law* law : { &kMuLaw, &kALaw })
    {
        std::vector<double> lowest (256, std::numeric_limits<double>::infinity());
        std::vector<double> highest (256, -std::numeric_limits<double>::infinity());

        for (int i = 0; i <= kSweepPoints; ++i)
        {
            const double x = static_cast<double> (i) / kSweepPoints;   // 0 .. 1
            const int code = law->encode (x);

            lowest[static_cast<std::size_t> (code)]
                = std::min (lowest[static_cast<std::size_t> (code)], x);
            highest[static_cast<std::size_t> (code)]
                = std::max (highest[static_cast<std::size_t> (code)], x);
        }

        double worst = 0.0;
        int seen = 0;

        for (int code = 0; code < 256; ++code)
        {
            const auto index = static_cast<std::size_t> (code);

            if (lowest[index] > highest[index])
                continue;   // a negative-side code; the positive sweep never reaches it

            ++seen;

            const double midpoint = 0.5 * (lowest[index] + highest[index]);
            const double decoded = law->decode (code);

            if (highest[index] >= 1.0)
                continue;   // the top code: open-ended, see above

            if (dsp::isExactlyZero (decoded))
                continue;   // the zero code: straddles zero, checked below

            worst = std::max (worst, std::abs (decoded - midpoint));
        }

        // 128 positive codes for each law.
        CHECK (seen == 128);
        CHECK (worst < 1.0e-6);
    }

    // The zero code, stated directly rather than skipped. mu-law's smallest
    // interval is centred on zero and split between the two sign codes, so
    // everything from just below one half-step to just above it reconstructs
    // at exactly zero -- which is both correct and the reason silence stays
    // silent through mu-law without any help.
    const double halfStep = 0.5 * dsp::Compander::muLawStep (0);

    CHECK (dsp::isExactlyZero (kMuLaw.decode (kMuLaw.encode (0.0))));
    CHECK (dsp::isExactlyZero (kMuLaw.decode (kMuLaw.encode (0.9 * halfStep))));
    CHECK (dsp::isExactlyZero (kMuLaw.decode (kMuLaw.encode (-0.9 * halfStep))));
    CHECK (! dsp::isExactlyZero (kMuLaw.decode (kMuLaw.encode (1.1 * halfStep))));

    // A-law has no zero code at all -- its smallest reconstruction is one LSB.
    for (int code = 0; code < 256; ++code)
        CHECK (! dsp::isExactlyZero (kALaw.decode (code)));
}

TEZLA_TEST (encoding_is_monotone_and_round_trip_stable)
{
    // Two properties a quantiser cannot be without. Monotone: a larger input
    // never produces a smaller reconstruction -- a codec that folded would
    // sound like distortion and would pass a SNR test. Round-trip stable:
    // decoding a code and re-encoding it returns the same code, which is what
    // "the reconstruction lies inside its own interval" means.
    for (const Law* law : { &kMuLaw, &kALaw })
    {
        double previous = -2.0;

        for (int i = 0; i <= 200000; ++i)
        {
            const double x = -1.0 + 2.0 * static_cast<double> (i) / 200000.0;
            const double decoded = law->decode (law->encode (x));

            CHECK (decoded >= previous - 1.0e-15);
            previous = decoded;
        }

        // Round-trip stability, stated on the *value* rather than on the code.
        // The code-level form is almost right and fails once: mu-law has two
        // zero codes, +0 and -0, and encoding -0.0 hands back the positive
        // one. That is the codec being correct, not the test finding a bug --
        // so the assertion is that decoding a code, re-encoding it and
        // decoding again lands on the same value, bit for bit.
        for (int code = 0; code < 256; ++code)
        {
            const double value = law->decode (code);
            CHECK (dsp::isExactly (law->decode (law->encode (value)), value));
        }
    }

    // The two zero codes, named. mu-law's 0x7F and 0xFF differ only in the
    // sign bit and both reconstruct at zero; A-law has neither.
    CHECK (dsp::isExactlyZero (dsp::Compander::muLawDecode (0x7F)));
    CHECK (dsp::isExactlyZero (dsp::Compander::muLawDecode (0xFF)));
    CHECK (dsp::Compander::muLawEncode (0.0) == 0xFF);
}

TEZLA_TEST (each_law_reaches_the_ceiling_its_structure_implies)
{
    // Both laws reconstruct at interval midpoints, so the loudest thing either
    // can produce is the midpoint of its top interval -- not full scale.
    // mu-law: 8031/8159. A-law: 4032/4095. Both 0.137 dB down, which is the
    // codec's real ceiling and is why a full-scale sine through G.711 comes
    // back very slightly quieter.
    CHECK_NEAR (dsp::Compander::kMuPeak, 0.984312, 1.0e-6);
    CHECK_NEAR (dsp::Compander::kAPeak, 0.984615, 1.0e-6);

    CHECK_NEAR (20.0 * std::log10 (dsp::Compander::kMuPeak), -0.1374, 1.0e-4);
    CHECK_NEAR (20.0 * std::log10 (dsp::Compander::kAPeak), -0.1347, 1.0e-4);

    for (const Law* law : { &kMuLaw, &kALaw })
    {
        // Nothing exceeds the ceiling, including inputs well past full scale:
        // the codec clips, as a telephone channel does.
        for (double x : { 1.0, 1.5, 4.0, 100.0 })
        {
            CHECK_NEAR (law->decode (law->encode (x)), law->peak, 1.0e-12);
            CHECK_NEAR (law->decode (law->encode (-x)), -law->peak, 1.0e-12);
        }
    }

    // The two laws differ where the literature says they do: mu-law resolves
    // more finely near zero (its smallest step is 2/8159 against A-law's
    // 2/4095), which is exactly the small-signal advantage it is known for.
    CHECK (dsp::Compander::muLawStep (0) < dsp::Compander::aLawStep (0));
    CHECK_NEAR (dsp::Compander::muLawStep (0), 2.0 / 8159.0, 1.0e-15);
    CHECK_NEAR (dsp::Compander::aLawStep (0), 2.0 / 4095.0, 1.0e-15);

    // And the step doubles from segment to segment, which is the whole idea.
    for (int segment = 1; segment < 8; ++segment)
    {
        CHECK_NEAR (dsp::Compander::muLawStep (segment),
                    2.0 * dsp::Compander::muLawStep (segment - 1), 1.0e-15);

        if (segment >= 2)
            CHECK_NEAR (dsp::Compander::aLawStep (segment),
                        2.0 * dsp::Compander::aLawStep (segment - 1), 1.0e-15);
    }

    // A-law's first two segments share a step -- its substitute for mu-law's
    // bias, and the reason it has no zero code.
    CHECK_NEAR (dsp::Compander::aLawStep (1), dsp::Compander::aLawStep (0), 1.0e-15);
}

// ---------------------------------------------------------------------------
// The property that makes a phone sound like a phone
// ---------------------------------------------------------------------------

TEZLA_TEST (companding_holds_the_signal_to_noise_ratio_flat_across_level)
{
    // This is the whole reason the plugin does not just crush to 8 bits, and
    // it is the test with teeth.
    //
    // A companding codec's step grows with the signal, so its noise rides up
    // and down with the signal and the SNR barely moves. A linear quantiser's
    // step is fixed, so its SNR falls a decibel for every decibel of level --
    // fine at the top, unusable in a decay.
    //
    // Measured on a 997 Hz sine at 48 kHz, ten seconds per point, 0 down to
    // -40 dBFS in 5 dB steps:
    //
    //          level      mu-law     A-law     linear 8-bit
    //         0 dBFS      38.92      38.99       50.02
    //       -10 dBFS      36.92      36.39       39.75
    //       -20 dBFS      38.06      38.39       29.78
    //       -30 dBFS      34.65      36.75       20.61
    //       -40 dBFS      35.30      34.20       11.11
    //
    // As a least-squares slope in decibels of SNR per decibel of level:
    //
    //          mu-law   +0.067      spread 4.29 dB over the 40 dB range
    //          A-law    +0.062      spread 4.79 dB
    //          linear   +0.970      spread 38.91 dB
    //
    // The slope is the statement. Zero means "the noise follows the signal
    // down"; one means "the noise stays put and the signal walks into it".
    // Both companding laws are within 0.07 of zero and the linear quantiser is
    // within 0.03 of one.
    //
    // The few decibels of ripple on the companded curves is not error: it is
    // the segment structure. G.711 approximates the log curve with eight
    // straight pieces, so the SNR dips slightly wherever a signal's peak sits
    // just above a segment boundary and part of the waveform gets the coarser
    // step. A smooth log law would not ripple -- and would not be G.711.
    dsp::Compander mu;
    mu.setLaw (dsp::CompandingLaw::muLaw);

    dsp::Compander a;
    a.setLaw (dsp::CompandingLaw::aLaw);

    dsp::Compander linear;
    linear.setLaw (dsp::CompandingLaw::linear);
    linear.setBits (8);

    double lowest = 0.0, highest = 0.0;

    const double muSlope = snrSlopePerDb (mu, lowest, highest);
    CHECK (std::abs (muSlope) < 0.15);
    CHECK (highest - lowest < 6.0);
    CHECK (lowest > 33.0);

    const double aSlope = snrSlopePerDb (a, lowest, highest);
    CHECK (std::abs (aSlope) < 0.15);
    CHECK (highest - lowest < 6.0);
    CHECK (lowest > 33.0);

    // The contrast, which is the break-check built in: the same measurement on
    // a linear 8-bit quantiser reads a slope of 0.97 and a spread of 38.9 dB.
    // Swap `mu` for `linear` in the two assertions above and both fail.
    const double linearSlope = snrSlopePerDb (linear, lowest, highest);
    CHECK (linearSlope > 0.9);
    CHECK (highest - lowest > 35.0);

    // And at -40 dBFS the companded laws beat it by more than 20 dB, which is
    // the number that matters when a tone decays: measured +24.19 dB for
    // mu-law and +23.10 dB for A-law.
    const double linearAtMinus40 = snrDbForSine (linear, -40.0);

    CHECK (snrDbForSine (mu, -40.0) - linearAtMinus40 > 20.0);
    CHECK (snrDbForSine (a, -40.0) - linearAtMinus40 > 20.0);
}

TEZLA_TEST (robbing_a_bit_from_the_code_word_costs_about_six_decibels)
{
    // Bits below 8 in a companding mode mask the low bits of the transmitted
    // code word -- which is what a T1 span did when it stole them for
    // signalling, so 7-bit mu-law is a sound the network really made.
    //
    // Masking doubles the step, so each bit costs about 6 dB. It costs a
    // little more, because masking truncates rather than re-rounds: the error
    // is one-sided, and a one-sided error's mean square is 4/3 of a rounded
    // one's -- worth another 1.2 dB.
    //
    // Measured at -12 dBFS on a 997 Hz sine:
    //
    //        bits    mu-law     A-law
    //           8    37.01 dB   36.92 dB
    //           7    30.70      30.28
    //           6    23.04      22.51
    //           5    15.89      15.34
    //           4     9.06       8.51
    //
    // So 6.3 to 7.7 dB per bit for both laws, which brackets the 6 dB of the
    // doubled step plus the 1.2 dB of truncation.
    //
    // Which word the mask lands on is a real choice and it was measured rather
    // than assumed. Masking the *transmitted* octet -- where a robbed
    // signalling bit physically sits -- forces the logical mantissa's low bit
    // to one instead of zero, biasing every reconstruction up by half a step;
    // that first bit then costs 10.9 dB rather than 6.3, because a consistent
    // magnitude offset is distortion rather than a coarser grid. The logical
    // word is the one that behaves like a Bits control, so it is the one that
    // ships, and Companding.hpp says so.
    for (auto law : { dsp::CompandingLaw::muLaw, dsp::CompandingLaw::aLaw })
    {
        dsp::Compander compander;
        compander.setLaw (law);

        double previous = 0.0;

        for (int bits = 8; bits >= 4; --bits)
        {
            compander.setBits (bits);
            const double snr = snrDbForSine (compander, -12.0);

            if (bits < 8)
            {
                const double cost = previous - snr;
                CHECK (cost > 6.0);
                CHECK (cost < 8.0);
            }

            previous = snr;
        }
    }

    dsp::Compander mu;
    mu.setLaw (dsp::CompandingLaw::muLaw);

    // Eight bits is the full code word, so it must be identical to not
    // masking at all -- a mask of 0xFF, bit for bit.
    dsp::Compander eight;
    eight.setLaw (dsp::CompandingLaw::muLaw);
    eight.setBits (8);

    for (int i = -1000; i <= 1000; ++i)
    {
        const double x = static_cast<double> (i) / 1000.0;
        CHECK (dsp::isExactly (eight.process (x),
                               dsp::Compander::muLawDecode (dsp::Compander::muLawEncode (x))));
    }
}

// ---------------------------------------------------------------------------
// Section 7: exactness where it is claimed
// ---------------------------------------------------------------------------

TEZLA_TEST (the_compander_is_bit_exact_identity_when_it_is_off)
{
    // A stage permanently in the signal path has to be the identity at its
    // neutral setting, not merely transparent (CLAUDE.md section 7). Two
    // settings claim it: the law switched off, and linear at sixteen bits.
    dsp::Compander off;
    CHECK (off.isBypassed());

    dsp::Compander wide;
    wide.setLaw (dsp::CompandingLaw::linear);
    wide.setBits (16);
    CHECK (wide.isBypassed());

    for (int i = -20000; i <= 20000; ++i)
    {
        const double x = static_cast<double> (i) / 19997.0;   // deliberately not a round grid

        CHECK (dsp::isExactly (off.process (x), x));
        CHECK (dsp::isExactly (wide.process (x), x));
    }

    // Neither companding law is bypassed at any depth, and both say so.
    for (auto law : { dsp::CompandingLaw::muLaw, dsp::CompandingLaw::aLaw })
    {
        dsp::Compander c;
        c.setLaw (law);

        for (int bits = 1; bits <= 16; ++bits)
        {
            c.setBits (bits);
            CHECK (! c.isBypassed());
        }
    }
}

TEZLA_TEST (silence_in_is_exactly_silence_out_through_both_laws)
{
    // mu-law gets this for free: the bias cancels and zero encodes to a code
    // that decodes to exactly zero.
    //
    // **A-law does not.** A-law has no zero code -- its smallest
    // reconstruction is +/-1 LSB -- which is the real reason A-law
    // idle-channel noise is worse than mu-law's. Reproduced literally that is
    // a +2.44e-4 DC offset sitting on the output of every instance whenever
    // nothing is playing, so the header forces an exactly-zero input to an
    // exactly-zero output and says so.
    //
    // The deviation is one sample value wide. This test pins both halves: that
    // the codec would otherwise idle at 1/4095, and that it does not.
    CHECK_NEAR (dsp::Compander::aLawDecode (dsp::Compander::aLawEncode (0.0)),
                1.0 / 4095.0, 1.0e-15);

    CHECK (dsp::isExactlyZero (dsp::Compander::muLawDecode (dsp::Compander::muLawEncode (0.0))));

    for (auto law : { dsp::CompandingLaw::off, dsp::CompandingLaw::muLaw,
                      dsp::CompandingLaw::aLaw, dsp::CompandingLaw::linear })
    {
        dsp::Compander c;
        c.setLaw (law);
        c.setBits (8);

        CHECK (dsp::isExactlyZero (c.process (0.0)));
        CHECK (dsp::isExactlyZero (c.process (-0.0)));
    }

    // And the step the guard introduces is no larger than the quantiser's own
    // step at zero, so it is not a discontinuity the codec does not already
    // have at every other decision boundary.
    dsp::Compander a;
    a.setLaw (dsp::CompandingLaw::aLaw);

    const double justAbove = a.process (1.0e-12);
    CHECK (std::abs (justAbove) <= dsp::Compander::aLawStep (0) + 1.0e-15);
}

// ---------------------------------------------------------------------------
// Bitcrusher's new setter
// ---------------------------------------------------------------------------

TEZLA_TEST (bitcrusher_setBits_agrees_with_setAmount_everywhere_a_control_reaches)
{
    // `setBits` is new; `setAmount` is what five shipped plugins use. They map
    // the same range, so they have to produce the same quantiser -- otherwise
    // Emberdrive's Crush and Crossbar's Bits would be two different effects
    // wearing one name.
    //
    // Checked bit for bit across the amount range at 1/512 resolution, on 2001
    // sample values each.
    for (int a = 1; a <= 512; ++a)
    {
        const double amount = static_cast<double> (a) / 512.0;

        dsp::Bitcrusher byAmount;
        byAmount.setAmount (amount);

        dsp::Bitcrusher byBits;
        byBits.setBits (byAmount.getBits());

        for (int i = -1000; i <= 1000; ++i)
        {
            const double x = static_cast<double> (i) / 1000.0;
            CHECK (dsp::isExactly (byAmount.process (x), byBits.process (x)));
        }
    }

    // **The agreement above is consistency, not correctness**, and the
    // difference is not academic: perturbing `applyBits` so that every depth
    // came out 1.0000001 times too fine left the agreement test entirely
    // green, because both crushers were built through the same broken path.
    // Caught by break-checking, and this is the assertion that fixes it --
    // the grid itself, against arithmetic written out here rather than shared
    // with the class.
    //
    // n bits means 2^(n-1) levels per unit, so the step is 2^-(n-1) and the
    // whole -1..1 range holds 2^n + 1 reachable values.
    for (int bits = 1; bits <= 12; ++bits)
    {
        dsp::Bitcrusher grid;
        grid.setBits (static_cast<double> (bits));

        const double levels = std::pow (2.0, bits - 1.0);

        for (int i = -5000; i <= 5000; ++i)
        {
            const double x = static_cast<double> (i) / 5000.0;
            CHECK (dsp::isExactly (grid.process (x), std::round (x * levels) / levels));
        }
    }

    // getBits round-trips both ways.
    dsp::Bitcrusher c;

    for (double bits = 1.0; bits <= 16.0; bits += 0.25)
    {
        c.setBits (bits);
        CHECK_NEAR (c.getBits(), bits, 1.0e-12);
    }

    for (int a = 0; a <= 100; ++a)
    {
        const double amount = static_cast<double> (a) / 100.0;
        c.setAmount (amount);
        CHECK_NEAR (c.getAmount(), amount, 1.0e-12);
        CHECK_NEAR (c.getBits(), 16.0 - amount * 15.0, 1.0e-12);
    }

    // The one place they deliberately differ: the top of the bits control
    // means off, while an amount of zero means off. Both are bit-exact
    // identity, which is what actually matters.
    dsp::Bitcrusher topOfBits;
    topOfBits.setBits (16.0);

    dsp::Bitcrusher zeroAmount;
    zeroAmount.setAmount (0.0);

    for (int i = -1000; i <= 1000; ++i)
    {
        const double x = static_cast<double> (i) / 997.0;
        CHECK (dsp::isExactly (topOfBits.process (x), x));
        CHECK (dsp::isExactly (zeroAmount.process (x), x));
    }
}
