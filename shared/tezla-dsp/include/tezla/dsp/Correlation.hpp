#pragma once

// Stereo correlation: whether the two channels agree, and what happens if
// someone folds them to mono.
//
//   r = sum(L*R) / sqrt(sum(L^2) * sum(R^2))
//
//   +1   identical, or a pure level difference. Folds to mono untouched.
//    0   uncorrelated. Folding loses 3 dB and nothing else.
//   -1   polarity inverted. Folding cancels it to silence.
//
// The normalisation is what makes it a *correlation* rather than a level
// reading: scaling either channel changes nothing, so a hard-panned mix reads
// the same quiet as it does loud.
//
// ---------------------------------------------------------------------------
// Why the per-band reading is the one that matters
// ---------------------------------------------------------------------------
//
// A full-band correlation meter reads the mix, and a mix is mostly mid and top,
// so a sub that is quietly out of phase barely moves it. That failure survives
// headphones -- where each ear gets its own channel and nothing cancels -- and
// then removes the bass on any system that sums to mono, which is most club
// rigs and every phone speaker.
//
// So the sub band gets its own reading, and the honest rule is that below about
// 120 Hz it should sit near +1. That is not taste; it is what "the sub still
// exists in mono" means as a number.
//
// The bands come from the crossover the rest of the suite already uses, so a
// band here is the same band Emberdrive processes.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Crossover.hpp"

namespace tezla::dsp {

/// One sliding-window correlation.
///
/// The three running sums are kept incrementally and rebuilt from the ring
/// periodically. Incremental sums of squares drift -- the same random walk
/// BoxStackSmoother documents -- and a correlation is a ratio of them, so the
/// drift would show up as a reading that slowly wandered off +1 on a signal
/// that had not changed.
class CorrelationMeter
{
public:
    /// How often the running sums are rebuilt from the ring, in samples.
    static constexpr int kResyncInterval = 65536;

    /// Allocates. Never call from the audio thread.
    void prepare (double sampleRate, double windowSeconds = 0.400)
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        length_ = std::max (16, static_cast<int> (std::lround (windowSeconds * rate)));

        left_.assign (static_cast<std::size_t> (length_), 0.0);
        right_.assign (static_cast<std::size_t> (length_), 0.0);

        reset();
    }

    void reset() noexcept
    {
        std::fill (left_.begin(), left_.end(), 0.0);
        std::fill (right_.begin(), right_.end(), 0.0);

        writePosition_ = 0;
        sinceResync_ = 0;

        sumLeftSquared_ = sumRightSquared_ = sumProduct_ = 0.0;
    }

    void process (double left, double right) noexcept
    {
        auto& oldLeft = left_[static_cast<std::size_t> (writePosition_)];
        auto& oldRight = right_[static_cast<std::size_t> (writePosition_)];

        sumLeftSquared_  += left * left   - oldLeft * oldLeft;
        sumRightSquared_ += right * right - oldRight * oldRight;
        sumProduct_      += left * right  - oldLeft * oldRight;

        oldLeft = left;
        oldRight = right;

        if (++writePosition_ >= length_)
            writePosition_ = 0;

        if (++sinceResync_ >= kResyncInterval)
            resync();
    }

    /// -1 to +1. Silence reads +1 rather than 0: two channels that are both
    /// nothing are not *disagreeing*, and a meter that swings to the middle
    /// every time the music stops is a meter nobody watches.
    [[nodiscard]] double getCorrelation() const noexcept
    {
        const double denominator = std::sqrt (std::max (0.0, sumLeftSquared_)
                                            * std::max (0.0, sumRightSquared_));

        // Below this there is no signal to correlate. The threshold is on the
        // *energy product*, so it scales correctly with window length.
        if (denominator < 1.0e-20)
            return 1.0;

        return std::clamp (sumProduct_ / denominator, -1.0, 1.0);
    }

    [[nodiscard]] int getWindowSamples() const noexcept { return length_; }

private:
    void resync() noexcept
    {
        double ll = 0.0, rr = 0.0, lr = 0.0;

        for (std::size_t i = 0; i < left_.size(); ++i)
        {
            ll += left_[i] * left_[i];
            rr += right_[i] * right_[i];
            lr += left_[i] * right_[i];
        }

        sumLeftSquared_ = ll;
        sumRightSquared_ = rr;
        sumProduct_ = lr;
        sinceResync_ = 0;
    }

    std::vector<double> left_, right_;

    int length_        { 16 };
    int writePosition_ { 0 };
    int sinceResync_   { 0 };

    double sumLeftSquared_  { 0.0 };
    double sumRightSquared_ { 0.0 };
    double sumProduct_      { 0.0 };
};

/// Full-band correlation plus one reading per crossover band.
///
/// Uses the suite's own ThreeBandSplitter, so "the low band" here is the same
/// low band Emberdrive processes -- which is the point of having one crossover.
class StereoAnalyser
{
public:
    enum Band { low = 0, mid, high, numBands };

    /// Where the sub check happens. 120 Hz because that is about where a club
    /// system stops being able to place a sound and starts merely moving air.
    static constexpr double kDefaultLowCrossoverHz  = 120.0;
    static constexpr double kDefaultHighCrossoverHz = 2000.0;

    void prepare (double sampleRate, double windowSeconds = 0.400)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        full_.prepare (sampleRate_, windowSeconds);

        for (auto& band : bands_)
            band.prepare (sampleRate_, windowSeconds);

        leftCrossover_.prepare (sampleRate_);
        rightCrossover_.prepare (sampleRate_);

        setCrossovers (kDefaultLowCrossoverHz, kDefaultHighCrossoverHz);
        reset();
    }

    void setCrossovers (double lowHz, double highHz) noexcept
    {
        leftCrossover_.setCrossovers (lowHz, highHz);
        rightCrossover_.setCrossovers (lowHz, highHz);
        lowCrossoverHz_ = lowHz;
    }

    void reset() noexcept
    {
        full_.reset();

        for (auto& band : bands_)
            band.reset();

        leftCrossover_.reset();
        rightCrossover_.reset();

        peakWidth_ = 0.0;
    }

    void process (const double* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numSamples <= 0 || numChannels < 2)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const double left = channels[0][i];
            const double right = channels[1][i];

            full_.process (left, right);

            double leftLow = 0.0, leftMid = 0.0, leftHigh = 0.0;
            double rightLow = 0.0, rightMid = 0.0, rightHigh = 0.0;

            leftCrossover_.process (left, leftLow, leftMid, leftHigh);
            rightCrossover_.process (right, rightLow, rightMid, rightHigh);

            bands_[low].process (leftLow, rightLow);
            bands_[mid].process (leftMid, rightMid);
            bands_[high].process (leftHigh, rightHigh);

            // Side energy relative to mid, held with a slow fall -- the number
            // behind a width display. Mid/side rather than L/R because that is
            // what a width control acts on.
            const double m = 0.5 * (left + right);
            const double s = 0.5 * (left - right);
            const double width = std::abs (m) > 1.0e-12 ? std::abs (s) / std::abs (m) : 0.0;

            peakWidth_ = std::max (width, peakWidth_ * 0.99999);
        }
    }

    [[nodiscard]] double getCorrelation() const noexcept { return full_.getCorrelation(); }

    [[nodiscard]] double getBandCorrelation (Band band) const noexcept
    {
        return bands_[static_cast<std::size_t> (band)].getCorrelation();
    }

    /// True when the low band is correlated enough to survive a fold to mono.
    ///
    /// 0.5 rather than something stricter: a sub with a little stereo reverb on
    /// it is fine, and a meter that cries wolf gets ignored. Below 0.5 the fold
    /// starts costing real level -- at r = 0 it is 3 dB, and it gets worse fast.
    [[nodiscard]] bool isLowBandMonoSafe() const noexcept
    {
        return getBandCorrelation (low) >= 0.5;
    }

    [[nodiscard]] double getLowCrossoverHz() const noexcept { return lowCrossoverHz_; }

private:
    CorrelationMeter full_;
    std::array<CorrelationMeter, numBands> bands_;

    ThreeBandSplitter<double> leftCrossover_;
    ThreeBandSplitter<double> rightCrossover_;

    double sampleRate_      { 48000.0 };
    double lowCrossoverHz_  { kDefaultLowCrossoverHz };
    double peakWidth_       { 0.0 };
};


// ---------------------------------------------------------------------------
// StereoScope -- the sample pairs behind a goniometer
// ---------------------------------------------------------------------------
//
// A correlation number says how much the channels agree. It does not say *how*
// they disagree, and those are different questions: a mix that is wide and one
// that is a hard-panned pair can read the same r, and they need opposite fixes.
// The scope is the picture the number summarises.
//
// One buffer of interleaved pairs rather than two buffers, and that is the
// whole design decision. Two ring buffers with two write indices can be read a
// block apart, and a goniometer fed L from now and R from a millisecond ago
// draws a rotation that is not in the audio -- an artefact indistinguishable
// from a real phase problem, which is exactly what the display exists to find.
// One index cannot tear that way.
//
// Sized in **seconds**, not samples, so the picture spans the same slice of
// time at 44.1 and at 192 kHz. Drawing it thins the point cloud by striding,
// which is a visual decimation of something already dense -- the scope is a
// display, and the numbers next to it are the measurement.
class StereoScope
{
public:
    /// About the shortest window that still shows a bass cycle whole. Longer
    /// smears a moving image into a blob; shorter flickers.
    static constexpr double kDefaultSeconds = 0.050;

    /// Allocates. Never call from the audio thread.
    void prepare (double sampleRate, double seconds = kDefaultSeconds)
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto wanted = static_cast<std::size_t> (rate * std::max (seconds, 0.001));

        // Power of two, so the wrap is a mask rather than a modulo.
        std::size_t pairs = 1;
        while (pairs < wanted)
            pairs <<= 1;

        buffer_.assign (pairs * 2, 0.0);
        mask_ = pairs - 1;
        write_.store (0, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0);
        write_.store (0, std::memory_order_relaxed);
    }

    /// Audio thread. Real-time safe. A mono input is duplicated, which draws
    /// the 45-degree line a mono signal actually is.
    void push (const double* const* channels, int numChannels, int numSamples) noexcept
    {
        if (buffer_.empty() || channels == nullptr || numChannels < 1 || numSamples <= 0)
            return;

        const double* left  = channels[0];
        const double* right = numChannels > 1 ? channels[1] : channels[0];

        if (left == nullptr || right == nullptr)
            return;

        std::size_t write = write_.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            buffer_[write * 2]     = left[i];
            buffer_[write * 2 + 1] = right[i];
            write = (write + 1) & mask_;
        }

        write_.store (write, std::memory_order_release);
    }

    /// Message thread. Copies the most recent `numPairs` in chronological
    /// order, taking every `stride`-th pair so a caller can ask for a fixed
    /// number of points whatever the sample rate.
    ///
    /// Returns how many pairs were written, which is zero if the scope is not
    /// prepared or the request does not fit.
    [[nodiscard]] int readLatest (double* leftOut, double* rightOut,
                                  int numPairs, int stride = 1) const noexcept
    {
        if (buffer_.empty() || leftOut == nullptr || rightOut == nullptr
            || numPairs <= 0 || stride < 1)
            return 0;

        const std::size_t pairs = mask_ + 1;
        const auto span = static_cast<std::size_t> (numPairs) * static_cast<std::size_t> (stride);

        if (span > pairs)
            return 0;

        const std::size_t write = write_.load (std::memory_order_acquire);
        std::size_t read = (write + pairs - span) & mask_;

        for (int i = 0; i < numPairs; ++i)
        {
            leftOut[i]  = buffer_[read * 2];
            rightOut[i] = buffer_[read * 2 + 1];
            read = (read + static_cast<std::size_t> (stride)) & mask_;
        }

        return numPairs;
    }

    /// How many pairs the buffer holds. A caller picks its stride from this.
    [[nodiscard]] std::size_t getCapacity() const noexcept
    {
        return buffer_.empty() ? 0 : mask_ + 1;
    }

private:
    std::vector<double> buffer_;   ///< interleaved L, R
    std::size_t mask_ { 0 };
    std::atomic<std::size_t> write_ { 0 };
};

} // namespace tezla::dsp
