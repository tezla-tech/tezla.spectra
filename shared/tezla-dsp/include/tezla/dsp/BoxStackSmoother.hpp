#pragma once

// A cascade of moving averages: the smoothing half of the limiter's guarantee.
//
// Paired with RunningMinimum, this is what makes a look-ahead limiter's ceiling
// exact. The proof needs exactly three things of the kernel, and a cascade of
// box filters has all three by construction rather than by design:
//
//   non-negative      each box is, and a convolution of non-negative kernels is
//   sums to one       each box does, and the product of the sums is one
//   finite support    the support is the sum of the boxes', so it is bounded
//
// Any kernel with those three works. Boxes are chosen because the recursive
// form costs one add and one subtract per stage per sample whatever the length,
// so a 20 ms attack at 192 kHz is the same work as a 1 ms one.
//
// Four stages rather than one. A single box gives a gain curve with corners in
// it, and a corner in the gain is a discontinuity in the derivative of the
// output -- audible as a tick on a clean tone even though the ceiling is still
// respected. Two boxes give a triangle, four a shape close enough to a Gaussian
// that the curvature never jumps. The cost is the same three operations again.
//
// The one thing a recursive moving average gets wrong on its own is drift: the
// running sum is updated rather than recomputed, so rounding error accumulates
// as a random walk. At a thousand samples of window and a billion samples of
// runtime that reaches about 1e-11 of the output -- inaudible, and still enough
// to fail an assertion that the ceiling is *never* exceeded. Each stage's sum
// is therefore recomputed from its own ring periodically, which costs O(length)
// once every 65536 samples and removes the question.
//
// What resyncing does *not* remove is the per-sample rounding, and it is worth
// stating the size rather than implying it is zero. Measured: taps come out at
// -7e-18 where they should be 0, a step settles 6.7e-16 above its own input,
// and a minimum feeding this cascade ends up about **1e-14** above the sample
// it is supposed to stay under. That last figure is the one that matters, and
// it is -280 dBFS -- forty decibels below a 24-bit noise floor.
//
// It is rounding rather than a misalignment, and that was checked rather than
// assumed: widening the minimum window leaves it unchanged, while shortening
// the resync interval from 65536 to 256 only takes it from 1.05e-14 to
// 5.8e-15. A structural error would move with the first and ignore the second.
// Two hundred and fifty-six times the resync work to halve a number at -280 dB
// is not a trade worth making.
//
// Two answers, because the two failures are not the same size. Each stage's
// output is clamped at zero, since a negative gain would invert polarity and
// that is a real defect rather than a rounding one; it costs one comparison.
// The high side is left alone here and clamped once at the end of the limiter,
// where a single clamp to the ceiling makes the delivered output exact.

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace tezla::dsp {

class BoxStackSmoother
{
public:
    /// How many moving averages are cascaded. Four is the shape; the count is
    /// fixed so the per-sample cost cannot depend on a control.
    static constexpr int kNumStages = 4;

    /// How often each stage's running sum is rebuilt from its ring, in samples.
    static constexpr int kResyncInterval = 65536;

    /// Allocates for the longest total support that will ever be asked for.
    void prepare (int maximumLength)
    {
        capacity_ = std::max (1, maximumLength);

        // Worst case for one stage is the whole support in a single box, which
        // cannot happen with the split below but costs nothing to allow for.
        for (auto& stage : stages_)
            stage.ring.assign (static_cast<std::size_t> (capacity_), 0.0);

        setLength (capacity_);
        reset();
    }

    /// Primes every stage as though `value` had been the input forever.
    ///
    /// Unity by default: a limiter's gain starts at 1, and a smoother primed
    /// with zeros would mute the first N samples after every transport start.
    void reset (double value = 1.0) noexcept
    {
        for (auto& stage : stages_)
        {
            std::fill (stage.ring.begin(), stage.ring.end(), value);
            stage.sum = value * static_cast<double> (stage.length);
            stage.writePosition = 0;
        }

        sinceResync_ = 0;
    }

    /// The total support, in samples. Clamped to what prepare() allocated.
    ///
    /// The stages are split as evenly as the length allows, so the support is
    /// exactly what was asked for rather than what a division happened to give.
    /// Rebuilds each stage's sum, which costs O(length) once per change and
    /// nothing per sample -- but it is not free, so callers that automate the
    /// attack should not call this every chunk.
    void setLength (int length) noexcept
    {
        // One, not kNumStages. Four boxes of length 1 have a support of 1 and
        // pass their input straight through, which is exactly what a limiter
        // with no look-ahead needs -- clamping the floor to the stage count
        // instead gave a support of 4 and three samples of latency that the
        // plugin would then have reported as zero.
        const int wanted = std::clamp (length, 1, capacity_);

        if (wanted == length_)
            return;

        length_ = wanted;

        // A cascade of boxes of length L(i) has support sum(L(i) - 1) + 1, so
        // the lengths have to add up to length_ - 1 + kNumStages.
        const int spread    = length_ - 1;
        const int base      = spread / kNumStages;
        const int remainder = spread % kNumStages;

        for (int i = 0; i < kNumStages; ++i)
        {
            auto& stage = stages_[static_cast<std::size_t> (i)];
            stage.length = base + 1 + (i < remainder ? 1 : 0);
            stage.writePosition = 0;
        }

        resync();
    }

    [[nodiscard]] int getLength() const noexcept { return length_; }
    [[nodiscard]] int getCapacity() const noexcept { return capacity_; }

    /// The delay this introduces, in samples.
    ///
    /// The full support minus one, not the half-support a group delay would
    /// suggest. The guarantee needs the *last* tap of the kernel to still be
    /// looking at a window that contains the sample being processed, and that
    /// is what fixes the alignment; reading the group delay instead would put
    /// the gain curve half a window early and let peaks through.
    [[nodiscard]] int getLatencySamples() const noexcept { return length_ - 1; }

    [[nodiscard]] double process (double x) noexcept
    {
        double value = x;

        for (auto& stage : stages_)
        {
            auto& slot = stage.ring[static_cast<std::size_t> (stage.writePosition)];

            stage.sum += value - slot;
            slot = value;

            if (++stage.writePosition >= stage.length)
                stage.writePosition = 0;

            // At zero rather than merely near it: the sum is accumulated
            // rather than recomputed, so it can land a few ULP below zero on a
            // signal that has just gone silent, and a negative gain inverts
            // polarity. Cheaper to prevent than to explain.
            value = std::max (0.0, stage.sum / static_cast<double> (stage.length));
        }

        if (++sinceResync_ >= kResyncInterval)
            resync();

        return value;
    }

private:
    struct Stage
    {
        std::vector<double> ring;
        double sum { 0.0 };
        int length { 1 };
        int writePosition { 0 };
    };

    /// Recomputes each running sum from its ring, so accumulated rounding does
    /// not drift the kernel away from summing to one.
    void resync() noexcept
    {
        for (auto& stage : stages_)
        {
            double total = 0.0;

            for (int i = 0; i < stage.length; ++i)
                total += stage.ring[static_cast<std::size_t> (i)];

            stage.sum = total;
        }

        sinceResync_ = 0;
    }

    std::array<Stage, kNumStages> stages_ {};

    int capacity_    { 1 };
    int length_      { 0 };
    int sinceResync_ { 0 };
};

} // namespace tezla::dsp
