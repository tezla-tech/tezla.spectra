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

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/TruePeakDetector.hpp>

#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include "CapstoneEngine.hpp"

using namespace tezla;
using namespace tezla::measure;

namespace
{
constexpr double kRate = 48000.0;

/// Runs a block of audio through a configured engine in host-sized chunks.
///
/// Deliberately not one call: a limiter that only held its ceiling when handed
/// the whole signal at once would be useless in a DAW, and the Emberdrive
/// buffer-size bug is the reason this project checks rather than assumes.
std::vector<std::vector<double>> render (capstone::Engine& engine,
                                         std::vector<std::vector<double>> input,
                                         int blockSize,
                                         double* worstClampExcess = nullptr)
{
    const int channels = static_cast<int> (input.size());
    const int total = static_cast<int> (input[0].size());

    for (int offset = 0; offset < total; )
    {
        const int span = std::min (blockSize, total - offset);

        double* pointers[capstone::Engine::kMaxChannels] {};

        for (int c = 0; c < channels; ++c)
            pointers[c] = input[static_cast<std::size_t> (c)].data() + offset;

        engine.process (pointers, channels, span);
        offset += span;

        if (worstClampExcess != nullptr)
            *worstClampExcess = std::max (*worstClampExcess, engine.getLimiterClampExcess());
    }

    return input;
}

std::vector<std::vector<double>> noise (int length, double amplitude, unsigned seed = 7u)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> dist (-amplitude, amplitude);

    std::vector<std::vector<double>> x (2, std::vector<double> (static_cast<std::size_t> (length)));

    for (int i = 0; i < length; ++i)
    {
        x[0][static_cast<std::size_t> (i)] = dist (rng);
        x[1][static_cast<std::size_t> (i)] = dist (rng);
    }

    return x;
}

double peakOf (const std::vector<std::vector<double>>& x, int from = 0)
{
    double peak = 0.0;

    for (const auto& channel : x)
        for (std::size_t i = static_cast<std::size_t> (from); i < channel.size(); ++i)
            peak = std::max (peak, std::abs (channel[i]));

    return peak;
}

/// The reconstructed peak of a rendered signal, measured with a detector that
/// was itself checked against the ITU bound in test_TruePeakDetector.
double truePeakOf (const std::vector<std::vector<double>>& x, int from = 0)
{
    double peak = 0.0;

    for (const auto& channel : x)
    {
        dsp::TruePeakDetector detector;
        detector.prepare (dsp::TruePeakDetector::kMaxFactor);
        detector.setFactor (16);
        detector.reset();

        for (std::size_t i = 0; i < channel.size(); ++i)
        {
            const double reading = detector.process (channel[i]);

            if (static_cast<int> (i) >= from)
                peak = std::max (peak, reading);
        }
    }

    return peak;
}
} // namespace

TEZLA_TEST (capstone_is_bit_exact_with_both_stages_off)
{
    // The neutral setting has to be the input, sample for sample. "Almost
    // identity" means every project changes the day the plugin updates.
    capstone::Engine engine;
    engine.prepare (kRate, 512, 2);

    capstone::Parameters parameters;
    parameters.clipOn = false;
    parameters.limitOn = false;
    parameters.thresholdDb = 0.0;
    parameters.outputDb = 0.0;
    engine.setParameters (parameters);
    engine.reset();

    const auto input = noise (8192, 0.9);
    const auto output = render (engine, input, 128);

    CHECK (engine.getLatencySamples() == 0);

    bool exact = true;

    for (std::size_t c = 0; c < input.size(); ++c)
        for (std::size_t i = 0; i < input[c].size(); ++i)
            if (output[c][i] != input[c][i])
                exact = false;

    CHECK (exact);
}

TEZLA_TEST (capstone_clip_below_its_threshold_is_bit_exact)
{
    // With the clipper on but the signal never reaching it, the only thing in
    // the path is the oversampler -- so this is run with oversampling off,
    // which is the setting where the claim is exactly true. It is the property
    // SoftClip::excess exists for.
    capstone::Engine engine;
    engine.prepare (kRate, 512, 2);

    capstone::Parameters parameters;
    parameters.clipOn = true;
    parameters.clipThresholdDb = 0.0;
    parameters.clipOversampling = dsp::OversamplingMode::Off;
    parameters.limitOn = false;
    engine.setParameters (parameters);
    engine.reset();

    const auto input = noise (8192, 0.4);      // -8 dBFS, well under the corner
    const auto output = render (engine, input, 128);

    bool exact = true;

    for (std::size_t c = 0; c < input.size(); ++c)
        for (std::size_t i = 0; i < input[c].size(); ++i)
            if (output[c][i] != input[c][i])
                exact = false;

    CHECK (exact);
    CHECK (engine.getClipReductionDb() == 0.0);
}

TEZLA_TEST (capstone_holds_its_ceiling_across_the_parameter_space)
{
    // The product. Swept rather than sampled, against material chosen to break
    // it: noise, a sine that sits between samples, impulses and DC.
    int combinations = 0;
    double worstOver = -1.0e9;
    double worstClampExcess = 0.0;

    for (const double ceilingDb : { -12.0, -0.3, 3.0 })
    for (const double thresholdDb : { 0.0, -12.0, -30.0 })
    for (const bool clipOn : { false, true })
    for (const double attackMs : { 0.0, 1.0, 20.0 })
    for (const double kneeDb : { 0.0, 12.0 })
    for (const auto truePeak : { dsp::TruePeakMode::Off, dsp::TruePeakMode::Standard,
                                 dsp::TruePeakMode::Strict })
    for (const int blockSize : { 1, 64, 512 })
    {
        ++combinations;

        capstone::Engine engine;
        engine.prepare (kRate, 512, 2);

        capstone::Parameters parameters;
        parameters.ceilingDb = ceilingDb;
        parameters.thresholdDb = thresholdDb;
        parameters.clipOn = clipOn;
        parameters.clipThresholdDb = ceilingDb + 3.0;
        parameters.clipOversampling = dsp::OversamplingMode::X2;
        parameters.attackMs = attackMs;
        parameters.kneeDb = kneeDb;
        parameters.truePeak = truePeak;
        parameters.limitOn = true;

        // Hold off and a fast release on purpose. Hold widens the minimum
        // window backwards, which papers over a misaligned window; with it at
        // zero and the release short, a lone transient in quiet is the case
        // that actually exercises the alignment.
        parameters.holdMs = 0.0;
        parameters.releaseMs = 1.0;
        engine.setParameters (parameters);
        engine.reset();

        auto input = noise (4000, 1.4);

        for (int i = 0; i < 4000; ++i)
        {
            // A sine at a quarter of the sample rate, offset so no sample lands
            // on a peak, plus impulses and a DC tail.
            input[0][static_cast<std::size_t> (i)] += 0.9
                * std::sin (2.0 * std::numbers::pi * 0.25 * i + std::numbers::pi * 0.25);

            if (i % 601 == 0)
                input[1][static_cast<std::size_t> (i)] = 3.0;

            if (i > 3500)
                input[0][static_cast<std::size_t> (i)] = 2.0;

            // A lone transient in near-silence, which is what a misaligned
            // minimum window mishandles and what uniformly loud material hides
            // completely.
            if (i > 1200 && i < 1900)
                input[0][static_cast<std::size_t> (i)] = i == 1500 ? 4.0 : 0.02;
        }

        const auto output = render (engine, input, blockSize, &worstClampExcess);

        // Linear, against the exact number the limiter clamps to. Comparing in
        // decibels instead makes this fail for the wrong reason: dbToGain and
        // gainToDb are not exact inverses, and at a ceiling of +3 dB the round
        // trip alone reads 4.4e-16 dB high on a sample that is exactly on the
        // ceiling. That is the measurement's error, not the limiter's.
        worstOver = std::max (worstOver, peakOf (output) - dsp::dbToGain (ceilingDb));
    }

    CHECK (combinations == 972);

    // Never above the ceiling, at all, anywhere in that space.
    CHECK (worstOver <= 0.0);

    // And the assertion with teeth. The line above is held by the clamp at the
    // end of LimiterCore whatever the chain does -- halving the minimum window
    // against the smoother's support left the clamp removing 1.02 of full
    // scale and the peak reading still landed exactly on the ceiling. This is
    // what says the gain arriving at each sample was already correct.
    CHECK (worstClampExcess < 1.0e-12);
}

TEZLA_TEST (capstone_output_is_independent_of_the_host_block_size)
{
    // The Emberdrive lesson. Anything recomputed per callback rather than per
    // sample makes the sound depend on a host setting, and it is invisible
    // until someone changes their buffer size.
    const auto settings = [] (capstone::Engine& engine)
    {
        capstone::Parameters parameters;
        parameters.thresholdDb = -12.0;
        parameters.ceilingDb = -0.3;
        parameters.clipOn = true;
        parameters.clipThresholdDb = 1.0;
        parameters.clipShape = 0.4;
        parameters.clipOversampling = dsp::OversamplingMode::X4;
        parameters.limitOn = true;
        parameters.attackMs = 2.0;
        parameters.holdMs = 10.0;
        parameters.releaseMs = 80.0;
        parameters.truePeak = dsp::TruePeakMode::Standard;
        engine.setParameters (parameters);
        engine.reset();
    };

    const auto input = noise (16384, 1.1);

    std::vector<std::vector<std::vector<double>>> outputs;

    for (const int blockSize : { 1, 64, 100, 512, 4096 })
    {
        capstone::Engine engine;
        engine.prepare (kRate, 4096, 2);
        settings (engine);
        outputs.push_back (render (engine, input, blockSize));
    }

    double worst = 0.0;

    for (std::size_t k = 1; k < outputs.size(); ++k)
        for (std::size_t c = 0; c < outputs[k].size(); ++c)
            for (std::size_t i = 0; i < outputs[k][c].size(); ++i)
                worst = std::max (worst, std::abs (outputs[k][c][i] - outputs[0][c][i]));

    // Exactly zero, not nearly. Every stage here is either per-sample or
    // per-parameter-change; nothing is per-callback.
    CHECK (worst == 0.0);
}

TEZLA_TEST (capstone_true_peak_mode_controls_the_reconstructed_peak)
{
    // What the detector is for, measured on the finished plugin rather than on
    // the parts. The measuring instrument is a 16x detector, which is
    // legitimate rather than circular: it was checked against the
    // Recommendation's own worst-case bound in test_TruePeakDetector before
    // being used to check anything else.
    constexpr double ceilingDb = -1.0;

    // 0 is the textbook worst case -- a sine at exactly a quarter of the sample
    // rate, offset 45 degrees, where every sample reads 1/sqrt(2) of the true
    // peak. 1 is the case that matters more in practice: dense content near
    // Nyquist, where the ITU's own ratio of 4 is measurably not a guarantee.
    const auto measure = [&] (int signal, dsp::TruePeakMode mode)
    {
        capstone::Engine engine;
        engine.prepare (kRate, 512, 2);

        capstone::Parameters parameters;
        parameters.ceilingDb = ceilingDb;
        parameters.thresholdDb = -12.0;
        parameters.clipOn = false;
        parameters.limitOn = true;
        parameters.attackMs = 1.0;
        parameters.truePeak = mode;
        engine.setParameters (parameters);
        engine.reset();

        std::vector<std::vector<double>> x (2, std::vector<double> (20000));

        for (int i = 0; i < 20000; ++i)
        {
            const double v = signal == 0
                ? 0.90 * std::sin (2.0 * std::numbers::pi * 0.25 * i + std::numbers::pi * 0.25)
                : 0.90 * ((i / 3) % 2 != 0 ? 1.0 : -1.0);

            x[0][static_cast<std::size_t> (i)] = v;
            x[1][static_cast<std::size_t> (i)] = v;
        }

        const auto y = render (engine, x, 128);

        return dsp::gainToDb (truePeakOf (y, 2000), -200.0) - ceilingDb;
    };

    // Every sample sits exactly on the ceiling and the waveform between them
    // goes 3 dB past it. Measured +3.011 against the formula's 3.0103 -- the
    // detector is not approximating here, it is missing the peak entirely.
    CHECK (measure (0, dsp::TruePeakMode::Off) > 2.9);
    CHECK (measure (0, dsp::TruePeakMode::Standard) < 0.05);
    CHECK (measure (0, dsp::TruePeakMode::Strict) < 0.01);

    // And the case that decides the default. On dense near-Nyquist content the
    // sample meter is +1.51 dB over, Standard brings it to +0.26, and only
    // Strict holds the ceiling. Standard agreeing with every other dBTP meter
    // is worth having; it is still not a guarantee, and the tooltip says so.
    const double sampleOver   = measure (1, dsp::TruePeakMode::Off);
    const double standardOver = measure (1, dsp::TruePeakMode::Standard);
    const double strictOver   = measure (1, dsp::TruePeakMode::Strict);

    CHECK (sampleOver > 1.0);
    CHECK (standardOver > 0.1);
    CHECK (standardOver < sampleOver - 1.0);
    CHECK (strictOver < 0.01);
}

TEZLA_TEST (capstone_reports_the_latency_it_actually_has)
{
    // A limiter that lies about its latency is worse than one with more of it:
    // the host's delay compensation is only as good as the number it is given.
    capstone::Engine engine;
    engine.prepare (kRate, 512, 2);

    capstone::Parameters parameters;
    parameters.clipOn = false;
    parameters.limitOn = true;
    parameters.lookaheadOn = false;
    parameters.truePeak = dsp::TruePeakMode::Off;
    engine.setParameters (parameters);

    // Look-ahead off, sample peak, no clipper: exactly zero, not nearly.
    CHECK (engine.getLatencySamples() == 0);

    // Hold is free -- it widens the minimum window backwards, so it needs no
    // future samples and must not move the number.
    parameters.holdMs = 100.0;
    engine.setParameters (parameters);
    CHECK (engine.getLatencySamples() == 0);

    // The ITU filter costs its group delay whatever the look-ahead is doing.
    parameters.truePeak = dsp::TruePeakMode::Standard;
    engine.setParameters (parameters);
    CHECK (engine.getTruePeakFactor() == 4);        // 48 kHz, so the ITU ratio
    CHECK (engine.getLatencySamples() == 6);

    // And the look-ahead is the attack, in samples, at this rate.
    parameters.lookaheadOn = true;
    parameters.attackMs = 5.0;
    engine.setParameters (parameters);
    CHECK (engine.getLatencySamples() == static_cast<int> (std::lround (0.005 * kRate)) - 1 + 6);

    // Measured rather than taken on trust: an impulse through the limiter with
    // no gain reduction comes out exactly latency samples later.
    parameters.clipOn = false;
    parameters.truePeak = dsp::TruePeakMode::Off;
    parameters.attackMs = 2.0;
    parameters.ceilingDb = 6.0;         // high enough that nothing is limited
    engine.setParameters (parameters);
    engine.reset();

    const int expected = engine.getLatencySamples();

    std::vector<std::vector<double>> x (2, std::vector<double> (4096, 0.0));
    x[0][100] = 0.5;
    x[1][100] = 0.5;

    const auto y = render (engine, x, 256);

    int found = -1;

    for (std::size_t i = 0; i < y[0].size(); ++i)
        if (std::abs (y[0][i]) > 1.0e-9)
        {
            found = static_cast<int> (i);
            break;
        }

    CHECK (found == 100 + expected);
}

TEZLA_TEST (capstone_is_silent_on_silence)
{
    // Including with everything switched on. A limiter's gain rises towards
    // unity on silence, and unity times nothing is still nothing -- but a
    // feedback path or an uninitialised delay would show here.
    capstone::Engine engine;
    engine.prepare (kRate, 512, 2);

    capstone::Parameters parameters;
    parameters.thresholdDb = -30.0;
    parameters.clipOn = true;
    parameters.clipThresholdDb = -12.0;
    parameters.limitOn = true;
    parameters.attackMs = 5.0;
    parameters.autoRelease = true;
    parameters.truePeak = dsp::TruePeakMode::Strict;
    parameters.outputDb = 12.0;
    engine.setParameters (parameters);
    engine.reset();

    std::vector<std::vector<double>> x (2, std::vector<double> (8192, 0.0));
    const auto y = render (engine, x, 128);

    CHECK (peakOf (y) == 0.0);
}

TEZLA_TEST (capstone_listen_solos_what_the_stages_removed)
{
    // Listen has to be exactly the difference, so that Listen plus the output
    // reconstructs the signal that went into the two stages. Anything else is
    // a "difference" the user cannot reason about.
    const auto run = [] (bool listen)
    {
        capstone::Engine engine;
        engine.prepare (kRate, 512, 2);

        capstone::Parameters parameters;
        parameters.thresholdDb = -12.0;
        parameters.ceilingDb = -0.3;
        parameters.clipOn = true;
        parameters.clipThresholdDb = 1.0;
        parameters.clipOversampling = dsp::OversamplingMode::Off;
        parameters.limitOn = true;
        parameters.attackMs = 1.0;
        parameters.listen = listen;
        engine.setParameters (parameters);
        engine.reset();

        return render (engine, noise (8192, 1.0), 128);
    };

    const auto wet = run (false);
    const auto delta = run (true);

    // The clipper is on, so there is genuinely something to hear.
    CHECK (peakOf (delta, 1000) > 1.0e-3);

    // wet + delta is the driven signal, delayed. Reconstructing it here would
    // need the delay line; what can be checked without one is that the two are
    // complementary -- where the limiter took nothing, the delta is silent.
    capstone::Engine reference;
    reference.prepare (kRate, 512, 2);

    capstone::Parameters quiet;
    quiet.thresholdDb = 0.0;
    quiet.ceilingDb = 6.0;
    quiet.clipOn = false;
    quiet.limitOn = true;
    quiet.attackMs = 1.0;
    quiet.listen = true;
    reference.setParameters (quiet);
    reference.reset();

    // Nothing above the ceiling, nothing clipped: the delta must be silence.
    const auto nothing = render (reference, noise (8192, 0.2), 128);

    CHECK (peakOf (nothing, 1000) < 1.0e-12);
}

TEZLA_TEST (capstone_clipper_aliasing_falls_with_oversampling)
{
    // The clip stage is the only nonlinearity in the plugin, so it is the only
    // thing here that can alias. ADAA band-limits the shaper and oversampling
    // moves the images; the house rule is that neither alone is enough.
    constexpr std::size_t length = 1 << 14;
    const double frequency = binExactFrequency (1000.0, kRate, length);

    const auto aliasingAt = [&] (dsp::OversamplingMode mode)
    {
        capstone::Engine engine;
        engine.prepare (kRate, 1024, 2);

        capstone::Parameters parameters;
        parameters.clipOn = true;
        parameters.clipThresholdDb = -12.0;      // driven well into the corner
        parameters.clipShape = 0.0;              // the hard end, the worst case
        parameters.clipOversampling = mode;
        parameters.limitOn = false;
        engine.setParameters (parameters);
        engine.reset();

        std::vector<std::vector<double>> x (2, std::vector<double> (2 * length));

        for (std::size_t i = 0; i < 2 * length; ++i)
        {
            const double v = 0.9 * std::sin (2.0 * std::numbers::pi * frequency
                                             * static_cast<double> (i) / kRate);
            x[0][i] = v;
            x[1][i] = v;
        }

        const auto y = render (engine, x, 512);

        // The second half only: the first carries the oversampler's fill and
        // the DFT treats its block as circular.
        std::vector<double> settled (y[0].begin() + static_cast<long> (length), y[0].end());

        return analyseHarmonics (settled, kRate, frequency).audibleAliasingDb;
    };

    const double off = aliasingAt (dsp::OversamplingMode::Off);
    const double x4  = aliasingAt (dsp::OversamplingMode::X4);
    const double x8  = aliasingAt (dsp::OversamplingMode::X8);

    // Each step has to buy something, or the control is decoration.
    CHECK (x4 < off);
    CHECK (x8 < x4);

    // And the house target: nothing inharmonic above -60 dBFS in the audible
    // band, at the setting Auto picks for a 48 kHz session.
    CHECK (x4 < -60.0);
}

TEZLA_TEST (capstone_bypass_is_a_clean_delayed_copy_of_the_input)
{
    // A shipped bug, reported by ear before any test caught it: engaging bypass
    // produced crackling that sounded like buffer underruns.
    //
    // It was. setParameters() runs once per block and pushed the latency into
    // the BypassMixer every time; setLatency() clears the dry delay line,
    // because a ring at a new length holds nothing meaningful. So every callback
    // wiped the dry path, and the bypassed output was the first `latency`
    // samples of each block as silence followed by a fragment of signal.
    // Measured at the time: at 64-sample blocks with 53 samples of latency, 83%
    // of the output samples were exactly zero.
    //
    // Emberdrive and Halo never showed it because they call setLatency only when
    // the latency actually changes. The guard now lives in BypassMixer, so a
    // caller cannot reintroduce it -- see test_BypassMixer.cpp.
    constexpr int length = 4096;

    for (const int blockSize : { 64, 128, 512 })
    {
        capstone::Engine engine;
        engine.prepare (kRate, 512, 2);

        capstone::Parameters parameters;
        parameters.bypass = true;
        parameters.limitOn = true;
        parameters.attackMs = 1.0;
        parameters.truePeak = dsp::TruePeakMode::Standard;
        engine.setParameters (parameters);
        engine.reset();

        std::vector<std::vector<double>> x (2, std::vector<double> (length));

        for (int i = 0; i < length; ++i)
        {
            const double v = 0.5 * std::sin (2.0 * std::numbers::pi * 300.0 * i / kRate);
            x[0][static_cast<std::size_t> (i)] = v;
            x[1][static_cast<std::size_t> (i)] = v;
        }

        auto y = x;

        for (int offset = 0; offset < length; offset += blockSize)
        {
            const int span = std::min (blockSize, length - offset);
            double* pointers[2] { y[0].data() + offset, y[1].data() + offset };

            // Once per block, exactly as the processor does. That is the call
            // pattern the bug lived in, so the test has to reproduce it rather
            // than configure the engine once and render.
            engine.setParameters (parameters);
            engine.process (pointers, 2, span);
        }

        const int latency = engine.getLatencySamples();

        // Past the crossfade, the bypassed output is the input delayed by
        // exactly the reported latency. Bit-exact: a bypass that is only
        // approximately the input makes every A/B comparison a lie.
        bool exact = true;
        int zeros = 0;

        for (int i = 1200; i < length; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            if (y[0][index] != x[0][static_cast<std::size_t> (i - latency)])
                exact = false;

            if (y[0][index] == 0.0)
                ++zeros;
        }

        CHECK (exact);

        // And stated the way the failure presented, so a regression is
        // recognisable from the symptom rather than only from the assertion.
        CHECK (zeros == 0);
    }
}
