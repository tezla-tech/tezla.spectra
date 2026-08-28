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

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/Waveshapers.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
using namespace tezla::measure;

namespace {

template <typename Shaper>
std::vector<double> shapeNaive (const std::vector<double>& input, const Shaper& shaper, double drive)
{
    std::vector<double> output (input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = shaper.evaluate (drive * input[i]);
    return output;
}

template <typename Shaper>
std::vector<double> shapeAdaa (const std::vector<double>& input, const Shaper& shaper, double drive)
{
    Adaa1<Shaper> adaa;
    std::vector<double> output (input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        output[i] = adaa.process (drive * input[i], shaper);
    return output;
}

} // namespace

TEZLA_TEST (logcosh_survives_extreme_input)
{
    // cosh(710) overflows a double. Drive plus a transient gets there, and the
    // naive form fails by going to infinity and then to NaN -- which is silence
    // with a click in front of it, not an obvious crash.
    CHECK_NEAR (logCosh (0.0), 0.0, 1.0e-15);
    CHECK_NEAR (logCosh (1.0), std::log (std::cosh (1.0)), 1.0e-12);
    CHECK_NEAR (logCosh (-3.0), std::log (std::cosh (3.0)), 1.0e-12);

    for (const double x : { 100.0, 800.0, 5000.0, 1.0e6 })
    {
        CHECK (std::isfinite (logCosh (x)));
        // For large x, log(cosh(x)) -> x - log(2).
        CHECK_NEAR (logCosh (x), x - std::numbers::ln2_v<double>, 1.0e-9);
    }
}

TEZLA_TEST (antiderivatives_match_numerical_integration)
{
    // If F1 is not really the integral of f, ADAA produces a plausible-looking
    // but wrong curve. Check them against each other rather than trusting the
    // algebra.
    const auto check = [] (const auto& shaper)
    {
        constexpr double step = 1.0e-6;
        for (const double x : { -4.0, -1.5, -0.3, 0.0, 0.3, 1.5, 4.0 })
        {
            const double numericalDerivative =
                (shaper.antiderivative (x + step) - shaper.antiderivative (x - step)) / (2.0 * step);

            CHECK_NEAR (numericalDerivative, shaper.evaluate (x), 1.0e-6);
        }
    };

    check (BiasedTanh { 0.0 });
    check (BiasedTanh { 0.4 });
    check (BiasedTanh { 1.2 });
    check (HardClip {});
}

TEZLA_TEST (adaa_fallback_branch_agrees_with_the_main_branch)
{
    // The seam between the divided form and the midpoint fallback is where ADAA
    // implementations crackle: the switch happens thousands of times a second
    // on ordinary material, and if the two branches disagree the difference is
    // broadband noise. Compare them at the same input pair, which is the only
    // comparison that means anything.
    const BiasedTanh shaper { 0.5 };
    constexpr double tolerance = Adaa1<BiasedTanh>::kTolerance;

    for (const double base : { -2.0, -0.5, 0.0, 0.7, 3.0 })
    {
        const double delta = tolerance * 1.5;

        const double dividedBranch  = (shaper.antiderivative (base + delta)
                                     - shaper.antiderivative (base)) / delta;
        const double midpointBranch = shaper.evaluate (base + 0.5 * delta);

        CHECK_NEAR (dividedBranch, midpointBranch, 1.0e-8);

        // And confirm the class actually takes each branch where it should.
        {
            Adaa1<BiasedTanh> adaa;
            (void) adaa.process (base, shaper);
            CHECK_NEAR (adaa.process (base + delta, shaper), dividedBranch, 1.0e-9);
        }
        {
            const double tinyDelta = tolerance * 0.25;
            Adaa1<BiasedTanh> adaa;
            (void) adaa.process (base, shaper);
            CHECK_NEAR (adaa.process (base + tinyDelta, shaper),
                        shaper.evaluate (base + 0.5 * tinyDelta), 1.0e-12);
        }
    }
}

TEZLA_TEST (adaa_is_transparent_at_low_level)
{
    // "Clean" has to mean clean. Well below the knee the shaper must be close
    // to a straight wire, or the plugin can never get out of the way.
    const BiasedTanh shaper { 0.0 };
    Adaa1<BiasedTanh> adaa;

    const auto input = sine (1000.0, 0.001, 48000.0, 4096);

    // The first call primes the one-sample history and returns f(x) directly;
    // it has no previous sample to average against. Prime it, then measure.
    (void) adaa.process (input[0], shaper);

    double worstError = 0.0;
    for (std::size_t i = 1; i < input.size(); ++i)
    {
        const double y = adaa.process (input[i], shaper);
        // Half a sample of delay is inherent to the method, so compare against
        // the midpoint of the input pair rather than the sample itself.
        worstError = std::max (worstError, std::abs (y - 0.5 * (input[i] + input[i - 1])));
    }

    CHECK (worstError < 1.0e-6);
}

TEZLA_TEST (adaa_beats_naive_shaping_where_it_matters)
{
    // At gentle drive a tanh is smooth enough that ADAA has little to fix.
    // At the drive this music actually uses it is worth 20 dB or more.
    // Measured in the audible band and in steady state -- see the two comments
    // in the test below for why both of those qualifications are load-bearing.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 65536;

    const double frequency = binExactFrequency (2000.0, fs, fftSize);
    const auto input = sine (frequency, 0.5, fs, 2 * fftSize);
    const BiasedTanh shaper { 0.0 };

    const auto steadyState = [] (const std::vector<double>& v)
    {
        return std::vector<double> (v.end() - static_cast<std::ptrdiff_t> (fftSize), v.end());
    };

    const auto naive = analyseHarmonics (steadyState (shapeNaive (input, shaper, 24.0)), fs, frequency);
    const auto adaa  = analyseHarmonics (steadyState (shapeAdaa  (input, shaper, 24.0)), fs, frequency);

    // Neither may achieve its number by refusing to distort.
    CHECK (naive.thdDb > -12.0);
    CHECK (adaa.thdDb  > -12.0);

    CHECK (adaa.audibleAliasingDb < naive.audibleAliasingDb - 5.0);
}

TEZLA_TEST (saturator_clears_the_aliasing_target_at_every_session_rate)
{
    // The acceptance test for the whole anti-aliasing design, and the numbers
    // this repository has to keep beating.
    //
    // Two things about how it is measured:
    //
    // Steady state. The oversampler's start-up ramp is a discontinuity, and an
    // FFT reads a discontinuity as broadband noise. Including it reports -30 dB
    // for a chain that is really at -130 dB.
    //
    // Audible band. With x4 at a 48 kHz session the entire residual is one
    // harmonic sitting in the decimator's transition band at ~22 kHz. Counting
    // it gives -79 dB; below 18 kHz the same signal is at -157 dB. The second
    // number is the one that describes what anyone will hear.
    constexpr std::size_t fftSize = 65536;
    const BiasedTanh shaper { 0.0 };

    struct Case { const char* name; double sampleRate; };
    const Case cases[] = {
        { "44.1 kHz session, Auto", 44100.0 },
        { "48 kHz session, Auto",   48000.0 },
        { "96 kHz session, Auto",   96000.0 },
        { "192 kHz session, Auto",  192000.0 },
    };

    for (const auto& testCase : cases)
    {
        const int factor = autoOversamplingFactor (testCase.sampleRate);
        const double frequency = binExactFrequency (2000.0, testCase.sampleRate, fftSize);
        const auto input = sine (frequency, 0.5, testCase.sampleRate, 2 * fftSize);

        std::vector<double> output (input.size(), 0.0);
        Adaa1<BiasedTanh> adaa;
        Oversampler oversampler;
        oversampler.prepare (512, 1, factor);

        for (std::size_t offset = 0; offset < input.size(); offset += 512)
        {
            const int numSamples = static_cast<int> (std::min<std::size_t> (512, input.size() - offset));
            const double* inputPointer = input.data() + offset;
            double* outputPointer      = output.data() + offset;

            double* const* oversampled = oversampler.upsample (&inputPointer, numSamples);
            for (int i = 0; i < numSamples * factor; ++i)
                oversampled[0][i] = adaa.process (24.0 * oversampled[0][i], shaper);

            oversampler.downsample (&outputPointer, numSamples);
        }

        const std::vector<double> steadyState (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                               output.end());
        const auto report = analyseHarmonics (steadyState, testCase.sampleRate, frequency);

        std::printf ("        %-24s x%d  audible aliasing %8.1f dB, THD %6.2f dB\n",
                     testCase.name, factor, report.audibleAliasingDb, report.thdDb);

        // CLAUDE.md section 7 asks for better than -60 dB at maximum drive.
        // Measured here: -106 dB or better at every rate, at drive x24.
        CHECK (report.audibleAliasingDb < -90.0);

        // It must still be saturating hard.
        CHECK (report.thdDb > -12.0);
    }
}
