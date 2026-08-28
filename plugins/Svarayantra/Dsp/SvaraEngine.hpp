// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The instrument: a soundfont, a tuning, and a fixed pool of voices.
//
// A note-on searches the selected preset's zones and starts one voice per
// matching zone -- that is how the format expresses layers and stereo pairs.
// The played frequency comes from the Tuning, not from the key number: this
// is the same microtuning system as Sonitus (Scala files, the built-in scale
// library, concert pitch), pointed at samples instead of oscillators.
//
// Control changes are applied on a 32-sample timer counted in samples and
// carried across process() calls, so the output does not depend on the
// host's buffer size (CLAUDE.md section 7). Voices render in chunks cut at
// that timer's boundary; pitch bend, vibrato and the modulation envelope
// move at chunk heads, the envelopes per sample.
//
// Voice management, and what happens when it runs out:
//   * exclusive class (hi-hat choke): starting a zone with class N quick-
//     releases (~10 ms) every sounding voice of class N first -- a choke
//     that cuts dead would pop.
//   * stealing at the polyphony ceiling: the quietest candidate -- released
//     voices first, then the oldest -- is CUT, and the new note takes the
//     slot. The references do the same; at 64 voices the cut is rare.
//   * retirement is by activity (the CPU-zombie lesson): a voice whose
//     player ran off the end or whose envelope can never be heard again
//     frees its slot the moment that becomes true, and the tests assert the
//     count, not the silence.
//
// The engine holds only pointers to the font (sample pool + resolved model);
// the owner keeps them alive and swaps them from the message thread with
// setFont, which resets all voices -- a font swap mid-note is a hard cut by
// design, never a voice pointing into a freed pool.

#include <cstdint>

#include <tezla/dsp/Tuning.hpp>

#include "Sf2File.hpp"
#include "Sf2Model.hpp"
#include "SvaraVoice.hpp"

namespace tezla::svarayantra {

class SvaraEngine
{
public:
    static constexpr int kMaxVoices = 64;
    static constexpr int kControlIntervalSamples = 32;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;

        for (auto& voice : voices_)
            voice.prepare (sampleRate);

        controlCountdown_ = 0;
        sustainDown_ = false;
        bendNormalized_ = 0.0;
        modWheel_ = 0.0;
    }

    /// Points the engine at a loaded font. Both pointers must outlive every
    /// later call; pass nullptrs to unload. All voices stop -- a swap is a
    /// hard cut, never a voice left reading a freed pool.
    void setFont (const Sf2File* file, const Sf2Model* model) noexcept
    {
        file_ = file;
        model_ = model;
        presetIndex_ = 0;

        for (auto& voice : voices_)
            voice.kill();
    }

    /// Chooses the sounding preset by bank and program, falling back to the
    /// first preset when the pair does not exist. Returns whether it did.
    bool selectPreset (int bank, int program) noexcept
    {
        if (model_ == nullptr || model_->presets.empty())
            return false;

        for (std::size_t i = 0; i < model_->presets.size(); ++i)
        {
            if (model_->presets[i].bank == bank
                && model_->presets[i].program == program)
            {
                presetIndex_ = i;
                return true;
            }
        }

        presetIndex_ = 0;
        return false;
    }

    [[nodiscard]] std::size_t presetIndex() const noexcept { return presetIndex_; }

    /// The microtuning. Same object and workflow as Sonitus: the owner sets
    /// scales, Scala files and concert pitch on it directly.
    [[nodiscard]] tezla::dsp::Tuning& tuning() noexcept { return tuning_; }
    [[nodiscard]] const tezla::dsp::Tuning& tuning() const noexcept { return tuning_; }

    void noteOn (int key, int velocity) noexcept
    {
        if (model_ == nullptr || file_ == nullptr
            || presetIndex_ >= model_->presets.size())
            return;

        if (velocity <= 0)
        {
            noteOff (key);
            return;
        }

        const auto& preset = model_->presets[presetIndex_];
        const double targetHz = tuning_.frequencyFor (key);
        const std::int16_t* pool = file_->samples.data();

        for (const auto& zone : preset.zones)
        {
            if (! zone.matches (key, velocity))
                continue;

            // An exclusive class chokes what is already sounding in it.
            if (zone.exclusiveClass != 0)
                for (auto& voice : voices_)
                    if (voice.isActive()
                        && voice.exclusiveClass() == zone.exclusiveClass)
                        voice.quickRelease();

            auto* voice = allocateVoice();
            voice->start (pool, zone, key, velocity, targetHz, nextSerial_++,
                          bendNormalized_ * bendRangeSemitones_ * 100.0,
                          modWheel_);
        }
    }

    void noteOff (int key) noexcept
    {
        for (auto& voice : voices_)
        {
            if (! voice.isActive() || voice.key() != key || voice.isReleasing())
                continue;

            if (sustainDown_)
                voice.deferRelease();
            else
                voice.release();
        }
    }

    void setSustainPedal (bool down) noexcept
    {
        if (sustainDown_ && ! down)
            for (auto& voice : voices_)
                if (voice.isActive() && voice.hasDeferredRelease())
                    voice.release();

        sustainDown_ = down;
    }

    /// Pitch bend as -1..+1 of the bend range (default +-2 semitones).
    void setPitchBend (double normalized) noexcept
    {
        bendNormalized_ = normalized < -1.0 ? -1.0
                        : normalized > 1.0 ? 1.0
                                           : normalized;
    }

    void setBendRangeSemitones (double semitones) noexcept
    {
        bendRangeSemitones_ = semitones < 0.0 ? 0.0
                            : semitones > 48.0 ? 48.0
                                               : semitones;
    }

    /// Mod wheel 0..1: up to 50 cents of vibrato, the format's default.
    void setModWheel (double amount) noexcept
    {
        modWheel_ = amount < 0.0 ? 0.0 : amount > 1.0 ? 1.0 : amount;
    }

    void setOutputGain (double gain) noexcept { outputGain_ = gain; }

    /// All keys up, all voices released -- MIDI panic without the click.
    void allNotesOff() noexcept
    {
        for (auto& voice : voices_)
            if (voice.isActive())
                voice.release();

        sustainDown_ = false;
    }

    [[nodiscard]] int activeVoiceCount() const noexcept
    {
        int count = 0;

        for (const auto& voice : voices_)
            count += voice.isActive() ? 1 : 0;

        return count;
    }

    /// Renders and REPLACES the buffers. Chunks are cut at the control
    /// timer's boundary, never the call's, so 64- and 512-sample blocks
    /// produce identical output.
    void process (double* left, double* right, int count) noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            left[i] = 0.0;
            right[i] = 0.0;
        }

        const double bendCents = bendNormalized_ * bendRangeSemitones_ * 100.0;
        int at = 0;

        while (at < count)
        {
            // A chunk that begins exactly on the timer's boundary carries a
            // control update; one cut short by a block edge carries on with
            // the targets it has. That distinction is what makes 64- and
            // 512-sample blocks produce identical output.
            const bool controlHead = controlCountdown_ <= 0;

            if (controlHead)
                controlCountdown_ = kControlIntervalSamples;

            const int chunk = controlCountdown_ < count - at ? controlCountdown_
                                                             : count - at;

            for (auto& voice : voices_)
                voice.renderAdd (left + at, right + at, chunk, controlHead,
                                 bendCents, modWheel_);

            controlCountdown_ -= chunk;
            at += chunk;
        }

        if (outputGain_ != 1.0)
        {
            for (int i = 0; i < count; ++i)
            {
                left[i] *= outputGain_;
                right[i] *= outputGain_;
            }
        }
    }

private:
    /// A free slot, else the best victim: a releasing voice before a held
    /// one, oldest serial within each class. The victim is cut.
    [[nodiscard]] SvaraVoice* allocateVoice() noexcept
    {
        for (auto& voice : voices_)
            if (! voice.isActive())
                return &voice;

        SvaraVoice* victim = &voices_[0];

        for (auto& voice : voices_)
        {
            const bool voiceBetter =
                (voice.isReleasing() && ! victim->isReleasing())
                || (voice.isReleasing() == victim->isReleasing()
                    && voice.serial() < victim->serial());

            if (voiceBetter)
                victim = &voice;
        }

        victim->kill();
        return victim;
    }

    SvaraVoice voices_[kMaxVoices];
    tezla::dsp::Tuning tuning_;

    const Sf2File* file_ { nullptr };
    const Sf2Model* model_ { nullptr };
    std::size_t presetIndex_ { 0 };

    double sampleRate_ { 48000.0 };
    int controlCountdown_ { 0 };
    std::uint64_t nextSerial_ { 1 };

    bool sustainDown_ { false };
    double bendNormalized_ { 0.0 };
    double bendRangeSemitones_ { 2.0 };
    double modWheel_ { 0.0 };
    double outputGain_ { 1.0 };
};

} // namespace tezla::svarayantra
