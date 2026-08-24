#pragma once

// Halfband FIR filters, the building block of the oversampler.
//
// A halfband lowpass has its cutoff at a quarter of the sample rate it runs at,
// and every even-offset tap except the centre is exactly zero. That is worth
// exploiting: it makes the filter roughly twice as cheap as its tap count
// suggests, and it makes one of the two polyphase branches a plain delay.
//
// Design is a windowed sinc with a Kaiser window, computed at run time from the
// requested tap count and stopband attenuation. No coefficient tables: a table
// is a number nobody can check, and this one can be measured (see
// tests/test_Oversampler.cpp).

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace tezla::dsp {

namespace detail {

/// Modified Bessel function of the first kind, order zero. Series expansion;
/// this only ever runs at prepare time.
[[nodiscard]] inline double besselI0 (double x) noexcept
{
    double sum  = 1.0;
    double term = 1.0;

    for (int k = 1; k < 64; ++k)
    {
        term *= (x * 0.5) / static_cast<double> (k);
        const double contribution = term * term;
        sum += contribution;

        if (contribution < sum * 1.0e-17)
            break;
    }

    return sum;
}

/// Kaiser window beta for a requested stopband attenuation, per Kaiser's
/// standard empirical formula.
[[nodiscard]] inline double kaiserBeta (double stopbandDb) noexcept
{
    if (stopbandDb > 50.0)
        return 0.1102 * (stopbandDb - 8.7);
    if (stopbandDb >= 21.0)
        return 0.5842 * std::pow (stopbandDb - 21.0, 0.4) + 0.07886 * (stopbandDb - 21.0);
    return 0.0;
}

} // namespace detail

/// One tap of a sparse FIR: which delay-line position, and by how much.
struct SparseTap
{
    int    index {};
    double coefficient {};
};

/// Halfband coefficients, already split into the two polyphase branches and
/// stripped of the structural zeros.
struct HalfbandCoefficients
{
    int numTaps {};
    std::vector<SparseTap> allTaps;    ///< the whole filter, zeros removed
    std::vector<SparseTap> evenBranch; ///< h[2k], indexed by k
    std::vector<SparseTap> oddBranch;  ///< h[2k+1], indexed by k

    /// Group delay in samples, at the rate the filter runs at.
    [[nodiscard]] int groupDelay() const noexcept { return (numTaps - 1) / 2; }

    /// Magnitude response at a normalised frequency (0 = DC, 0.5 = Nyquist),
    /// for verifying the design without running signal through it.
    [[nodiscard]] double magnitudeAt (double normalisedFrequency) const noexcept
    {
        double real = 0.0;
        double imaginary = 0.0;
        const double omega = 2.0 * std::numbers::pi * normalisedFrequency;

        for (const auto& tap : allTaps)
        {
            const double phase = omega * static_cast<double> (tap.index);
            real      += tap.coefficient * std::cos (phase);
            imaginary -= tap.coefficient * std::sin (phase);
        }

        return std::sqrt (real * real + imaginary * imaginary);
    }
};

/// Designs a halfband lowpass. `numTaps` must be odd; the centre tap is 0.5 and
/// every other even-offset tap is zero by construction.
[[nodiscard]] inline HalfbandCoefficients designHalfband (int numTaps, double stopbandDb = 100.0)
{
    assert (numTaps >= 3 && (numTaps % 2) == 1);

    HalfbandCoefficients result;
    result.numTaps = numTaps;

    const int    centre = (numTaps - 1) / 2;
    const double beta   = detail::kaiserBeta (stopbandDb);
    const double i0Beta = detail::besselI0 (beta);

    for (int i = 0; i < numTaps; ++i)
    {
        const int offset = i - centre;

        double h = 0.0;
        if (offset == 0)
        {
            h = 0.5;
        }
        else if ((offset % 2) != 0)
        {
            // Ideal halfband impulse response; sin(pi*offset/2) is +/-1 for odd
            // offsets, so this reduces to an alternating 1/(pi*offset).
            const double sinc = std::sin (std::numbers::pi * static_cast<double> (offset) * 0.5)
                              / (std::numbers::pi * static_cast<double> (offset));

            const double t = 2.0 * static_cast<double> (i) / static_cast<double> (numTaps - 1) - 1.0;
            const double window = detail::besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / i0Beta;

            h = sinc * window;
        }

        if (h == 0.0)
            continue;

        result.allTaps.push_back ({ i, h });

        if ((i % 2) == 0)
            result.evenBranch.push_back ({ i / 2, h });
        else
            result.oddBranch.push_back ({ (i - 1) / 2, h });
    }

    return result;
}

/// A short circular delay line with a power-of-two mask, so reads are a
/// masked subtraction rather than a modulo.
class DelayLine
{
public:
    void prepare (int minimumLength)
    {
        std::size_t size = 1;
        while (size < static_cast<std::size_t> (minimumLength) + 1)
            size <<= 1;

        buffer_.assign (size, 0.0);
        mask_  = size - 1;
        write_ = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0);
        write_ = 0;
    }

    void push (double value) noexcept
    {
        write_ = (write_ + 1) & mask_;
        buffer_[write_] = value;
    }

    /// `age` 0 is the most recently pushed sample.
    [[nodiscard]] double at (int age) const noexcept
    {
        return buffer_[(write_ - static_cast<std::size_t> (age)) & mask_];
    }

    [[nodiscard]] double dot (const std::vector<SparseTap>& taps) const noexcept
    {
        double sum = 0.0;
        for (const auto& tap : taps)
            sum += tap.coefficient * at (tap.index);
        return sum;
    }

private:
    std::vector<double> buffer_;
    std::size_t mask_  { 0 };
    std::size_t write_ { 0 };
};

/// Doubles the sample rate. One input sample in, two output samples out.
class HalfbandUpsampler
{
public:
    void prepare (const HalfbandCoefficients& coefficients)
    {
        coefficients_ = coefficients;
        line_.prepare (coefficients.numTaps);
    }

    void reset() noexcept { line_.reset(); }

    void process (const double* input, double* output, int numInputSamples) noexcept
    {
        for (int n = 0; n < numInputSamples; ++n)
        {
            line_.push (input[n]);
            // The factor of two restores the level lost to zero-stuffing.
            output[2 * n]     = 2.0 * line_.dot (coefficients_.evenBranch);
            output[2 * n + 1] = 2.0 * line_.dot (coefficients_.oddBranch);
        }
    }

private:
    HalfbandCoefficients coefficients_;
    DelayLine line_;
};

/// Halves the sample rate. Two input samples in, one output sample out.
class HalfbandDownsampler
{
public:
    void prepare (const HalfbandCoefficients& coefficients)
    {
        coefficients_ = coefficients;
        line_.prepare (coefficients.numTaps);
    }

    void reset() noexcept { line_.reset(); }

    void process (const double* input, double* output, int numOutputSamples) noexcept
    {
        for (int n = 0; n < numOutputSamples; ++n)
        {
            // The dot product is evaluated with the *even* high-rate sample as
            // the most recent one. Taking it after pushing the odd sample
            // instead would decimate on the wrong phase and make the composite
            // delay a half sample short of the reported latency -- which reads
            // as a broadband -31 dB error on a round trip that should null.
            line_.push (input[2 * n]);
            output[n] = line_.dot (coefficients_.allTaps);
            line_.push (input[2 * n + 1]);
        }
    }

private:
    HalfbandCoefficients coefficients_;
    DelayLine line_;
};

} // namespace tezla::dsp
