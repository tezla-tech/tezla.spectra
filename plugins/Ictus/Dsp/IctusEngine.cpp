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
#include <tezla/dsp/Exact.hpp>

namespace tezla::ictus {

namespace
{
/// The order the mono engine always summed the pads in -- kept, so that with
/// nothing spread and every pan at centre the two channels are the old
/// render bit for bit (the golden renders in plugins/Ictus/PLAN.md).
constexpr int kSumOrder[kPadCount] = {
    static_cast<int> (PadIndex::kick1),  static_cast<int> (PadIndex::kick2),
    static_cast<int> (PadIndex::snare1), static_cast<int> (PadIndex::snare2),
    static_cast<int> (PadIndex::perc),   static_cast<int> (PadIndex::hatClosed),
    static_cast<int> (PadIndex::hatOpen), static_cast<int> (PadIndex::clap)
};

constexpr int kSnare1 = static_cast<int> (PadIndex::snare1);
} // namespace

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

    // The rooms' lines are sized for the largest internal rate the factor
    // can reach, here, off the audio thread; rebuildForRate() then only
    // re-aims them.
    for (auto& room : room_)
        room.prepare (sampleRate_ * kMaxFactor, dsp::EarlyReflections::kMaxSeconds);

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
    snareClap_.prepare (internalRate_);

    masterGain_.prepare (internalRate_, 0.02);
    masterGain_.setCurrentAndTarget (dsp::dbToGain (parameters_.masterDb));

    for (int pad = 0; pad < kPadCount; ++pad)
    {
        pan_[pad].prepare (internalRate_, 0.02);
        pan_[pad].setCurrentAndTarget (std::clamp (parameters_.pan[pad], -1.0, 1.0));

        width_[pad].prepare (internalRate_, 0.02);
        width_[pad].setCurrentAndTarget (std::clamp (parameters_.width[pad], 0.0, 2.0));

        monoBelow_[pad].prepare (internalRate_);
        monoBelow_[pad].setMode (dsp::SvfMode::highpass);
        monoBelow_[pad].setResonance (dsp::SvfFilter::resonanceForQ (kMonoBelowQ));
    }

    sideRingSamples_ = std::max (1, static_cast<int> (kSideRingSeconds * internalRate_));

    for (int index = 0; index < kRoomCount; ++index)
    {
        auto& room = room_[index];
        const auto& settings = parameters_.room[index];

        room.setSampleRate (internalRate_);
        room.design (std::clamp (settings.seconds, dsp::EarlyReflections::kMinSeconds,
                                 dsp::EarlyReflections::kMaxSeconds),
                     kRoomSeeds[index]);
        room.setToneHz (settings.toneHz);

        roomLevel_[index].prepare (internalRate_, 0.02);
        roomLevel_[index].setCurrentAndTarget (std::clamp (settings.level, 0.0, 1.0));
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
    snareClap_.reset();
    oversampler_.reset();

    for (int pad = 0; pad < kPadCount; ++pad)
    {
        monoBelow_[pad].reset();
        sideRing_[pad] = 0;
    }

    for (auto& room : room_)
        room.reset();

    snareClapPending_ = -1;
    snareClapNote_ = -1;
    snareClapVelocity_ = 0.0;

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

void Engine::aimRoom (PadIndex pad) noexcept
{
    const int index = roomIndexFor (pad);

    if (index < 0)
        return;

    const auto& settings = parameters_.room[index];

    // Not run at all with the level at 0 and nothing ringing, so a room
    // nobody has turned up costs nothing -- not even a redesign.
    if (dsp::isExactlyZero (std::clamp (settings.level, 0.0, 1.0)) && ! room_[index].isActive()
        && dsp::isExactlyZero (roomLevel_[index].getCurrent()))
        return;

    const double wanted = std::clamp (settings.seconds, dsp::EarlyReflections::kMinSeconds,
                                      dsp::EarlyReflections::kMaxSeconds);

    if (! dsp::isExactly (wanted, room_[index].getLengthSeconds()))
        room_[index].design (wanted, kRoomSeeds[index]);
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

    aimRoom (index);
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

    aimRoom (index);
    pad.start (note, settings, fundamentalHz, velocity, seed, toBoundary);

    if (index == PadIndex::snare1)
        scheduleSnareClapLayer (note, velocity);
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

    aimRoom (PadIndex::clap);
    clap_.start (note, parameters_.clap, velocity, seed, toBoundary);
}

void Engine::scheduleSnareClapLayer (int note, double velocity) noexcept
{
    const double amount = std::clamp (parameters_.snareClap, 0.0, 1.0);

    if (dsp::isExactlyZero (amount))
    {
        snareClapPending_ = -1;
        return;
    }

    snareClapNote_ = note;
    snareClapVelocity_ = velocity;

    const int offset = static_cast<int> (std::lround (
        std::clamp (parameters_.snareClapOffsetSeconds, 0.0, 0.05) * internalRate_));

    if (offset <= 0)
        startSnareClapLayer();
    else
        snareClapPending_ = offset;
}

void Engine::startSnareClapLayer() noexcept
{
    snareClapPending_ = -1;

    // The CLAP page's sound at the snare's own Clap level, whatever the clap
    // pad's Level is doing -- muting the clap pad must not mute the snare.
    ClapSettings layer = parameters_.clap;
    layer.level = std::clamp (parameters_.snareClap, 0.0, 1.0);

    const std::uint64_t seed = (kSeedBase ^ (kPadSalt * kSnareClapSalt)) + kHitGolden * hitCount_;
    const int toBoundary = sinceControl_ > 0 ? sinceControl_ : 0;

    snareClap_.start (snareClapNote_, layer, snareClapVelocity_, seed, toBoundary);
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
    {
        snare1_.release (note);
        snareClap_.release (note);
    }

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
    snareClap_.choke();
    snareClapPending_ = -1;
}

void Engine::choke (PadIndex pad) noexcept
{
    switch (pad)
    {
        case PadIndex::kick1:  kick1_.choke();  break;
        case PadIndex::kick2:  kick2_.choke();  break;

        case PadIndex::snare1:
            snare1_.choke();
            snareClap_.choke();
            snareClapPending_ = -1;
            break;

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
         + hatClosed_.activeHits() + hatOpen_.activeHits() + clap_.activeHits()
         + snareClap_.activeHits();
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
    snareClap_.advanceControl (kControlIntervalSamples);

    // The continuous filters are re-aimed here, state intact: Mono below's
    // corner and a room's tone are automatable and move under a ringing
    // pad without a step (both setters refuse a no-op).
    for (int pad = 0; pad < kPadCount; ++pad)
    {
        const double hz = std::clamp (parameters_.monoBelowHz[pad], 0.0, 2000.0);
        monoBelowOn_[pad] = ! dsp::isExactlyZero (hz);

        if (monoBelowOn_[pad])
            monoBelow_[pad].setCutoffHz (hz);
    }

    for (int index = 0; index < kRoomCount; ++index)
        room_[index].setToneHz (parameters_.room[index].toneHz);
}

void Engine::renderChunk (double* left, double* right, int numSamples) noexcept
{
    bool silent = true;

    // Which rooms run this chunk: one with its level at exactly 0, not on
    // its way anywhere, and nothing left in its line costs nothing and
    // changes nothing.
    bool roomOn[kRoomCount];

    for (int index = 0; index < kRoomCount; ++index)
        roomOn[index] = ! dsp::isExactlyZero (roomLevel_[index].getCurrent())
                     || roomLevel_[index].isSmoothing()
                     || room_[index].isActive();

    const bool layerOn = snareClap_.isActive();

    for (int i = 0; i < numSamples; ++i)
    {
        double mid[kPadCount];
        double side[kPadCount];

        mid[static_cast<int> (PadIndex::kick1)] = kick1_.process (side[static_cast<int> (PadIndex::kick1)]);
        mid[static_cast<int> (PadIndex::kick2)] = kick2_.process (side[static_cast<int> (PadIndex::kick2)]);
        mid[static_cast<int> (PadIndex::snare1)] = snare1_.process (side[static_cast<int> (PadIndex::snare1)]);
        mid[static_cast<int> (PadIndex::snare2)] = snare2_.process (side[static_cast<int> (PadIndex::snare2)]);
        mid[static_cast<int> (PadIndex::perc)] = perc_.process (side[static_cast<int> (PadIndex::perc)]);
        mid[static_cast<int> (PadIndex::hatClosed)] = hatClosed_.process (side[static_cast<int> (PadIndex::hatClosed)]);
        mid[static_cast<int> (PadIndex::hatOpen)] = hatOpen_.process (side[static_cast<int> (PadIndex::hatOpen)]);
        mid[static_cast<int> (PadIndex::clap)] = clap_.process (side[static_cast<int> (PadIndex::clap)]);

        // The clap layer sits under Snare 1: its placement, its room.
        if (layerOn)
        {
            double layerSide = 0.0;
            mid[kSnare1] += snareClap_.process (layerSide);
            side[kSnare1] += layerSide;
        }

        // The rooms, fed by their pad's mid, returned as mid and side.
        for (int index = 0; index < kRoomCount; ++index)
        {
            if (! roomOn[index])
                continue;

            const int pad = static_cast<int> (kRoomPads[index]);
            double roomLeft = 0.0;
            double roomRight = 0.0;

            room_[index].process (mid[pad], roomLeft, roomRight);

            const double level = roomLevel_[index].next();
            mid[pad] += level * (0.5 * (roomLeft + roomRight));
            side[pad] += level * (0.5 * (roomLeft - roomRight));
        }

        double l = 0.0;
        double r = 0.0;

        // Per pad: the side through Width and Mono below, then the two
        // channels formed and placed by the balance law, summed in the order
        // the mono engine always used. A pad whose side is exactly 0.0 skips
        // the side path and both channels are its mid -- the old render.
        for (int n = 0; n < kPadCount; ++n)
        {
            const int pad = kSumOrder[n];
            const double widthNow = width_[pad].isSmoothing() ? width_[pad].next() : width_[pad].getCurrent();
            const double panNow = pan_[pad].isSmoothing() ? pan_[pad].next() : pan_[pad].getCurrent();
            const double m = mid[pad];
            double s = side[pad];

            if (! dsp::isExactlyZero (s) || sideRing_[pad] > 0)
            {
                s *= widthNow;

                if (monoBelowOn_[pad])
                    s = monoBelow_[pad].process (s);

                if (! dsp::isExactlyZero (side[pad]))
                {
                    sideRing_[pad] = sideRingSamples_;
                }
                else if (--sideRing_[pad] <= 0)
                {
                    // The side has been exactly zero for the ring time: the
                    // filter's tail is below anything, so it is cleared and
                    // the pad is mono again, exactly.
                    sideRing_[pad] = 0;
                    monoBelow_[pad].reset();
                    s = 0.0;
                }
            }

            const double padLeft = dsp::isExactlyZero (s) ? m : m + s;
            const double padRight = dsp::isExactlyZero (s) ? m : m - s;

            if (n == 0)
            {
                l = padLeft * balanceLeft (panNow);
                r = padRight * balanceRight (panNow);
            }
            else
            {
                l += padLeft * balanceLeft (panNow);
                r += padRight * balanceRight (panNow);
            }
        }

        const double master = masterGain_.next();

        left[i] = l * master;
        right[i] = r * master;

        silent = silent && std::abs (left[i]) < kIdleThreshold && std::abs (right[i]) < kIdleThreshold;
    }

    bool roomsRinging = false;

    for (int index = 0; index < kRoomCount; ++index)
        roomsRinging = roomsRinging || (roomOn[index] && room_[index].isActive());

    if (silent && activeHitCount() == 0 && ! roomsRinging)
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

    // The pans, the widths and the room levels: jumped to on the first call
    // after a rebuild, ramped after. prepare() runs before any parameter is
    // known, so a smoother primed there starts at its default, and a pad
    // saved hard left would open every project drifting across the field
    // for its first hits.
    for (int pad = 0; pad < kPadCount; ++pad)
    {
        const double wantedPan = std::clamp (parameters_.pan[pad], -1.0, 1.0);
        const double wantedWidth = std::clamp (parameters_.width[pad], 0.0, 2.0);

        // Guarded like the rooms': a smoother re-targeted every block reads
        // as moving every block, and a moving smoother costs a step a sample
        // for every pad. At rest the render reads the value it holds.
        if (! pansPrimed_)
        {
            pan_[pad].setCurrentAndTarget (wantedPan);
            width_[pad].setCurrentAndTarget (wantedWidth);
        }
        else
        {
            if (! dsp::isExactly (wantedPan, pan_[pad].getTarget()))
                pan_[pad].setTarget (wantedPan);

            if (! dsp::isExactly (wantedWidth, width_[pad].getTarget()))
                width_[pad].setTarget (wantedWidth);
        }
    }

    for (int index = 0; index < kRoomCount; ++index)
    {
        const double wanted = std::clamp (parameters_.room[index].level, 0.0, 1.0);

        // Guarded: setTarget marks the smoother as moving until next() has
        // run, and a room that is "moving" is a room that runs. Pushed every
        // block unguarded, a room at 0 would have been fed for the first
        // chunk of every block a pad sounded in -- and then kept running by
        // its own line for the rest of the hit (CLAUDE.md section 7, the
        // setter that must refuse a no-op).
        if (! pansPrimed_)
            roomLevel_[index].setCurrentAndTarget (wanted);
        else if (! dsp::isExactly (wanted, roomLevel_[index].getTarget()))
            roomLevel_[index].setTarget (wanted);
    }

    pansPrimed_ = true;

    const int factor = oversampler_.getFactor();
    const int internalSamples = numSamples * factor;

    // An idle instrument costs nothing. Nothing here can wake by itself:
    // every stage is a hit that has retired exactly or a filter parked below
    // -240 dBFS, and the counter resets the instant a hit starts.
    if (idleSamples_ >= static_cast<int> (internalRate_ * kIdleSecondsBeforeSkipping)
        && activeHitCount() == 0 && snareClapPending_ < 0)
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

        // A clap layer counting down to its start lands on a chunk edge cut
        // for it, so its offset is exact whatever the block size.
        if (snareClapPending_ == 0)
            startSnareClapLayer();

        int take = std::min (sinceControl_, internalSamples - done);

        if (snareClapPending_ > 0)
            take = std::min (take, snareClapPending_);

        renderChunk (internal[0] + done, internal[1] + done, take);

        done += take;
        sinceControl_ -= take;

        if (snareClapPending_ > 0)
            snareClapPending_ -= take;
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
