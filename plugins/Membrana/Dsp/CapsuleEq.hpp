// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The mic model as one stage: position (pattern, distance, angle) realised
// analytically, body diffraction and grille realised as a minimum-phase FIR,
// and the level policy that lets the user judge tone rather than loudness.
//
// The whole stage is RELATIVE to the reference condition: the same mic,
// 1 m away, on axis. Membrana does not pretend to know what really recorded
// the input; it re-images the input as if that reference recording had been
// made at the knobs' position instead. Two consequences fall out:
//
//  * The mic's own fixed colourations divide out. In particular the LF
//    diaphragm corner cancels between the simulated position and the
//    reference, so the applied position EQ is the RATIO
//
//        E(s) = [D s + G c/r] / [s + (1-a) c/1m]
//
//    (MicPattern's terms), which is not a pole at DC but a finite low shelf:
//    E(0) = cos(theta)/r, E(inf) = D. A cardioid at 2 cm plateaus +34 dB
//    below ~27 Hz -- honest physics, and unusable under a dubstep sub, which
//    is where lowLimitHz comes in: the second-order highpass (Q 0.707) of
//    the SIMULATED mic's diaphragm engages whenever the position EQ does,
//    bounding that plateau. At the reference position (and for an omni,
//    whose E is identically 1 at every distance) neither runs, and the
//    plugin's neutral settings stay bit-exact.
//
//  * The FIR carries only the SHAPE of the body diffraction, not its level.
//    The sphere series' raw output at close range contains the geometric
//    near field (+18 dB at rho = 1.25 -- pinned in test_SphereDiffraction),
//    which is level, already owned by the proximity model and the trim. So
//    the FIR target is the character-scaled difference
//
//        C(f) = character * [ |H(rho(r), mu(f), theta)|dB
//                             - |H(rho(1m), mu(f), 0)|dB ]  re-zeroed at
//
//    the 600 Hz splice (its own value there subtracted), exactly 0.0 below
//    the splice by assignment, eased in over 600..1200 Hz -- plus the
//    grille bell (depth * 6 dB at grilleHz, Q 2.5). At the reference
//    position the difference is identically zero whatever `character` is,
//    which is what keeps the default -- character at 35% -- bit-exact.
//
// Realisation: tezla::dsp::MinimumPhaseFir on a 1024 design grid, 96 taps
// at 48 kHz scaled with the rate (cap 256). The sphere response is
// minimum-phase at every range and angle (Duda & Martens 1998, Sec II.D),
// so this is the faithful phase, not an approximation; latency 0.
//
// Retune is state-preserving, never prepare(): a parameter change redesigns
// coefficients and taps into the spare tap bank at the next applyChanges()
// (the caller's control-chunk boundary), the banks swap, and the FIR's
// delay line and the analytic sections' memories are untouched. Clearing
// them instead is audible -- the anti-click test was first run against a
// reset()-based retune and failed, which is the required red.
//
// Level policy: with autoLevel on, the composed response of the ACTUAL
// designed coefficients is measured at 1 kHz at design time and divided
// out, so Distance reads as tone. Off, the physical 20 log10(1 m / r) is
// applied instead, clamped to +24 dB. The trim is smoothed (30 ms).

#include <array>
#include <cmath>
#include <cstddef>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/MinimumPhaseFir.hpp>
#include <tezla/dsp/SmoothedValue.hpp>

#include "MicPattern.hpp"
#include "SphereDiffraction.hpp"

namespace tezla::membrana {

class CapsuleEq
{
public:
    static constexpr int kMaxTaps = 256;
    static constexpr int kFft = 1024;
    static constexpr double kSpliceHz = 600.0;    // FIR target exactly 0 below
    static constexpr double kRampTopHz = 1200.0;  // eased fully in by here
    static constexpr double kGrilleQ = 2.5;
    static constexpr double kGrilleFullDb = 6.0;  // depth 100% = +6 dB
    static constexpr double kReferenceMetres = 1.0;
    static constexpr double kMinRho = 1.2;        // the mouth cannot occupy the body
    static constexpr double kMaxPhysicalDb = 24.0;
    static constexpr double kTrimSmoothingSeconds = 0.030;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        activeTaps_ = static_cast<int> (96.0 * sampleRate / 48000.0);
        activeTaps_ = activeTaps_ < 96 ? 96 : activeTaps_ > kMaxTaps ? kMaxTaps : activeTaps_;
        trimGain_.prepare (sampleRate, kTrimSmoothingSeconds);
        redesign();
        reset();
    }

    void reset() noexcept
    {
        delay_.fill (0.0);
        write_ = 0;
        posZ_ = 0.0;
        lowLimit_.reset();
        trimGain_.setCurrentAndTarget (trimLinear_);
    }

    // -- parameters (no-op guarded; changes land at applyChanges()) ---------

    /// 0 omni, 0.5 cardioid, 1 figure-8 (the panel's Pattern knob).
    void setPattern (double pattern01) noexcept { set (pattern_, pattern01); }

    /// Diffracting body diameter in millimetres.
    void setBodyMm (double mm) noexcept { set (bodyMm_, mm); }

    /// How much of the raw diffraction survives, 0..1.
    void setCharacter (double amount01) noexcept { set (character_, amount01); }

    /// Grille resonance depth 0..1 (x 6 dB) and centre frequency.
    void setGrille (double depth01, double hz) noexcept
    {
        set (grille_, depth01);
        set (grilleHz_, hz);
    }

    /// Source distance in metres and off-axis angle in degrees.
    void setPosition (double distanceMetres, double axisDegrees) noexcept
    {
        set (distanceM_, distanceMetres);
        set (axisDeg_, axisDegrees);
    }

    /// The simulated diaphragm's LF corner, bounding the proximity shelf.
    void setLowLimitHz (double hz) noexcept { set (lowLimitHz_, hz); }

    void setAutoLevel (bool on) noexcept
    {
        if (autoLevel_ != on)
        {
            autoLevel_ = on;
            dirty_ = true;
        }
    }

    /// Call at a control-chunk boundary: pending parameter changes become a
    /// state-preserving redesign (coefficients move, memories stay).
    void applyChanges() noexcept
    {
        if (! dirty_)
            return;

        dirty_ = false;
        redesign();

        if (isNeutral())
            trimGain_.setCurrentAndTarget (1.0);
        else
            trimGain_.setTarget (trimLinear_);
    }

    /// True when every engaged mechanism is at its no-op point: the branch
    /// process() takes to return the input verbatim.
    [[nodiscard]] bool isNeutral() const noexcept
    {
        return posNeutral_ && firNeutral_ && tezla::dsp::isExactly (trimLinear_, 1.0);
    }

    [[nodiscard]] static int latencySamples() noexcept { return 0; }

    [[nodiscard]] double process (double x) noexcept
    {
        if (isNeutral())
            return x;   // bit-exact by branch; never by arithmetic

        double y = x;

        if (! posNeutral_)
        {
            // First-order position section, transposed direct form II.
            const double out = posB0_ * y + posZ_;
            posZ_ = posB1_ * y - posA1_ * out;
            y = lowLimit_.process (out);
        }

        if (! firNeutral_)
        {
            delay_[static_cast<std::size_t> (write_)] = y;

            double acc = 0.0;
            int read = write_;
            const auto& taps = tapBanks_[static_cast<std::size_t> (activeBank_)];

            for (int n = 0; n < activeTaps_; ++n)
            {
                acc += taps[static_cast<std::size_t> (n)]
                         * delay_[static_cast<std::size_t> (read)];
                read = read == 0 ? activeTaps_ - 1 : read - 1;
            }

            write_ = write_ + 1 == activeTaps_ ? 0 : write_ + 1;
            y = acc;
        }

        return y * trimGain_.next();
    }

    // -- introspection: the same coefficients that play ---------------------

    /// Composed response of the DESIGNED filters at hz, in dB: the digital
    /// position section, the LF-limit biquad, the FIR taps by direct DFT,
    /// and the (unsmoothed) trim. The editor's curve and the fit tests read
    /// this, so what is drawn and what is asserted is what plays.
    [[nodiscard]] double renderedDbAt (double hz) const noexcept
    {
        double db = 20.0 * std::log10 (trimLinear_ > 0.0 ? trimLinear_ : 1.0e-9);

        const double w = 2.0 * kPi * hz / sampleRate_;
        const double cw = std::cos (w), sw = std::sin (w);

        if (! posNeutral_)
        {
            // |b0 + b1 z^-1| / |1 + a1 z^-1| at z = e^{jw}
            const double nr = posB0_ + posB1_ * cw, ni = -posB1_ * sw;
            const double dr = 1.0 + posA1_ * cw,  di = -posA1_ * sw;
            db += 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di));
            db += lowLimitDbAt (hz);
        }

        if (! firNeutral_)
        {
            double re = 0.0, im = 0.0;
            const auto& taps = tapBanks_[static_cast<std::size_t> (activeBank_)];

            for (int n = 0; n < activeTaps_; ++n)
            {
                const double angle = -w * n;
                re += taps[static_cast<std::size_t> (n)] * std::cos (angle);
                im += taps[static_cast<std::size_t> (n)] * std::sin (angle);
            }

            db += 10.0 * std::log10 (re * re + im * im);
        }

        return db;
    }

    /// The analytic composed target at hz, in dB -- what renderedDbAt is
    /// asked to match. Fit error = |rendered - target|.
    [[nodiscard]] double targetDbAt (double hz) const noexcept
    {
        double db = 20.0 * std::log10 (trimLinear_ > 0.0 ? trimLinear_ : 1.0e-9);

        if (! posNeutral_)
        {
            // |E(j 2 pi f)| for E(s) = (B1 s + B0) / (s + A0).
            const double w = 2.0 * kPi * hz;
            const double num = posAnalogB1_ * posAnalogB1_ * w * w
                                 + posAnalogB0_ * posAnalogB0_;
            const double den = w * w + posAnalogA0_ * posAnalogA0_;
            db += 10.0 * std::log10 (num / den);
            db += analogHighpassDbAt (hz);
        }

        if (! firNeutral_)
            db += firTargetDbAt (hz);

        return db;
    }

    [[nodiscard]] double trimDb() const noexcept
    {
        return 20.0 * std::log10 (trimLinear_);
    }

    [[nodiscard]] int tapCount() const noexcept { return activeTaps_; }

private:
    static constexpr double kPi = 3.141592653589793;

    void set (double& slot, double value) noexcept
    {
        if (tezla::dsp::isExactly (slot, value))
            return;

        slot = value;
        dirty_ = true;
    }

    /// The full redesign: predicates, analytic position section, LF limit,
    /// FIR target and taps (into the spare bank), then the trim. Bounded
    /// work at a control boundary; nothing here touches filter state.
    void redesign() noexcept
    {
        const double a = 1.0 - pattern_;             // MicPattern's parameter
        const double cosTheta = std::cos (axisDeg_ * kPi / 180.0);
        const double d = MicPattern::level (a, cosTheta);
        const double g = MicPattern::gradientWeight (a, cosTheta);
        const double c = MicPattern::kSpeedOfSound;

        // Position: E(s) = (D s + G c/r) / (s + (1-a) c/ref).
        posAnalogB1_ = d;
        posAnalogB0_ = g * c / distanceM_;
        posAnalogA0_ = (1.0 - a) * c / kReferenceMetres;

        posNeutral_ = tezla::dsp::isExactly (posAnalogB1_, 1.0)
                      && tezla::dsp::isExactly (posAnalogB0_, posAnalogA0_);

        if (! posNeutral_)
        {
            // Bilinear, K = 2 fs (no prewarp: every corner is far below
            // Fs/8, where a first-order section is rate-independent).
            const double k = 2.0 * sampleRate_;
            const double norm = 1.0 / (k + posAnalogA0_);
            posB0_ = (posAnalogB1_ * k + posAnalogB0_) * norm;
            posB1_ = (posAnalogB0_ - posAnalogB1_ * k) * norm;
            posA1_ = (posAnalogA0_ - k) * norm;

            lowLimit_.setCoefficients (tezla::dsp::design::highpass (
                lowLimitHz_, 0.7071067811865476, sampleRate_));
        }

        // FIR: engaged by anything that shapes it.
        const bool sphereNeutral =
            tezla::dsp::isExactly (distanceM_, kReferenceMetres)
            && tezla::dsp::isExactlyZero (axisDeg_);
        firNeutral_ = (tezla::dsp::isExactlyZero (character_) || sphereNeutral)
                      && tezla::dsp::isExactlyZero (grille_);

        if (! firNeutral_)
        {
            cacheSphereTarget();

            std::array<double, kFft / 2 + 1> halfLog {};

            for (int bin = 0; bin <= kFft / 2; ++bin)
            {
                const double hz = static_cast<double> (bin) * sampleRate_ / kFft;
                halfLog[static_cast<std::size_t> (bin)] =
                    firTargetDbAt (hz) * (kLn10 / 20.0);
            }

            const int spare = 1 - activeBank_;
            tezla::dsp::MinimumPhaseFir<kFft>::design (
                halfLog.data(),
                tapBanks_[static_cast<std::size_t> (spare)].data(),
                activeTaps_);
            activeBank_ = spare;
        }

        // Trim, from the composed design (position + LF limit + FIR at
        // 1 kHz), or the physical distance level. Exactly 1.0 by predicate
        // when nothing is engaged and the physical level is 0 dB.
        double trimDb = 0.0;

        if (autoLevel_)
        {
            if (! posNeutral_ || ! firNeutral_)
                trimDb = -composedPreTrimDbAt (1000.0);
        }
        else
        {
            const double physical = 20.0 * std::log10 (kReferenceMetres / distanceM_);
            trimDb = physical > kMaxPhysicalDb ? kMaxPhysicalDb : physical;
        }

        trimLinear_ = tezla::dsp::isExactlyZero (trimDb)
                          ? 1.0
                          : std::pow (10.0, trimDb / 20.0);
    }

    /// The rendered response before the trim -- what autoLevel divides
    /// out. Measured on the ACTUAL designed coefficients (the digital
    /// sections and a direct DFT of the freshly designed taps), not on the
    /// analytic target, so the 1 kHz hold is exact rather than exact minus
    /// the fit error.
    [[nodiscard]] double composedPreTrimDbAt (double hz) const noexcept
    {
        double db = 0.0;

        const double w = 2.0 * kPi * hz / sampleRate_;

        if (! posNeutral_)
        {
            const double cw = std::cos (w), sw = std::sin (w);
            const double nr = posB0_ + posB1_ * cw, ni = -posB1_ * sw;
            const double dr = 1.0 + posA1_ * cw,  di = -posA1_ * sw;
            db += 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di));
            db += lowLimitDbAt (hz);
        }

        if (! firNeutral_)
        {
            double re = 0.0, im = 0.0;
            const auto& taps = tapBanks_[static_cast<std::size_t> (activeBank_)];

            for (int n = 0; n < activeTaps_; ++n)
            {
                const double angle = -w * n;
                re += taps[static_cast<std::size_t> (n)] * std::cos (angle);
                im += taps[static_cast<std::size_t> (n)] * std::sin (angle);
            }

            db += 10.0 * std::log10 (re * re + im * im);
        }

        return db;
    }

    /// Sphere values that depend only on the parameters, cached per design:
    /// the two 600 Hz anchors that re-zero the shape at the splice.
    void cacheSphereTarget() noexcept
    {
        const double radius = bodyMm_ * 0.5e-3;
        rho_ = distanceM_ / radius;
        rho_ = rho_ < kMinRho ? kMinRho : rho_;
        rhoRef_ = kReferenceMetres / radius;
        cosThetaFir_ = std::cos (axisDeg_ * kPi / 180.0);
        radius_ = radius;

        const double mu600 = SphereDiffraction::muFor (kSpliceHz, radius);
        spliceDeltaDb_ = SphereDiffraction::magnitudeDb (rho_, mu600, cosThetaFir_)
                         - SphereDiffraction::magnitudeDb (rhoRef_, mu600, 1.0);
    }

    /// The FIR's analytic target at hz, in dB: exactly 0 below the splice,
    /// the re-zeroed character-scaled sphere difference plus the grille
    /// bell above it, eased over the splice octave.
    [[nodiscard]] double firTargetDbAt (double hz) const noexcept
    {
        if (hz < kSpliceHz)
            return 0.0;

        double db = 0.0;

        if (! tezla::dsp::isExactlyZero (character_))
        {
            const double mu = SphereDiffraction::muFor (hz, radius_);
            const double delta =
                SphereDiffraction::magnitudeDb (rho_, mu, cosThetaFir_)
                - SphereDiffraction::magnitudeDb (rhoRef_, mu, 1.0)
                - spliceDeltaDb_;

            double ease = 1.0;

            if (hz < kRampTopHz)
            {
                const double t = (hz - kSpliceHz) / (kRampTopHz - kSpliceHz);
                ease = 0.5 - 0.5 * std::cos (t * kPi);
            }

            db += character_ * delta * ease;
        }

        if (! tezla::dsp::isExactlyZero (grille_))
        {
            // A symmetric-in-log-f bell: full depth at the centre, half
            // where |f/fc - fc/f| = 1/Q.
            const double x = (hz / grilleHz_ - grilleHz_ / hz) * kGrilleQ;
            db += grille_ * kGrilleFullDb / (1.0 + x * x);
        }

        return db;
    }

    /// |HP(j 2 pi f)| of the analog second-order Butterworth prototype the
    /// digital LF limit is designed from.
    [[nodiscard]] double analogHighpassDbAt (double hz) const noexcept
    {
        const double ratio = lowLimitHz_ / hz;
        const double r2 = ratio * ratio;
        return -10.0 * std::log10 (1.0 + r2 * r2);   // Butterworth: 1 + (fc/f)^4
    }

    /// The digital LF-limit biquad's magnitude, from its coefficients.
    [[nodiscard]] double lowLimitDbAt (double hz) const noexcept
    {
        return 20.0 * std::log10 (lowLimit_.getCoefficients().magnitudeAt (hz, sampleRate_));
    }

    static constexpr double kLn10 = 2.302585092994046;

    // parameters
    double pattern_ { 0.5 };
    double bodyMm_ { 50.0 };
    double character_ { 0.35 };
    double grille_ { 0.0 };
    double grilleHz_ { 7000.0 };
    double distanceM_ { 1.0 };
    double axisDeg_ { 0.0 };
    double lowLimitHz_ { 40.0 };
    bool autoLevel_ { true };
    bool dirty_ { false };

    // design results
    bool posNeutral_ { true };
    bool firNeutral_ { true };
    double posAnalogB1_ { 1.0 }, posAnalogB0_ { 0.0 }, posAnalogA0_ { 0.0 };
    double posB0_ { 1.0 }, posB1_ { 0.0 }, posA1_ { 0.0 };
    double trimLinear_ { 1.0 };
    double rho_ { 40.0 }, rhoRef_ { 40.0 }, cosThetaFir_ { 1.0 };
    double radius_ { 0.025 };
    double spliceDeltaDb_ { 0.0 };

    // state
    double sampleRate_ { 48000.0 };
    int activeTaps_ { 96 };
    int activeBank_ { 0 };
    std::array<std::array<double, kMaxTaps>, 2> tapBanks_ {};
    std::array<double, kMaxTaps> delay_ {};
    int write_ { 0 };
    double posZ_ { 0.0 };
    tezla::dsp::Biquad<double> lowLimit_;
    tezla::dsp::SmoothedValue<double> trimGain_;
};

} // namespace tezla::membrana
