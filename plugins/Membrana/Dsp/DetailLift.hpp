// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The thing a 5 kHz shelf cannot do: bring consonants and breath up against
// the vowels without brightening the vowels -- and never, ever, lift the
// noise floor. A bounded upward expander on the high band, with a floor
// below which nothing rises.
//
// The split is complementary BY CONSTRUCTION: h = x - OnePoleLP(x) at
// detHz, recombined as y = x + g h. No crossover allpass ever touches the
// audio path, so the body of the voice passes through arithmetic-free of
// phase colour, and g = 0 is the identity candidate. (Not the identity
// ITSELF: x + 0 * h flips the sign of -0.0, so neutral is a BRANCH on the
// static predicate -- detail exactly 0, or disabled -- returning the input
// verbatim. The Phonoss pattern, and the -0.0 trap written down again so
// nobody re-trips it.)
//
// A first-order residual leaks: h still carries the vowel's own low band
// at -20 dB/decade below the corner (a 300 Hz body reads ~-20 dB in h at a
// 3 kHz split). Two consequences, handled two ways:
//
//   * The DETECTOR does not listen to h. It listens through its own
//     4th-order Butterworth highpass at the same corner -- steep enough
//     that a vowel ALONE, at any level, reads below the window and the
//     lift stays exactly zero: the vowel-untouched claim is exact, not
//     approximate. The sidechain filter is detector-only, so its phase
//     costs the audio nothing.
//   * While real high-band detail IS being lifted, the leak rides up with
//     it. Measured at maximum detail with a -10 dB vowel and -38 dB
//     consonant bursts: the lift peaks at 3.8 dB and the vowel body rises
//     at most 0.06 dB for the lifted moment, masked by the consonant that
//     caused it. Bounded and stated here rather than hidden; the worst
//     CONCEIVABLE case (full 12 dB of sustained lift, g = 3) bounds the
//     vowel rise at 0.6 dB through the -20 dB leak.
//
// Topology, per the read Reiss paper (docs/DSP-REFERENCES.md, Membrana
// section): feed-forward, level to dB, the static curve, then a smooth
// branching one-pole ON THE LIFT in the log domain -- their Eq (16) family
// with the step-invariant Eq (7) coefficients -- attack 2 ms while the
// lift FALLS (a returning vowel is protected almost instantly), release
// 80 ms while it RISES (detail eases in). One measured deviation from the
// paper's raw-instantaneous detection: this curve is a WINDOW, not a
// compressor's monotone slope, and feeding it |x| sample by sample makes
// the asymmetric smoother hunt to the window's low edge on tonal signals
// (a -38 dB sine settled at 0.96 dB of a nominal 2.9). So the level is a
// 5 ms one-pole mean of the detector magnitude before the curve -- their
// no-attack-lag argument protects peak CATCHING, which an upward lift
// with a structural bound does not need. The paper does not treat upward
// expansion; the curve is ours on their machinery:
//
//     T_d    = floor + 20                    (the detail window's top)
//     first  = s(clamp01((T_d - L) / 15))    (quieter detail lifts more)
//     second = s(clamp01((L - floor) / 6))   (nothing at the floor lifts)
//     lift   = detail * first * second,  s(x) = x^2 (3 - 2x)
//
// Both factors live in [0, 1], so lift <= detail <= +12 dB by
// construction, and the smoother is a convex combination -- the bound
// cannot be overshot, and the sweep test asserts it anyway. Silence sits
// below the floor, calls for zero lift, and zero times zero high band is
// exact zero out.
//
// The dB-to-linear conversion runs per sample here (unlike the presence
// stage's 64-sample timer): a 2 ms attack quantised to 64-sample updates
// would step audibly, and a per-sample pow is what every dynamics stage in
// the suite already pays.

#include <cmath>
#include <cstddef>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Exact.hpp>

namespace tezla::membrana {

class DetailLift
{
public:
    static constexpr int kChannels = 2;
    static constexpr double kWindowDb = 20.0;      // T_d = floor + this
    static constexpr double kKneeDb = 15.0;
    static constexpr double kFloorEaseDb = 6.0;
    static constexpr double kAttackSeconds = 0.002;   // lift falling
    static constexpr double kReleaseSeconds = 0.080;  // lift rising
    static constexpr double kMeanSeconds = 0.005;     // detector magnitude mean
    static constexpr double kSilenceFloorDb = -120.0;
    static constexpr double kSilenceFloorLinear = 1.0e-6;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        attackAlpha_ = std::exp (-1.0 / (kAttackSeconds * sampleRate));
        releaseAlpha_ = std::exp (-1.0 / (kReleaseSeconds * sampleRate));
        meanCoeff_ = 1.0 - std::exp (-1.0 / (kMeanSeconds * sampleRate));
        designSplit();
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < kChannels; ++c)
        {
            lpState_[c] = 0.0;
            sidechain1_[c].reset();
            sidechain2_[c].reset();
        }

        detectorMean_ = 0.0;
        liftSmoothedDb_ = 0.0;
        lastTargetDb_ = 0.0;
        gain_ = 0.0;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }

    /// The Detail knob, in dB (0..12 at the panel).
    void setAmountDb (double db) noexcept { amountDb_ = db; }

    /// The split corner. State-preserving retune: the one-pole coefficient
    /// and the sidechain biquads move, their memories stay.
    void setSplitHz (double hz) noexcept
    {
        if (tezla::dsp::isExactly (hz, splitHz_))
            return;

        splitHz_ = hz;
        designSplit();
    }

    /// The floor, dBFS: at or below it, nothing lifts. Hiss stays down.
    void setFloorDb (double db) noexcept { floorDb_ = db; }

    [[nodiscard]] bool isNeutral() const noexcept
    {
        return ! enabled_ || tezla::dsp::isExactlyZero (amountDb_);
    }

    /// The static curve alone, as a pure function -- the exact values the
    /// tests pin, uncoupled from detectors and smoothing.
    [[nodiscard]] static double curveLiftDb (double detailDb, double floorDb,
                                             double levelDb) noexcept
    {
        const auto eased = [] (double x)
        {
            x = x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x;
            return x * x * (3.0 - 2.0 * x);
        };

        const double first = eased ((floorDb + kWindowDb - levelDb) / kKneeDb);
        const double second = eased ((levelDb - floorDb) / kFloorEaseDb);
        return detailDb * first * second;
    }

    /// The smoothed lift currently applied, for the activity lane.
    [[nodiscard]] double currentLiftDb() const noexcept { return liftSmoothedDb_; }

    /// What the window asked for on the last sample, before the smoother --
    /// display only (see PresenceTracker::currentTargetDb).
    [[nodiscard]] double currentTargetDb() const noexcept { return lastTargetDb_; }

    /// One channel's steep sidechain view of the input -- the linked level
    /// is the max of these across channels.
    [[nodiscard]] double detectorMagnitude (int channel, double x) noexcept
    {
        const auto c = static_cast<std::size_t> (channel);
        return std::abs (sidechain2_[c].process (sidechain1_[c].process (x)));
    }

    /// The shared control path (the Phonoss linking pattern): 5 ms mean,
    /// the curve, the branching smoother, the gain. One decision for all
    /// channels, so the ride cannot pull the image sideways.
    [[nodiscard]] double computeGain (double linkedDetectorMagnitude) noexcept
    {
        detectorMean_ += meanCoeff_ * (linkedDetectorMagnitude - detectorMean_);
        const double levelDb = detectorMean_ > kSilenceFloorLinear
                                   ? 20.0 * std::log10 (detectorMean_)
                                   : kSilenceFloorDb;

        const double liftDb = curveLiftDb (amountDb_, floorDb_, levelDb);
        lastTargetDb_ = liftDb;

        const double alpha = liftDb < liftSmoothedDb_ ? attackAlpha_ : releaseAlpha_;
        liftSmoothedDb_ += (1.0 - alpha) * (liftDb - liftSmoothedDb_);

        gain_ = std::pow (10.0, liftSmoothedDb_ / 20.0) - 1.0;
        return gain_;
    }

    /// One channel's complementary split and recombination with the shared
    /// gain.
    [[nodiscard]] double applyTo (int channel, double x, double gain) noexcept
    {
        const auto c = static_cast<std::size_t> (channel);
        lpState_[c] += lpCoeff_ * (x - lpState_[c]);
        return x + gain * (x - lpState_[c]);
    }

    [[nodiscard]] double process (double x) noexcept
    {
        if (isNeutral())
            return x;   // verbatim: never x + 0 * h

        const double gain = computeGain (detectorMagnitude (0, x));
        return applyTo (0, x, gain);
    }

    void processBlock (double* samples, int count) noexcept
    {
        for (int n = 0; n < count; ++n)
            samples[n] = process (samples[n]);
    }

private:
    void designSplit() noexcept
    {
        // One-pole coefficient from the actual rate (corner << Fs/8).
        lpCoeff_ = 1.0 - std::exp (-2.0 * 3.141592653589793 * splitHz_ / sampleRate_);

        // 4th-order Butterworth highpass: Q pair 0.5412, 1.3066.
        const auto stage1 = tezla::dsp::design::highpass (
            splitHz_, 0.5411961001461969, sampleRate_);
        const auto stage2 = tezla::dsp::design::highpass (
            splitHz_, 1.3065629648763766, sampleRate_);

        for (int c = 0; c < kChannels; ++c)
        {
            sidechain1_[c].setCoefficients (stage1);
            sidechain2_[c].setCoefficients (stage2);
        }
    }

    double sampleRate_ { 48000.0 };
    double attackAlpha_ { 0.0 }, releaseAlpha_ { 0.0 };

    bool enabled_ { true };
    double amountDb_ { 0.0 };
    double splitHz_ { 3000.0 };
    double floorDb_ { -55.0 };

    double lpCoeff_ { 0.0 };
    double lpState_[kChannels] {};
    double meanCoeff_ { 0.0 };
    double detectorMean_ { 0.0 };
    double liftSmoothedDb_ { 0.0 };
    double lastTargetDb_ { 0.0 };
    double gain_ { 0.0 };

    tezla::dsp::Biquad<double> sidechain1_[kChannels], sidechain2_[kChannels];
};

} // namespace tezla::membrana
