// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Saturation curves.
//
// Every shaper here provides both the function and its antiderivative, because
// that is what antiderivative antialiasing needs (see Adaa.hpp). That
// requirement is the main constraint on which curves are usable: a shaper whose
// integral has no closed form has to be oversampled brutally instead.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tezla::dsp {

/// log(cosh(x)), evaluated so it does not overflow.
///
/// The naive form computes cosh(x) first, which overflows a double at |x| > 710
/// and loses precision long before that. Drive plus a transient reaches those
/// values easily, and the failure looks like the plugin going silent.
[[nodiscard]] inline double logCosh (double x) noexcept
{
    const double absX = std::abs (x);
    return absX + std::log1p (std::exp (-2.0 * absX)) - std::numbers::ln2_v<double>;
}

/// Biased hyperbolic tangent -- the workhorse curve.
///
///   bias = 0   symmetric, odd harmonics only, compressive knee. Tape-like.
///   bias > 0   asymmetric, brings in even harmonics. Valve-like.
///
/// The output is normalised by the small-signal gain so that changing bias
/// changes the *harmonic content* without changing the level. Without that,
/// a "character" control doubles as a volume control and the user cannot judge
/// what it is doing.
///
/// Asymmetry also produces a DC offset, which grows with drive. A DcBlocker
/// after this stage is not optional.
class BiasedTanh
{
public:
    explicit BiasedTanh (double bias = 0.0) noexcept { setBias (bias); }

    void setBias (double bias) noexcept
    {
        bias_        = bias;
        tanhBias_    = std::tanh (bias);
        logCoshBias_ = logCosh (bias);

        // sech^2(bias) is the slope at the origin; dividing by it restores
        // unity small-signal gain.
        const double slopeAtOrigin = 1.0 - tanhBias_ * tanhBias_;
        normalisation_ = 1.0 / std::max (slopeAtOrigin, 1.0e-6);
    }

    [[nodiscard]] double getBias() const noexcept { return bias_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        return (std::tanh (x + bias_) - tanhBias_) * normalisation_;
    }

    /// First antiderivative, with the constant chosen so F1(0) = 0. The choice
    /// is not arbitrary: ADAA subtracts two nearby values of this function, and
    /// an offset of, say, log(cosh(bias)) would throw away precision in exactly
    /// the small-signal case where it is least affordable.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        return (logCosh (x + bias_) - logCoshBias_ - tanhBias_ * x) * normalisation_;
    }

private:
    double bias_          { 0.0 };
    double tanhBias_      { 0.0 };
    double logCoshBias_   { 0.0 };
    double normalisation_ { 1.0 };
};

/// Sine wavefolder -- the destruction stage.
///
///   f(x) = N(g) * sin(g*x) / g
///
/// The form is chosen so that g = 0 is *exactly* the identity, not merely close
/// to it: sin(g*x)/g tends to x as g tends to zero. That matters because this
/// sits in the signal path at all times, and a folder that colours the sound at
/// its zero setting is a folder you can never turn off.
///
/// Past g = pi/2 the curve turns over and starts folding the signal back on
/// itself. Each further pi of g*x is another fold, so the harmonic content keeps
/// evolving as g rises instead of converging on a square wave the way a clipper
/// does. That is the whole reason a x100 range is musically interesting rather
/// than just loud: at g = 100 a full-scale input is folded about 32 times per
/// half cycle, and the spectrum is nothing like distortion.
///
/// N(g) holds the output level roughly constant once folding starts, so the
/// Range switch changes the *sound* rather than the volume.
class SineFolder
{
public:
    explicit SineFolder (double gain = 0.0) noexcept { setGain (gain); }

    /// 0 is a straight wire. Folding begins around pi/2.
    void setGain (double gain) noexcept
    {
        gain_ = std::max (gain, 0.0);
        // Below pi/2 nothing has folded yet and the curve is its own best
        // normalisation; above it the peak is 1/g, so scale by g*2/pi to hold
        // the level. The two agree exactly at pi/2, so there is no step.
        normalisation_ = std::max (1.0, gain_ * 2.0 / std::numbers::pi);
        negligible_ = gain_ < 1.0e-8;
    }

    [[nodiscard]] double getGain() const noexcept { return gain_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        if (negligible_)
            return x;

        return normalisation_ * std::sin (gain_ * x) / gain_;
    }

    /// Antiderivative, with F1(0) = 0.
    ///
    /// Written as 2*sin^2(g*x/2) rather than the algebraically identical
    /// 1 - cos(g*x). They agree on paper and not in floating point: for small
    /// g*x, 1 - cos(g*x) is a difference of two numbers either side of 1 and
    /// loses almost every significant digit, which ADAA then divides by an
    /// equally small number. The half-angle form has no cancellation at all.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        if (negligible_)
            return 0.5 * x * x;

        const double halfAngle = std::sin (0.5 * gain_ * x);
        return normalisation_ * 2.0 * halfAngle * halfAngle / (gain_ * gain_);
    }

private:
    double gain_          { 0.0 };
    double normalisation_ { 1.0 };
    bool   negligible_    { true };
};

/// Blends the signal toward full-wave rectification.
///
///   f(x) = (1 - a) * x + a * |x|
///
/// At a = 1 every negative half cycle is flipped up, which doubles the
/// fundamental frequency: an octave-up ghost that tracks whatever note is
/// playing, without any pitch tracking. Partway between, the two halves of the
/// waveform become progressively less alike, which is a very direct way of
/// generating even harmonics -- a different route to the same place the valve
/// end of Character reaches by biasing the curve.
///
/// Rectification also produces a large DC offset, by construction: the mean of
/// |x| is not zero. The DC blocker downstream is not optional with this in the
/// path.
class Rectifier
{
public:
    explicit Rectifier (double amount = 0.0) noexcept { setAmount (amount); }

    /// 0 is a straight wire, 1 is full-wave rectification.
    void setAmount (double amount) noexcept { amount_ = std::clamp (amount, 0.0, 1.0); }

    [[nodiscard]] double getAmount() const noexcept { return amount_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        return (1.0 - amount_) * x + amount_ * std::abs (x);
    }

    /// Antiderivative, F1(0) = 0.
    ///
    /// The integral of |x| is x*|x|/2 -- which is x^2/2 for positive x and
    /// -x^2/2 for negative, joining smoothly at the origin. Writing it that way
    /// rather than as a branch keeps it continuous for ADAA, which straddles
    /// the origin constantly on any signal that crosses zero.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        return (1.0 - amount_) * 0.5 * x * x + amount_ * 0.5 * x * std::abs (x);
    }

private:
    double amount_ { 0.0 };
};

/// Bounded even-harmonic generator -- the partner curve to BiasedTanh.
///
///   f(x) = 1 - 1 / sqrt(1 + (g*x)^2)
///
/// The virtual-bass literature reaches for the arc-tangent square root device,
/// whose even term is sqrt(1 - (g*x)^2). That term is imaginary past
/// |g*x| = 1 and its slope goes to infinity as it approaches the edge. A
/// plugin that has to survive a hot drum bus cannot have a domain, so this
/// curve is used instead. Three properties earn it the place:
///
///   * defined, smooth and bounded for every input, however hard it is driven;
///   * slope at the origin is exactly zero, so it contributes no linear term
///     at all -- which is what HarmonicGenerator.hpp is built around;
///   * the antiderivative is elementary, so ADAA applies.
///
/// Musically it is second-harmonic dominant at low gain and behaves like a
/// smoothed full-wave rectifier at high gain, so one control walks from an
/// octave-up shimmer to something with real bite.
///
/// Being even, it produces a large DC offset by construction. A DcBlocker
/// downstream is not optional.
class SoftEven
{
public:
    explicit SoftEven (double gain = 1.0) noexcept { setGain (gain); }

    /// Gain 0 is exactly the zero function, not approximately: no harmonics,
    /// no DC, nothing to fade out.
    void setGain (double gain) noexcept { gain_ = std::max (gain, 0.0); }

    [[nodiscard]] double getGain() const noexcept { return gain_; }

    /// Written as u^2 / (r * (r + 1)) rather than as 1 - 1/r.
    ///
    /// The two are algebraically identical and only one of them is usable: for
    /// quiet input both 1 and 1/r are within u^2/2 of each other, so the direct
    /// form subtracts almost the whole number away and returns what is left of
    /// the rounding. This form never subtracts anything.
    [[nodiscard]] double evaluate (double x) const noexcept
    {
        const double u = gain_ * x;
        const double r = std::sqrt (1.0 + u * u);
        return (u * u) / (r * (r + 1.0));
    }

    /// Antiderivative, F1(0) = 0:  x - asinh(g*x)/g.
    ///
    /// The same trap one level deeper, and this one cannot be written away: as
    /// the gain falls both terms tend to x and the answer is the u^3/6 that
    /// survives the subtraction. Below the threshold this evaluates the series
    /// for u - asinh(u) instead, which has no subtraction in it.
    ///
    /// Getting this wrong would be silent. ADAA divides differences of this
    /// function by differences of the input, so the error arrives as noise on
    /// quiet material rather than as anything that looks like a bug.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        if (gain_ <= 0.0)
            return 0.0;

        const double u = gain_ * x;

        if (std::abs (u) < kSeriesThreshold)
        {
            const double u2 = u * u;

            // u - asinh(u) = u^3/6 - 3u^5/40 + 5u^7/112 - 35u^9/1152 + 63u^11/2816
            const double series = u * u2 * (1.0 / 6.0
                                    + u2 * (-3.0 / 40.0
                                    + u2 * (5.0 / 112.0
                                    + u2 * (-35.0 / 1152.0
                                    + u2 * (63.0 / 2816.0)))));

            return series / gain_;
        }

        return x - std::asinh (u) / gain_;
    }

private:
    /// Where the two forms are equally wrong -- about 1e-12 relative error
    /// each. Below this the subtraction dominates the error, above it the
    /// truncated series does.
    static constexpr double kSeriesThreshold = 0.05;

    double gain_ { 1.0 };
};

/// The odd partner of SoftEven: f(u) = u|u| / (1 + u^2), u = gain * x.
///
/// Odd, so it makes only odd harmonics -- the third first -- and bounded, so a
/// hot transient produces harmonics rather than an explosion. Like SoftEven
/// it has **no linear term**: for quiet input it is u|u|, second order in the
/// level, so used in parallel (`y = x + h * f(x)`) it adds nothing at all to
/// the fundamental of a small signal and everything it adds at a large one is
/// content the input did not have. That is the property a drum's Harmonics
/// control wants -- a knob that thickens a body without turning it up -- and
/// it is what a series soft clip cannot offer, since a series clip with gain
/// 1 is the signal itself.
///
/// Gain 0 is exactly the zero function, so a knob at zero is bit-exactly
/// nothing rather than a quiet version of something.
class SoftOdd
{
public:
    explicit SoftOdd (double gain = 1.0) noexcept { setGain (gain); }

    void setGain (double gain) noexcept { gain_ = std::max (gain, 0.0); }

    [[nodiscard]] double getGain() const noexcept { return gain_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        const double u = gain_ * x;
        return u * std::abs (u) / (1.0 + u * u);
    }

    /// Antiderivative, F1(0) = 0:  (|u| - atan|u|) / g.
    ///
    /// Even, as the antiderivative of an odd function is -- the first draft
    /// carried the curve's sign into it and the integral test caught it on
    /// its first run. The same subtraction trap SoftEven documents applies:
    /// as the gain falls |u| and atan|u| agree to within |u|^3 / 3 and the
    /// direct form returns rounding. Below the threshold the series for
    /// |u| - atan|u| is evaluated instead, which subtracts nothing.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        if (gain_ <= 0.0)
            return 0.0;

        const double a = std::abs (gain_ * x);

        if (a < kSeriesThreshold)
        {
            const double a2 = a * a;

            // |u| - atan|u| = |u|^3/3 - |u|^5/5 + |u|^7/7 - |u|^9/9 + |u|^11/11
            const double series = a * a2 * (1.0 / 3.0
                                    + a2 * (-1.0 / 5.0
                                    + a2 * (1.0 / 7.0
                                    + a2 * (-1.0 / 9.0
                                    + a2 * (1.0 / 11.0)))));

            return series / gain_;
        }

        return (a - std::atan (a)) / gain_;
    }

private:
    /// Where the two forms are equally wrong, as for SoftEven: below this the
    /// subtraction dominates the error, above it the truncated series does.
    static constexpr double kSeriesThreshold = 0.05;

    double gain_ { 1.0 };
};

/// Hard clip, kept for measurement baselines rather than for musical use.
/// Its aliasing figures are the numbers everything else is compared against.
class HardClip
{
public:
    [[nodiscard]] static double evaluate (double x) noexcept
    {
        return x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x);
    }

    /// Piecewise: x^2/2 inside the linear region, |x| - 1/2 outside, which is
    /// what makes the two halves join continuously at |x| = 1.
    [[nodiscard]] static double antiderivative (double x) noexcept
    {
        const double absX = std::abs (x);
        return absX <= 1.0 ? 0.5 * x * x : absX - 0.5;
    }
};

/// Adjustable-knee clipper, and the shape control of a mastering clipper.
///
///   knee = 0    hard clip at +/-1, the corner exactly at the threshold
///   knee = 1    tanh, bending from the origin and asymptotic to +/-1
///
/// Between the two the curve is the identity up to |x| = t (where t = 1 - knee)
/// and a tanh from there on, scaled so the slopes agree at the join:
///
///   f(x) = x                                            |x| <= t
///        = sign(x) * ( t + (1-t) * tanh((|x|-t)/(1-t)) ) |x| >  t
///
/// Three properties earn it the job. It is *exactly* the identity below the
/// knee, so a signal that never reaches the threshold is passed through bit for
/// bit. It reaches +/-1 exactly at knee 0 and asymptotically after that, so the
/// shape control changes the sound and not the level. And it is C1 everywhere,
/// because tanh'(0) = 1 makes the outer slope 1 at the join -- a corner there
/// would be a second clipper, audible as a rasp that no amount of oversampling
/// removes.
class SoftClip
{
public:
    explicit SoftClip (double knee = 0.0) noexcept { setKnee (knee); }

    /// 0 = hard corner, 1 = tanh. Anything between is a knee starting at
    /// 1 - knee.
    void setKnee (double knee) noexcept
    {
        knee_  = std::clamp (knee, 0.0, 1.0);
        t_     = 1.0 - knee_;
        width_ = knee_;                       // 1 - t, the tanh's scale
        hard_  = width_ < 1.0e-9;
    }

    [[nodiscard]] double getKnee() const noexcept { return knee_; }

    /// Where the curve stops being the identity, in linear units.
    [[nodiscard]] double getThreshold() const noexcept { return t_; }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        const double absX = std::abs (x);

        if (absX <= t_)
            return x;

        if (hard_)
            return x < 0.0 ? -1.0 : 1.0;

        const double outer = t_ + width_ * std::tanh ((absX - t_) / width_);
        return x < 0.0 ? -outer : outer;
    }

    /// First antiderivative, with F(0) = 0. Even, because f is odd.
    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        const double absX = std::abs (x);

        if (absX <= t_)
            return 0.5 * x * x;

        if (hard_)
            return absX - 0.5;

        const double u = (absX - t_) / width_;

        // The constant is fixed by matching t*t + 0 + C to t*t/2 at the join.
        return t_ * absX + width_ * width_ * logCosh (u) - 0.5 * t_ * t_;
    }

    // ---- the part worth antialiasing ----------------------------------------
    //
    // f(x) = x + g(x). The x is linear and generates no harmonics; every
    // harmonic the clipper makes comes from g, and g is *exactly zero* below
    // the knee.
    //
    // That distinction is not cosmetic. ADAA replaces f(x[n]) by the average of
    // f over the segment from x[n-1] to x[n], and for a linear f that average
    // is the midpoint -- a half-sample averager, which is a lowpass. Run the
    // whole curve through ADAA and a clean signal well under the threshold
    // comes out 0.47 dB down at 20 kHz at x4 from 48 kHz, for nothing.
    // Band-limiting g instead and adding x back leaves the clean signal
    // untouched to the bit, and band-limits precisely the part that needed it.
    //
    // What it costs: the linear and nonlinear parts end up half an oversampled
    // sample apart. That is a phase error on the harmonics alone, at 192 kHz or
    // above, against a measurable dip across the whole top octave. Not a close
    // trade.

    [[nodiscard]] double excess (double x) const noexcept
    {
        return std::abs (x) <= t_ ? 0.0 : evaluate (x) - x;
    }

    [[nodiscard]] double excessAntiderivative (double x) const noexcept
    {
        return std::abs (x) <= t_ ? 0.0 : antiderivative (x) - 0.5 * x * x;
    }

private:
    double knee_  { 0.0 };
    double t_     { 1.0 };
    double width_ { 0.0 };
    bool   hard_  { true };
};

/// Adaa1 adaptor: band-limits the clipper's excess rather than the whole curve.
///
/// Exists so `Adaa1<SoftClipExcess>` reads as what it is. Below the knee both
/// the value and the antiderivative are exactly zero, so ADAA returns exactly
/// zero -- including through its midpoint fallback -- and the caller's `x` is
/// returned unchanged.
struct SoftClipExcess
{
    SoftClip clip;

    void setKnee (double knee) noexcept { clip.setKnee (knee); }

    [[nodiscard]] double evaluate (double x) const noexcept { return clip.excess (x); }

    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        return clip.excessAntiderivative (x);
    }
};

} // namespace tezla::dsp
