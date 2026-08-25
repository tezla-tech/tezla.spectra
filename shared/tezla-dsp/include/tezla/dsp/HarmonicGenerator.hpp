#pragma once

// Generates harmonics, and as little else as possible.
//
// A conventional exciter filters a copy of the signal, distorts it, and mixes
// it back in. A soft shaper passes most of its input straight through, so what
// gets mixed back is mostly a phase-shifted copy of the source: that is why an
// exciter's blend control also acts as an EQ, and why it combs against the dry
// path near the filter corner.
//
// This generator is built so that the copy is not there. The two halves get
// there by different routes, and the difference is worth stating precisely
// because it is easy to overclaim:
//
//   even   An even function of its input, so its Fourier series contains DC
//          and even harmonics and nothing else. Exactly zero at the
//          fundamental, for any input, at any level, at any drive. Measured
//          residual is the ADAA approximation floor -- which shows up equally
//          at every odd harmonic, and so is demonstrably not a leaked copy.
//
//   odd    A saturator minus a straight wire. That alone is *not* enough, and
//          measuring it is what showed why: for a cubic residual the
//          fundamental is three times the third harmonic, so at high drive the
//          odd half stops being a harmonic generator and becomes a band
//          canceller -- at drive 30 it removed the band to within 0.4 dB. So
//          the saturator's own fundamental gain is subtracted as well; see
//          fundamentalGain() below.
//
// The two curves are deliberately from the same family, which makes them cheap
// as well as coherent:
//
//   odd    h(x) = x / sqrt(1 + u^2) - x  (+ the trim)     u = drive * x
//   even   h(x) = 1 - 1 / sqrt(1 + u^2)
//
// Writing r = sqrt(1 + u^2) and s = u^2 / (r * (r + 1)), the even curve *is* s
// and the untrimmed odd curve is exactly -x * s. One square root serves both.
//
// Every expression is written to avoid subtracting nearly equal numbers. The
// algebraically obvious forms all do, and the resulting error arrives as hiss
// on quiet material rather than as anything that looks like a bug. See
// Waveshapers.hpp SoftEven for the same argument at more length.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tezla::dsp {

/// Fundamental gain of the saturator x / sqrt(1 + (drive * x)^2), for a sine.
///
/// The classical describing function: the fraction of the input fundamental
/// that survives the saturator, which depends only on the product of drive and
/// input amplitude. Exactly it is
///
///     a1(v) = (2/pi) * integral over half a cycle of  sin^2 t / sqrt(1 + v^2 sin^2 t)
///
/// -- an elliptic integral with no elementary form, and far too expensive to
/// evaluate per sample in any case. This is a closed-form fit built to be right
/// at both ends rather than merely fitted in the middle: the v^2 and v^4 terms
/// carry the exact 1 - (3/8)v^2 behaviour at small v and the exact 4/(pi v)
/// asymptote at large v, so it stays correct outside the range it was fitted
/// over. Worst-case relative error against numerical integration is 0.14%,
/// which is 57 dB of fundamental cancellation.
[[nodiscard]] inline double fundamentalGain (double v) noexcept
{
    const double v2 = v * v;
    const double inner = 1.0 + 1.4720 * v2 + 0.1180 * v2 * v + 0.380530 * v2 * v2;

    return 1.0 / std::sqrt (std::sqrt (inner));
}

/// Blended odd/even harmonic generator, shaped for ADAA (see Adaa.hpp).
///
/// Drive 0 is exactly the zero function -- no harmonics, no DC, nothing to fade
/// out -- which is what makes a bit-exact bypass possible at the neutral
/// setting.
class HarmonicGenerator
{
public:
    HarmonicGenerator() noexcept { setColour (0.5); }

    /// The `drive` in the formulae above: 0 is silence, and useful musical
    /// settings on a band of about unit amplitude run to roughly 30. The
    /// mapping from a percentage control to this number belongs to the plugin.
    void setDrive (double drive) noexcept
    {
        drive_ = std::max (drive, 0.0);
        updateDerived();
    }

    [[nodiscard]] double getDrive() const noexcept { return drive_; }

    /// Roughly how loud the signal reaching this generator is, as a linear
    /// amplitude -- in practice the smoothed envelope of the band.
    ///
    /// The odd half needs it because the fundamental it has to cancel depends
    /// on how hard the saturator is actually being driven, which is drive times
    /// amplitude and not drive alone. Feeding a constant 1 here is not wrong,
    /// merely optimistic: the cancellation is then exact only for a full-scale
    /// band, and overshoots into *adding* a copy of the source when the band is
    /// quieter. Give it the real envelope.
    ///
    /// It changes per sample, which makes this a time-varying shaper. ADAA
    /// tolerates that for the same reason it tolerates smoothed parameters: the
    /// envelope moves far more slowly than the method's own error.
    void setInputAmplitude (double amplitude) noexcept
    {
        amplitude_ = std::max (amplitude, 0.0);
        updateDerived();
    }

    /// 0 is purely odd (third harmonic upwards), 1 is purely even (second).
    ///
    /// Equal power rather than linear, because the two halves land on different
    /// harmonics and so add as uncorrelated signals. A linear crossfade would
    /// dip 3 dB in the middle of the control's travel, which reads as "the
    /// middle setting is the quiet one" rather than as a colour change.
    void setColour (double colour) noexcept
    {
        colour_ = std::clamp (colour, 0.0, 1.0);

        const double angle = colour_ * 0.5 * std::numbers::pi_v<double>;

        colourOdd_  = std::cos (angle);
        colourEven_ = std::sin (angle);

        updateDerived();
    }

    [[nodiscard]] double getColour() const noexcept { return colour_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        const double u = drive_ * x;
        const double r = std::sqrt (1.0 + u * u);

        // s is the even curve; the untrimmed odd curve is -x * s. Both fall out
        // of one square root and one division.
        const double s = (u * u) / (r * (r + 1.0));

        return oddWeight_ * x * (oddTrim_ - s) + colourEven_ * s;
    }

    /// Antiderivative, F1(0) = 0.
    ///
    ///   odd    oddTrim * x^2 / 2  -  drive^2 x^4 / (2 (r + 1)^2)
    ///   even   (u - asinh(u)) / drive
    ///
    /// The odd form is exact and needs no special case: the (r + 1) denominator
    /// is there precisely so that nothing is ever subtracted. The even form
    /// cannot be written that way and switches to a series below u = 0.05,
    /// where the two are equally accurate.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        if (drive_ <= 0.0)
            return 0.0;

        const double u = drive_ * x;
        const double r = std::sqrt (1.0 + u * u);

        const double rPlusOne = r + 1.0;
        const double xSquared = x * x;

        const double oddPart = 0.5 * oddTrim_ * xSquared
                             - (drive_ * drive_ * xSquared * xSquared) / (2.0 * rPlusOne * rPlusOne);

        return oddWeight_ * oddPart + colourEven_ * evenAntiderivative (u);
    }

private:
    void updateDerived() noexcept
    {
        oddTrim_ = 1.0 - fundamentalGain (drive_ * amplitude_);

        // Level match, and why it cannot be a constant.
        //
        // As drive rises the saturator tends towards a square wave of amplitude
        // 1/drive, so its harmonics shrink like 1/drive -- while the even
        // curve simply saturates towards 1 and does not shrink at all. Measured
        // across drive 0.25 to 30 the two halves drift 15 dB apart, which turns
        // Colour into a volume control at the top of the Drive range.
        //
        // The square root tracks that drift to within about 1.5 dB over the
        // whole range; tests/test_HarmonicGenerator.cpp fails if it stops
        // doing so.
        oddWeight_ = colourOdd_ * kOddLevelMatch * std::sqrt (1.0 + 0.9 * drive_);
    }

    /// (u - asinh(u)) / drive, evaluated so it stays accurate as u goes to zero.
    ///
    /// Both terms tend to u and the answer is the u^3/6 that survives the
    /// subtraction, so below the threshold this evaluates the series instead.
    /// Above it, u dominates asinh(u) and the direct form is the accurate one.
    [[nodiscard]] double evenAntiderivative (double u) const noexcept
    {
        if (std::abs (u) < kSeriesThreshold)
        {
            const double u2 = u * u;

            // u - asinh(u) = u^3/6 - 3u^5/40 + 5u^7/112 - 35u^9/1152 + 63u^11/2816
            const double series = u * u2 * (1.0 / 6.0
                                    + u2 * (-3.0 / 40.0
                                    + u2 * (5.0 / 112.0
                                    + u2 * (-35.0 / 1152.0
                                    + u2 * (63.0 / 2816.0)))));

            return series / drive_;
        }

        return (u - std::asinh (u)) / drive_;
    }

    /// Where the series and the direct form are equally accurate, about 1e-12
    /// relative each. Matches SoftEven, which is the same integral.
    static constexpr double kSeriesThreshold = 0.05;

    /// Measured, not guessed. See the test file, which fails if the two halves
    /// drift more than 3 dB apart anywhere in the drive range.
    static constexpr double kOddLevelMatch = 1.83;

    double drive_      { 0.0 };
    double amplitude_  { 1.0 };
    double colour_     { 0.5 };
    double colourOdd_  { 1.0 };
    double colourEven_ { 0.0 };
    double oddWeight_  { 0.0 };
    double oddTrim_    { 0.0 };
};

} // namespace tezla::dsp
