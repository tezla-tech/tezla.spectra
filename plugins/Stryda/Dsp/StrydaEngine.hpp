// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The instrument: voices, oversampling, and the control chunking.
//
// Everything renders at the internal (oversampled) rate into
// `Oversampler::internalBuffers()`; only decimation happens per block. That is
// the generator path Sonitus and Ictus already use, and it is why an operator
// running at index 16 does not fold: table 1 of `tezla-measure stryda` says x4
// at 48 kHz holds a hard patch at -114.6 dB where the host rate alone reads
// +2.6 dB.
//
// Control changes land on a **sample-counted chunk boundary**, not a block
// boundary (CLAUDE.md section 7). Rebuilding once per block makes the output
// depend on the host's buffer size, and no arrangement of a per-call timer
// fixes that -- measured on Emberdrive at 0.296 of full scale between 64- and
// 512-sample blocks.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/Tuning.hpp>

#include "StrydaVoice.hpp"

namespace tezla::stryda
{

class StrydaEngine
{
public:
    static constexpr int kMaxVoices = 16;

    /// 32 internal samples, as Sonitus and Ictus use. Small enough that a
    /// sweeping index does not step audibly, large enough that the per-chunk
    /// work is a rounding error.
    static constexpr int kControlChunk = 32;

    void prepare (double sampleRate, int maxBlockSize)
    {
        hostRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        maxBlockSize_ = std::max (1, maxBlockSize);

        oversampler_.prepare (maxBlockSize_, 2, factor_);

        internalRate_ = hostRate_ * static_cast<double> (factor_);

        for (std::size_t v = 0; v < voices_.size(); ++v)
        {
            voices_[v].prepare (internalRate_);
            voices_[v].setSeed (0x9e3779b97f4a7c15ull ^ (v * 0x632be59bd9b4e019ull));
        }

        reset();
    }

    void reset() noexcept
    {
        oversampler_.reset();

        for (auto& voice : voices_)
            voice.reset();

        chunkCountdown_ = 0;
    }

    void setOversamplingMode (dsp::OversamplingMode mode) noexcept { mode_ = mode; }
    void setRenderOversampling (dsp::RenderOversampling render) noexcept { render_ = render; }

    /// Re-resolve the factor. Returns true when it changed, so the processor
    /// knows to re-declare its latency.
    bool updateFactor (bool offline) noexcept
    {
        const auto effective = dsp::effectiveOversamplingMode (mode_, render_, offline);
        const int wanted = dsp::oversamplingFactor (effective, hostRate_);

        if (wanted == factor_)
            return false;

        factor_ = wanted;
        oversampler_.setFactor (factor_);
        internalRate_ = hostRate_ * static_cast<double> (factor_);

        for (auto& voice : voices_)
            voice.prepare (internalRate_);

        return true;
    }

    [[nodiscard]] int getFactor() const noexcept { return factor_; }
    [[nodiscard]] double getInternalRate() const noexcept { return internalRate_; }
    [[nodiscard]] int getLatencySamples() const noexcept { return oversampler_.getLatencySamples(); }

    void setParameters (const VoiceParameters& parameters) noexcept { parameters_ = parameters; }

    void setPolyphony (int voices) noexcept
    {
        polyphony_ = std::clamp (voices, 1, kMaxVoices);
    }

    [[nodiscard]] dsp::Tuning& getTuning() noexcept { return tuning_; }
    [[nodiscard]] const dsp::Tuning& getTuning() const noexcept { return tuning_; }

    void noteOn (int note, double velocity) noexcept
    {
        auto& voice = voices_[static_cast<std::size_t> (allocate (note))];
        voice.noteOn (note, tuning_.frequencyFor (note), velocity);
        voice.applyParameters (parameters_, internalRate_);
    }

    void noteOff (int note) noexcept
    {
        for (auto& voice : voices_)
            if (voice.isActive() && voice.getNote() == note && ! voice.isReleasing())
                voice.noteOff();
    }

    void allNotesOff() noexcept
    {
        for (auto& voice : voices_)
            if (voice.isActive())
                voice.noteOff();
    }

    /// How far through the current control chunk the engine is. Exposed so a
    /// test can assert that the chunk grid is anchored to the stream and not to
    /// the host's block, which is the property that makes the output
    /// buffer-size independent once a parameter actually moves.
    [[nodiscard]] int getChunkCountdown() const noexcept { return chunkCountdown_; }

    [[nodiscard]] int getActiveVoiceCount() const noexcept
    {
        int count = 0;
        for (const auto& voice : voices_)
            if (voice.isActive())
                ++count;
        return count;
    }

    /// Render `numSamples` host samples into a stereo pair.
    void process (double* left, double* right, int numSamples) noexcept
    {
        double* const* internal = oversampler_.internalBuffers();
        const int internalSamples = numSamples * factor_;

        int written = 0;
        while (written < internalSamples)
        {
            if (chunkCountdown_ <= 0)
            {
                for (auto& voice : voices_)
                    if (voice.isActive())
                        voice.applyParameters (parameters_, internalRate_);

                chunkCountdown_ = kControlChunk;
            }

            // Cut the loop at the chunk boundary, never at the block's: this is
            // what makes the output independent of the host's buffer size.
            const int run = std::min (chunkCountdown_, internalSamples - written);

            for (int i = 0; i < run; ++i)
            {
                double l = 0.0;
                double r = 0.0;

                for (auto& voice : voices_)
                    voice.process (l, r);

                internal[0][written + i] = l;
                internal[1][written + i] = r;
            }

            written += run;
            chunkCountdown_ -= run;
        }

        double* outputs[2] { left, right };
        oversampler_.downsample (outputs, numSamples);
    }

private:
    [[nodiscard]] int allocate (int note) noexcept
    {
        // A repeated note takes its own voice back, so a fast retrigger does
        // not spend two.
        for (int v = 0; v < polyphony_; ++v)
            if (voices_[static_cast<std::size_t> (v)].isActive()
                && voices_[static_cast<std::size_t> (v)].getNote() == note)
                return v;

        for (int v = 0; v < polyphony_; ++v)
            if (! voices_[static_cast<std::size_t> (v)].isActive())
                return v;

        // Steal the oldest releasing voice if there is one, otherwise the
        // oldest held voice -- taking a note the player is still holding is
        // the last resort, not the first.
        int best = 0;
        std::uint64_t oldest = 0;
        bool foundReleasing = false;

        for (int v = 0; v < polyphony_; ++v)
        {
            auto& voice = voices_[static_cast<std::size_t> (v)];
            const bool releasing = voice.isReleasing();

            if (releasing && ! foundReleasing)
            {
                foundReleasing = true;
                best = v;
                oldest = voice.getAge();
                continue;
            }

            if (releasing == foundReleasing && voice.getAge() > oldest)
            {
                best = v;
                oldest = voice.getAge();
            }
        }

        return best;
    }

    double hostRate_ { 48000.0 };
    double internalRate_ { 192000.0 };
    int maxBlockSize_ { 512 };
    int factor_ { 4 };
    int polyphony_ { 8 };
    int chunkCountdown_ { 0 };

    dsp::OversamplingMode mode_ { dsp::OversamplingMode::Auto };
    dsp::RenderOversampling render_ { dsp::RenderOversampling::sameAsLive };
    dsp::Oversampler oversampler_;
    dsp::Tuning tuning_;

    std::array<StrydaVoice, kMaxVoices> voices_ {};
    VoiceParameters parameters_ {};
};

} // namespace tezla::stryda
