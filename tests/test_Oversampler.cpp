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
#include <vector>

#include <tezla/dsp/Oversampler.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;

namespace {

/// Runs a signal through upsample -> (nothing) -> downsample and returns the
/// result. With no processing in between, this must be a pure delay.
std::vector<double> roundTrip (int factor, const std::vector<double>& input, int blockSize = 256)
{
    Oversampler oversampler;
    oversampler.prepare (blockSize, 1, factor);

    std::vector<double> output (input.size(), 0.0);

    for (std::size_t offset = 0; offset < input.size(); offset += static_cast<std::size_t> (blockSize))
    {
        const int numSamples = static_cast<int> (std::min (static_cast<std::size_t> (blockSize),
                                                           input.size() - offset));
        const double* inputPointer = input.data() + offset;
        double* outputPointer      = output.data() + offset;

        (void) oversampler.upsample (&inputPointer, numSamples);
        oversampler.downsample (&outputPointer, numSamples);
    }

    return output;
}

} // namespace

TEZLA_TEST (oversampler_auto_factor_targets_the_same_internal_rate)
{
    CHECK (autoOversamplingFactor (44100.0)  == 4);
    CHECK (autoOversamplingFactor (48000.0)  == 4);
    CHECK (autoOversamplingFactor (88200.0)  == 2);
    CHECK (autoOversamplingFactor (96000.0)  == 2);
    CHECK (autoOversamplingFactor (176400.0) == 1);
    CHECK (autoOversamplingFactor (192000.0) == 1);

    // Every host rate must land within half an octave of 192 kHz internally --
    // that is what makes the plugin sound the same at every session rate.
    for (const double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const double internalRate = rate * autoOversamplingFactor (rate);
        CHECK (internalRate >= 170000.0);
        CHECK (internalRate <= 200000.0);
    }
}

TEZLA_TEST (oversampler_latency_is_a_whole_number_of_samples)
{
    // A fractional latency cannot be reported honestly to the host, and would
    // stop the dry path lining up. The tap counts are chosen to avoid it.
    const int expected[] = { 0, 47, 63, 71 };
    int index = 0;

    for (const int factor : { 1, 2, 4, 8 })
    {
        Oversampler oversampler;
        oversampler.prepare (256, 2, factor);

        CHECK (oversampler.getFactor() == factor);
        CHECK (oversampler.getLatencySamples() == expected[index++]);
    }
}

TEZLA_TEST (oversampler_round_trip_is_a_pure_delay)
{
    // Up then straight back down, with nothing in between, must return the
    // input unchanged apart from the reported latency. If this fails, every
    // number the plugin produces afterwards is suspect.
    constexpr double fs = 48000.0;
    const int numSamples = 8192;

    for (const int factor : { 1, 2, 4, 8 })
    {
        // Tones plus a transient, all band-limited. A single-sample impulse
        // would be the obvious transient to use and is exactly wrong: it has
        // energy all the way to Nyquist, including the transition band the
        // halfband is there to remove, so a correct filter cannot reproduce it
        // and the test would be measuring the filter doing its job. The
        // transient here is a Hann-windowed burst, which stays in band.
        std::vector<double> input (static_cast<std::size_t> (numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double> (i);
            input[static_cast<std::size_t> (i)] =
                  0.3 * std::sin (2.0 * std::numbers::pi *  100.0 * t / fs)
                + 0.3 * std::sin (2.0 * std::numbers::pi * 1000.0 * t / fs)
                + 0.2 * std::sin (2.0 * std::numbers::pi * 5000.0 * t / fs);
        }

        constexpr int burstStart  = 1000;
        constexpr int burstLength = 48;
        for (int i = 0; i < burstLength; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i)
                                                        / static_cast<double> (burstLength - 1));
            input[static_cast<std::size_t> (burstStart + i)] +=
                0.5 * window * std::sin (2.0 * std::numbers::pi * 2000.0
                                         * static_cast<double> (i) / fs);
        }

        const auto output = roundTrip (factor, input);

        Oversampler probe;
        probe.prepare (256, 1, factor);
        const int latency = probe.getLatencySamples();

        double worstError = 0.0;
        for (int i = latency + 200; i < numSamples - 16; ++i)
            worstError = std::max (worstError,
                                   std::abs (output[static_cast<std::size_t> (i)]
                                           - input[static_cast<std::size_t> (i - latency)]));

        // Measured: 1.5e-7 at x2 rising to 2.6e-6 at x8, i.e. better than
        // -110 dB. 1e-5 leaves room for compiler and platform variation without
        // letting a real regression through.
        CHECK (worstError < 1.0e-5);
    }
}

TEZLA_TEST (decimator_rejects_what_would_otherwise_fold_back)
{
    // The test that actually matters for aliasing. A base-rate input can never
    // contain stopband content -- there is no room for it below Nyquist -- so
    // the only honest way to check the decimator is to put the offending tone
    // straight into the oversampled buffer, which is exactly what a saturator
    // does when it generates a harmonic above the host's Nyquist.
    //
    // 30 kHz at a 96 kHz internal rate would fold to 18 kHz, right in the
    // middle of the audible band, if the decimator let it through.
    constexpr double internalRate = 96000.0;
    constexpr int    blockSize    = 256;
    constexpr int    numBlocks    = 64;

    Oversampler oversampler;
    oversampler.prepare (blockSize, 1, 2);

    std::vector<double> silence (static_cast<std::size_t> (blockSize), 0.0);
    std::vector<double> collected;
    collected.reserve (static_cast<std::size_t> (blockSize * numBlocks));

    int internalSampleIndex = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        const double* inputPointer = silence.data();
        double* const* oversampled = oversampler.upsample (&inputPointer, blockSize);

        // Replace the (silent) oversampled signal with a full-scale 30 kHz tone.
        for (int i = 0; i < blockSize * 2; ++i)
            oversampled[0][i] = std::sin (2.0 * std::numbers::pi * 30000.0
                                          * static_cast<double> (internalSampleIndex++) / internalRate);

        std::vector<double> output (static_cast<std::size_t> (blockSize), 0.0);
        double* outputPointer = output.data();
        oversampler.downsample (&outputPointer, blockSize);

        collected.insert (collected.end(), output.begin(), output.end());
    }

    // Ignore the filter's start-up ramp, then look at what survived.
    double peak = 0.0;
    for (std::size_t i = 2048; i < collected.size(); ++i)
        peak = std::max (peak, std::abs (collected[i]));

    const double rejectionDb = 20.0 * std::log10 (std::max (peak, 1.0e-12));
    CHECK (rejectionDb < -90.0);
}

TEZLA_TEST (oversampler_passes_the_audible_band_at_every_session_rate)
{
    // The passband edge sits above 20 kHz at every rate Auto will choose, so
    // nothing audible is lost. Checked against the running rate of the first
    // stage, which is the most selective one.
    const auto coefficients = designHalfband (Oversampler::kTapsPerStage[0], Oversampler::kStopbandDb);

    for (const double hostRate : { 44100.0, 48000.0, 96000.0 })
    {
        const double runningRate = hostRate * 2.0;

        for (const double audioFrequency : { 100.0, 1000.0, 10000.0, 20000.0 })
        {
            if (audioFrequency >= hostRate * 0.5)
                continue;

            const double db = 20.0 * std::log10 (coefficients.magnitudeAt (audioFrequency / runningRate));
            CHECK_NEAR (db, 0.0, 0.1);
        }
    }
}

TEZLA_TEST (oversampler_does_not_alias_a_tone_near_nyquist)
{
    // Upsampling must not create images, and downsampling must not fold
    // anything back. A 20 kHz tone at 48 kHz is the hard case.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = tezla::measure::binExactFrequency (20000.0, fs, fftSize);
    const auto input = tezla::measure::sine (frequency, 0.5, fs, 2 * fftSize);

    for (const int factor : { 2, 4 })
    {
        const auto output = roundTrip (factor, input);

        // Discard the filter's start-up ramp. Analysing it would read as
        // broadband noise and blame the filter for the measurement's own edge.
        const std::vector<double> steadyState (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                               output.end());
        const auto report = tezla::measure::analyseHarmonics (steadyState, fs, frequency);

        CHECK_NEAR (report.fundamentalDbFs, -6.02, 0.2);
        CHECK (report.aliasingDb < -90.0);
    }
}

TEZLA_TEST (oversampler_is_silent_for_silence)
{
    for (const int factor : { 1, 2, 4, 8 })
    {
        const std::vector<double> silence (4096, 0.0);
        const auto output = roundTrip (factor, silence);

        for (const double sample : output)
            CHECK (sample == 0.0);
    }
}

TEZLA_TEST (oversampler_handles_ragged_block_sizes)
{
    // Hosts do not promise a constant block size, and FL Studio in particular
    // hands over short blocks around loop points.
    constexpr double fs = 48000.0;
    const int numSamples = 4096;

    std::vector<double> input (static_cast<std::size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        input[static_cast<std::size_t> (i)] =
            0.5 * std::sin (2.0 * std::numbers::pi * 440.0 * static_cast<double> (i) / fs);

    Oversampler oversampler;
    oversampler.prepare (512, 1, 4);

    std::vector<double> output (static_cast<std::size_t> (numSamples), 0.0);
    const int blockSizes[] = { 1, 7, 64, 3, 512, 128, 17 };
    int blockIndex = 0;

    for (int offset = 0; offset < numSamples;)
    {
        const int numThisBlock = std::min (blockSizes[blockIndex % 7], numSamples - offset);
        blockIndex++;

        const double* inputPointer = input.data() + offset;
        double* outputPointer      = output.data() + offset;

        (void) oversampler.upsample (&inputPointer, numThisBlock);
        oversampler.downsample (&outputPointer, numThisBlock);

        offset += numThisBlock;
    }

    const int latency = oversampler.getLatencySamples();
    double worstError = 0.0;
    for (int i = latency + 200; i < numSamples; ++i)
        worstError = std::max (worstError,
                               std::abs (output[static_cast<std::size_t> (i)]
                                       - input[static_cast<std::size_t> (i - latency)]));

    CHECK (worstError < 1.0e-4);
}
