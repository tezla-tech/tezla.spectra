// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Stick-slip friction on the modal bank -- the bow, playable on any object,
// which is the point: bowed bells and (impossibly) bowed membranes.
//
// The mechanism, from the physics: rosin's friction coefficient is high when
// bow and object move together (stick) and falls as they slide (slip). That
// falling curve is a negative differential resistance -- over the operating
// point, force RISES with object velocity -- so the bow pumps energy into a
// mode until its swing carries the contact out of the negative-slope region.
// The amplitude self-limits where the object's contact velocity rides near
// the bow's own speed: "the string moves with the bow".
//
// The curve is the classic regularised kinetic-friction family used across
// the bowed-string literature (McIntyre-Woodhouse and everything downstream
// -- see docs/DSP-REFERENCES.md, "Physical modelling"):
//
//     mu(dv) = tanh(dv / vStick) * (muD + (muS - muD) / (1 + |dv| / vCurve))
//
// tanh regularises the stick discontinuity into a steep, bounded, odd ramp;
// the hyperbolic tail decays from static muS toward dynamic muD. muS = 0.8
// and muD = 0.3 are the standard rosin figures quoted throughout that
// literature.
//
// THE HAIR IS A FILTER, AND THAT IS WHAT MAKES THIS STABLE. A bare
// curve-in-a-loop is numerically vicious here: the stick ramp's slope times
// the bank's velocity feedthrough (64 modes, each weighted by its own
// 2 pi f) gives a one-sample loop gain in the thousands, which in an
// explicit discrete loop is not an oscillator but sample-rate chatter.
// The physical bow does not couple that way: hair and rosin are compliant,
// so both the velocity the bow feels and the force it transmits pass
// through that compliance. Modelled as one-pole low-passes (corner in Hz,
// recomputed from the actual rate, CLAUDE.md section 6) on both paths, the
// loop gain falls as 1/f^2 above the corner: high modes decouple, low modes
// sing, and the stick region relaxes smoothly instead of chattering. The
// corner and the velocity/force scales that map the dimensionless Speed
// and Pressure controls onto the bank's units are VOICING constants tuned
// by the onset measurement in test_Bow.cpp -- stated as such, per section
// 2.1: the curve is physics, the scaling is a choice.
//
// The section 7 contract for a feedback loop around a nonlinearity:
//   * |mu| <= muS everywhere, so the force is bounded by
//     pressure * kForceScale * muS by construction;
//   * a soft knee clamp INSIDE the loop backstops that bound at four times
//     the largest legitimate force. It is bit-exact identity below twice
//     that force, so in normal operation it does exactly nothing -- and
//     getClampExcess() measures what it had to do, which is the assertion
//     with teeth (the Capstone lesson: measure the guard, not the guarded);
//   * the parameter sweep in test_Bow.cpp covers the whole pressure x speed
//     plane and pins the output ceiling.
//
// Silence contract: from exact rest, zero pressure -- or zero speed --
// yields bit-exact zero force forever (every term is exactly zero
// arithmetically). A moving, pressed bow on a silent object DOES start
// sound from nothing: that is the instrument, the energy source is the
// bow's motion, and Pressure is what makes it defeatable (section 7,
// "intentional features must be defeatable").
//
// One measured consequence for the voice (M7): the kinetic force's constant
// component statically deflects the object -- the bank output carries a
// standing offset of ~0.16 at pressure 0.2 up to ~0.7 at full pressure --
// exactly as a real bow bends a real string. The voice's DC blocker removes
// it from the audio path; it must NOT be removed inside the loop, where it
// is the operating point the curve works around.

#include <cmath>
#include <numbers>

namespace tezla::malleus {

class Bow
{
public:
    /// Static friction coefficient -- the classic rosin figure.
    static constexpr double kMuStatic = 0.8;

    /// Dynamic (sliding) coefficient, same source.
    static constexpr double kMuDynamic = 0.3;

    /// Width of the regularised stick region, in bow-speed units.
    static constexpr double kVStick = 0.02;

    /// Velocity scale of the stick-to-slip transition.
    static constexpr double kVCurve = 0.15;

    /// Force per unit pressure at full static friction.
    static constexpr double kForceScale = 0.2;

    /// The largest force the curve can legitimately produce.
    static constexpr double kMaxNormalForce = kForceScale * kMuStatic;

    /// The knee clamp: identity below kClampKnee, saturating to kClampLimit.
    static constexpr double kClampKnee = 2.0 * kMaxNormalForce;
    static constexpr double kClampLimit = 4.0 * kMaxNormalForce;

    /// Maps the bank's contactVelocity() (output units per second) into
    /// bow-speed units. Tuned by the onset measurement.
    static constexpr double kVelocityScale = 1.0e-4;

    /// The rate the per-sample injection is referenced to: force() returns
    /// momentum per SAMPLE, scaled by kReferenceRate / fs, so the momentum
    /// per second -- and with it the loop's growth rate and the limit
    /// cycle -- is the same at every host rate (CLAUDE.md section 6).
    static constexpr double kReferenceRate = 48000.0;

    /// The hair-compliance corner, Hz, applied to both loop paths.
    static constexpr double kHairCornerHz = 2000.0;

    /// The friction curve itself, exposed for the curve-shape test.
    [[nodiscard]] static double friction (double relativeVelocity) noexcept
    {
        const double magnitude = std::abs (relativeVelocity);
        const double kinetic = kMuDynamic
                             + (kMuStatic - kMuDynamic) / (1.0 + magnitude / kVCurve);

        return std::tanh (relativeVelocity / kVStick) * kinetic;
    }

    /// The section 7 backstop, exposed for its own shape test: bit-exact
    /// identity below kClampKnee; above it, a C1 exponential approach to
    /// kClampLimit (continuous value and slope at the knee, so nothing
    /// steps when a broken curve first crosses it). The curve's own bound
    /// keeps every legitimate force under half the knee, so in a healthy
    /// bow this function is the identity, measurably -- see
    /// getClampExcess().
    [[nodiscard]] static double clampForce (double force) noexcept
    {
        const double magnitude = std::abs (force);

        if (magnitude <= kClampKnee)
            return force;

        constexpr double kRange = kClampLimit - kClampKnee;
        const double soft = kClampLimit
                          - kRange * std::exp (-(magnitude - kClampKnee) / kRange);

        return force < 0.0 ? -soft : soft;
    }

    /// Sets the rate and clears the compliance state, as prepare always
    /// does. Arithmetic only -- safe anywhere.
    void prepare (double sampleRate) noexcept
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
        coefficient_ = 1.0 - std::exp (-2.0 * std::numbers::pi * kHairCornerHz / rate);
        injectionScale_ = kReferenceRate / rate;
        reset();
    }

    void reset() noexcept
    {
        velocityState_ = 0.0;
        forceState_ = 0.0;
    }

    /// Bow pressure, 0..1. Zero is bit-exact zero force from rest.
    void setPressure (double pressure) noexcept
    {
        pressure_ = pressure < 0.0 ? 0.0 : pressure > 1.0 ? 1.0 : pressure;
    }

    /// Bow speed, 0..1 in the units the friction curve lives in.
    void setSpeed (double speed) noexcept
    {
        speed_ = speed < 0.0 ? 0.0 : speed > 1.0 ? 1.0 : speed;
    }

    [[nodiscard]] double getPressure() const noexcept { return pressure_; }
    [[nodiscard]] double getSpeed() const noexcept { return speed_; }

    /// One sample of bow force from the bank's contact velocity. The caller
    /// closes the loop:  bank.process (bow.force (bank.contactVelocity())).
    [[nodiscard]] double force (double contactVelocity) noexcept
    {
        // The velocity the bow feels, through the hair's compliance.
        velocityState_ += coefficient_
                        * (contactVelocity * kVelocityScale - velocityState_);

        const double relative = speed_ - velocityState_;
        const double raw = pressure_ * kForceScale * friction (relative);

        // The force it transmits, through the same compliance.
        forceState_ += coefficient_ * (raw - forceState_);

        const double bounded = clampForce (forceState_);
        const double excess = std::abs (forceState_ - bounded);

        if (excess > clampExcess_)
            clampExcess_ = excess;

        return bounded * injectionScale_;
    }

    /// The most the knee clamp has had to remove since the last reset.
    /// A healthy curve leaves this at exactly zero -- the clamp is a
    /// backstop, and this accessor is how a test proves it stayed one.
    [[nodiscard]] double getClampExcess() const noexcept { return clampExcess_; }

    void resetClampExcess() noexcept { clampExcess_ = 0.0; }

private:
    double pressure_ { 0.0 };
    double speed_ { 0.5 };
    double coefficient_ { 0.25 };
    double injectionScale_ { 1.0 };
    double velocityState_ { 0.0 };
    double forceState_ { 0.0 };
    double clampExcess_ { 0.0 };
};

} // namespace tezla::malleus
