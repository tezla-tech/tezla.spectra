// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TranspectusEngine.hpp"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>

namespace tezla::transpectus
{

namespace
{
/// How fast the short-window true peak behind PSR falls back, per sample at
/// 48 kHz. Slow enough to read, fast enough to follow a chorus.
constexpr double kShortPeakFallPerSecond = 12.0;

/// Nothing below this is a reading.
constexpr double kFloorDb = -200.0;
} // namespace

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_  = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels_ = std::clamp (numChannels, 1, kMaxChannels);

    loudness_.prepare (sampleRate_, numChannels_);
    stereo_.prepare (sampleRate_);

    for (auto& detector : detectors_)
        detector.prepare (dsp::TruePeakDetector::kMaxFactor);

    // Two seconds of history, so the editor can pull a 2048-point window
    // whenever it happens to tick.
    capture_.prepare (static_cast<int> (sampleRate_ * 2.0));

    // Sized in seconds, so the goniometer spans the same slice of time at
    // 44.1 as at 192 kHz rather than showing a quarter of the music.
    scope_.prepare (sampleRate_);

    monoScratch_.assign (static_cast<std::size_t> (std::max (maxBlockSize, 1)), 0.0);

    setParameters (parameters_);
    reset();
}

void Engine::reset()
{
    loudness_.reset();
    stereo_.reset();
    scope_.reset();

    for (auto& detector : detectors_)
        detector.reset();

    truePeakDb_.store (kFloorDb, std::memory_order_relaxed);
    shortTruePeakDb_.store (kFloorDb, std::memory_order_relaxed);
    resetCorrelationHold();
}

void Engine::setParameters (const Parameters& parameters)
{
    parameters_ = parameters;

    const int factor = dsp::truePeakFactorFor (parameters_.truePeak, sampleRate_);

    for (auto& detector : detectors_)
        detector.setFactor (factor);

    stereo_.setCrossovers (parameters_.monoCheckHz,
                           dsp::StereoAnalyser::kDefaultHighCrossoverHz);
}

void Engine::resetMeasurement() noexcept
{
    loudness_.resetIntegration();
    truePeakDb_.store (kFloorDb, std::memory_order_relaxed);
    shortTruePeakDb_.store (kFloorDb, std::memory_order_relaxed);
    resetCorrelationHold();
}

void Engine::process (const double* const* channels, int numChannels, int numSamples) noexcept
{
    const dsp::ScopedNoDenormals noDenormals;

    if (numSamples <= 0 || numChannels <= 0)
        return;

    const int active = std::min (numChannels, numChannels_);
    const auto span = static_cast<std::size_t> (std::min (numSamples,
                                                          static_cast<int> (monoScratch_.size())));

    loudness_.process (channels, active, numSamples);

    if (active >= 2)
    {
        stereo_.process (channels, active, numSamples);

        // The worst moment, held. Plain loads and stores: the audio thread
        // is the only writer, and a display race costs one stale frame.
        const double correlation = stereo_.getCorrelation();
        const double low = stereo_.getBandCorrelation (dsp::StereoAnalyser::low);

        if (correlation < minCorrelation_.load (std::memory_order_relaxed))
            minCorrelation_.store (correlation, std::memory_order_relaxed);

        if (low < minLowCorrelation_.load (std::memory_order_relaxed))
            minLowCorrelation_.store (low, std::memory_order_relaxed);
    }

    // The scope takes whatever it is given: a mono input draws the 45-degree
    // line a mono signal is, rather than nothing.
    scope_.push (channels, active, numSamples);

    // True peak, per channel, held. The hold is what makes it a delivery
    // reading rather than a flicker: a master's dBTP is its worst moment, not
    // its current one.
    double blockPeak = 0.0;

    for (int channel = 0; channel < active; ++channel)
    {
        auto& detector = detectors_[static_cast<std::size_t> (channel)];

        for (int i = 0; i < numSamples; ++i)
            blockPeak = std::max (blockPeak, detector.process (channels[channel][i]));
    }

    const double blockPeakDb = dsp::gainToDb (blockPeak, kFloorDb);

    if (blockPeakDb > truePeakDb_.load (std::memory_order_relaxed))
        truePeakDb_.store (blockPeakDb, std::memory_order_relaxed);

    // And a decaying one for PSR, which has to move with the music.
    const double fall = kShortPeakFallPerSecond * numSamples / sampleRate_;
    const double decayed = shortTruePeakDb_.load (std::memory_order_relaxed) - fall;
    shortTruePeakDb_.store (std::max (blockPeakDb, decayed), std::memory_order_relaxed);

    // The mono sum for the spectrum. Halved rather than summed, so two
    // identical channels read as one rather than 6 dB louder than one.
    for (std::size_t i = 0; i < span; ++i)
    {
        double sum = 0.0;

        for (int channel = 0; channel < active; ++channel)
            sum += channels[channel][i];

        monoScratch_[i] = sum / static_cast<double> (active);
    }

    capture_.push (monoScratch_.data(), static_cast<int> (span));
}

double Engine::getPlr() const noexcept
{
    const double loudness = loudness_.getIntegratedLufs();
    const double peak = truePeakDb_.load (std::memory_order_relaxed);

    if (loudness <= dsp::LoudnessMeter::kSilenceLufs || peak <= kFloorDb)
        return 0.0;

    return peak - loudness;
}

double Engine::getPsr() const noexcept
{
    const double loudness = loudness_.getShortTermLufs();
    const double peak = shortTruePeakDb_.load (std::memory_order_relaxed);

    if (loudness <= dsp::LoudnessMeter::kSilenceLufs || peak <= kFloorDb)
        return 0.0;

    return peak - loudness;
}

const LoudnessTarget& Engine::getTarget() const noexcept
{
    return kLoudnessTargets[static_cast<std::size_t> (
        std::clamp (parameters_.targetIndex, 0, kNumLoudnessTargets - 1))];
}

double Engine::getTargetDeltaDb() const noexcept
{
    const double loudness = loudness_.getIntegratedLufs();

    if (loudness <= dsp::LoudnessMeter::kSilenceLufs)
        return 0.0;

    const auto& target = getTarget();
    const double delta = loudness - target.lufs;

    // The half that gets forgotten. Several platforms only ever turn a loud
    // master down; a quiet one on YouTube plays quiet, and reporting "it will
    // be turned up 6 dB" there would be simply false.
    if (delta < 0.0 && ! target.boostsQuietMaterial)
        return 0.0;

    return delta;
}

} // namespace tezla::transpectus
