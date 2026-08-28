// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// An AHDSR envelope generator. The library has envelope *followers* -- things
// that measure a signal -- and no generators at all, because until Sonitus
// nothing in the suite made a sound of its own.
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
// `1/overshoot` of an exponential -- the curved part -- rather than
// asymptotically crawling into its own destination and never arriving.
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
// ---------------------------------------------------------------------------
// Tension is bipolar, and per segment
// ---------------------------------------------------------------------------
//
// The trick above only bends a curve **one way**: aiming past the destination
// gives fast-then-slow, and no value of the overshoot gives the opposite. A
// single control over it can therefore run from "sharp analogue" to "nearly
// straight" and no further, which is half a control.
//
// The other half is the *mirror*, and it falls out of the same recursion with
// one sign change. For a segment from A to B, distance d = B - A:
//
//   positive tension   aim at B + d(T-1), start at A, and **approach** it.
//                      Fast at first, decelerating: a capacitor charging.
//
//   negative tension   aim at A - d(T-1), start at A, and **recede** from it.
//                      Slow at first, accelerating: the same curve reflected.
//
// Both are `level = target + (level - target) * c`, one multiply-add. The only
// difference is that the second uses `1/c` instead of `c`, because receding
// from a target is approaching it with time running backwards. Solving for the
// escape factor that lands exactly on B at u = 1 gives `T / (T - 1)`, which is
// precisely the reciprocal of the approach factor -- so the mirror costs a
// division at *design* time and nothing at all per sample, and the segment
// still lasts exactly the time it was asked for.
//
// **Zero is straight**, or as straight as the arithmetic allows: `|tension| = 0`
// maps to an overshoot of 32, where the curve deviates from a line by 0.004 at
// its midpoint. Exactly linear is the limit T -> infinity and degenerates the
// time expression, so it is approached rather than reached. The kink where the
// two branches meet is twice that deviation and is neither audible nor visible.
//
// Each of the three timed segments has its own tension, because they want
// different ones: a percussive envelope is usually a sharp positive decay under
// a straight attack, and a swell is the reverse.
//
// ---------------------------------------------------------------------------
// Hold
// ---------------------------------------------------------------------------
//
// A stage between the attack and the decay that sits at full level for a set
// time. It is what makes a plucked or gated sound possible without setting the
// sustain to 1 and shortening the note: attack up, stay there, then fall.
//
// The elapsed time is counted rather than a remaining time counted down, so
// changing the hold length while one is running takes effect immediately and
// does not restart it -- the same rule the other setters follow.
//
// ---------------------------------------------------------------------------
// It ends, exactly
// ---------------------------------------------------------------------------
//
// An exponential release approaches zero and never reaches it, which in a
// polyphonic instrument means a voice that is never free and a bus that is
// never silent. Because the release aims past zero it crosses in finite time;
// when it does, the level is set to exactly 0.0 and the envelope goes idle.
// `isActive()` is then the voice manager's answer to "can I take this one", and
// CLAUDE.md section 7's silence-in-silence-out is a property of the envelope
// rather than of a gate somewhere downstream.
//
// And it ends *on time even under a control-rate caller*, which took two
// separate defences to make true: every setter refuses an unchanged value (see
// the Controls block), and the release aims from the level it started at
// rather than from wherever it currently is (see `aimRelease`). Without them,
// a voice pushing its settings every 32 samples re-aimed the release from the
// current level each chunk, the exit stretched to ~11x the stated time, and
// the user met it as a CPU meter pinned at 100% seconds after every key was up.

#include <algorithm>
#include <cmath>

#include "Exact.hpp"

namespace tezla::dsp {

/// Which segment the envelope is in.
///
/// **Append-only** if it is ever exposed as a choice parameter -- CLAUDE.md
/// section 8. It is a status today, not a control, which is why `hold` is at
/// the end rather than between `attack` and `decay` where it belongs
/// chronologically: appending costs a moment's confusion here and renumbering
/// would cost a silently wrong stored value somewhere else.
enum class AdsrStage
{
    idle = 0,
    attack,
    decay,
    sustain,
    release,
    hold
};

class Adsr
{
public:
    /// The tension control's two ends, as the overshoot factor.
    ///
    /// 1.05 is a hard exponential -- only the first 5% of the curve is used, so
    /// it is nearly all knee. 32 is straight to within 0.004 at the midpoint,
    /// but not so straight that the arithmetic loses its footing: at exactly
    /// linear, `ln(T/(T-1))` is the limit `1/T` and the segment-time expression
    /// degenerates.
    static constexpr double kSharpestOvershoot = 1.05;
    static constexpr double kStraightestOvershoot = 32.0;

    /// Below this the release is over and the level is set to exactly zero.
    ///
    /// -100 dB on a linear envelope, which is inaudible under anything and well
    /// above the denormal range. The release aims past zero so this is reached
    /// in finite time rather than approached.
    static constexpr double kSilence = 1.0e-5;

    /// The shortest a segment can be. Zero is allowed and means one sample --
    /// what a click is made of, and sometimes what is wanted -- but the
    /// coefficient has to be computed without dividing by it.
    static constexpr double kMinimumSeconds = 0.0;
    static constexpr double kMaximumSeconds = 30.0;

    /// The overshoot a tension maps to. Geometric, so the control is even: the
    /// interesting half of the range is all below an overshoot of 2, and a
    /// linear map would spend most of its travel between "straight" and
    /// "slightly straighter".
    [[nodiscard]] static double overshootFor (double tension) noexcept
    {
        const double magnitude = std::clamp (std::abs (tension), 0.0, 1.0);

        return kStraightestOvershoot
                 * std::pow (kSharpestOvershoot / kStraightestOvershoot, magnitude);
    }

    /// Where a segment aims, given where it starts, where it must arrive and
    /// how it bends. Public and static because `MultiEnvelope` shares this --
    /// one definition of the tension curve, not two that drift.
    [[nodiscard]] static double targetFor (double from, double to, double tension) noexcept
    {
        const double distance = to - from;
        const double beyond = distance * (overshootFor (tension) - 1.0);

        return tension < 0.0 ? from - beyond : to + beyond;
    }

    /// How many time constants a segment spans for a given overshoot. Shared
    /// with `MultiEnvelope` for the same reason as `targetFor`.
    [[nodiscard]] static double scaleFor (double overshoot) noexcept
    {
        return std::log (overshoot / (overshoot - 1.0));
    }

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
        releaseFrom_ = 0.0;
        heldSamples_ = 0;
        stage_ = AdsrStage::idle;
    }

    // -----------------------------------------------------------------------
    // Controls
    //
    // **Every setter refuses a no-op** -- CLAUDE.md section 7's general rule,
    // and here it is load-bearing rather than hygiene. A synth voice pushes
    // all eight settings at the control rate whether they moved or not, and
    // re-aiming a running segment is not free of consequence: the decay and
    // release aim relative to where they are, so re-aiming them every chunk
    // restarts the curve at its own steep end each time and the exit becomes
    // a geometric crawl -- a release took ~11x its stated time to cross the
    // silence floor, voices retired slower than chords arrived, and the user
    // met the bug as a CPU meter pinned at 100% long after every key was up.
    // -----------------------------------------------------------------------

    void setAttackSeconds (double seconds) noexcept
    {
        const double clamped = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);

        if (isExactly (clamped, attackSeconds_))
            return;

        attackSeconds_ = clamped;
        updateCoefficients();
        refreshCurrentSegment();
    }

    /// How long the envelope sits at full level before the decay begins.
    void setHoldSeconds (double seconds) noexcept
    {
        holdSeconds_ = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);
    }

    void setDecaySeconds (double seconds) noexcept
    {
        const double clamped = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);

        if (isExactly (clamped, decaySeconds_))
            return;

        decaySeconds_ = clamped;
        updateCoefficients();
        refreshCurrentSegment();
    }

    /// The level the envelope holds at while the note is down, 0 to 1.
    void setSustain (double sustain) noexcept
    {
        const double clamped = std::clamp (sustain, 0.0, 1.0);

        if (isExactly (clamped, sustain_))
            return;

        sustain_ = clamped;
        refreshCurrentSegment();
    }

    void setReleaseSeconds (double seconds) noexcept
    {
        const double clamped = std::clamp (seconds, kMinimumSeconds, kMaximumSeconds);

        if (isExactly (clamped, releaseSeconds_))
            return;

        releaseSeconds_ = clamped;
        updateCoefficients();
        refreshCurrentSegment();
    }

    /// Bipolar, -1 to +1. **Positive is the analogue shape** -- fast at first
    /// and decelerating, which is what a capacitor does and what the ear
    /// expects. Negative is the same curve reflected: slow at first,
    /// accelerating. Zero is straight.
    ///
    /// It changes the shape of the segment and **not** how long it lasts -- see
    /// the header.
    void setAttackTension (double tension) noexcept
    {
        const double clamped = std::clamp (tension, -1.0, 1.0);

        if (isExactly (clamped, attackTension_))
            return;

        attackTension_ = clamped;
        updateCoefficients();
        refreshCurrentSegment();
    }

    void setDecayTension (double tension) noexcept
    {
        const double clamped = std::clamp (tension, -1.0, 1.0);

        if (isExactly (clamped, decayTension_))
            return;

        decayTension_ = clamped;
        updateCoefficients();
        refreshCurrentSegment();
    }

    void setReleaseTension (double tension) noexcept
    {
        const double clamped = std::clamp (tension, -1.0, 1.0);

        if (isExactly (clamped, releaseTension_))
            return;

        releaseTension_ = clamped;
        updateCoefficients();
        refreshCurrentSegment();
    }

    [[nodiscard]] double getAttackSeconds() const noexcept { return attackSeconds_; }
    [[nodiscard]] double getHoldSeconds() const noexcept { return holdSeconds_; }
    [[nodiscard]] double getDecaySeconds() const noexcept { return decaySeconds_; }
    [[nodiscard]] double getSustain() const noexcept { return sustain_; }
    [[nodiscard]] double getReleaseSeconds() const noexcept { return releaseSeconds_; }

    [[nodiscard]] double getAttackTension() const noexcept { return attackTension_; }
    [[nodiscard]] double getDecayTension() const noexcept { return decayTension_; }
    [[nodiscard]] double getReleaseTension() const noexcept { return releaseTension_; }

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
        aimAttack();
    }

    /// Releases from wherever the envelope currently is, including mid-attack
    /// or mid-hold.
    void noteOff() noexcept
    {
        if (stage_ == AdsrStage::idle)
            return;

        stage_ = AdsrStage::release;
        releaseFrom_ = level_;
        aimRelease();
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

            case AdsrStage::hold:
                // Counted up rather than down, so lengthening or shortening the
                // hold while it runs takes effect at once and never restarts
                // it. A hold set past where we already are ends immediately,
                // which is the same answer a countdown would give.
                if (++heldSamples_ >= holdSamplesNow())
                    beginDecay();

                return level_;

            case AdsrStage::attack:
                level_ = target_ + (level_ - target_) * coefficient_;

                if (level_ >= 1.0)
                {
                    level_ = 1.0;
                    beginHold();
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

    /// Advances `numSamples` at once and returns the level.
    ///
    /// For a modulation envelope, which runs at the control rate rather than
    /// per sample. Written as a loop rather than a closed form on purpose: a
    /// segment can *end* inside the chunk, and only stepping through it finds
    /// the boundary. A closed form would sail past a 1 ms decay in a 32-sample
    /// chunk and land somewhere the envelope never goes.
    double skip (int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            (void) process();

        return level_;
    }

private:
    /// `ln(T / (T - 1))`: how many time constants a segment aiming at T takes to
    /// cover the distance it was actually asked to cover.
    /// exp(-1 / (tau * fs)), with `tau` derived so the segment lasts `seconds`.
    ///
    /// **Inverted for a negative tension**, which is the whole of the mirror:
    /// receding from a target is approaching it with time running backwards,
    /// and the escape factor that lands exactly on the destination is the
    /// reciprocal of the approach factor. One division here, nothing per sample.
    [[nodiscard]] double coefficientFor (double seconds, double tension) const noexcept
    {
        if (seconds <= 0.0)
            return 0.0;

        const double tau = seconds / scaleFor (overshootFor (tension));
        const double approach = std::exp (-1.0 / (tau * sampleRate_));

        return tension < 0.0 ? 1.0 / approach : approach;
    }

    void updateCoefficients() noexcept
    {
        attackCoefficient_ = coefficientFor (attackSeconds_, attackTension_);
        decayCoefficient_ = coefficientFor (decaySeconds_, decayTension_);
        releaseCoefficient_ = coefficientFor (releaseSeconds_, releaseTension_);
    }

    /// Where a segment travelling `from` -> `to` has to aim.
    ///
    /// Positive tension aims past the destination and approaches it; negative
    /// aims past the *origin* and recedes from it. Both land exactly on `to`
    /// after the stated time.
    /// A zero-length segment must still finish. With no time to travel in, the
    /// coefficient is zero and the level jumps to the target -- which for a
    /// negative tension would be on the wrong side of the destination, so the
    /// target is the destination itself in that case.
    [[nodiscard]] static bool isInstant (double seconds) noexcept { return seconds <= 0.0; }

    void aimAttack() noexcept
    {
        coefficient_ = attackCoefficient_;
        target_ = isInstant (attackSeconds_) ? 1.0 : targetFor (0.0, 1.0, attackTension_);
    }

    /// Aims from the level the release *started* at, not from wherever it now
    /// is -- and that distinction is what lets a release survive being
    /// re-aimed. The target of a segment aimed from the current level shrinks
    /// with the level, so re-aiming it repeatedly (a knob dragged during a
    /// tail, a genuine modulation of the release time) restarts the curve at
    /// its own steep end every time and the exit recedes towards forever.
    /// Aimed from the fixed starting level, the target stays put: a re-aim
    /// with unchanged settings is exactly idempotent, and one with changed
    /// settings still crosses the silence floor within the new stated time.
    void aimRelease() noexcept
    {
        coefficient_ = releaseCoefficient_;
        target_ = isInstant (releaseSeconds_) ? 0.0
                                              : targetFor (releaseFrom_, 0.0, releaseTension_);
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
                aimAttack();
                break;

            case AdsrStage::decay:
                aimDecayAtSustain();
                break;

            case AdsrStage::sustain:
                level_ = sustain_;
                break;

            case AdsrStage::release:
                aimRelease();
                break;

            case AdsrStage::hold:
            case AdsrStage::idle:
            default:
                break;
        }
    }

    [[nodiscard]] int holdSamplesNow() const noexcept
    {
        return static_cast<int> (holdSeconds_ * sampleRate_);
    }

    void beginHold() noexcept
    {
        heldSamples_ = 0;

        if (holdSamplesNow() <= 0)
        {
            beginDecay();
            return;
        }

        stage_ = AdsrStage::hold;
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
        startedDecayAbove_ = level_;

        const double distance = sustain_ - level_;

        if (isExactlyZero (distance))
        {
            level_ = sustain_;
            stage_ = AdsrStage::sustain;
            return;
        }

        target_ = isInstant (decaySeconds_) ? sustain_
                                            : targetFor (level_, sustain_, decayTension_);
    }

    /// Whether the decay has passed the sustain, whichever way it was going.
    ///
    /// Against the *sustain* rather than the target, because the two are on
    /// opposite sides for a negative tension -- a segment receding from a
    /// target behind it still has to stop when it reaches where it was going.
    [[nodiscard]] bool decayHasArrived() const noexcept
    {
        return sustain_ < startedDecayAbove_ ? level_ <= sustain_ : level_ >= sustain_;
    }

    double sampleRate_ { 48000.0 };

    double attackSeconds_ { 0.005 };
    double holdSeconds_ { 0.0 };
    double decaySeconds_ { 0.100 };
    double sustain_ { 0.7 };
    double releaseSeconds_ { 0.200 };

    double attackTension_ { 0.35 };
    double decayTension_ { 0.35 };
    double releaseTension_ { 0.35 };

    double attackCoefficient_ { 0.0 };
    double decayCoefficient_ { 0.0 };
    double releaseCoefficient_ { 0.0 };

    double level_ { 0.0 };
    double target_ { 0.0 };
    double coefficient_ { 0.0 };

    /// Where the decay started from, so "has it arrived" knows which way it is
    /// travelling without inferring it from the target -- which is on the far
    /// side for a negative tension.
    double startedDecayAbove_ { 1.0 };

    /// Where the release started from, so `aimRelease` aims a fixed target
    /// however often it is called -- see its comment.
    double releaseFrom_ { 0.0 };

    int heldSamples_ { 0 };

    AdsrStage stage_ { AdsrStage::idle };
};

} // namespace tezla::dsp
