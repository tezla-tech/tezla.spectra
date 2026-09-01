// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Presence as dynamics, not tone: a high shelf whose gain LEANS IN when the
// singer backs off. A static 4.5 kHz shelf brightens the loud phrases the
// same as the quiet ones -- which is exactly what an engineer riding the
// gear does not do. This stage rides it: full lift on the quiet line, none
// on the shout, and the Track control chooses how much of the shelf is
// ridden versus standing.
//
// Topology, per Giannoulis, Massberg & Reiss (JAES 60(6), 2012 -- read
// first-hand; docs/DSP-REFERENCES.md, Membrana section), their sec 3.2 and
// conclusion: FEED-FORWARD, with the smoothing detector in the LOG domain
// placed AFTER the gain computer. The instantaneous |x| goes to dB, through
// the static curve, and the branching one-pole smooths the computed lift
// itself. What that buys, in their words and our measurements: no attack
// lag, no fixed detector threshold, a guaranteed-smooth return, and the
// knee free to vary; their Fig. 9 makes the same choice the one that
// minimises distortion of the gain modulation. Their Eq (7) is the
// step-invariant coefficient alpha = e^(-1/(tau fs)) whose value can move
// without clicks, and the branching form here is their Eq (16) family moved
// to the log domain: the attack coefficient while the lift FALLS (the
// singer got louder -- back off in 120 ms), the release while it RISES
// (lean in slowly, 400 ms). The paper does not treat upward gain riding;
// the curve below is our own design on their validated machinery.
//
// The curve, with W = 12 dB of knee below the explicit threshold T:
//
//     xc   = clamp01((T - L) / W)
//     s    = xc^2 (3 - 2 xc)                     (Hermite ease)
//     lift = amount * ((1 - track) + track * s)
//
// track = 0 is a static shelf of exactly `amount` dB, track = 1 is fully
// ridden; the lift is bounded by `amount` (itself capped at +9 dB by the
// parameter range) BY CONSTRUCTION -- s and the mix both live in [0, 1] --
// and the smoother is a convex combination, so no transient can overshoot
// the bound. The sweep test asserts it anyway, over the whole space.
//
// Realisation: y = x + g * HP(x) with the TPT SvfFilter highpass -- the ZDF
// structure takes coefficient moves without state stepping -- and
// g = 10^(lift/20) - 1, so the shelf's high side approaches `lift` dB
// asymptotically (a second-order highpass still carries a little phase an
// octave or three above its corner, so the measured gain at 8 x fc reads
// ~0.1 dB shy; the tests probe at 16 x). The pow() that turns dB into g
// runs on its OWN sample-counted timer (64 samples), never per host block:
// a per-block update would make the output depend on the buffer size,
// which is Emberdrive's 0.296 lesson (CLAUDE.md section 7). g is
// additionally smoothed 30 ms against zipper.
//
// Silence: |x| below the -120 dB floor reads as -120, the curve calls for
// full lift, and full lift times zero is zero -- silence in, silence out,
// exactly. Neutral (disabled, or amount exactly 0) is a branch that
// returns the input verbatim, never x + 0 * HP(x): the -0.0 trap.

#include <cmath>
#include <cstddef>

#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/SvfFilter.hpp>

namespace tezla::membrana {

class PresenceTracker
{
public:
    static constexpr int kChannels = 2;
    static constexpr double kKneeDb = 12.0;
    static constexpr double kAttackSeconds = 0.120;    // lift falling
    static constexpr double kReleaseSeconds = 0.400;   // lift rising
    static constexpr double kGainSmoothingSeconds = 0.030;
    static constexpr int kGainUpdateSamples = 64;      // pow() cadence, sample-counted
    static constexpr double kSilenceFloorDb = -120.0;
    static constexpr double kSilenceFloorLinear = 1.0e-6;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        attackAlpha_ = std::exp (-1.0 / (kAttackSeconds * sampleRate));
        releaseAlpha_ = std::exp (-1.0 / (kReleaseSeconds * sampleRate));
        for (auto& shelf : shelf_)
        {
            shelf.prepare (sampleRate);
            shelf.setMode (tezla::dsp::SvfMode::highpass);
            shelf.setCutoffHz (frequencyHz_);
        }
        gain_.prepare (sampleRate, kGainSmoothingSeconds);
        reset();
    }

    void reset() noexcept
    {
        for (auto& shelf : shelf_)
            shelf.reset();

        liftSmoothedDb_ = 0.0;
        gain_.setCurrentAndTarget (0.0);
        gainCountdown_ = 1;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }

    /// The Presence knob, in dB (0..9 at the panel).
    void setAmountDb (double db) noexcept { amountDb_ = db; }

    /// Shelf corner. State-preserving: the TPT structure takes it live.
    void setFrequencyHz (double hz) noexcept
    {
        if (tezla::dsp::isExactly (hz, frequencyHz_))
            return;

        frequencyHz_ = hz;

        for (auto& shelf : shelf_)
            shelf.setCutoffHz (hz);
    }

    /// The explicit threshold, dBFS. Levels at or above it get no ridden
    /// lift; the knee eases in over the 12 dB below it.
    void setThresholdDb (double db) noexcept { thresholdDb_ = db; }

    /// 0 = static shelf, 1 = fully ridden.
    void setTrack (double track01) noexcept { track_ = track01; }

    [[nodiscard]] bool isNeutral() const noexcept
    {
        return ! enabled_ || tezla::dsp::isExactlyZero (amountDb_);
    }

    /// The smoothed lift the shelf is currently applying, for the panel's
    /// activity lane and the curve tests.
    [[nodiscard]] double currentLiftDb() const noexcept { return liftSmoothedDb_; }

    /// The shared control path: one decision for however many channels
    /// listen (the Phonoss linking pattern -- an unlinked gain ride would
    /// pull the centre image sideways, CLAUDE.md section 7). Feed it the
    /// LINKED magnitude -- max of the channels -- and apply the returned
    /// gain to every channel.
    [[nodiscard]] double computeGain (double linkedMagnitude) noexcept
    {
        // Instantaneous level in dB, floored so silence cannot produce
        // -inf. The smoother after the curve does the averaging a detector
        // would otherwise do -- that placement is the paper's point.
        const double levelDb = linkedMagnitude > kSilenceFloorLinear
                                   ? 20.0 * std::log10 (linkedMagnitude)
                                   : kSilenceFloorDb;

        // Static curve.
        double xc = (thresholdDb_ - levelDb) / kKneeDb;
        xc = xc < 0.0 ? 0.0 : xc > 1.0 ? 1.0 : xc;
        const double eased = xc * xc * (3.0 - 2.0 * xc);
        const double liftDb = amountDb_ * ((1.0 - track_) + track_ * eased);

        // Branching smooth one-pole in the log domain (their Eq 16 family):
        // attack while the lift falls, release while it rises.
        const double alpha = liftDb < liftSmoothedDb_ ? attackAlpha_ : releaseAlpha_;
        liftSmoothedDb_ += (1.0 - alpha) * (liftDb - liftSmoothedDb_);

        // dB -> linear on the sample-counted timer, never per block.
        if (--gainCountdown_ <= 0)
        {
            gainCountdown_ = kGainUpdateSamples;
            gain_.setTarget (std::pow (10.0, liftSmoothedDb_ / 20.0) - 1.0);
        }

        return gain_.next();
    }

    /// One channel's shelf, driven by the shared gain.
    [[nodiscard]] double applyTo (int channel, double x, double gain) noexcept
    {
        return x + gain * shelf_[static_cast<std::size_t> (channel)].process (x);
    }

    [[nodiscard]] double process (double x) noexcept
    {
        if (isNeutral())
            return x;   // verbatim: the -0.0 trap forbids x + 0 * HP(x)

        return applyTo (0, x, computeGain (std::abs (x)));
    }

    void processBlock (double* samples, int count) noexcept
    {
        for (int n = 0; n < count; ++n)
            samples[n] = process (samples[n]);
    }

private:
    double sampleRate_ { 48000.0 };
    double attackAlpha_ { 0.0 }, releaseAlpha_ { 0.0 };

    bool enabled_ { true };
    double amountDb_ { 0.0 };
    double frequencyHz_ { 4500.0 };
    double thresholdDb_ { -28.0 };
    double track_ { 0.65 };

    double liftSmoothedDb_ { 0.0 };
    int gainCountdown_ { 1 };

    tezla::dsp::SvfFilter shelf_[kChannels];
    tezla::dsp::SmoothedValue<double> gain_;
};

} // namespace tezla::membrana
