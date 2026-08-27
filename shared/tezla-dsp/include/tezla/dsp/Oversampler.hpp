#pragma once

// Oversampling for the nonlinear stages.
//
// Why this exists, in one measured number: a naive hard clipper at 4x drive
// produces -47 dB of inharmonic energy at 48 kHz and -65 dB at 192 kHz. Running
// the nonlinearity faster is most of the answer to aliasing (the rest is making
// the shaper itself band-limited -- see Adaa.hpp).
//
// The policy this implements: design against a fixed *internal* rate rather
// than whatever the host hands us, so the plugin sounds the same at every
// session rate. Auto picks the factor to land near 192 kHz internally.
//
// Filters are linear-phase halfband FIRs. That is a deliberate exception to the
// minimum-phase default in CLAUDE.md section 6: a halfband's transition band
// sits at Nyquist, so its pre-ringing is above 20 kHz and inaudible, and a
// windowed-sinc designed at run time can be verified by measurement in a way a
// table of IIR coefficients cannot. Minimum phase remains the rule for
// tone-shaping filters, where pre-ringing lands in the audible band.

#include <algorithm>
#include <array>
#include <vector>

#include "HalfbandFir.hpp"

namespace tezla::dsp {

enum class OversamplingMode
{
    Auto = 0,
    Off,
    X2,
    X4,
    X8
};

/// The factor Auto picks for a given host rate, chosen so the nonlinear stages
/// always run near ~192 kHz.
///
///   44.1 / 48 kHz    -> x4   (~176-192 kHz)
///   88.2 / 96 kHz    -> x2   (~176-192 kHz)
///   176.4 / 192 kHz+ -> x1   (already there)
[[nodiscard]] inline int autoOversamplingFactor (double sampleRate) noexcept
{
    if (sampleRate <= 56000.0)  return 4;
    if (sampleRate <= 112000.0) return 2;
    return 1;
}

[[nodiscard]] inline int oversamplingFactor (OversamplingMode mode, double sampleRate) noexcept
{
    switch (mode)
    {
        case OversamplingMode::Auto: return autoOversamplingFactor (sampleRate);
        case OversamplingMode::Off:  return 1;
        case OversamplingMode::X2:   return 2;
        case OversamplingMode::X4:   return 4;
        case OversamplingMode::X8:   return 8;
    }
    return 1;
}

/// Cascaded halfband oversampler, x1 / x2 / x4 / x8.
///
/// Tap counts per stage are chosen so the total round-trip latency is a whole
/// number of base-rate samples at every factor. A fractional latency would
/// either have to be reported wrong to the host or corrected with a fractional
/// delay on the dry path; picking the tap counts avoids the problem entirely.
///
///   stage 1 (base -> x2): 95 taps -> 94/2 = 47 base-rate samples
///   stage 2 (x2   -> x4): 65 taps -> 64/4 = 16
///   stage 3 (x4   -> x8): 65 taps -> 64/8 =  8
///
/// giving 47 / 63 / 71 samples for x2 / x4 / x8.
class Oversampler
{
public:
    static constexpr int kMaxStages = 3;
    static constexpr std::array<int, kMaxStages> kTapsPerStage { 95, 65, 65 };
    static constexpr double kStopbandDb = 100.0;

    /// Allocates. Call from prepareToPlay, never from the audio thread.
    ///
    /// Every stage is built, not just the ones this factor needs, because the
    /// factor is a *parameter*: it can change while audio is running, and
    /// re-preparing then would allocate on the audio thread -- which CLAUDE.md
    /// 2.2 forbids outright. Use setFactor() for that, which touches no memory.
    ///
    /// The cost is the two stages a x2 session is not using. At a 4096-sample
    /// block in stereo that is about 900 kB against 400, which is a fair price
    /// for never allocating in processBlock.
    void prepare (int maxBlockSize, int numChannels, int factor)
    {
        numChannels_ = std::max (numChannels, 1);
        maxBlockSize_ = std::max (maxBlockSize, 1);

        for (int stage = 0; stage < kMaxStages; ++stage)
        {
            const auto coefficients = designHalfband (kTapsPerStage[static_cast<std::size_t> (stage)], kStopbandDb);

            upsamplers_[static_cast<std::size_t> (stage)].resize (static_cast<std::size_t> (numChannels_));
            downsamplers_[static_cast<std::size_t> (stage)].resize (static_cast<std::size_t> (numChannels_));
            buffers_[static_cast<std::size_t> (stage)].resize (static_cast<std::size_t> (numChannels_));

            const int stageBlockSize = maxBlockSize_ * (2 << stage);

            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto c = static_cast<std::size_t> (channel);
                upsamplers_[static_cast<std::size_t> (stage)][c].prepare (coefficients);
                downsamplers_[static_cast<std::size_t> (stage)][c].prepare (coefficients);
                buffers_[static_cast<std::size_t> (stage)][c].assign (static_cast<std::size_t> (stageBlockSize), 0.0);
            }
        }

        // Factor 1 still needs somewhere for the caller to work in place.
        passThrough_.resize (static_cast<std::size_t> (numChannels_));
        for (auto& channel : passThrough_)
            channel.assign (static_cast<std::size_t> (maxBlockSize_), 0.0);

        pointers_.assign (static_cast<std::size_t> (numChannels_), nullptr);

        setFactor (factor);
    }

    /// Changes the oversampling factor without touching memory.
    ///
    /// Safe from the audio thread, which is the whole point of prepare()
    /// building every stage. Everything is cleared, including the stages just
    /// switched out: a stage that stops being called keeps whatever state it
    /// had, and switching it back in later would dump that straight into the
    /// signal as a click. The same argument the engines already make about
    /// filters that get bypassed.
    void setFactor (int factor) noexcept
    {
        factor_ = std::clamp (factor, 1, 8);

        numStages_ = 0;
        for (int f = factor_; f > 1; f /= 2)
            ++numStages_;

        reset();
    }

    void reset() noexcept
    {
        // Every stage, not only the active ones -- see setFactor().
        for (int stage = 0; stage < kMaxStages; ++stage)
            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto s = static_cast<std::size_t> (stage);
                const auto c = static_cast<std::size_t> (channel);

                if (c >= upsamplers_[s].size())
                    continue;

                upsamplers_[s][c].reset();
                downsamplers_[s][c].reset();
                std::fill (buffers_[s][c].begin(), buffers_[s][c].end(), 0.0);
            }

        for (auto& channel : passThrough_)
            std::fill (channel.begin(), channel.end(), 0.0);
    }

    [[nodiscard]] int getFactor() const noexcept { return factor_; }

    /// Round-trip latency in base-rate samples for a given factor. A pure
    /// function of the tap counts, so a UI can ask what a factor *would* cost
    /// without having an engine prepared for it.
    [[nodiscard]] static constexpr int latencyForFactor (int factor) noexcept
    {
        int stages = 0;
        for (int f = factor; f > 1; f /= 2)
            ++stages;

        int latency = 0;
        for (int stage = 0; stage < stages && stage < kMaxStages; ++stage)
            latency += (kTapsPerStage[static_cast<std::size_t> (stage)] - 1) >> (stage + 1);

        return latency;
    }

    /// Round-trip latency in base-rate samples. Whole number at every factor.
    [[nodiscard]] int getLatencySamples() const noexcept { return latencyForFactor (factor_); }

    /// Upsamples `numSamples` base-rate frames and returns the oversampled
    /// buffers, which hold numSamples * factor frames. The caller processes
    /// them in place, then calls downsample(). Allocation-free.
    [[nodiscard]] double* const* upsample (const double* const* input, int numSamples) noexcept
    {
        if (numStages_ == 0)
        {
            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto c = static_cast<std::size_t> (channel);
                std::copy (input[channel], input[channel] + numSamples, passThrough_[c].begin());
                pointers_[c] = passThrough_[c].data();
            }
            return pointers_.data();
        }

        for (int stage = 0; stage < numStages_; ++stage)
        {
            const auto s = static_cast<std::size_t> (stage);
            const int inputSamples = numSamples * (1 << stage);

            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto c = static_cast<std::size_t> (channel);
                const double* source = stage == 0 ? input[channel] : buffers_[s - 1][c].data();
                upsamplers_[s][c].process (source, buffers_[s][c].data(), inputSamples);
            }
        }

        const auto last = static_cast<std::size_t> (numStages_ - 1);
        for (int channel = 0; channel < numChannels_; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            pointers_[c] = buffers_[last][c].data();
        }
        return pointers_.data();
    }

    /// The oversampled buffers, without running the interpolation filters.
    ///
    /// For a **generator**. An instrument has nothing to upsample -- it makes
    /// its audio at the internal rate -- and running the FIRs over silence is
    /// 115,000 multiply-adds per 512-sample block at x4 to produce zeros. This
    /// hands back exactly the buffers `upsample()` would have filled, so a
    /// caller can write into them directly and then call `downsample()`.
    ///
    /// Each buffer holds `maxBlockSize * factor` frames. Writing fewer than
    /// `numSamples * factor` leaves stale audio behind them, so a generator
    /// fills what it is going to downsample.
    [[nodiscard]] double* const* internalBuffers() noexcept
    {
        if (numStages_ == 0)
        {
            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto c = static_cast<std::size_t> (channel);
                pointers_[c] = passThrough_[c].data();
            }

            return pointers_.data();
        }

        const auto last = static_cast<std::size_t> (numStages_ - 1);

        for (int channel = 0; channel < numChannels_; ++channel)
        {
            const auto c = static_cast<std::size_t> (channel);
            pointers_[c] = buffers_[last][c].data();
        }

        return pointers_.data();
    }

    /// Downsamples what upsample() handed out, back into `output`.
    void downsample (double* const* output, int numSamples) noexcept
    {
        if (numStages_ == 0)
        {
            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto c = static_cast<std::size_t> (channel);
                std::copy (passThrough_[c].begin(), passThrough_[c].begin() + numSamples, output[channel]);
            }
            return;
        }

        for (int stage = numStages_ - 1; stage >= 0; --stage)
        {
            const auto s = static_cast<std::size_t> (stage);
            const int outputSamples = numSamples * (1 << stage);

            for (int channel = 0; channel < numChannels_; ++channel)
            {
                const auto c = static_cast<std::size_t> (channel);
                double* destination = stage == 0 ? output[channel] : buffers_[s - 1][c].data();
                downsamplers_[s][c].process (buffers_[s][c].data(), destination, outputSamples);
            }
        }
    }

private:
    int factor_       { 1 };
    int numStages_    { 0 };
    int numChannels_  { 2 };
    int maxBlockSize_ { 512 };

    std::array<std::vector<HalfbandUpsampler>,   kMaxStages> upsamplers_;
    std::array<std::vector<HalfbandDownsampler>, kMaxStages> downsamplers_;
    std::array<std::vector<std::vector<double>>, kMaxStages> buffers_;

    std::vector<std::vector<double>> passThrough_;
    std::vector<double*> pointers_;
};

} // namespace tezla::dsp
