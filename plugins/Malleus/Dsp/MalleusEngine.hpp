// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The whole instrument, framework-free: sixteen voices, age-based
// stealing, the shared sympathetic bank under everything, and one
// dsp::Tuning that pitches the keys, roots every voice's Overtone Lock,
// and tunes the taraf -- the thesis of the plugin in one dependency.
//
// The render loop cuts at the control boundary, never the callback's
// (CLAUDE.md section 7, the Emberdrive lesson): tension glides retune at
// kControlIntervalSamples whatever the host's buffer size, and the test
// asserts 64-sample and 512-sample renders are bit-identical with a roll
// running.
//
// Retirement is a measurement: a voice is gone when its key is up AND its
// vactrol reads exactly dark, at which point it contributes bit-exact
// nothing and costs one branch. The engine-level activity test plays
// sixteen notes, releases them, and asserts the count reaches zero and
// the post-death render cost returns to the idle baseline -- the Sonitus
// zombie-voice lesson (#118), asserted from birth this time.
//
// Per-hit variation is seeded: every note-on derives a fresh seed from the
// note-on counter, so two hits of the same key differ (roll humanise,
// scrape grain) while reset() + the same MIDI replays the same take
// bit-exactly.

#include <cstdint>

#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/Tuning.hpp>

#include "MalleusVoice.hpp"
#include "SympatheticBank.hpp"

namespace tezla::malleus {

class MalleusEngine
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kControlIntervalSamples = 48;

    /// Below this the ringing taraf is inaudible history: reset it so the
    /// engine's output returns to bit-exact zero rather than decaying
    /// forever.
    static constexpr double kSympatheticSilenceFloor = 1.0e-18;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        for (auto& voice : voices_)
            voice.prepare (sampleRate_);

        sympathetic_.prepare (sampleRate_);
        retuneSympathetic();
        reset();
    }

    void reset() noexcept
    {
        for (auto& voice : voices_)
            voice.prepare (sampleRate_);

        sympathetic_.reset();
        sinceControl_ = 0;
        noteOnCount_ = 0;
    }

    // -- the object and how it is hit (forwarded into new notes) ---------

    [[nodiscard]] VoiceSettings& settings() noexcept { return settings_; }

    /// Installs a scale: keys, Overtone Lock and taraf all follow. Not for
    /// the audio thread (Scale owns a vector) -- the processor swaps it
    /// from the message thread, as every tuning plugin here does.
    bool setScale (const dsp::Scale& scale)
    {
        if (! tuning_.setScale (scale))
            return false;

        lockScale_ = scale;
        retuneSympathetic();
        return true;
    }

    [[nodiscard]] dsp::Tuning& tuning() noexcept { return tuning_; }

    // -- the taraf -------------------------------------------------------

    void setSympathetic (int stringCount, double level, double coupling,
                         double drone, double t60Seconds,
                         double brightness) noexcept
    {
        sympatheticCount_ = stringCount < 0 ? 0
                          : stringCount > SympatheticBank::kMaxStrings
                              ? SympatheticBank::kMaxStrings
                              : stringCount;
        sympatheticLevel_ = level < 0.0 ? 0.0 : level > 1.0 ? 1.0 : level;
        sympatheticT60_ = t60Seconds < 0.1 ? 0.1 : t60Seconds > 30.0 ? 30.0
                                                                     : t60Seconds;
        sympatheticBrightness_ = brightness;
        sympathetic_.setCoupling (coupling);
        sympathetic_.setDrone (drone);
        retuneSympathetic();
    }

    /// The lowest taraf string's key; strings walk up consecutive degrees.
    void setSympatheticRoot (int note) noexcept
    {
        sympatheticRoot_ = note < 0 ? 0 : note > 115 ? 115 : note;
        retuneSympathetic();
    }

    // -- playing ---------------------------------------------------------

    void noteOn (int note, double velocity) noexcept
    {
        const double hz = tuning_.frequencyFor (note);

        if (hz <= 0.0)
            return;   // unmapped key: silence is the correct answer

        MalleusVoice* chosen = nullptr;

        for (auto& voice : voices_)
            if (! voice.isActive())
            {
                chosen = &voice;
                break;
            }

        if (chosen == nullptr)
        {
            // Steal the oldest -- its ring is the most finished thing here.
            // Age is the note-on counter, so oldest means SMALLEST: first
            // written the other way round, which stole the newest voice,
            // and the seventeen-notes test went red on it.
            chosen = &voices_[0];

            for (auto& voice : voices_)
                if (voice.getAge() < chosen->getAge())
                    chosen = &voice;
        }

        ++noteOnCount_;

        const std::uint64_t seed = kSeedBase
                                 + 0x9E3779B97F4A7C15ULL
                                     * static_cast<std::uint64_t> (noteOnCount_);

        chosen->noteOn (note, hz, velocity, seed, settings_, lockScale_,
                        noteOnCount_);
    }

    void noteOff (int note) noexcept
    {
        for (auto& voice : voices_)
            if (voice.isHeld() && voice.getNote() == note)
                voice.noteOff();
    }

    void allNotesOff() noexcept
    {
        for (auto& voice : voices_)
            voice.noteOff();
    }

    // -- rendering -------------------------------------------------------

    void process (double* out, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
            out[n] = 0.0;

        int done = 0;

        while (done < numSamples)
        {
            // Cut at the control boundary, not the callback's (section 7).
            if (sinceControl_ <= 0)
            {
                for (auto& voice : voices_)
                    voice.controlTick (kControlIntervalSamples);

                maintainSympathetic();
                sinceControl_ = kControlIntervalSamples;
            }

            const int take = numSamples - done < sinceControl_
                               ? numSamples - done
                               : sinceControl_;

            for (auto& voice : voices_)
                voice.render (out + done, take);

            if (sympatheticCount_ > 0)
                for (int n = done; n < done + take; ++n)
                    out[n] += sympatheticLevel_ * sympathetic_.process (out[n]);

            done += take;
            sinceControl_ -= take;
        }
    }

    // -- what the tests and meters read ----------------------------------

    [[nodiscard]] int activeVoiceCount() const noexcept
    {
        int count = 0;

        for (const auto& voice : voices_)
            if (voice.isActive())
                ++count;

        return count;
    }

    [[nodiscard]] double sympatheticEnergy() const noexcept
    {
        return sympathetic_.energy();
    }

    [[nodiscard]] const MalleusVoice& voiceForTest (int index) const noexcept
    {
        return voices_[static_cast<std::size_t> (
            index < 0 ? 0 : index >= kMaxVoices ? kMaxVoices - 1 : index)];
    }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

private:
    static constexpr std::uint64_t kSeedBase = 0x7E21ABA5E0000001ULL;

    void maintainSympathetic() noexcept
    {
        if (sympatheticCount_ <= 0)
            return;

        // Nobody playing and the ring below audibility: make silence exact.
        if (activeVoiceCount() == 0)
        {
            const double energy = sympathetic_.energy();

            if (energy > 0.0 && energy < kSympatheticSilenceFloor)
                sympathetic_.reset();
        }
    }

    void retuneSympathetic() noexcept
    {
        double frequencies[SympatheticBank::kMaxStrings] {};

        for (int s = 0; s < sympatheticCount_; ++s)
        {
            const double hz = tuning_.frequencyFor (sympatheticRoot_ + s);
            frequencies[s] = hz > 0.0 ? hz : 100.0 * (s + 1);
        }

        if (sympatheticCount_ > 0)
            sympathetic_.setStrings (frequencies, sympatheticCount_,
                                     sympatheticT60_, sympatheticBrightness_);
    }

    double sampleRate_ { 44100.0 };

    MalleusVoice voices_[kMaxVoices];
    VoiceSettings settings_;

    dsp::Tuning tuning_;
    dsp::Scale lockScale_ { dsp::Tuning::twelveToneEqual() };

    SympatheticBank sympathetic_;
    int sympatheticCount_ { 0 };
    int sympatheticRoot_ { 48 };
    double sympatheticLevel_ { 0.5 };
    double sympatheticT60_ { 8.0 };
    double sympatheticBrightness_ { 0.6 };

    int sinceControl_ { 0 };
    long long noteOnCount_ { 0 };
};

} // namespace tezla::malleus
