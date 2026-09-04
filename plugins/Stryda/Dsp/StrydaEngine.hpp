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

#include "RatioSequencer.hpp"
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

        sequencer_.prepare (internalRate_);

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

        sequencer_.reset();
        stepEdge_ = false;
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

        sequencer_.prepare (internalRate_);

        // The cap is resolved against the *internal* Nyquist, which has just
        // moved, so the scale every voice is holding is now the answer to a
        // different question. Re-ask it at the next chunk.
        capChunksLeft_ = 1;

        return true;
    }

    [[nodiscard]] int getFactor() const noexcept { return factor_; }
    [[nodiscard]] double getInternalRate() const noexcept { return internalRate_; }
    [[nodiscard]] int getLatencySamples() const noexcept { return oversampler_.getLatencySamples(); }

    /// Take the patch and resolve everything the voices should not have to.
    ///
    /// The ratio modes are quantised here rather than in the voice because the
    /// quantiser needs the loaded scale, and the scale lives with the tuning.
    /// A voice therefore only ever handles a plain ratio, and **Free returns
    /// its input bit for bit**, so the default costs nothing and changes
    /// nothing.
    void setParameters (const VoiceParameters& parameters) noexcept
    {
        parameters_ = parameters;
        patchRatios_ = {};

        for (int op = 0; op < StrydaVoice::kNumOperators; ++op)
        {
            auto& settings = parameters_.operators[static_cast<std::size_t> (op)];

            settings.ratio = resolveRatio (settings.ratio,
                                           static_cast<RatioMode> (settings.ratioMode),
                                           tuning_.getScale());

            // Kept so the sequencer can be switched off mid-note and the
            // target operator go back to the ratio the patch actually asks for,
            // rather than to whichever step happened to be playing.
            patchRatios_[static_cast<std::size_t> (op)] = settings.ratio;
        }
    }

    [[nodiscard]] RatioSequencer& getSequencer() noexcept { return sequencer_; }
    [[nodiscard]] const RatioSequencer& getSequencer() const noexcept { return sequencer_; }

    /// Where the host's transport is, so the pattern can lock to the bar.
    ///
    /// Called once per block from the processor. The sequencer keeps its rate
    /// between anchors and free-runs, which is what makes
    /// `samplesToNextStep` exact where the cutting happens.
    void setTransport (double ppqPosition, double bpm, bool playing) noexcept
    {
        bpm_ = bpm > 0.0 ? bpm : bpm_;

        if (playing && sequencer_.isEnabled())
            sequencer_.anchorToPpq (ppqPosition, bpm_, division_);
        else
            sequencer_.setDivision (division_, bpm_);
    }

    void setSequencerDivision (int index) noexcept { division_ = index; }
    [[nodiscard]] int getSequencerDivision() const noexcept { return division_; }

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
            // A control chunk is due, or the sequencer has just crossed a step
            // edge. The edge is an EXTRA refresh rather than a re-phasing of
            // the chunk grid: the grid stays anchored to the stream and the
            // test that proves it keeps proving it.
            //
            // **And the refresh has to reach the voices, not just the
            // parameters.** Pushing the new ratio into `parameters_` and
            // waiting for the next chunk to apply it puts the jump back where
            // it would have been without the cut -- which is exactly what the
            // first version did, and the test that measures *where* the output
            // first changes is what caught it.
            const bool chunkDue = chunkCountdown_ <= 0;

            if (chunkDue || stepEdge_)
            {
                pushSequencedRatio();
                stepEdge_ = false;

                for (auto& voice : voices_)
                    if (voice.isActive())
                        voice.applyParameters (parameters_);
            }

            if (chunkDue)
            {
                // The cap stays on its own coarser sub-grid: it costs a
                // bandwidth bisection, where applying parameters costs
                // arithmetic on numbers already to hand.
                const bool resolveCap = --capChunksLeft_ <= 0;

                if (resolveCap)
                {
                    capChunksLeft_ = kCapChunks;

                    for (auto& voice : voices_)
                        if (voice.isActive())
                        {
                            voice.refreshIndexCap (parameters_, internalRate_);
                            ++capResolutions_;
                        }
                }

                chunkCountdown_ = kControlChunk;
            }

            // Cut the loop at the chunk boundary, never at the block's: this is
            // what makes the output independent of the host's buffer size.
            int run = std::min (chunkCountdown_, internalSamples - written);

            // And at the sequencer's step edge, which is a far larger event
            // than a voicing rebuild -- a ratio jump swaps one harmonic
            // identity for another. The step grid is anchored to the stream
            // too (rate, or the transport), so cutting at it stays buffer-size
            // independent.
            if (sequencer_.isEnabled())
            {
                const double toStep = sequencer_.samplesToNextStep();

                if (toStep > 0.0 && toStep < static_cast<double> (run))
                {
                    run = std::max (1, static_cast<int> (std::ceil (toStep)));
                    stepEdge_ = true;
                }
            }

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

            if (sequencer_.isEnabled())
                sequencer_.advance (run);
        }

        double* outputs[2] { left, right };
        oversampler_.downsample (outputs, numSamples);
    }

private:
    /// Put the sequencer's current ratio on its target operator.
    ///
    /// Called at every control chunk, so glide is heard at 32-sample
    /// resolution, and again at every step edge, so a jump lands exactly on
    /// the beat rather than up to a chunk late.
    ///
    /// When the sequencer is off -- or its target is out of range, which is
    /// how "no destination" is spelt -- the operator goes back to the ratio
    /// the patch asks for. Restoring rather than leaving it is what stops a
    /// switched-off sequencer freezing a note on whichever step was playing.
    void pushSequencedRatio() noexcept
    {
        const int target = sequencer_.getTarget();

        if (target < 0 || target >= StrydaVoice::kNumOperators)
            return;

        const auto slot = static_cast<std::size_t> (target);
        auto& settings = parameters_.operators[slot];

        settings.ratio = sequencer_.isEnabled()
                           ? sequencer_.currentRatio (
                                 static_cast<RatioMode> (settings.ratioMode),
                                 tuning_.getScale())
                           : patchRatios_[slot];
    }

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

    RatioSequencer sequencer_;
    std::array<double, StrydaVoice::kNumOperators> patchRatios_ {};
    double bpm_ { 120.0 };
    int division_ { 7 };          ///< 1/16 in `dsp::divisions`
    bool stepEdge_ { false };

    dsp::OversamplingMode mode_ { dsp::OversamplingMode::Auto };
    dsp::RenderOversampling render_ { dsp::RenderOversampling::sameAsLive };
    dsp::Oversampler oversampler_;
    dsp::Tuning tuning_;

    std::array<StrydaVoice, kMaxVoices> voices_ {};
    VoiceParameters parameters_ {};
};

} // namespace tezla::stryda
