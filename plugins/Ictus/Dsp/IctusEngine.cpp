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
    snare1_.prepare (internalRate_);
    snare2_.prepare (internalRate_);
    perc_.prepare (internalRate_);
    hatClosed_.prepare (internalRate_);
    hatOpen_.prepare (internalRate_);
    clap_.prepare (internalRate_);

    masterGain_.prepare (internalRate_, 0.02);
    masterGain_.setCurrentAndTarget (dsp::dbToGain (parameters_.masterDb));

    for (int pad = 0; pad < kPadCount; ++pad)
    {
        pan_[pad].prepare (internalRate_, 0.02);
        pan_[pad].setCurrentAndTarget (std::clamp (parameters_.pan[pad], -1.0, 1.0));
    }

    pansPrimed_ = false;

    reset();
}

void Engine::reset() noexcept
{
    kick1_.reset();
    kick2_.reset();
    snare1_.reset();
    snare2_.reset();
    perc_.reset();
    hatClosed_.reset();
    hatOpen_.reset();
    clap_.reset();
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

std::uint64_t Engine::nextSeed (PadIndex index) noexcept
{
    ++hitCount_;
    ++padHits_[static_cast<int> (index)];

    return (kSeedBase ^ (kPadSalt * static_cast<std::uint64_t> (static_cast<int> (index) + 1)))
         + kHitGolden * hitCount_;
}

void Engine::startKick (Pad<KickEngine>& pad, PadIndex index, const KickSettings& settings,
                        int note, double velocity, bool keyed) noexcept
{
    reconcileFactor();

    const std::uint64_t seed = nextSeed (index);
    padVelocity_[static_cast<int> (index)] = velocity;

    // The landed pitch: from the key through the tuning in Bass mode or
    // with Follow key lit, else the pad's own Tune -- snapped to the
    // tuning's nearest degree with Note lit, so a drum can sit in the key
    // of the bass line (12-TET until a scale is loaded; then that scale).
    const double endHz = (keyed || settings.followKey) ? tuning_.frequencyFor (note)
                       : settings.noteSnap ? tuning_.nearestScaleHz (settings.tuneHz)
                                           : settings.tuneHz;

    // A note between two process() calls lands at whatever offset the
    // control grid is at; the hit gets that many samples as its first,
    // partial chunk. Zero means the tick is due and will serve it.
    const int toBoundary = sinceControl_ > 0 ? sinceControl_ : 0;

    pad.start (note, settings, endHz, velocity, seed, toBoundary);
}

void Engine::startSnare (Pad<SnareEngine>& pad, PadIndex index, const SnareSettings& settings,
                         int note, double velocity) noexcept
{
    reconcileFactor();

    const std::uint64_t seed = nextSeed (index);
    padVelocity_[static_cast<int> (index)] = velocity;

    const double fundamentalHz = settings.followKey ? tuning_.frequencyFor (note)
                               : settings.noteSnap ? tuning_.nearestScaleHz (settings.tuneHz)
                                                   : settings.tuneHz;

    const int toBoundary = sinceControl_ > 0 ? sinceControl_ : 0;

    pad.start (note, settings, fundamentalHz, velocity, seed, toBoundary);
}

void Engine::startHat (Pad<HatEngine>& pad, PadIndex index, bool open,
                       int note, double velocity) noexcept
{
    reconcileFactor();

    const std::uint64_t seed = nextSeed (index);
    padVelocity_[static_cast<int> (index)] = velocity;

    const int toBoundary = sinceControl_ > 0 ? sinceControl_ : 0;

    pad.start (note, parameters_.hat, open, velocity, seed, toBoundary);
}

void Engine::startClap (int note, double velocity) noexcept
{
    reconcileFactor();

    const std::uint64_t seed = nextSeed (PadIndex::clap);
    padVelocity_[static_cast<int> (PadIndex::clap)] = velocity;

    const int toBoundary = sinceControl_ > 0 ? sinceControl_ : 0;

    clap_.start (note, parameters_.clap, velocity, seed, toBoundary);
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

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::snare1)])
        startSnare (snare1_, PadIndex::snare1, parameters_.snare1, note, velocity);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::snare2)])
        startSnare (snare2_, PadIndex::snare2, parameters_.snare2, note, velocity);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::perc)])
        startSnare (perc_, PadIndex::perc, parameters_.perc, note, velocity);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::hatClosed)])
    {
        // The foot on the pedal: a closed hit silences whatever the open pad
        // is ringing, before it strikes. Skipped when the two pads are on the
        // same key, where the user has asked for both and choking one with
        // the other would leave a hat that cannot sound.
        if (parameters_.hat.choke
            && parameters_.padNotes[static_cast<int> (PadIndex::hatOpen)] != note)
            hatOpen_.choke();

        startHat (hatClosed_, PadIndex::hatClosed, false, note, velocity);
    }

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::hatOpen)])
        startHat (hatOpen_, PadIndex::hatOpen, true, note, velocity);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::clap)])
        startClap (note, velocity);
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

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::snare1)])
        snare1_.release (note);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::snare2)])
        snare2_.release (note);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::perc)])
        perc_.release (note);

    // Both hat pads take a note-off when Gate is lit -- one control set, so
    // the closed and open pads gate together -- and so does the clap.
    if (note == parameters_.padNotes[static_cast<int> (PadIndex::hatClosed)])
        hatClosed_.release (note);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::hatOpen)])
        hatOpen_.release (note);

    if (note == parameters_.padNotes[static_cast<int> (PadIndex::clap)])
        clap_.release (note);
}

void Engine::allNotesOff() noexcept
{
    kick1_.choke();
    kick2_.choke();
    snare1_.choke();
    snare2_.choke();
    perc_.choke();
    hatClosed_.choke();
    hatOpen_.choke();
    clap_.choke();
}

void Engine::choke (PadIndex pad) noexcept
{
    switch (pad)
    {
        case PadIndex::kick1:  kick1_.choke();  break;
        case PadIndex::kick2:  kick2_.choke();  break;
        case PadIndex::snare1: snare1_.choke(); break;
        case PadIndex::snare2: snare2_.choke(); break;
        case PadIndex::perc:   perc_.choke();   break;

        case PadIndex::hatClosed: hatClosed_.choke(); break;
        case PadIndex::hatOpen:   hatOpen_.choke();   break;
        case PadIndex::clap:      clap_.choke();      break;

        case PadIndex::count:
        default:
            break;
    }
}

int Engine::activeHitCount() const noexcept
{
    return kick1_.activeHits() + kick2_.activeHits()
         + snare1_.activeHits() + snare2_.activeHits() + perc_.activeHits()
         + hatClosed_.activeHits() + hatOpen_.activeHits() + clap_.activeHits();
}

void Engine::controlTick() noexcept
{
    kick1_.advanceControl (kControlIntervalSamples);
    kick2_.advanceControl (kControlIntervalSamples);
    snare1_.advanceControl (kControlIntervalSamples);
    snare2_.advanceControl (kControlIntervalSamples);
    perc_.advanceControl (kControlIntervalSamples);
    hatClosed_.advanceControl (kControlIntervalSamples);
    hatOpen_.advanceControl (kControlIntervalSamples);
    clap_.advanceControl (kControlIntervalSamples);
}

void Engine::renderChunk (double* left, double* right, int numSamples) noexcept
{
    bool silent = true;

    constexpr int iKick1 = static_cast<int> (PadIndex::kick1);
    constexpr int iKick2 = static_cast<int> (PadIndex::kick2);
    constexpr int iSnare1 = static_cast<int> (PadIndex::snare1);
    constexpr int iSnare2 = static_cast<int> (PadIndex::snare2);
    constexpr int iPerc = static_cast<int> (PadIndex::perc);
    constexpr int iHatClosed = static_cast<int> (PadIndex::hatClosed);
    constexpr int iHatOpen = static_cast<int> (PadIndex::hatOpen);
    constexpr int iClap = static_cast<int> (PadIndex::clap);

    for (int i = 0; i < numSamples; ++i)
    {
        const double kick1 = kick1_.process();
        const double kick2 = kick2_.process();
        const double snare1 = snare1_.process();
        const double snare2 = snare2_.process();
        const double perc = perc_.process();
        const double hatClosed = hatClosed_.process();
        const double hatOpen = hatOpen_.process();
        const double clap = clap_.process();

        const double pKick1 = pan_[iKick1].next();
        const double pKick2 = pan_[iKick2].next();
        const double pSnare1 = pan_[iSnare1].next();
        const double pSnare2 = pan_[iSnare2].next();
        const double pPerc = pan_[iPerc].next();
        const double pHatClosed = pan_[iHatClosed].next();
        const double pHatOpen = pan_[iHatOpen].next();
        const double pClap = pan_[iClap].next();

        // The balance law, pad by pad, summed in the order the mono engine
        // always summed in: with every pan at centre each gain is exactly 1.0
        // and both channels are the old render bit for bit (the round-1
        // golden render in plugins/Ictus/PLAN.md).
        double l = kick1 * balanceLeft (pKick1);
        l += kick2 * balanceLeft (pKick2);
        l += snare1 * balanceLeft (pSnare1);
        l += snare2 * balanceLeft (pSnare2);
        l += perc * balanceLeft (pPerc);
        l += hatClosed * balanceLeft (pHatClosed);
        l += hatOpen * balanceLeft (pHatOpen);
        l += clap * balanceLeft (pClap);

        double r = kick1 * balanceRight (pKick1);
        r += kick2 * balanceRight (pKick2);
        r += snare1 * balanceRight (pSnare1);
        r += snare2 * balanceRight (pSnare2);
        r += perc * balanceRight (pPerc);
        r += hatClosed * balanceRight (pHatClosed);
        r += hatOpen * balanceRight (pHatOpen);
        r += clap * balanceRight (pClap);

        const double master = masterGain_.next();

        left[i] = l * master;
        right[i] = r * master;

        silent = silent && std::abs (left[i]) < kIdleThreshold && std::abs (right[i]) < kIdleThreshold;
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

    // The pans: jumped to on the first call after a rebuild, ramped after.
    // prepare() runs before any parameter is known, so a smoother primed
    // there starts at centre, and a pad saved hard left would open every
    // project drifting across the field for its first hits.
    for (int pad = 0; pad < kPadCount; ++pad)
    {
        const double wanted = std::clamp (parameters_.pan[pad], -1.0, 1.0);

        if (pansPrimed_)
            pan_[pad].setTarget (wanted);
        else
            pan_[pad].setCurrentAndTarget (wanted);
    }

    pansPrimed_ = true;

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
