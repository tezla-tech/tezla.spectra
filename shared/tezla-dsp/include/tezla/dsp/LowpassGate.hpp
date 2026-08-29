// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A vactrol low-pass gate -- the west-coast "ping", modelled from the
// component's mechanism (CLAUDE.md section 2.1) rather than from any
// product's schematic.
//
// A vactrol is an LED shining on a CdS photoresistor in one dark package.
// Three behaviours make the sound, and each is a modelled mechanism here:
//
//   * The LED is effectively instant at audio rates, but a strike is a
//     FLASH, not a step: the light pulse itself decays over a few
//     milliseconds. That flash shape is the attack transient.
//
//   * The CdS cell responds to light quickly (sub-millisecond here, one
//     smoothing pole) but goes dark SLOWLY, and -- the signature -- its
//     dark decay is not exponential. A brightly-lit cell loses
//     conductance fast; as it darkens the decay drags longer and longer.
//     Modelled as dg/dt = -g (kLin + kQuad g): quadratic-dominated while
//     bright (fast first drop), settling to a linear-rate exponential
//     tail (so the gate does reach closed in finite time, unlike a pure
//     power law). The two rates are voicing constants; the SHAPE -- decay
//     rate falling as the cell darkens -- is the component's documented
//     behaviour and the thing the test pins.
//
//   * Cutoff and gain are COUPLED, because the same photoresistance both
//     divides the signal down and forms the filter with the load: a
//     closing gate darkens as it quietens. That coupling is why a pinged
//     LPG reads as a struck object rather than a chopped one.
//
// Audio path: two cascaded one-poles (12 dB/oct, non-resonant, as a
// vactrol RC network is), cutoff swept by conductance, times a linear
// conductance gain. Coefficients are recomputed from the actual sample
// rate every sample -- one exp per sample, measured harmless -- and all
// time constants are per-second, so the trajectory is identical at every
// host rate (section 6).
//
// Exact-silence contract: once the cell has fully darkened (conductance
// under 1e-5, gain under -100 dB) the conductance SNAPS to exactly zero,
// and output = gain * filtered is then bit-exact zero whatever the input
// -- a closed gate is closed, not quiet (section 7). The snap lands five
// decades below audibility, so it cannot click.

#include <cmath>
#include <numbers>

namespace tezla::dsp {

class LowpassGate
{
public:
    /// Dark-decay rates, per second: dg/dt = -g (kLin + kQuad g).
    /// Bright cell: ~(kLin + kQuad) per second. Dark cell: kLin.
    static constexpr double kDarkLinear = 6.0;
    static constexpr double kDarkQuadratic = 54.0;

    /// The strike flash's exponential decay rate, per second (~4 ms).
    static constexpr double kFlashRate = 250.0;

    /// The cell's light-response smoothing corner, Hz (~0.6 ms).
    static constexpr double kAttackCornerHz = 265.0;

    /// Cutoff span and shape: fc = kCutoffFloorHz + kCutoffSpanHz * g^1.7.
    static constexpr double kCutoffFloorHz = 25.0;
    static constexpr double kCutoffSpanHz = 14000.0;
    static constexpr double kCutoffShape = 1.7;

    /// Below this conductance the cell is dark: snap to exactly zero.
    static constexpr double kSnapFloor = 1.0e-5;

    /// Below this the strike flash is out: snap to exactly zero. Without
    /// it the exponential flash NEVER reaches zero and keeps re-lighting
    /// a snapped-dark cell by ~1e-168 a sample -- output forever tiny
    /// instead of exactly silent. Found by the closed-gate test.
    static constexpr double kFlashFloor = 1.0e-7;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        timeStep_ = 1.0 / sampleRate_;
        attackCoefficient_ = 1.0 - std::exp (-2.0 * std::numbers::pi
                                             * kAttackCornerHz / sampleRate_);
        flashDecay_ = std::exp (-kFlashRate / sampleRate_);
        reset();
    }

    void reset() noexcept
    {
        conductance_ = 0.0;
        flash_ = 0.0;
        hold_ = 0.0;
        stage1_ = 0.0;
        stage2_ = 0.0;
    }

    /// Strikes the gate: a light flash at this level (0..1). A new flash
    /// only ever brightens -- re-striking mid-flash cannot dim the LED.
    void ping (double level) noexcept
    {
        const double l = level < 0.0 ? 0.0 : level > 1.0 ? 1.0 : level;

        if (l > flash_)
            flash_ = l;
    }

    /// Holds the LED lit at this level (0..1) -- the drone/bow posture.
    /// Zero releases it into the dark decay.
    void setHold (double level) noexcept
    {
        hold_ = level < 0.0 ? 0.0 : level > 1.0 ? 1.0 : level;
    }

    /// The cell's current conductance, 0..1 -- the number the tests and a
    /// voice's retirement logic read.
    [[nodiscard]] double conductance() const noexcept { return conductance_; }

    [[nodiscard]] double process (double input) noexcept
    {
        // The light reaching the cell: the decaying strike flash or the
        // held level, whichever is brighter.
        flash_ *= flashDecay_;

        if (flash_ < kFlashFloor)
            flash_ = 0.0;   // an exponential never reaches dark; this does

        const double light = flash_ > hold_ ? flash_ : hold_;

        if (light > conductance_)
        {
            // Brightening: the cell chases the light through one fast pole.
            conductance_ += attackCoefficient_ * (light - conductance_);
        }
        else
        {
            // Darkening: the vactrol signature. The decay RATE falls with
            // the conductance itself -- fast first drop, dragging tail.
            const double rate = conductance_ * (kDarkLinear
                                                + kDarkQuadratic * conductance_);
            conductance_ -= rate * timeStep_;

            if (conductance_ < light)
                conductance_ = light;

            if (conductance_ < kSnapFloor)
                conductance_ = 0.0;   // dark is dark: exact, not small
        }

        // Coupled cutoff and gain from the one conductance.
        const double cutoff = kCutoffFloorHz
                            + kCutoffSpanHz * std::pow (conductance_, kCutoffShape);
        const double coefficient = 1.0 - std::exp (-2.0 * std::numbers::pi
                                                   * cutoff / sampleRate_);

        stage1_ += coefficient * (input - stage1_);
        stage2_ += coefficient * (stage1_ - stage2_);

        return conductance_ * stage2_;
    }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

private:
    double sampleRate_ { 44100.0 };
    double timeStep_ { 1.0 / 44100.0 };
    double attackCoefficient_ { 0.03 };
    double flashDecay_ { 0.994 };

    double conductance_ { 0.0 };
    double flash_ { 0.0 };
    double hold_ { 0.0 };
    double stage1_ { 0.0 };
    double stage2_ { 0.0 };
};

} // namespace tezla::dsp
