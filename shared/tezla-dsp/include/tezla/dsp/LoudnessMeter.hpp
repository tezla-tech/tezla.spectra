#pragma once

// Loudness to ITU-R BS.1770-5 / EBU R128: how loud this actually is, as opposed
// to how high it peaks.
//
// A peak meter answers a question about the converter. This answers the question
// a streaming platform will ask, and the one an ear asks: it K-weights the
// signal to approximate how the ear values frequency, takes a mean square over
// defined windows, and gates out the silence so a quiet intro cannot drag the
// number down.
//
// Three readings, all defined by the standard rather than by us:
//
//   Momentary    400 ms, ungated. Moves with the bar.
//   Short-term   3 s, ungated. What "how loud is this section" means.
//   Integrated   the whole programme, gated. What a platform measures.
//
// ---------------------------------------------------------------------------
// The coefficient trap, which is CLAUDE.md section 6 in its purest form
// ---------------------------------------------------------------------------
//
// The Recommendation prints its K-weighting coefficients **for 48 kHz only**.
// They look like constants and they are not: using them at 44.1 or 96 kHz gives
// a filter with the wrong corner frequencies and a loudness reading that is
// quietly wrong, with nothing to notice.
//
// So the filters are designed here from the analogue prototype at the actual
// rate. The prototype parameters below are the published values that reproduce
// the printed table, and reproducing it is the test: at 48 kHz this design
// agrees with the Recommendation's own numbers to **8.9e-16**, which is double
// rounding. See tests/test_LoudnessMeter.cpp.
//
// One honest wrinkle, measured rather than assumed. The standard's -0.691 dB
// offset is a fixed constant, but the bilinear transform warps the filter's
// 1 kHz gain slightly with sample rate -- +0.7005 dB at 44.1 kHz against
// +0.6707 at 192 kHz. So a -23 dBFS tone reads -22.990 at 44.1 kHz and -23.020
// at 192 kHz. That spread is in the Recommendation, not in this code, and it is
// five times inside EBU Tech 3341's +/-0.1 LU tolerance.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "Biquad.hpp"

namespace tezla::dsp {

class LoudnessMeter
{
public:
    static constexpr int kMaxChannels = 2;

    /// The standard's own numbers.
    static constexpr double kOffsetDb          = -0.691;  ///< so a -23 dBFS tone reads -23 LUFS
    static constexpr double kAbsoluteGateLufs  = -70.0;   ///< silence never counts
    static constexpr double kRelativeGateLu    = -10.0;   ///< below the ungated mean

    /// A new gating block every 100 ms: 400 ms blocks at 75% overlap, as the
    /// Recommendation specifies.
    static constexpr double kBlockIntervalSeconds = 0.100;

    /// The two windows, counted in those intervals. This is the primary form:
    /// a window that is not a whole number of gating intervals cannot be
    /// assembled from them at all, and the standard specifies both as integers.
    static constexpr int kMomentaryIntervals = 4;    ///< 400 ms
    static constexpr int kShortTermIntervals = 30;   ///< 3 s

    /// Derived, so the two cannot disagree. Writing 0.400 out and asserting it
    /// matched was the same statement made twice -- and made in floating point,
    /// where 4 x 0.1 is not obliged to be the double nearest 0.4.
    static constexpr double kMomentarySeconds = kMomentaryIntervals * kBlockIntervalSeconds;
    static constexpr double kShortTermSeconds = kShortTermIntervals * kBlockIntervalSeconds;

    /// Anything at or below this is reported as silence rather than as a very
    /// large negative number that looks like a reading.
    static constexpr double kSilenceLufs = -200.0;

    /// How long the integrated measurement can run before it stops taking new
    /// blocks. One hour, which is far past any session, and the alternative --
    /// a histogram -- trades exactness for memory nobody needs back.
    static constexpr double kMaximumIntegrationSeconds = 3600.0;

    /// The K-weighting pair, designed for a given rate.
    struct KWeighting
    {
        BiquadCoefficients<double> shelf;      ///< high-frequency shelf, the "head" filter
        BiquadCoefficients<double> highpass;   ///< RLB
    };

    /// Designs both stages from the analogue prototype at `sampleRate`.
    ///
    /// The prototype parameters are the published values that reproduce the
    /// Recommendation's printed 48 kHz table exactly; the generalisation to
    /// other rates is the bilinear transform. Taken rather than derived, under
    /// CLAUDE.md section 9: no measurement we could run would tell us that a
    /// filter of our own was the wrong one to have chosen, because the standard
    /// *is* the definition. Attributed in docs/DSP-REFERENCES.md.
    [[nodiscard]] static KWeighting designKWeighting (double sampleRate) noexcept
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        KWeighting k;

        {
            constexpr double f0 = 1681.974450955533;
            constexpr double gainDb = 3.999843853973347;
            constexpr double q = 0.7071752369554196;

            const double kk = std::tan (std::numbers::pi * f0 / rate);
            const double vh = std::pow (10.0, gainDb / 20.0);
            const double vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + kk / q + kk * kk;

            k.shelf = { (vh + vb * kk / q + kk * kk) / a0,
                        2.0 * (kk * kk - vh) / a0,
                        (vh - vb * kk / q + kk * kk) / a0,
                        2.0 * (kk * kk - 1.0) / a0,
                        (1.0 - kk / q + kk * kk) / a0 };
        }

        {
            constexpr double f0 = 38.13547087602444;
            constexpr double q = 0.5003270373238773;

            const double kk = std::tan (std::numbers::pi * f0 / rate);
            const double denominator = 1.0 + kk / q + kk * kk;

            k.highpass = { 1.0, -2.0, 1.0,
                           2.0 * (kk * kk - 1.0) / denominator,
                           (1.0 - kk / q + kk * kk) / denominator };
        }

        return k;
    }

    /// Allocates. Never call from the audio thread.
    void prepare (double sampleRate, int numChannels)
    {
        sampleRate_  = sampleRate > 0.0 ? sampleRate : 48000.0;
        numChannels_ = std::clamp (numChannels, 1, kMaxChannels);

        const auto weighting = designKWeighting (sampleRate_);

        for (auto& channel : channels_)
        {
            channel.shelf.setCoefficients (weighting.shelf);
            channel.highpass.setCoefficients (weighting.highpass);
        }

        intervalSamples_ = std::max (1, static_cast<int> (std::lround (kBlockIntervalSeconds * sampleRate_)));

        // Everything is counted on the standard's own 100 ms grid rather than
        // per sample. A 400 ms block is four intervals and a 3 s window is
        // thirty, so each reading is a sum of a handful of doubles instead of a
        // walk over 144000 samples -- and the readings then land exactly where
        // the Recommendation says they should, which a continuously sliding
        // window would not.
        intervals_.assign (static_cast<std::size_t> (kShortTermIntervals), 0.0);

        blocks_.clear();
        blocks_.reserve (static_cast<std::size_t> (kMaximumIntegrationSeconds
                                                   / kBlockIntervalSeconds) + 1);

        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : channels_)
        {
            channel.shelf.reset();
            channel.highpass.reset();
        }

        std::fill (intervals_.begin(), intervals_.end(), 0.0);
        intervalWrite_ = 0;
        intervalsFilled_ = 0;

        currentSum_ = 0.0;
        sinceInterval_ = 0;

        resetIntegration();
    }

    /// Clears the integrated measurement without touching the filters -- the
    /// "restart measurement" button, which is a different thing from a
    /// transport reset.
    void resetIntegration() noexcept
    {
        blocks_.clear();
        integrationFull_ = false;
    }

    void process (const double* const* channels, int numChannels, int numSamples) noexcept
    {
        const int active = std::min (numChannels, numChannels_);

        if (numSamples <= 0 || active <= 0 || intervals_.empty())
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // The weighted sum of squares across channels, which is the
            // quantity all three readings are a mean of. Stereo weights are
            // 1.0 each; only surround channels differ.
            double sum = 0.0;

            for (int channel = 0; channel < active; ++channel)
            {
                auto& state = channels_[static_cast<std::size_t> (channel)];
                const double weighted = state.highpass.process (state.shelf.process (channels[channel][i]));
                sum += weighted * weighted;
            }

            currentSum_ += sum;

            if (++sinceInterval_ >= intervalSamples_)
                closeInterval();
        }
    }

    // ---- readings -----------------------------------------------------------

    [[nodiscard]] double getMomentaryLufs() const noexcept
    {
        return loudnessOf (meanSquareOver (kMomentaryIntervals));
    }

    [[nodiscard]] double getShortTermLufs() const noexcept
    {
        return loudnessOf (meanSquareOver (kShortTermIntervals));
    }

    /// The gated measurement, which is the one a platform runs.
    ///
    /// Two passes, both from the Recommendation: throw away everything below
    /// -70 LUFS absolute, take the mean of what is left, then throw away
    /// everything more than 10 LU below *that* and take the mean again. The
    /// second pass is what stops a quiet passage counting as loudly as a
    /// chorus.
    [[nodiscard]] double getIntegratedLufs() const noexcept
    {
        if (blocks_.empty())
            return kSilenceLufs;

        const double absoluteGate = energyFor (kAbsoluteGateLufs);

        double sum = 0.0;
        std::size_t count = 0;

        for (const double z : blocks_)
            if (z > absoluteGate)
            {
                sum += z;
                ++count;
            }

        if (count == 0)
            return kSilenceLufs;

        const double relativeGate = energyFor (loudnessOf (sum / static_cast<double> (count))
                                               + kRelativeGateLu);

        double gatedSum = 0.0;
        std::size_t gatedCount = 0;

        for (const double z : blocks_)
            if (z > absoluteGate && z > relativeGate)
            {
                gatedSum += z;
                ++gatedCount;
            }

        return gatedCount == 0 ? kSilenceLufs
                               : loudnessOf (gatedSum / static_cast<double> (gatedCount));
    }

    /// True once the integration has stopped taking new blocks. Reported rather
    /// than silently wrapping: a number that quietly stops updating is worse
    /// than one that says it has.
    [[nodiscard]] bool isIntegrationFull() const noexcept { return integrationFull_; }

    /// How many 400 ms blocks the integration currently holds.
    [[nodiscard]] std::size_t getBlockCount() const noexcept { return blocks_.size(); }

private:
    struct ChannelState
    {
        Biquad<double> shelf;
        Biquad<double> highpass;
    };

    /// Turns a mean square into a loudness. The standard's whole definition.
    [[nodiscard]] static double loudnessOf (double meanSquare) noexcept
    {
        return meanSquare > 0.0 ? kOffsetDb + 10.0 * std::log10 (meanSquare) : kSilenceLufs;
    }

    /// And back, for comparing a gate threshold against stored energies without
    /// taking a logarithm per block.
    [[nodiscard]] static double energyFor (double lufs) noexcept
    {
        return std::pow (10.0, (lufs - kOffsetDb) * 0.1);
    }

    /// Mean square over the last `count` intervals, or 0 if that many have not
    /// happened yet -- a partial window reads low and there is no honest way to
    /// scale it up.
    [[nodiscard]] double meanSquareOver (int count) const noexcept
    {
        if (intervalsFilled_ < count || count <= 0)
            return 0.0;

        const int size = static_cast<int> (intervals_.size());
        double sum = 0.0;
        int index = intervalWrite_ - 1;

        for (int i = 0; i < count; ++i)
        {
            if (index < 0)
                index += size;

            sum += intervals_[static_cast<std::size_t> (index)];
            --index;
        }

        return sum / static_cast<double> (count * intervalSamples_);
    }

    /// Files the interval just finished, and with it the 400 ms gating block
    /// that ends on the same boundary.
    void closeInterval() noexcept
    {
        intervals_[static_cast<std::size_t> (intervalWrite_)] = currentSum_;

        if (++intervalWrite_ >= static_cast<int> (intervals_.size()))
            intervalWrite_ = 0;

        intervalsFilled_ = std::min (intervalsFilled_ + 1, static_cast<int> (intervals_.size()));

        currentSum_ = 0.0;
        sinceInterval_ = 0;

        // Blocks overlap by 75%: one ends every interval, once four exist. A
        // partial first block would read low and drag the integration down for
        // the rest of the take.
        if (intervalsFilled_ >= kMomentaryIntervals)
            addBlock (meanSquareOver (kMomentaryIntervals));
    }

    void addBlock (double meanSquare) noexcept
    {
        // reserve() in prepare() sized this, so push_back cannot allocate --
        // and once it is full the integration says so rather than growing on
        // the audio thread.
        if (blocks_.size() >= blocks_.capacity())
        {
            integrationFull_ = true;
            return;
        }

        blocks_.push_back (meanSquare);
    }

    std::array<ChannelState, kMaxChannels> channels_ {};

    /// Sum of weighted squares within each finished 100 ms interval.
    std::vector<double> intervals_;

    /// Mean square of each finished 400 ms block, for the gating.
    std::vector<double> blocks_;

    double sampleRate_   { 48000.0 };
    int numChannels_     { 2 };
    int intervalSamples_ { 1 };

    int intervalWrite_   { 0 };
    int intervalsFilled_ { 0 };

    double currentSum_   { 0.0 };
    int sinceInterval_   { 0 };

    bool integrationFull_ { false };
};

} // namespace tezla::dsp
