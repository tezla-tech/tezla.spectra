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
#include <string>
#include <vector>

#include <tezla/dsp/Decibels.hpp>

#include "TranspectusEngine.hpp"

using namespace tezla;

namespace
{
constexpr double kRate = 48000.0;

std::vector<std::vector<double>> tone (double seconds, double levelDbFs,
                                       double frequency = 1000.0)
{
    const auto length = static_cast<std::size_t> (kRate * seconds);
    const double amplitude = std::pow (10.0, levelDbFs / 20.0);

    std::vector<std::vector<double>> x (2, std::vector<double> (length));

    for (std::size_t i = 0; i < length; ++i)
    {
        const double v = amplitude
            * std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / kRate);

        x[0][i] = v;
        x[1][i] = v;
    }

    return x;
}

void feed (transpectus::Engine& engine, const std::vector<std::vector<double>>& signal,
           int blockSize = 512)
{
    const auto total = static_cast<int> (signal[0].size());

    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int span = std::min (blockSize, total - offset);
        const double* pointers[2] { signal[0].data() + offset, signal[1].data() + offset };
        engine.process (pointers, 2, span);
    }
}
} // namespace

TEZLA_TEST (transpectus_never_touches_the_audio)
{
    // The whole contract. An analyser that coloured the signal would be worse
    // than no analyser, because you would be mixing into it -- and the failure
    // would be invisible, since nobody A/Bs a meter.
    transpectus::Engine engine;
    engine.prepare (kRate, 512, 2);

    const auto original = tone (2.0, -6.0, 220.0);
    auto working = original;

    feed (engine, working);

    bool untouched = true;

    for (std::size_t c = 0; c < original.size(); ++c)
        for (std::size_t i = 0; i < original[c].size(); ++i)
            if (working[c][i] != original[c][i])
                untouched = false;

    CHECK (untouched);
}

TEZLA_TEST (transpectus_measures_loudness_and_true_peak_together)
{
    // A -23 dBFS sine: -23 LUFS by the standard, and a true peak of -23 dBTP
    // because a sine's peak is its amplitude. Both from the same signal, which
    // is what makes PLR a subtraction rather than two measurements.
    transpectus::Engine engine;
    engine.prepare (kRate, 512, 2);

    feed (engine, tone (10.0, -23.0));

    CHECK (std::abs (engine.getIntegratedLufs() + 23.0) < 0.1);
    CHECK (std::abs (engine.getTruePeakDb() + 23.0) < 0.2);

    // A sine has a crest factor of 3.01 dB, and PLR is peak minus loudness, so
    // the -0.691 offset and the K-weighting cancel out to about 0.
    CHECK (std::abs (engine.getPlr()) < 0.3);
}

TEZLA_TEST (transpectus_plr_falls_when_the_transients_are_squashed)
{
    // The number nobody meters, doing the job it exists for. Same peak level,
    // very different dynamics: a sine is at its peak most of the time, an
    // impulse train almost never.
    const auto peakDbOf = [] (const std::vector<std::vector<double>>& signal)
    {
        transpectus::Engine engine;
        engine.prepare (kRate, 512, 2);
        feed (engine, signal);
        return engine.getPlr();
    };

    // Sparse impulses at full scale: enormous peak, very little loudness.
    std::vector<std::vector<double>> spiky (2, std::vector<double> (static_cast<std::size_t> (kRate * 10), 0.0));

    for (std::size_t i = 0; i < spiky[0].size(); i += 4800)
    {
        spiky[0][i] = 0.5;
        spiky[1][i] = 0.5;
    }

    const double spikyPlr = peakDbOf (spiky);
    const double sinePlr  = peakDbOf (tone (10.0, -6.0));

    // The spiky signal has far more headroom above its own loudness.
    CHECK (spikyPlr > sinePlr + 15.0);

    // And the sine's is the textbook value for a sine, near zero.
    CHECK (std::abs (sinePlr) < 0.5);
}

TEZLA_TEST (transpectus_says_what_a_platform_will_do_and_which_way)
{
    // The readout the plugin is for -- and the half that gets forgotten: some
    // platforms only ever turn a loud master down, so being under the target
    // there means nothing happens rather than being turned up.
    const auto deltaFor = [] (double levelDbFs, const char* platform)
    {
        transpectus::Engine engine;
        engine.prepare (kRate, 512, 2);

        transpectus::Parameters parameters;

        for (int i = 0; i < transpectus::kNumLoudnessTargets; ++i)
            if (std::string (transpectus::kLoudnessTargets[static_cast<std::size_t> (i)].name)
                == platform)
                parameters.targetIndex = i;

        engine.setParameters (parameters);
        feed (engine, tone (10.0, levelDbFs));

        return engine.getTargetDeltaDb();
    };

    // A loud master: -8 LUFS against Spotify's -14 is 6 dB of attenuation.
    CHECK (std::abs (deltaFor (-8.0, "Spotify") - 6.0) < 0.3);

    // The same master on Apple Music, which normalises to -16.
    CHECK (std::abs (deltaFor (-8.0, "Apple Music") - 8.0) < 0.3);

    // A quiet master on Spotify, which boosts: it is turned up.
    CHECK (deltaFor (-20.0, "Spotify") < -5.0);

    // The same quiet master on YouTube, which does not boost: nothing happens,
    // and saying "it will be turned up 6 dB" there would be simply false.
    CHECK (deltaFor (-20.0, "YouTube") == 0.0);
}

TEZLA_TEST (transpectus_carries_the_sub_check_through_to_the_engine)
{
    // The band correlation, reached the way the panel reaches it.
    transpectus::Engine engine;
    engine.prepare (kRate, 512, 2);

    const auto length = static_cast<std::size_t> (kRate * 3);
    std::vector<std::vector<double>> x (2, std::vector<double> (length));

    for (std::size_t i = 0; i < length; ++i)
    {
        const double t = static_cast<double> (i) / kRate;
        const double mix = 0.5 * std::sin (2.0 * std::numbers::pi * 1200.0 * t);
        const double sub = 0.10 * std::sin (2.0 * std::numbers::pi * 45.0 * t);

        x[0][i] = mix + sub;
        x[1][i] = mix - sub;
    }

    feed (engine, x);

    CHECK (engine.getCorrelation() > 0.85);
    CHECK (! engine.isLowBandMonoSafe());
}

TEZLA_TEST (transpectus_reading_is_independent_of_the_host_block_size)
{
    double first = 0.0;
    bool same = true;

    for (const int blockSize : { 1, 64, 512, 4096 })
    {
        transpectus::Engine engine;
        engine.prepare (kRate, 4096, 2);
        feed (engine, tone (5.0, -14.0), blockSize);

        const double reading = engine.getIntegratedLufs();

        if (blockSize == 1)
            first = reading;
        else if (std::abs (reading - first) > 1.0e-9)
            same = false;
    }

    CHECK (same);
}

TEZLA_TEST (transpectus_resets_its_measurement_without_a_transport_stop)
{
    transpectus::Engine engine;
    engine.prepare (kRate, 512, 2);

    feed (engine, tone (5.0, -10.0));
    CHECK (std::abs (engine.getIntegratedLufs() + 10.0) < 0.2);
    CHECK (engine.getTruePeakDb() > -11.0);

    engine.resetMeasurement();

    CHECK (engine.getIntegratedLufs() <= dsp::LoudnessMeter::kSilenceLufs);
    CHECK (engine.getTruePeakDb() <= -199.0);

    // And it starts measuring again rather than staying cleared.
    feed (engine, tone (5.0, -10.0));
    CHECK (std::abs (engine.getIntegratedLufs() + 10.0) < 0.2);
}

TEZLA_TEST (transpectus_is_quiet_about_silence)
{
    transpectus::Engine engine;
    engine.prepare (kRate, 512, 2);

    const std::vector<std::vector<double>> quiet (2, std::vector<double> (static_cast<std::size_t> (kRate * 3), 0.0));
    feed (engine, quiet);

    CHECK (engine.getIntegratedLufs() <= dsp::LoudnessMeter::kSilenceLufs);

    // No reading rather than a nonsense one, and no NaN anywhere.
    CHECK (engine.getPlr() == 0.0);
    CHECK (engine.getPsr() == 0.0);
    CHECK (engine.getTargetDeltaDb() == 0.0);

    // Two silent channels agree with each other.
    CHECK (engine.getCorrelation() == 1.0);
}
