// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A multi-stage breakpoint envelope: up to sixteen points, a sustain point, a
// loop region, per-segment bipolar tension.
//
// ---------------------------------------------------------------------------
// What it is, and what it is instead of
// ---------------------------------------------------------------------------
//
// The ADV envelopes in Sonitus, in the spirit of the extra assignable
// envelopes an FM workstation offers (workflow inspiration only -- nothing
// here is anyone else's code or curves). An AHDSR describes one gesture:
// rise, sit, fall, hold, fall. A pattern -- a gallop, a pump that breathes in
// threes, a slow bloom with a dip in the middle -- is not one gesture, and
// bending an AHDSR far enough to fake one is how patches become haunted.
//
// A delay stage falls out for free: a first segment that takes its time
// arriving at level 0 is a delay.
//
// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------
//
// Point i holds (seconds, level, tension): the segment that *arrives* at that
// point. Gate on starts from the current level -- 0 from silence, wherever it
// was on a retrigger, the same anti-click rule the AHDSR rework established --
// and travels to point 0, then 1, and so on.
//
// **Sustain index**: arriving there with the gate held parks the envelope.
// The points after it are the release chain, traveled from wherever the
// envelope is when the gate lifts. If the sustain point is the last one, the
// release falls to 0 reusing the final point's time and tension.
//
// **Loop**: with the gate held, arriving at the sustain point travels *back*
// to the loop-start point -- the return leg uses that point's own time and
// tension -- then forward again, indefinitely. The loop period is therefore
// exact: seconds[loopStart] + sum of seconds[loopStart+1 .. sustain].
//
// ---------------------------------------------------------------------------
// Why segments are counted in samples
// ---------------------------------------------------------------------------
//
// The tension arithmetic is Adsr's, shared as statics rather than copied: a
// segment aims past its destination (or recedes from its origin, for the
// mirror) with the time constant derived so it passes through the destination
// at exactly its stated duration. Counting the samples and snapping on the
// boundary therefore lands exactly where the curve does -- it is not a fudge,
// it is the design -- and it makes "a segment lasts its stated time" true by
// construction instead of by threshold detection.
//
// Parameter changes take effect at the next segment boundary, deliberately:
// these are pattern envelopes, and a pattern that reshapes itself mid-leg
// under a control sweep is a pattern that clicks. (The AHDSR keeps its
// re-aiming mid-segment behaviour; the two are different instruments.)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <tezla/dsp/Adsr.hpp>

namespace tezla::dsp
{

class MultiEnvelope
{
public:
    /// Sixteen, which is the length of a bar in sixteenths -- the number that
    /// makes a looping ADV envelope a *pattern* rather than a shape. Eight was
    /// the original ceiling and cost nothing to raise: the arithmetic is per
    /// segment, so the run-time cost is the segments actually used, and the
    /// only price is 3 x 8 more parameters per envelope and 192 bytes of
    /// storage in each one.
    static constexpr int kMaxPoints = 16;
    static constexpr double kMaximumSeconds = 20.0;

    struct Point
    {
        double seconds { 0.1 };
        double level { 0.0 };
        double tension { 0.0 };
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        level_ = 0.0;
        target_ = 0.0;
        coefficient_ = 0.0;
        remaining_ = 0;
        heading_ = -1;
        gate_ = false;
        sustaining_ = false;
        releasing_ = false;
        finished_ = true;
        returning_ = false;
    }

    // -----------------------------------------------------------------------
    // Controls. Stored immediately; a running segment finishes its leg on the
    // values it was aimed with.
    // -----------------------------------------------------------------------

    void setPointCount (int count) noexcept
    {
        count_ = std::clamp (count, 2, kMaxPoints);
    }

    [[nodiscard]] int getPointCount() const noexcept { return count_; }

    void setSustainIndex (int index) noexcept { sustainIndex_ = index; }
    void setLoopStart (int index) noexcept { loopStart_ = index; }
    void setLoop (bool loop) noexcept { loop_ = loop; }

    void setPoint (int index, double seconds, double level, double tension) noexcept
    {
        if (index < 0 || index >= kMaxPoints)
            return;

        auto& point = points_[static_cast<std::size_t> (index)];

        point.seconds = std::clamp (seconds, 0.0, kMaximumSeconds);
        point.level = std::clamp (level, 0.0, 1.0);
        point.tension = std::clamp (tension, -1.0, 1.0);
    }

    [[nodiscard]] const Point& getPoint (int index) const noexcept
    {
        return points_[static_cast<std::size_t> (std::clamp (index, 0, kMaxPoints - 1))];
    }

    // -----------------------------------------------------------------------
    // Gates
    // -----------------------------------------------------------------------

    /// From the current level, always -- 0 from silence, mid-flight on a
    /// retrigger. The anti-click rule.
    void noteOn() noexcept
    {
        gate_ = true;
        sustaining_ = false;
        releasing_ = false;
        finished_ = false;
        returning_ = false;

        aimAt (0);
    }

    void noteOff() noexcept
    {
        gate_ = false;

        if (finished_)
            return;

        beginRelease();
    }

    /// Everything off, now.
    void kill() noexcept { reset(); }

    // -----------------------------------------------------------------------
    // Running
    // -----------------------------------------------------------------------

    [[nodiscard]] double process() noexcept
    {
        if (finished_ || sustaining_)
            return level_;

        if (remaining_ > 0)
        {
            level_ = target_ + coefficient_ * (level_ - target_);
            --remaining_;

            if (remaining_ > 0)
                return level_;
        }

        // The boundary: land exactly, then decide where next.
        arrive();
        return level_;
    }

    /// Runs `numSamples` and returns the final level -- the control-rate tick,
    /// the same shape as Adsr::skip. Parked or finished costs one branch.
    double skip (int numSamples) noexcept
    {
        if (finished_ || sustaining_)
            return level_;

        for (int i = 0; i < numSamples; ++i)
            (void) process();

        return level_;
    }

    [[nodiscard]] double getValue() const noexcept { return level_; }
    [[nodiscard]] bool isFinished() const noexcept { return finished_; }
    [[nodiscard]] bool isSustaining() const noexcept { return sustaining_; }

private:
    [[nodiscard]] int sustainPoint() const noexcept
    {
        return std::clamp (sustainIndex_, 0, count_ - 1);
    }

    [[nodiscard]] int loopPoint() const noexcept
    {
        return std::clamp (loopStart_, 0, sustainPoint());
    }

    /// Aims the segment that arrives at `index`, from the current level.
    void aimAt (int index) noexcept
    {
        heading_ = std::clamp (index, 0, count_ - 1);

        const auto& point = points_[static_cast<std::size_t> (heading_)];

        aimSegment (point.seconds, point.level, point.tension);
    }

    void aimSegment (double seconds, double to, double tension) noexcept
    {
        remaining_ = static_cast<long> (std::llround (seconds * sampleRate_));

        if (remaining_ <= 0)
        {
            // Instant: land now; arrive() runs on the next process() call.
            level_ = to;
            target_ = to;
            coefficient_ = 0.0;
            remaining_ = 1;
            level_ = to;
            pendingLand_ = to;
            return;
        }

        pendingLand_ = to;
        target_ = Adsr::targetFor (level_, to, tension);

        const double tau = seconds / Adsr::scaleFor (Adsr::overshootFor (tension));
        const double approach = std::exp (-1.0 / (tau * sampleRate_));

        coefficient_ = tension < 0.0 ? 1.0 / approach : approach;
    }

    /// The end of a segment: snap to the destination the curve was passing
    /// through anyway, then choose the next leg.
    void arrive() noexcept
    {
        level_ = pendingLand_;

        if (releasing_)
        {
            if (heading_ >= count_ - 1 || releaseToZero_)
            {
                finished_ = true;
                return;
            }

            aimAt (heading_ + 1);
            return;
        }

        const int sustain = sustainPoint();

        if (returning_)
        {
            // The return leg landed on the loop-start point; forward again.
            returning_ = false;

            if (loopPoint() == sustain)
            {
                // A one-point loop: bounce on the spot, one leg at a time.
                returning_ = true;
                aimAt (sustain);
                return;
            }

            aimAt (loopPoint() + 1);
            return;
        }

        if (heading_ == sustain)
        {
            if (loop_ && gate_)
            {
                returning_ = true;
                aimAt (loopPoint());
                return;
            }

            sustaining_ = gate_;

            if (! gate_)
                beginRelease();

            return;
        }

        aimAt (heading_ + 1);
    }

    void beginRelease() noexcept
    {
        releasing_ = true;
        sustaining_ = false;
        returning_ = false;

        const int sustain = sustainPoint();

        if (sustain >= count_ - 1)
        {
            // No points after the sustain: fall to 0 reusing the final
            // point's time and tension.
            releaseToZero_ = true;

            const auto& last = points_[static_cast<std::size_t> (count_ - 1)];
            heading_ = count_ - 1;
            aimSegment (last.seconds, 0.0, last.tension);
            return;
        }

        releaseToZero_ = false;
        aimAt (sustain + 1);
    }

    double sampleRate_ { 48000.0 };

    std::array<Point, static_cast<std::size_t> (kMaxPoints)> points_ {};
    int count_ { 4 };
    int sustainIndex_ { 2 };
    int loopStart_ { 0 };
    bool loop_ { false };

    double level_ { 0.0 };
    double target_ { 0.0 };
    double coefficient_ { 0.0 };
    double pendingLand_ { 0.0 };
    long remaining_ { 0 };
    int heading_ { -1 };

    bool gate_ { false };
    bool sustaining_ { false };
    bool releasing_ { false };
    bool releaseToZero_ { false };
    bool returning_ { false };
    bool finished_ { true };
};

} // namespace tezla::dsp
