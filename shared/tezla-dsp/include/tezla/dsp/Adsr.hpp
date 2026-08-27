#pragma once

// An envelope generator. The library has envelope *followers* -- things that
// measure a signal -- and no generators at all, because until now nothing in
// the suite made a sound of its own.
//
// ---------------------------------------------------------------------------
// The segments are RC curves, and the times still mean what they say
// ---------------------------------------------------------------------------
//
// A linear attack does not sound like an attack. Every analogue envelope is a
// capacitor charging through a resistor, so each segment is exponential, and
// the way to get that in one multiply-add per sample is the standard trick:
// aim past the target and stop early. A stage that wants to travel from 0 to 1
// aims at `overshoot` and finishes when it crosses 1, so it traverses the first
// `1/overshoot` of an exponential -- the curved part -- rather than asymptotically
// crawling into its own destination and never arriving.
//
// **The part that is usually wrong is the timing.** Aiming further past the
// target makes the segment straighter *and* faster, so an unadjusted
// implementation has a shape control that is also a time control: turn up the
// curve and the attack gets slower, with no way to hold one and change the
// other. The fix is arithmetic rather than taste. Reaching 1 from 0 against a
// target of T takes
//
//     t = -tau * ln(1 - 1/T)  =  tau * ln(T / (T - 1))
//
// so setting `tau = time / ln(T / (T-1))` makes the segment last exactly the
// time asked for at every shape. The same expression covers decay and release
// unchanged, because both travel a distance D against a target D*T away, which
// is the same 1/T fraction of the same curve.
//
// Measured, and asserted in tests/test_Adsr.cpp: a 100 ms attack takes 100 ms
// to within a sample at every shape from 0 to 1, while the level at the
// half-way point moves from 0.83 to 0.55.
//
// ---------------------------------------------------------------------------
// It ends, exactly
// ---------------------------------------------------------------------------
//
// An exponential release approaches zero and never reaches it, which in a
// polyphonic instrument means a voice that is never free and a bus that is
// never silent. Because the release aims *below* zero it crosses in finite
// time; when it does, the level is set to exactly 0.0 and the envelope goes
// idle. `isActive()` is then the voice manager's answer to "can I take this
// one", and CLAUDE.md section 7's silence-in-silence-out is a property of the
// envelope rather than of a gate somewhere downstream.

#include <algorithm>
#include <cmath>

#include "Exact.hpp"

namespace tezla::dsp {

/// Which segment the envelope is in.
///
/// **Append-only** if it is ever exposed as a choice parameter -- CLAUDE.md
/// section 8. It is a status today, not a control.
enum class AdsrStage
{
    idle = 0,
    attack,
    decay,
    sustain,
    release
};

class Adsr
{
public:
    /// The shape control's two ends, as the overshoot factor.
    ///
    /// 1.05 is a hard exponential -- only the first 5% of the curve is used, so
    /// it is nearly all knee. 8.0 is close enough to linear that the difference
    /// is not audible on a percussive envelope, but not so close that the
    /// arithmetic loses its footing (at exactly linear, `ln(T/(T-1))` is the
    /// limit of `1/T` and the segment time expression degenerates).
    static constexpr double kSharpestOvershoot = 1.05;
    static constexpr double kStraightestOvershoot = 8.0;

    /// Below this the release is over and the level is set to exactly zero.
    ///
    /// -100 dB on a linear envelope, which is inaudible under anything and well
    /// above the denormal range. The release aims below zero so this is reached
    /// in finite time rather than approached.
    static constexpr double kSilence = 1.0e-5;

    /// The shortest a segment can be. Zero is allowed and means one sample --
    /// what a click is made of, and sometimes what is wanted -- but the
    /// coefficient has to be computed without dividing by it.
    static constexpr double kMinimumSeconds = 0.0;
    static constexpr double kMaximumSeconds = 30.0;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        level_ = 0.0;
        target_ = 0.0;
        coefficient_ = 0.0;
        stage_ = AdsrStage::idle;
    }

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------

    void setAttackSeconds (double seconds) noexcept
    {
        attackSeconds_ = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);
        updateCoefficients();
        refreshCurrentSegment();
    }

    void setDecaySeconds (double seconds) noexcept
    {
        decaySeconds_ = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);
        updateCoefficients();
        refreshCurrentSegment();
    }

    /// The level the envelope holds at while the note is down, 0 to 1.
    void setSustain (double sustain) noexcept
    {
        sustain_ = std::clamp (sustain, 0.0, 1.0);
        refreshCurrentSegment();
    }

    void setReleaseSeconds (double seconds) noexcept
    {
        releaseSeconds_ = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);
        updateCoefficients();
        refreshCurrentSegment();
    }

    /// 0 is the sharpest knee, 1 is nearly straight. It changes the shape of
    /// every segment and **not** how long any of them lasts -- see the header.
    void setShape (double shape) noexcept
    {
        shape_ = std::clamp (shape, 0.0, 1.0);
        updateCoefficients();
        refreshCurrentSegment();
    }

    [[nodiscard]] double getAttackSeconds() const noexcept { return attackSeconds_; }
    [[nodiscard]] double getDecaySeconds() const noexcept { return decaySeconds_; }
    [[nodiscard]] double getSustain() const noexcept { return sustain_; }
    [[nodiscard]] double getReleaseSeconds() const noexcept { return releaseSeconds_; }
    [[nodiscard]] double getShape() const noexcept { return shape_; }

    /// The overshoot factor the shape control currently maps to. Public because
    /// the segment-time arithmetic is checkable from it, and a test that
    /// recomputes the mapping is a test of the test.
    [[nodiscard]] double getOvershoot() const noexcept { return overshoot_; }

    // -----------------------------------------------------------------------
    // Playing
    // -----------------------------------------------------------------------

    /// Starts the attack **from wherever the envelope currently is**.
    ///
    /// Which is the only click-free way to retrigger a voice that is still
    /// sounding, and is why there is no argument to choose otherwise: jumping
    /// to zero first is a discontinuity of the whole current level, and a
    /// caller that wants a voice to start from silence has `reset()` for it.
    /// The coefficient is unchanged, so a retrigger from part-way up reaches
    /// the top **sooner** than a cold one, not in the same time: from 0.8 with
    /// an overshoot of 2 it takes `ln(1.2/1) = 0.18` time constants against
    /// `ln(2/1) = 0.69` from zero, so a quarter of the time for a fifth of the
    /// distance. Same curve, entered further along. That is what a retrigger
    /// sounds like on an analogue envelope, and the alternative -- stretching
    /// the remainder over the full attack time -- would make a fast repeated
    /// note quieter than a slow one.
    void noteOn() noexcept
    {
        stage_ = AdsrStage::attack;
        target_ = overshoot_;
        coefficient_ = attackCoefficient_;
    }

    /// Releases from wherever the envelope currently is, including mid-attack.
    void noteOff() noexcept
    {
        if (stage_ == AdsrStage::idle)
            return;

        stage_ = AdsrStage::release;
        coefficient_ = releaseCoefficient_;
        target_ = -level_ * (overshoot_ - 1.0);
    }

    /// Cuts the note without a release -- for voice stealing, where the caller
    /// is about to reuse this voice and wants it silent now.
    void kill() noexcept { reset(); }

    [[nodiscard]] bool isActive() const noexcept { return stage_ != AdsrStage::idle; }
    [[nodiscard]] AdsrStage getStage() const noexcept { return stage_; }
    [[nodiscard]] double getLevel() const noexcept { return level_; }

    /// One sample. Returns the new level, 0 to 1.
    [[nodiscard]] double process() noexcept
    {
        switch (stage_)
        {
            case AdsrStage::idle:
                return 0.0;

            case AdsrStage::sustain:
                return level_;

            case AdsrStage::attack:
                level_ = target_ + (level_ - target_) * coefficient_;

                if (level_ >= 1.0)
                {
                    level_ = 1.0;
                    beginDecay();
                }

                return level_;

            case AdsrStage::decay:
                level_ = target_ + (level_ - target_) * coefficient_;

                if (decayHasArrived())
                {
                    level_ = sustain_;
                    stage_ = AdsrStage::sustain;
                }

                return level_;

            case AdsrStage::release:
                level_ = target_ + (level_ - target_) * coefficient_;

                if (level_ <= kSilence)
                {
                    level_ = 0.0;
                    stage_ = AdsrStage::idle;
                }

                return level_;
        }

        return 0.0;
    }

private:
    /// `ln(T / (T - 1))`: how many time constants a segment aiming at T takes to
    /// cover the distance it was actually asked to cover.
    [[nodiscard]] static double scaleFor (double overshoot) noexcept
    {
        return std::log (overshoot / (overshoot - 1.0));
    }

    /// exp(-1 / (tau * fs)), with `tau` derived so the segment lasts `seconds`.
    [[nodiscard]] double coefficientFor (double seconds) const noexcept
    {
        if (seconds <= 0.0)
            return 0.0;

        const double tau = seconds / scaleFor (overshoot_);

        return std::exp (-1.0 / (tau * sampleRate_));
    }

    void updateCoefficients() noexcept
    {
        // Geometric, so the control is even: the interesting half of the range
        // is all below an overshoot of 2 and a linear map would spend most of
        // its travel between "straight" and "slightly straighter".
        overshoot_ = kSharpestOvershoot
                       * std::pow (kStraightestOvershoot / kSharpestOvershoot, shape_);

        attackCoefficient_ = coefficientFor (attackSeconds_);
        decayCoefficient_ = coefficientFor (decaySeconds_);
        releaseCoefficient_ = coefficientFor (releaseSeconds_);
    }

    /// Re-aims the segment in progress at the new settings.
    ///
    /// CLAUDE.md section 7: a setter that changes a running stage must not
    /// restart it. Turning the sustain down while a note is held has to bend
    /// the decay towards the new level, not jump there and not begin the decay
    /// again -- and the level itself is never touched, which is what keeps the
    /// change click-free.
    void refreshCurrentSegment() noexcept
    {
        switch (stage_)
        {
            case AdsrStage::attack:
                target_ = overshoot_;
                coefficient_ = attackCoefficient_;
                break;

            case AdsrStage::decay:
                aimDecayAtSustain();
                break;

            case AdsrStage::sustain:
                level_ = sustain_;
                break;

            case AdsrStage::release:
                coefficient_ = releaseCoefficient_;
                target_ = -level_ * (overshoot_ - 1.0);
                break;

            case AdsrStage::idle:
            default:
                break;
        }
    }

    void beginDecay() noexcept
    {
        stage_ = AdsrStage::decay;
        aimDecayAtSustain();
    }

    /// Points the decay at the sustain from wherever the level currently is.
    ///
    /// **Signed**, and that is the whole of it. The obvious form aims below the
    /// sustain, which is correct only while the sustain is below the level --
    /// and the sustain is a modulation destination, so it can be raised past
    /// the level at any moment. The first version of this handled that by
    /// snapping the level up to the new sustain, which is a discontinuity of
    /// the whole difference: measured at 0.235 of full scale in one sample,
    /// exactly the click CLAUDE.md section 7 forbids. Aiming past the sustain
    /// *in the direction of travel* turns the same segment into a rise, takes
    /// the stated decay time to get there, and never steps.
    void aimDecayAtSustain() noexcept
    {
        coefficient_ = decayCoefficient_;

        const double distance = sustain_ - level_;

        if (isExactlyZero (distance))
        {
            level_ = sustain_;
            stage_ = AdsrStage::sustain;
            return;
        }

        target_ = sustain_ + distance * (overshoot_ - 1.0);
    }

    /// Whether the decay has passed the sustain, whichever way it was going.
    [[nodiscard]] bool decayHasArrived() const noexcept
    {
        return target_ < sustain_ ? level_ <= sustain_ : level_ >= sustain_;
    }

    double sampleRate_ { 48000.0 };

    double attackSeconds_ { 0.005 };
    double decaySeconds_ { 0.100 };
    double sustain_ { 0.7 };
    double releaseSeconds_ { 0.200 };
    double shape_ { 0.35 };

    double overshoot_ { 1.0 };
    double attackCoefficient_ { 0.0 };
    double decayCoefficient_ { 0.0 };
    double releaseCoefficient_ { 0.0 };

    double level_ { 0.0 };
    double target_ { 0.0 };
    double coefficient_ { 0.0 };

    AdsrStage stage_ { AdsrStage::idle };
};

} // namespace tezla::dsp
