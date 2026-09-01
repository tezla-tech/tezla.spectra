// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A state-variable filter in topology-preserving transform form -- the thing
// CLAUDE.md section 6 has been pointing at all along.
//
// ---------------------------------------------------------------------------
// Why not a biquad
// ---------------------------------------------------------------------------
//
// The repository already measured this and pinned it in tests/test_Biquad.cpp:
// a bilinear-transformed biquad's corner *warps* as it approaches Nyquist. A
// 4 kHz lowpass at Q 0.707 reads -29.9 dB at 15 kHz at 48 kHz against -23.3 dB
// at 192 k, and the 192 k curve is the one tracking the analogue prototype.
// The rule that came out of it was: trust a plain biquad only below Fs/8.
//
// A synth filter is swept, resonant, and expected to sound the same on every
// session. Fs/8 at 48 kHz is 6 kHz, which is nowhere near enough. So this is a
// TPT/ZDF structure instead, from Zavalishin's *The Art of VA Filter Design*:
// the cutoff is prewarped once with a tangent, and from there the discrete
// filter's corner sits exactly where it was asked to, at every rate.
//
// It also solves the zero-delay feedback properly rather than inserting a
// one-sample delay in the loop. That matters most exactly where a synth filter
// earns its keep -- at high resonance, where a delayed feedback path detunes
// the resonant peak and changes how the filter self-oscillates.
//
// ---------------------------------------------------------------------------
// The rail is inside the loop, and that is the sound
// ---------------------------------------------------------------------------
//
// A saturator after a filter is a distorted filter. A saturator *inside the
// resonance path* is a different instrument: as the resonance grows, the
// integrators drive themselves into the nonlinearity, their effective gain
// falls, and the resonance limits itself. That is what stops a real filter
// exploding, and it is why an overdriven analogue filter squashes and growls
// instead of simply clipping.
//
// **The right model is an op-amp rail, not a drive-dependent threshold**, and
// that took measuring to establish. The first version put a `tanh(x*A)/A` on
// the bandpass state, so the ceiling *fell* as the drive rose. It was wrong in
// three ways at once, all of them visible in one table -- a 1 kHz lowpass at
// 96 kHz fed a 0.5 sine:
//
//     res   drive |  gain at fc     THD  |  gain at fc/2
//     0.00   0.00 |     -6.02   -324.8   |    -1.94        <- clean
//     0.00   0.25 |    -12.78    -28.1   |    -6.94        <- 5 dB gone
//     0.60   0.00 |     29.98   -360.8   |
//     0.60   0.25 |    -11.00    -25.1   |
//     1.00   0.25 |    -10.97    -25.0   |
//
//   1. A quarter turn cost 5 dB of *passband* with the resonance at zero. The
//      drive was a volume control.
//   2. From a quarter turn on, every resonance setting read the same. The
//      resonance control went inert the moment any drive was dialled in.
//   3. The whole range happened in that first quarter: THD went from -325 dB
//      to -28 dB and then moved 4 dB over the remaining three quarters.
//
// So the ceiling is **fixed**, at the place a real integrator's ceiling is: its
// supply rail. Exactly linear to +/-1, saturating to +/-2 above that. Drive
// becomes what it is in a real circuit -- how hard you push the loop into that
// rail -- and is a pre-gain with a matching post-trim, geometric so the travel
// is even.
//
// What that buys, same measurement:
//
//     res   drive |  gain at fc     THD
//     0.00   0.00 |     -6.02   -324.8    clean, and stays clean
//     0.00   0.50 |     -6.02   -324.8    drive does nothing until pushed
//     0.60   0.00 |      9.26    -31.4    Q = 31.6 self-limits to +9 dB
//     0.60   0.25 |      5.18    -28.8
//     0.60   0.50 |      1.10    -26.7    even 4 dB steps
//     0.60   1.00 |     -7.06    -22.9
//
// The rail is **always on**, which is the point: it is the bound CLAUDE.md
// section 7 asks for around a nonlinear feedback loop, and a bound with an off
// switch is not one. It costs nothing in transparency because the shaper has a
// genuine linear region rather than a soft one -- below full scale it is the
// identity bit for bit, not nearly the identity. That is a stronger claim than
// a bypass branch could make, and it is why there is no bypass branch.
//
// The cost, stated plainly: above about Q = 30 the steady-state peak stops
// growing, because it is against the rail. The top half of the resonance
// control changes how long the filter *rings* -- 0.07 s at Q = 31.6 against
// 1.10 s at Q = 500 -- rather than how loud it gets. That is exactly what a
// real filter does at the rail, and it is the reason the resonance control is
// worth having up there at all.

#include <algorithm>
#include <cmath>

#include "Exact.hpp"

namespace tezla::dsp {

/// Which output of the state-variable structure to take.
///
/// **Append-only.** A choice parameter stores an index -- CLAUDE.md section 8.
enum class SvfMode
{
    lowpass = 0,
    bandpass,
    highpass,
    notch,

    count
};

class SvfFilter
{
public:
    /// The two ends of the resonance control, as Q.
    ///
    /// 0.5 is critically damped -- no peak at all, and k = 2 exactly, which is
    /// the bit-exact neutral the control needs at zero.
    ///
    /// 500 is where the filter sings. It is deliberately not *literal* self
    /// oscillation -- k stays strictly positive -- and with the rail in place
    /// that is a choice rather than a necessity: at k <= 0 the loop would grow
    /// until it met the rail and then sit there as a stable limit cycle, which
    /// is exactly how an analogue filter self-oscillates and is perfectly
    /// bounded. The reason not to is that it never stops. In a polyphonic
    /// instrument a voice whose filter keeps sounding after note-off is a
    /// stuck note, and the fix would be a gate somewhere -- which is a worse
    /// thing to own than a filter that decays on its own.
    ///
    /// Q = 500 is close enough to be indistinguishable in use: pinged at 1 kHz
    /// it takes **1.10 s** to fall 60 dB, measured, against the 1.098 s that
    /// 6.9 * Q / (pi * f0) predicts. That is a sustained tone, not a ping.
    static constexpr double kMinimumQ = 0.5;
    static constexpr double kMaximumQ = 500.0;

    /// The integrator rail: exactly linear to `kRailKnee`, saturating to
    /// `kRailCeiling`. An op-amp's supply, in other words, and the only bound
    /// this filter has -- there is no clamp after it, deliberately, because
    /// CLAUDE.md section 10 warns that a guard at the end of a chain makes
    /// every measurement of the guarded quantity true.
    ///
    /// The knee sits at full scale so that ordinary material never reaches it
    /// and resonant buildup always does. Below it the shaper is the identity
    /// *bit for bit*, which is what makes a clean setting genuinely clean
    /// rather than nearly clean.
    static constexpr double kRailKnee = 1.0;
    static constexpr double kRailCeiling = 2.0;

    /// How hard the drive control pushes the loop into that rail, at the top of
    /// its travel. Geometric: `gain = kDriveRange ^ drive`, so 8 means 18 dB
    /// spread evenly across the control instead of piled into its first
    /// quarter. Measured, 1 kHz lowpass at 96 kHz, resonance 0.6, sine at 0.5:
    ///
    ///     drive   gain-in     peak at fc      THD
    ///      0.00      1.00       +9.26 dB   -31.4 dB
    ///      0.25      1.68       +5.18 dB   -28.8 dB
    ///      0.50      2.83       +1.10 dB   -26.7 dB
    ///      0.75      4.76       -2.97 dB   -24.8 dB
    ///      1.00      8.00       -7.06 dB   -22.9 dB
    ///
    /// Level-dependent by construction, which is the honest part: with the
    /// resonance at zero and a 0.25 input, *every* drive setting reads -6.02 dB
    /// and -318 dB THD, because nothing ever reaches the rail. Play harder and
    /// it distorts; that is a circuit, not a curve.
    static constexpr double kDriveRange = 8.0;

    /// Cutoff is clamped to this fraction of Nyquist.
    ///
    /// The prewarp is a tangent, and tan() goes to infinity at Nyquist. A
    /// cutoff asked for above it is not a filter at all, and the coefficient it
    /// produces is not a large number, it is an infinite one.
    static constexpr double kMaximumCutoffFraction = 0.49;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        s1_ = 0.0;
        s2_ = 0.0;
    }

    void setMode (SvfMode mode) noexcept { mode_ = mode; }

    /// A **bipolar offset along the lowpass -> bandpass -> highpass axis**,
    /// centred on whatever `setMode` chose. -1 .. +1, and 0 is the mode itself.
    ///
    /// The point of it is that the mode is a *choice* and a choice cannot be
    /// modulated (CLAUDE.md section 8: destinations hold continuous controls
    /// only), so the filter's character was the one thing in a voice that no
    /// envelope could sweep. This is continuous, so it can be.
    ///
    /// **Why an offset rather than an absolute position.** A saved project has
    /// a mode and no morph, so morph's default has to mean "the mode I chose"
    /// -- an absolute 0..1 control would mean "lowpass" and would silently
    /// convert every bandpass patch ever saved. Centring on the mode makes the
    /// default bit-exact for all four modes and still lets one control sweep
    /// the whole axis from a lowpass patch, which is the common case.
    ///
    /// **Notch is not on this axis**, so morph is inert there. It is not
    /// between the other three -- it is the sum of the two ends -- and putting
    /// it on a slider between them would be inventing a shape nothing makes.
    void setMorph (double morph) noexcept
    {
        morph_ = std::clamp (morph, -1.0, 1.0);
    }

    [[nodiscard]] double getMorph() const noexcept { return morph_; }
    [[nodiscard]] SvfMode getMode() const noexcept { return mode_; }

    void setCutoffHz (double hz) noexcept
    {
        const double limit = sampleRate_ * kMaximumCutoffFraction;
        const double wanted = std::clamp (hz, 1.0, limit);

        if (isExactly (wanted, cutoffHz_))
            return;

        cutoffHz_ = wanted;
        updateCutoffCoefficient();
    }

    [[nodiscard]] double getCutoffHz() const noexcept { return cutoffHz_; }

    /// 0 is no resonance at all; 1 is the edge of self-oscillation.
    void setResonance (double resonance) noexcept
    {
        const double wanted = std::clamp (resonance, 0.0, 1.0);

        if (isExactly (wanted, resonance_))
            return;

        resonance_ = wanted;
        updateCoefficients();
    }

    [[nodiscard]] double getResonance() const noexcept { return resonance_; }

    /// The Q the control currently maps to. `k = 1/Q`, and at the corner the
    /// lowpass and highpass both read exactly Q -- which is what the cutoff
    /// accuracy test measures against.
    [[nodiscard]] double getQ() const noexcept { return 1.0 / k_; }

    /// How hard to push the loop into its rail. 0 is neutral and **bit-exact**
    /// neutral: `pow(kDriveRange, 0.0)` is exactly 1.0, and multiplying by 1.0
    /// is exact in IEEE arithmetic, so the pre-gain and post-trim both vanish
    /// without a branch to skip them.
    ///
    /// It is a continuous control with no special case at zero, which is the
    /// reason the rail is fixed rather than drive-dependent: a nonlinearity
    /// that switched on at the first increment would step the resonant peak by
    /// 48 dB between drive 0.000 and drive 0.001, and this is a modulation
    /// destination.
    void setDrive (double drive) noexcept
    {
        drive_ = std::clamp (drive, 0.0, 1.0);
        driveGain_ = std::pow (kDriveRange, drive_);
        driveTrim_ = 1.0 / driveGain_;
    }

    [[nodiscard]] double getDrive() const noexcept { return drive_; }

    /// One sample, with the cutoff optionally displaced for this sample only.
    ///
    /// `cutoffScale` multiplies the cutoff, which is what audio-rate filter FM
    /// wants: a modulator at an octave above the note swinging the corner by a
    /// factor of two either way. It is applied per sample and costs a tangent,
    /// so callers that are not doing FM should pass 1.0 and pay nothing.
    [[nodiscard]] double process (double input, double cutoffScale = 1.0) noexcept
    {
        const double g = isExactly (cutoffScale, 1.0) ? g_ : prewarp (cutoffHz_ * cutoffScale);

        // The TPT state-variable, solved for the bandpass output directly so
        // the feedback has no unit delay in it.
        //
        //     hp = (in - (2R + g) * s1 - s2) / (1 + 2R*g + g^2)
        //
        // written with the division folded into a single reciprocal.
        const double denominator = 1.0 / (1.0 + g * (g + k_));

        const double driven = input * driveGain_;

        const double highpass = (driven - s1_ * (g + k_) - s2_) * denominator;

        const double bandpass = highpass * g + s1_;
        s1_ = bandpass + highpass * g;

        const double lowpass = bandpass * g + s2_;
        s2_ = lowpass + bandpass * g;

        // The rail, on both integrator states, because both are op-amp outputs
        // and both are what a real one runs out of headroom on. Applying it to
        // the state rather than the output is the whole distinction: a shaper
        // on the output is a distorted filter, a shaper on the state is a
        // filter that cannot resonate past its supply.
        //
        // The damping term is deliberately *not* shaped. In a ladder the
        // resonance is positive feedback and saturating it reduces the
        // resonance; in a state-variable the resonance is **reduced damping**,
        // so saturating `k * bp` would reduce the damping further as the level
        // rose and the loop would run away. Same intuition, opposite sign, and
        // it is worth the sentence because the ladder version is the one
        // everybody has read about.
        s1_ = railed (s1_);
        s2_ = railed (s2_);

        // Morph off, or a mode with no place on the morph axis: the original
        // switch, untouched.
        //
        // **Not what makes morph 0 bit-exact**, which is worth stating because
        // the first version of this comment claimed it was and the break-check
        // disproved it: removing the `isExactlyZero` half leaves every
        // bit-exactness test green, because the blend really is exact at its
        // three landmarks -- `a * 1 + b * 0` and `a * 0 + b * 1` both are, in
        // IEEE arithmetic. So this is a **fast path** (a clamp, an add and four
        // multiplies a sample saved in the overwhelmingly common case) plus the
        // route notch has to take, and the exactness is the blend's own.
        if (isExactlyZero (morph_) || mode_ == SvfMode::notch)
        {
            switch (mode_)
            {
                case SvfMode::lowpass:  return lowpass * driveTrim_;
                case SvfMode::bandpass: return bandpass * driveTrim_;
                case SvfMode::highpass: return highpass * driveTrim_;
                case SvfMode::notch:    return (highpass + lowpass) * driveTrim_;
                case SvfMode::count:
                default:                return lowpass * driveTrim_;
            }
        }

        return blend (lowpass, bandpass, highpass) * driveTrim_;
    }

    /// The filter's own magnitude at one frequency. For tests and for drawing
    /// the curve on a panel.
    ///
    /// This is the *discrete* response, which is the analogue prototype
    /// evaluated on the warped frequency axis -- `tan(pi f / fs) / g` rather
    /// than `f / fc`. The distinction is the whole subject:
    ///
    /// **What a TPT structure guarantees is the corner, not the whole curve.**
    /// At `f = fc` the warp cancels exactly and the corner sits where it was
    /// asked for at every sample rate, which is precisely what a bilinear
    /// biquad fails to do. Far above the corner the response still warps,
    /// because it must -- a discrete response is symmetric about Nyquist, and
    /// Nyquist is a different frequency at every rate. No structure fixes that;
    /// CLAUDE.md section 6 says to put anything whose high-frequency shape
    /// matters inside an oversampled section, and that is why.
    [[nodiscard]] double magnitudeAt (double frequency) const noexcept
    {
        const double w = prewarp (frequency) / g_;
        const double w2 = w * w;

        // |H| for the second-order sections, with k = 1/Q. From
        //
        //     hp = s^2 / (s^2 + k s + 1)
        //     bp = s   / (s^2 + k s + 1)
        //     lp = 1   / (s^2 + k s + 1)
        //
        // so all three read **Q** at the corner, the bandpass included. Its
        // numerator is `w`, not `w * k`: the `w * k` form is the unity-gain
        // bandpass, a different normalisation, and this returns the structure's
        // raw `bp` node. Writing it the other way made this function disagree
        // with the filter it describes by exactly 20*log10(Q) -- which nothing
        // caught until the corner test started checking every mode instead of
        // only the lowpass.
        const double denominator = std::sqrt ((1.0 - w2) * (1.0 - w2) + w2 * k_ * k_);

        if (isExactlyZero (morph_) || mode_ == SvfMode::notch)
        {
            switch (mode_)
            {
                case SvfMode::lowpass:  return 1.0 / denominator;
                case SvfMode::bandpass: return w / denominator;
                case SvfMode::highpass: return w2 / denominator;
                case SvfMode::notch:    return std::abs (1.0 - w2) / denominator;
                case SvfMode::count:
                default:                return 1.0 / denominator;
            }
        }

        // The morphed response, worked out rather than approximated. The blend
        // is a sum of the three outputs, so its transfer function is the same
        // denominator over a numerator built from the same weights:
        //
        //     below the middle:  (1 - t) + t s
        //     above it:          (1 - t) s + t s^2
        //
        // and at s = jw those have magnitudes sqrt((1-t)^2 + (t w)^2) and
        // sqrt(((1-t) w)^2 + (t w^2)^2). Averaging the three *magnitudes*
        // instead would be wrong by however much they are out of phase, which
        // at the corner is a quarter turn -- the bandpass leads both others by
        // 90 degrees, and that is the whole reason a morph sounds like a sweep
        // rather than like a crossfade between two static filters.
        const double position = std::clamp (positionOf (mode_) + morph_, 0.0, 1.0);

        if (position <= 0.5)
        {
            const double t = position * 2.0;
            const double real = 1.0 - t;
            const double imaginary = t * w;

            return std::sqrt (real * real + imaginary * imaginary) / denominator;
        }

        const double t = position * 2.0 - 1.0;
        const double real = -t * w2;
        const double imaginary = (1.0 - t) * w;

        return std::sqrt (real * real + imaginary * imaginary) / denominator;
    }

private:
    /// Where a mode sits on the lowpass -> bandpass -> highpass axis. Notch is
    /// not on it and never reaches here.
    [[nodiscard]] static constexpr double positionOf (SvfMode mode) noexcept
    {
        switch (mode)
        {
            case SvfMode::bandpass: return 0.5;
            case SvfMode::highpass: return 1.0;
            case SvfMode::lowpass:
            case SvfMode::notch:
            case SvfMode::count:
            default:                return 0.0;
        }
    }

    /// The three outputs crossfaded at the morphed position.
    ///
    /// `a * (1 - t) + b * t` rather than `a + t * (b - a)`, because this form
    /// is exact at **both** ends -- t = 0 gives `a * 1 + b * 0` and t = 1 gives
    /// `a * 0 + b * 1` -- where the other is exact only at t = 0. The three
    /// landmark positions 0, 0.5 and 1 therefore hand back the plain lowpass,
    /// bandpass and highpass with no arithmetic between, which is what makes
    /// "morph 0.5 is the bandpass" a fact rather than an approximation.
    [[nodiscard]] double blend (double lowpass, double bandpass, double highpass) const noexcept
    {
        const double position = std::clamp (positionOf (mode_) + morph_, 0.0, 1.0);

        if (position <= 0.5)
        {
            const double t = position * 2.0;
            return lowpass * (1.0 - t) + bandpass * t;
        }

        const double t = position * 2.0 - 1.0;
        return bandpass * (1.0 - t) + highpass * t;
    }

    /// The integrator's supply rail.
    ///
    ///   |x| <= knee              the identity, bit for bit
    ///   |x| >  knee              knee + headroom * tanh((|x| - knee)/headroom)
    ///
    /// C1 continuous: the slope is exactly 1 on both sides of the knee, so
    /// there is no corner for a sweep to click on. The ceiling is approached
    /// asymptotically, so nothing can push a state past it however hard it is
    /// driven -- which is what makes this a bound rather than a taper.
    [[nodiscard]] static double railed (double x) noexcept
    {
        const double magnitude = std::abs (x);

        if (magnitude <= kRailKnee)
            return x;

        constexpr double headroom = kRailCeiling - kRailKnee;

        return std::copysign (kRailKnee + headroom * std::tanh ((magnitude - kRailKnee) / headroom), x);
    }

    /// The bilinear prewarp: tan(pi * f / fs).
    ///
    /// This one line is the difference between a filter whose corner is where
    /// it was asked for and one whose corner drifts as the sample rate changes.
    [[nodiscard]] double prewarp (double hz) const noexcept
    {
        const double limit = sampleRate_ * kMaximumCutoffFraction;
        const double clamped = std::clamp (hz, 1.0, limit);

        return std::tan (3.141592653589793 * clamped / sampleRate_);
    }

    /// The cutoff half of updateCoefficients() on its own. g depends on the
    /// cutoff and the sample rate; k depends on the resonance and nothing else.
    /// A cutoff change used to recompute both, which put a `pow` next to the
    /// `tan` on every sample of a modulated sweep -- Sonitus moves its cutoff
    /// through a per-sample smoother, so that was two transcendentals per
    /// filter per sample for a value that had not changed. Same expression,
    /// same inputs, same bits: this alters nothing but the work done.
    void updateCutoffCoefficient() noexcept
    {
        g_ = prewarp (cutoffHz_);
    }

    void updateCoefficients() noexcept
    {
        g_ = prewarp (cutoffHz_);

        // Q is **geometric** in the control, so the peak height is linear in
        // decibels: 15 dB per quarter turn, from -6 dB at rest to +54 dB at the
        // top. CLAUDE.md section 8 asks for ranges skewed so the useful part
        // sits mid-travel, and this is what that means here -- half travel is
        // Q = 15.8, which is squarely the squelchy, vocal region a synth filter
        // is bought for.
        //
        // The obvious mapping, k linear in the control, does the opposite and
        // measurably so: it puts Q = 1.0 at half travel and hides 21 dB of the
        // range in the last 1% of it. Measured, k-linear against Q-geometric:
        //
        //     control   k-linear     Q-geometric
        //       0.00     -6.02 dB      -6.02 dB
        //       0.25     -4.44 dB       9.02 dB
        //       0.50      1.93 dB      23.98 dB
        //       0.75     13.90 dB      38.99 dB
        //       1.00     53.98 dB      53.98 dB
        //
        // pow(x, 0.0) is exactly 1.0, so a control at zero still gives k = 2
        // exactly -- the neutral stays bit-exact rather than nearly so.
        k_ = 1.0 / (kMinimumQ * std::pow (kMaximumQ / kMinimumQ, resonance_));
    }

    double sampleRate_ { 48000.0 };
    double cutoffHz_   { 1000.0 };
    double resonance_  { 0.0 };
    double drive_      { 0.0 };
    double driveGain_  { 1.0 };
    double driveTrim_  { 1.0 };

    SvfMode mode_ { SvfMode::lowpass };
    double morph_ { 0.0 };

    double g_ { 0.0 };
    double k_ { 2.0 };

    double s1_ { 0.0 };
    double s2_ { 0.0 };
};

} // namespace tezla::dsp
