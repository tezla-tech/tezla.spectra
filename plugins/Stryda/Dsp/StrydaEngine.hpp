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

    /// How many control chunks pass between index-cap resolutions.
    ///
    /// The cap costs about 4.6 us per voice to resolve -- a bisection over a
    /// bandwidth prediction, not arithmetic -- so resolving it every chunk
    /// costs more than the synthesis does. Sixteen chunks is 512 internal
    /// samples, 2.7 ms at 192 kHz, which is faster than any hand moves and far
    /// slower than the audio.
    ///
    /// It is counted in **chunks, not blocks**: the chunk grid is anchored to
    /// the sample stream, so this grid is too, and the output does not depend
    /// on the host's buffer size. Counting the callback instead would
    /// reintroduce exactly the dependence `getChunkCountdown` exists to prove
    /// is absent.
    static constexpr int kCapChunks = 16;

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

        // 1, not `kCapChunks`: the first control chunk after a reset resolves
        // the cap rather than running fifteen chunks with whatever scale was
        // left over from the last stream.
        capChunksLeft_ = 1;
        capResolutions_ = 0;
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

        // The cap is resolved against the *internal* Nyquist, which has just
        // moved, so the scale every voice is holding is now the answer to a
        // different question. Re-ask it at the next chunk.
        capChunksLeft_ = 1;

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

    /// One note takes `unisonCount` voices, not one.
    ///
    /// The stack is built in two passes: first the voices this note already
    /// owns, so a fast retrigger reuses its own copies rather than spending a
    /// second stack of them, then free or stolen ones. The chosen indices are
    /// carried in a small array and excluded from the second pass, because a
    /// voice claimed a moment ago is not yet started and nothing else would
    /// stop `allocate` handing it out twice.
    void noteOn (int note, double velocity) noexcept
    {
        const int wanted = std::clamp (parameters_.extras.unisonCount, 1, kMaxVoices);
        const int count = std::min (wanted, polyphony_);

        std::array<int, kMaxVoices> chosen {};
        int taken = 0;

        for (int v = 0; v < polyphony_ && taken < count; ++v)
            if (voices_[static_cast<std::size_t> (v)].isActive()
                && voices_[static_cast<std::size_t> (v)].getNote() == note)
                chosen[static_cast<std::size_t> (taken++)] = v;

        while (taken < count)
        {
            chosen[static_cast<std::size_t> (taken)] = allocate (chosen.data(), taken);
            ++taken;
        }

        const double frequency = tuning_.frequencyFor (note);

        for (int i = 0; i < taken; ++i)
        {
            auto& voice = voices_[static_cast<std::size_t> (chosen[static_cast<std::size_t> (i)])];

            voice.noteOn (note, frequency, velocity);
            voice.setUnisonSlot (i, count);
            voice.applyParameters (parameters_);

            // Immediately, rather than waiting up to `kCapChunks` for the
            // sub-grid: a voice that started uncapped would alias for those
            // 2.7 ms, and the start of a note is exactly where the ear is
            // listening.
            voice.refreshIndexCap (parameters_, internalRate_);
            ++capResolutions_;
        }
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

    /// How many voice index-cap resolutions have happened since `prepare`.
    ///
    /// Exposed for the same reason `getChunkCountdown` is: the property that
    /// matters is a *rate*, and a wall clock cannot assert a rate on a shared
    /// machine without either false failures or a threshold so loose it catches
    /// nothing. This counts the expensive thing directly, so the test that
    /// guards the rig freeze is exact rather than statistical.
    [[nodiscard]] long long getCapResolutionCount() const noexcept { return capResolutions_; }

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
                const bool resolveCap = --capChunksLeft_ <= 0;

                if (resolveCap)
                    capChunksLeft_ = kCapChunks;

                for (auto& voice : voices_)
                {
                    if (! voice.isActive())
                        continue;

                    voice.applyParameters (parameters_);

                    if (resolveCap)
                    {
                        voice.refreshIndexCap (parameters_, internalRate_);
                        ++capResolutions_;
                    }
                }

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
    /// A free voice, or the best one to steal. `exclude` holds the indices
    /// already claimed by this note-on, which are not yet started and so would
    /// otherwise look free.
    [[nodiscard]] int allocate (const int* exclude, int excludeCount) noexcept
    {
        const auto claimed = [exclude, excludeCount] (int v)
        {
            for (int i = 0; i < excludeCount; ++i)
                if (exclude[i] == v)
                    return true;
            return false;
        };

        for (int v = 0; v < polyphony_; ++v)
            if (! voices_[static_cast<std::size_t> (v)].isActive() && ! claimed (v))
                return v;

        // Steal the oldest releasing voice if there is one, otherwise the
        // oldest held voice -- taking a note the player is still holding is
        // the last resort, not the first.
        // Seeded with the first unclaimed voice rather than with 0, so the
        // fallback can never hand back one this note-on has already taken --
        // which is reachable when every voice is held and none has aged yet.
        int best = 0;
        for (int v = 0; v < polyphony_; ++v)
            if (! claimed (v))
            {
                best = v;
                break;
            }

        std::uint64_t oldest = 0;
        bool foundReleasing = false;

        for (int v = 0; v < polyphony_; ++v)
        {
            if (claimed (v))
                continue;

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
    int capChunksLeft_ { 1 };
    long long capResolutions_ { 0 };

    dsp::OversamplingMode mode_ { dsp::OversamplingMode::Auto };
    dsp::RenderOversampling render_ { dsp::RenderOversampling::sameAsLive };
    dsp::Oversampler oversampler_;
    dsp::Tuning tuning_;

    std::array<StrydaVoice, kMaxVoices> voices_ {};
    VoiceParameters parameters_ {};
};

} // namespace tezla::stryda
