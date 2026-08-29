// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Magnetic tape hysteresis: the Jiles-Atherton model, solved in real time.
//
// ---------------------------------------------------------------------------
// The physics, and where every piece comes from
// ---------------------------------------------------------------------------
//
// Magnetisation M chases the field H around a loop it can never quite close:
// the walk up and the walk down take different paths, the difference is the
// energy the tape eats, and the shape of that loop IS the tape sound. The
// model is Jiles-Atherton in the dM/dt form of Chowdhury's DAFx-19 tape
// paper (read first-hand; its LaTeX ships in the GPLv3 AnalogTapeModel
// repository -- see `technical references/ferrite/` and
// docs/DSP-REFERENCES.md):
//
//     Q      = (H + alpha*M) / a          effective field, normalised
//     M_an   = Ms * L(Q)                  the anhysteretic curve
//     L(x)   = coth(x) - 1/x              Langevin; x/3 within |x| <= 1e-3
//     dM/dt  = Hdot * (f1 + f2) / f3
//       f1   = deltaM*(1-c)*(M_an - M) / [(1-c)*delta*k - alpha*(M_an - M)]
//       f2   = c*(Ms/a)*L'(Q)
//       f3   = 1 - c*alpha*(Ms/a)*L'(Q)
//
// delta is the sign of Hdot; deltaM gates f1 to zero whenever delta and
// (M_an - M) disagree, which is what forbids unphysical negative
// susceptibility. The ODE is integrated per sample with fourth-order
// Runge-Kutta, the paper's own choice -- the implicit alternative was tried
// first and measured diverging; the story is told at the solve site.
//
// Three production decisions are taken from the shipped CHOW plugin (GPL-3.0,
// attributed here and in docs/DSP-REFERENCES.md) because they are fitted
// knowledge measurement cannot re-derive, only confirm:
//
//   * NORMALISED units: Ms is O(1), k = 0.47875, alpha = 1.6e-3, and the
//     input sample is the field H directly. The physical A/m constants from
//     the paper describe the same loop scaled by ~3.5e5.
//   * The MUSICAL MAPPING: drive shrinks `a` (steeper anhysteretic curve),
//     saturation lowers Ms (earlier ceiling), bias raises `c` (a properly
//     biased machine records more reversibly, i.e. cleaner) -- structure
//     from the reference, ranges re-tuned here and pinned by measurement.
//   * The LEAKY derivative estimate (dAlpha = 0.75), taming the derivative
//     recursion the paper warns about near Nyquist. The stability sweep in
//     tests/test_Hysteresis.cpp is what licenses it.
//
// Guards are clamps, never mid-stream resets: H is bounded at +-10, a
// blown solve is clamped to +-1.5*Ms, and only a non-finite M (never seen
// in the sweep; kept because "never" is not a guarantee) resets state.
// Every engagement latches a sticky flag the tests assert stays CLEAR over
// the whole sane operating range -- a guard that fires silently would make
// every measurement of the guarded quantity a lie (CLAUDE.md section 10).

#include <cmath>

namespace tezla::ferrite {

class Hysteresis
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        T_ = 1.0 / sampleRate;
        derivativeScale_ = (1.0 + kDerivativeAlpha) / T_;
        reset();
    }

    void reset() noexcept
    {
        M_ = 0.0;
        lastH_ = 0.0;
        lastHdot_ = 0.0;
        clamped_ = false;
    }

    /// The musical controls, all 0..1. Cheap, allocation-free, and clears no
    /// state -- a parameter move lands on the running loop. No-op guarded.
    void setParameters (double drive, double saturation, double bias) noexcept
    {
        if (drive == drive_ && saturation == saturation_ && bias == bias_)
            return;

        drive_ = drive;
        saturation_ = saturation;
        bias_ = bias;

        // Saturation lowers the ceiling; drive steepens the approach to it;
        // bias makes the recording more reversible (cleaner), less biased
        // machines smear more of the input into the loop.
        Ms_ = 0.5 + 1.5 * (1.0 - saturation);
        a_ = Ms_ / (0.01 + 6.0 * drive);
        c_ = 0.005 + 0.975 * std::sqrt (bias);

        oneMinusC_ = 1.0 - c_;
        MsOverA_ = Ms_ / a_;
        upperLimit_ = 1.5 * Ms_;
    }

    [[nodiscard]] double getDrive() const noexcept { return drive_; }
    [[nodiscard]] double getSaturation() const noexcept { return saturation_; }
    [[nodiscard]] double getBias() const noexcept { return bias_; }
    [[nodiscard]] double saturationCeiling() const noexcept { return Ms_; }

    /// One sample of field in, one sample of magnetisation out.
    [[nodiscard]] double process (double H) noexcept
    {
        // The field is bounded before anything differentiates it: the
        // derivative recursion amplifies steps, and a hostile host can send
        // anything. +-10 is far beyond tape-sane drive.
        if (H > kFieldLimit)      { H = kFieldLimit; clamped_ = true; }
        else if (H < -kFieldLimit) { H = -kFieldLimit; clamped_ = true; }

        // Leaky trapezoidal derivative estimate (dAlpha = 0.75, reference's
        // value): the exact recursion (dAlpha = 1) rings at Nyquist.
        const double Hdot = derivativeScale_ * (H - lastH_)
                              - kDerivativeAlpha * lastHdot_;

        // Fourth-order Runge-Kutta, the paper's own choice -- and a choice
        // this file arrived at the hard way, so the reasoning is recorded:
        // the trapezoidal Newton-Raphson alternative was implemented first
        // and DIVERGED at hard drive with 20 kHz content (the Newton
        // denominator 1 - step*f' crosses zero when f' ~ Hdot ~ 1e6; |M|
        // reached 28158 in the unguarded sweep), and damping it only turned
        // the divergence into a step-limited thrash. RK4 is bounded in the
        // same corner, but still overshoots: with hard drive the loop's
        // slope dM/dH reaches ~2.5 while a 20 kHz full-amplitude field
        // moves ~1.3 per sample, and one step per sample overshot to 2.7x
        // the saturation ceiling (measured, rail lifted). So the step
        // adapts: a sample whose field moves more than kMaxFieldStep is
        // integrated in up to kMaxSubSteps RK4 sub-steps over linearly
        // interpolated H and Hdot. One sub-step is bit-identical to plain
        // RK4, so ordinary audio never pays; the accuracy tests integrate
        // the same trajectory 8x finer still and pin the difference.
        const double fieldStep = H - lastH_;
        const double magnitude = fieldStep >= 0.0 ? fieldStep : -fieldStep;
        const int subSteps = magnitude <= kMaxFieldStep
                               ? 1
                               : (static_cast<int> (magnitude / kMaxFieldStep) + 1
                                    < kMaxSubSteps
                                      ? static_cast<int> (magnitude / kMaxFieldStep) + 1
                                      : kMaxSubSteps);

        const double h = T_ / subSteps;
        double M = M_;

        for (int sub = 0; sub < subSteps; ++sub)
        {
            const double t0 = static_cast<double> (sub) / subSteps;
            const double t1 = static_cast<double> (sub + 1) / subSteps;
            const double tMid = 0.5 * (t0 + t1);

            const double H0 = lastH_ + fieldStep * t0;
            const double H1 = lastH_ + fieldStep * t1;
            const double Hm = lastH_ + fieldStep * tMid;

            const double Hd0 = lastHdot_ + (Hdot - lastHdot_) * t0;
            const double Hd1 = lastHdot_ + (Hdot - lastHdot_) * t1;
            const double Hdm = lastHdot_ + (Hdot - lastHdot_) * tMid;

            const double k1 = h * hysteresisRate (M, H0, Hd0);
            const double k2 = h * hysteresisRate (M + 0.5 * k1, Hm, Hdm);
            const double k3 = h * hysteresisRate (M + 0.5 * k2, Hm, Hdm);
            const double k4 = h * hysteresisRate (M + k3, H1, Hd1);

            M += k1 * (1.0 / 6.0) + k2 * (1.0 / 3.0)
               + k3 * (1.0 / 3.0) + k4 * (1.0 / 6.0);
        }

        // Clamp, never a mid-stream zero: an overshoot past what the
        // anhysteretic curve can reach is numerics, and snapping it to the
        // rail is inaudible where a reset to zero is a click.
        if (M > upperLimit_)       { M = upperLimit_; clamped_ = true; }
        else if (M < -upperLimit_) { M = -upperLimit_; clamped_ = true; }

        if (! std::isfinite (M))
        {
            reset();
            clamped_ = true;
            return 0.0;
        }

        M_ = M;
        lastH_ = H;
        lastHdot_ = Hdot;

        return M;
    }

    /// True if any guard has engaged since the last clearClamped(). The
    /// stability sweep asserts this stays FALSE across the whole sane range:
    /// a guard that engages routinely would be shaping the sound.
    [[nodiscard]] bool hasClamped() const noexcept { return clamped_; }
    void clearClamped() noexcept { clamped_ = false; }

    [[nodiscard]] double magnetization() const noexcept { return M_; }

private:
    static constexpr double kAlpha = 1.6e-3;      // inter-domain coupling
    static constexpr double kK = 0.47875;         // loop width (normalised)
    static constexpr double kDerivativeAlpha = 0.75;
    static constexpr double kFieldLimit = 10.0;

    // Sub-stepping engages above kMaxFieldStep of field motion per sample:
    // with the steepest musical mapping the loop's slope is ~2.5, so a
    // quarter-unit step bounds each sub-step's excursion well inside Ms.
    static constexpr double kMaxFieldStep = 0.25;
    static constexpr int kMaxSubSteps = 8;

    // Langevin function and derivatives, with the small-argument guards from
    // the paper (threshold widened to 1e-3 as in the reference: coth(x)-1/x
    // cancels catastrophically as x -> 0, and x/3 is exact to ~1e-7 there).
    struct Langevin
    {
        double value, prime, second;
    };

    [[nodiscard]] static Langevin langevin (double x) noexcept
    {
        if (x > 1.0e-3 || x < -1.0e-3)
        {
            const double coth = 1.0 / std::tanh (x);
            const double oneOverX = 1.0 / x;
            const double cothSq = coth * coth;

            return { coth - oneOverX,
                     oneOverX * oneOverX - cothSq + 1.0,
                     2.0 * coth * (cothSq - 1.0)
                       - 2.0 * oneOverX * oneOverX * oneOverX };
        }

        return { x * (1.0 / 3.0), 1.0 / 3.0, x * (-2.0 / 15.0) };
    }

    /// dM/dt of the Jiles-Atherton model at one point.
    [[nodiscard]] double hysteresisRate (double M, double H, double Hdot) const noexcept
    {
        const double Q = (H + kAlpha * M) / a_;
        const auto L = langevin (Q);

        const double MDiff = Ms_ * L.value - M;

        const double delta = Hdot >= 0.0 ? 1.0 : -1.0;
        const bool sameSign = (delta > 0.0) == (MDiff > 0.0) && MDiff != 0.0;

        const double f1Denominator = oneMinusC_ * delta * kK - kAlpha * MDiff;
        const double f1 = sameSign ? oneMinusC_ * MDiff / f1Denominator : 0.0;
        const double f2 = c_ * MsOverA_ * L.prime;
        const double f3 = 1.0 - c_ * kAlpha * MsOverA_ * L.prime;

        return Hdot * (f1 + f2) / f3;
    }

    double sampleRate_ { 192000.0 };
    double T_ { 1.0 / 192000.0 };
    double derivativeScale_ { (1.0 + kDerivativeAlpha) * 192000.0 };

    double drive_ { -1.0 }, saturation_ { -1.0 }, bias_ { -1.0 };
    double Ms_ { 1.25 }, a_ { 0.41 }, c_ { 0.7 };
    double oneMinusC_ { 0.3 }, MsOverA_ { 3.0 }, upperLimit_ { 1.9 };

    double M_ { 0.0 };
    double lastH_ { 0.0 };
    double lastHdot_ { 0.0 };
    bool clamped_ { false };
};

} // namespace tezla::ferrite
