#pragma once

// Linkwitz-Riley crossovers, for splitting the signal into bands that sum back
// together without a hole or a bump at the crossover point.
//
// LR4 is a cascade of two identical Butterworth second-order sections. Its
// lowpass and highpass sum to an allpass -- flat magnitude, with a phase shift
// -- which is the property that makes multiband processing possible at all. A
// pair of ordinary filters would leave a dip or a peak where they meet, and on
// a mix bus that is instantly audible.
//
// The phase shift is real and is the price of the flat sum. It means multiband
// mode is not phase-transparent against bypass; that is inherent to every
// crossover-based processor, not a shortcoming of this one.

#include <algorithm>
#include <cmath>

#include "Biquad.hpp"

namespace tezla::dsp {

/// One two-way Linkwitz-Riley 4th-order split.
template <typename Float = double>
class LinkwitzRiley4
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setCrossover (crossoverHz_);
        reset();
    }

    void setCrossover (Float hz) noexcept
    {
        // Keep the corner inside the band at every sample rate; a crossover
        // automated past Nyquist must clamp rather than produce nonsense.
        const auto nyquist = static_cast<Float> (sampleRate_ * 0.5);
        crossoverHz_ = std::clamp (hz, static_cast<Float> (10), nyquist * static_cast<Float> (0.45));

        // Butterworth Q; two of these in series make Linkwitz-Riley.
        const auto q = static_cast<Float> (0.70710678118654752);

        const auto lowpass  = design::lowpass  (crossoverHz_, q, sampleRate_);
        const auto highpass = design::highpass (crossoverHz_, q, sampleRate_);

        lowA_.setCoefficients (lowpass);
        lowB_.setCoefficients (lowpass);
        highA_.setCoefficients (highpass);
        highB_.setCoefficients (highpass);
    }

    [[nodiscard]] Float getCrossover() const noexcept { return crossoverHz_; }

    void reset() noexcept
    {
        lowA_.reset();
        lowB_.reset();
        highA_.reset();
        highB_.reset();
    }

    void process (Float input, Float& low, Float& high) noexcept
    {
        low  = lowB_.process (lowA_.process (input));
        high = highB_.process (highA_.process (input));
    }

    /// The allpass a band has to be run through to stay phase-aligned with a
    /// band that went through this split. LP + HP of an LR4 is exactly this.
    [[nodiscard]] BiquadCoefficients<Float> matchingAllpass() const noexcept
    {
        return design::allpass (crossoverHz_, static_cast<Float> (0.70710678118654752), sampleRate_);
    }

private:
    double sampleRate_  { 44100.0 };
    Float  crossoverHz_ { static_cast<Float> (1000) };

    Biquad<Float> lowA_, lowB_, highA_, highB_;
};

/// Three-way split, built from two two-way splits.
///
/// The low band gets an extra allpass. Without it the low band would arrive
/// with a different phase from the other two -- because they passed through the
/// second crossover and it did not -- and the three would no longer sum flat.
/// This is the part of a three-way crossover that is easy to leave out and
/// hard to hear as a bug rather than as "the multiband mode sounds a bit odd".
template <typename Float = double>
class ThreeBandSplitter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        lowSplit_.prepare (sampleRate_);
        highSplit_.prepare (sampleRate_);
        updateCompensation();
        reset();
    }

    void setCrossovers (Float lowHz, Float highHz) noexcept
    {
        // The bands must stay in order however they are automated.
        const Float safeLow  = std::min (lowHz, highHz);
        const Float safeHigh = std::max (lowHz, highHz);

        lowSplit_.setCrossover (safeLow);
        highSplit_.setCrossover (safeHigh);
        updateCompensation();
    }

    void reset() noexcept
    {
        lowSplit_.reset();
        highSplit_.reset();
        compensation_.reset();
    }

    void process (Float input, Float& low, Float& mid, Float& high) noexcept
    {
        Float belowFirst {}, aboveFirst {};
        lowSplit_.process (input, belowFirst, aboveFirst);
        highSplit_.process (aboveFirst, mid, high);
        low = compensation_.process (belowFirst);
    }

private:
    void updateCompensation() noexcept
    {
        compensation_.setCoefficients (highSplit_.matchingAllpass());
    }

    double sampleRate_ { 44100.0 };
    LinkwitzRiley4<Float> lowSplit_, highSplit_;
    Biquad<Float> compensation_;
};

} // namespace tezla::dsp
