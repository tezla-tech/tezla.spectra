// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "IctusEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <tezla/dsp/Decibels.hpp>

namespace tezla::ictus {

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize_ = std::max (maxBlockSize, 1);

    // Every stage is built at prepare, so a factor change later touches no
    // memory (Oversampler's contract). prepare() runs before any parameter
    // is known, which is why process() checks the factor it actually built
    // against the one the parameters ask for (CLAUDE.md section 7).
    oversampler_.prepare (maxBlockSize_, 2,
                          dsp::oversamplingFactor (effectiveOversampling(), sampleRate_));

    rebuildForRate();
}

void Engine::rebuildForRate() noexcept
{
    internalRate_ = sampleRate_ * oversampler_.getFactor();

    kick1_.prepare (internalRate_);
    kick2_.prepare (internalRate_);

    masterGain_.prepare (internalRate_, 0.02);
    masterGain_.setCurrentAndTarget (dsp::dbToGain (parameters_.masterDb));

    reset();
}

void Engine::reset() noexcept
{
    kick1_.reset();
    kick2_.reset();
    oversampler_.reset();

    sinceControl_ = 0;
    idleSamples_ = 0;

    for (auto& channel : carry_)
        for (auto& sample : channel)
            sample = 0.0;
}

void Engine::reconcileFactor() noexcept
{
    // The factor is a graph change: checked against what was actually built
    // rather than against a "parameters arrived" flag (CLAUDE.md section 7).
    // Done here as well as at the top of process(), because a hit started
    // between two calls would otherwise be wiped by the rebuild the next
    // call performs -- the first note after a factor change would vanish.
    // A change cuts every hit: an audible stop rather than a hit whose
    // filters were rebuilt underneath it.
    const int wanted = dsp::oversamplingFactor (effectiveOversampling(), sampleRate_);

    if (wanted != oversampler_.getFactor())
    {
        oversampler_.setFactor (wanted);
        rebuildForRate();
    }
}

void Engine::startKick (Pad<KickEngine>& pad, PadIndex index, const KickSettings& settings,
                        int note, double velocity, bool keyed) noexcept
{
    reconcileFactor();

    ++hitCount_;

    const std::uint64_t seed = (kSeedBase ^ (kPadSalt * static_cast<std::uint64_t> (static_cast<int> (index) + 1)))
                             + kHitGolden * hitCount_;

    // The landed pitch: from the key through the tuning in Bass mode or
    // with Follow key lit, else the pad's own Tune.
    const double endHz = (keyed || settings.followKey) ? tuning_.frequencyFor (note)
                                                       : settings.tuneHz;

    // A note between two process() calls lands at whatever offset the
    // control grid is at; the hit gets that many samples as its first,
    // partial chunk. Zero means the tick is due and will serve it.
    const int toBoundary = sinceControl_ > 0 ? sinceControl_ : 0;

    pad.start (note, settings, endHz, velocity, seed, toBoundary);
}

void Engine::noteOn (int note, double velocity) noexcept
{
    // Bass mode: every key is Kick 1 at that key's pitch, nothing else
    // sounds. A tuned sub-bass instrument made of the kick.
    if (parameters_.bassMode)
    {
        startKick (kick1_, PadIndex::kick1, parameters_.kick1, note, velocity, true);
        return;
    }

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::kick1)])
        startKick (kick1_, PadIndex::kick1, parameters_.kick1, note, velocity, false);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::kick2)])
        startKick (kick2_, PadIndex::kick2, parameters_.kick2, note, velocity, false);

    // The other six pads arrive with their engines (I3, I4).
}

void Engine::noteOff (int note) noexcept
{
    if (parameters_.bassMode)
    {
        kick1_.release (note);
        return;
    }

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::kick1)])
        kick1_.release (note);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::kick2)])
        kick2_.release (note);
}

void Engine::allNotesOff() noexcept
{
    kick1_.choke();
    kick2_.choke();
}

void Engine::choke (PadIndex pad) noexcept
{
    if (pad == PadIndex::kick1)
        kick1_.choke();
    else if (pad == PadIndex::kick2)
        kick2_.choke();
}

int Engine::activeHitCount() const noexcept
{
    return kick1_.activeHits() + kick2_.activeHits();
}

void Engine::controlTick() noexcept
{
    kick1_.advanceControl (kControlIntervalSamples);
    kick2_.advanceControl (kControlIntervalSamples);
}

void Engine::renderChunk (double* left, double* right, int numSamples) noexcept
{
    bool silent = true;

    for (int i = 0; i < numSamples; ++i)
    {
        const double x = (kick1_.process() + kick2_.process()) * masterGain_.next();

        // Mono into both channels until the chain and the pan arrive (I5).
        left[i] = x;
        right[i] = x;

        silent = silent && std::abs (x) < kIdleThreshold;
    }

    if (silent && activeHitCount() == 0)
        idleSamples_ += numSamples;
    else
        idleSamples_ = 0;
}

void Engine::process (double* const* output, int numSamples) noexcept
{
    const dsp::ScopedNoDenormals guard;

    if (numSamples <= 0)
        return;

    reconcileFactor();

    masterGain_.setTarget (dsp::dbToGain (parameters_.masterDb));

    const int factor = oversampler_.getFactor();
    const int internalSamples = numSamples * factor;

    // An idle instrument costs nothing. Nothing here can wake by itself:
    // every stage is a hit that has retired exactly or a filter parked below
    // -240 dBFS, and the counter resets the instant a hit starts.
    if (idleSamples_ >= static_cast<int> (internalRate_ * kIdleSecondsBeforeSkipping)
        && activeHitCount() == 0)
    {
        for (int channel = 0; channel < 2; ++channel)
            std::fill (output[channel], output[channel] + numSamples, 0.0);

        return;
    }

    double* const* internal = oversampler_.internalBuffers();

    int done = 0;

    while (done < internalSamples)
    {
        if (sinceControl_ <= 0)
        {
            controlTick();
            sinceControl_ = kControlIntervalSamples;
        }

        const int take = std::min (sinceControl_, internalSamples - done);

        renderChunk (internal[0] + done, internal[1] + done, take);

        done += take;
        sinceControl_ -= take;
    }

    // Half a host sample of delay before the decimators, so the latency the
    // plugin declares is the whole number the instrument really has (see
    // getLatencySamples()). The block is shifted right by factor / 2 internal
    // samples; the samples pushed off its end lead the next block.
    if (const int shift = factor / 2; shift > 0)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            double* buffer = internal[channel];
            double tail[4] {};

            for (int i = 0; i < shift; ++i)
                tail[i] = buffer[internalSamples - shift + i];

            std::memmove (buffer + shift, buffer,
                          static_cast<std::size_t> (internalSamples - shift) * sizeof (double));

            for (int i = 0; i < shift; ++i)
            {
                buffer[i] = carry_[channel][i];
                carry_[channel][i] = tail[i];
            }
        }
    }

    oversampler_.downsample (output, numSamples);
}

} // namespace tezla::ictus
