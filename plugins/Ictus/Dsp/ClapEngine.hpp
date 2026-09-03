// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The clap engine -- one hit, which is really several.
//
// A hand clap in a room is not one event. Several people clap at almost the
// same time, so the ear hears a handful of bursts a few milliseconds apart
// and then the room's answer to all of them at once. That structure IS the
// sound, and it is why a clap made from a single noise burst never convinces:
// synthesise the burst pattern and the tail separately and the thing lands.
//
// The recipe is Clark's, from the Nord Modular percussion chapter (read
// first-hand, docs/DSP-REFERENCES.md): "white noise amplitude-modulated by a
// fast envelope fired by four pulses about 11 ms apart". Everything past that
// is what makes it a control panel rather than one sound:
//
//   BURSTS   how many, two to six. Two is a pair of hands; six is a room.
//   FLAM     how far apart, and SKEW whether they crowd together or spread
//            out as they go. Real people do not clap on a grid, and an even
//            pattern is the one thing that always sounds programmed.
//   SNAP     how fast each burst falls. Short is a slap you can count; long
//            smears them into one gesture.
//   BODY     the part the first version had none of. A clap is not only
//            noise: the cupped hands are a cavity, and it rings. Three modes
//            of a `ModalResonator` struck by every burst give the hit a
//            PITCH under the hiss -- PITCH sets it, RING how long it holds.
//            At Body 0 the bank is not run at all.
//   NOISE    the hiss, with its own high-pass (TONE) so it can be a dry slap
//            or a bright spray independently of where the body sits.
//   COLOUR   the band the whole thing is heard through, WIDTH how wide.
//   TAIL     the room, starting with the last burst, with its own TONE as a
//            ratio of Colour -- a room is duller than the hands that fill it.
//   DRIVE    an antialiased soft clip over the sum, for a harder clap.
//   GATE     with RELEASE, the kick's: a note-off fades the whole hit from
//            wherever it is, so a long clap can be stopped by lifting the
//            key rather than by waiting for the room to finish.
//
// EVERYTHING IS SNAPSHOTTED AT `start()`. The tail is a `dsp::Adsr` killed
// the moment it reaches its zero sustain, the bursts are cut to exactly zero
// at a floor, and the body is cut at an energy floor, so a finished clap is
// exact zeros and the pad's activity count is honest (CLAUDE.md section 7).
//
// Humanising the spacing -- which is what makes a real clap different every
// time -- waits for I6, where every pad's deviations are one knob.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/UnisonBank.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::ictus {

/// Counts down, in samples, to each pulse of a burst pattern.
///
/// Sample counts rather than a wall clock: the spacing is set in
/// milliseconds and converted once at note-on, so a clap fired at 44.1 kHz
/// and at 192 kHz has its bursts at the same instants and not merely at
/// similar ones.
///
/// `skewRatio` multiplies each gap by the last: below 1 the bursts crowd
/// together as the hit goes on, above 1 they spread out. Exactly 1 is an even
/// pattern, and the multiply is skipped there so the spacing stays whole.
class BurstScheduler
{
public:
    static constexpr int kMaxBursts = 6;

    /// Arms `count` bursts: the first immediately, the next `spacingSamples`
    /// later, and each gap after that `skewRatio` times the one before.
    void start (int count, double spacingSamples, double skewRatio = 1.0) noexcept
    {
        count_ = std::clamp (count, 0, kMaxBursts);
        next_ = 0;
        countdown_ = 0;
        gap_ = std::max (1.0, spacingSamples);
        skew_ = std::clamp (skewRatio, 0.25, 4.0);
    }

    void reset() noexcept
    {
        count_ = 0;
        next_ = 0;
        countdown_ = 0;
    }

    /// One sample's worth of waiting. Returns the index of the burst that
    /// fires on this sample, or -1 for none.
    [[nodiscard]] int advance() noexcept
    {
        if (next_ >= count_)
            return -1;

        if (countdown_ > 0)
        {
            --countdown_;
            return -1;
        }

        const int fired = next_++;

        countdown_ = std::max (1, static_cast<int> (std::lround (gap_))) - 1;

        if (! dsp::isExactly (skew_, 1.0))
            gap_ = std::clamp (gap_ * skew_, 1.0, 1.0e7);

        return fired;
    }

    /// Whether any burst is still to come.
    [[nodiscard]] bool isPending() const noexcept { return next_ < count_; }

private:
    int count_ { 0 };
    int next_ { 0 };
    int countdown_ { 0 };
    double gap_ { 1.0 };
    double skew_ { 1.0 };
};

/// Every clap control.
struct ClapSettings
{
    // ---- the burst pattern ----------------------------------------------
    int    bursts { 4 };            ///< how many, 2..6; the chapter's four
    double flamSeconds { 0.011 };   ///< the first gap, 0.004..0.030; the chapter's 11 ms
    double skew { 0.0 };            ///< -1 crowding, 0 even, +1 spreading
    double snapSeconds { 0.0035 };  ///< how fast each burst falls, 0.001..0.020

    // ---- the two layers --------------------------------------------------
    double noise { 1.0 };           ///< the hiss's level, 0..1
    double noiseToneHz { 800.0 };   ///< the hiss's own high-pass, 200..8000 Hz
    double body { 0.0 };            ///< the cavity's level, 0..1; 0 runs no bank
    double bodyHz { 900.0 };        ///< the cavity's pitch, 200..2500 Hz
    double bodyRingSeconds { 0.06 };///< how long it holds, 0.01..0.5

    // ---- what it is heard through ---------------------------------------
    double colourHz { 1200.0 };     ///< the band-pass's centre, 300..6000 Hz
    double width { 0.5 };           ///< how wide it is, 0 (narrow) .. 1 (open)
    double drive { 0.0 };           ///< soft clip over the sum, 0..1

    // ---- the room --------------------------------------------------------
    double tailSeconds { 0.18 };    ///< its fall, 0.03..1
    double tailTone { 0.7 };        ///< its band as a ratio of Colour, 0.25..1.5

    double level { 0.75 };          ///< 0..1
    bool   gate { false };          ///< lit: a note-off fades the hit over `releaseSeconds`
    double releaseSeconds { 0.0 };  ///< 0..2; 0 is a 1 ms cut
    double velocityLevel { 1.0 };   ///< x * ((1 - a) + a * v), as everywhere here
};

class ClapEngine
{
public:
    static constexpr int kMaxBursts = BurstScheduler::kMaxBursts;

    /// The cavity's three modes. A pair of cupped hands is not a tube with a
    /// harmonic series -- these are inharmonic, so the body reads as a pitched
    /// knock rather than as a note.
    static constexpr int kBodyModes = 3;
    /// Chosen to sit well away from the integers: 2.13 was only 6 % off a
    /// harmonic 2 and the test that asks for inharmonicity refused it, which
    /// is the test doing its job -- a body close to a harmonic series reads
    /// as a pitched note rather than as a knock.
    static constexpr double kBodyRatios[kBodyModes] { 1.0, 1.57, 2.41 };
    static constexpr double kBodyDecays[kBodyModes] { 1.0, 0.6, 0.4 };
    static constexpr double kBodyEnergyFloor = 1.0e-12;
    static constexpr double kBodyGain = 0.7;

    /// A burst is cut to exactly zero once it is this far down, so a finished
    /// clap leaves exact zeros rather than a denormal trickle.
    static constexpr double kBurstFloor = 1.0e-6;

    /// Width's two ends, as Q -- narrow enough to place the smack, open
    /// enough to keep the bursts four separate slaps.
    static constexpr double kNarrowQ = 3.5;
    static constexpr double kOpenQ = 0.5;

    static constexpr double kDriveRange = 10.0;

    /// How much of the pre-gain is trimmed back off again.
    ///
    /// A clipper's output level depends on where the signal already sat
    /// against the threshold, so no single exponent holds both engines
    /// exactly level: a full 1/g is right for the clap, whose sum peaks at
    /// 0.37 and is barely clipped, and 8 dB too much for the hat, whose
    /// layers already reach 1.5 before the stage. 0.75 is the compromise,
    /// measured: over the whole control the clap moves +2.1 dB and the hat
    /// -2.7 dB, so Drive buys harmonics rather than loudness either way
    /// (CLAUDE.md section 7) without a level detector in the path.
    static constexpr double kDriveTrimExponent = 0.75;

    /// Skew's range, as the ratio each gap is multiplied by: at -1 every gap
    /// is two thirds of the one before, at +1 half again.
    static constexpr double kSkewLow = 0.667;
    static constexpr double kSkewHigh = 1.5;

    /// The shortest release -- a note-off with Release at 0 ramps out over
    /// this rather than stepping to zero (the kick's, snare's and hat's).
    static constexpr double kMinimumReleaseSeconds = 0.001;

    static constexpr std::uint64_t kNoiseSalt = 0x14057B7EF767814Full;

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        colour_.prepare (rate_);
        colour_.setMode (dsp::SvfMode::bandpass);

        tailBand_.prepare (rate_);
        tailBand_.setMode (dsp::SvfMode::bandpass);

        noiseTone_.prepare (rate_);
        noiseTone_.setMode (dsp::SvfMode::highpass);
        noiseTone_.setResonance (dsp::SvfFilter::resonanceForQ (0.707));

        body_.prepare (rate_);
        body_.setModeCount (kBodyModes);

        tail_.prepare (rate_);
        gateEnv_.prepare (rate_);

        reset();
    }

    void reset() noexcept
    {
        active_ = false;
        bodyOn_ = false;
        driveOn_ = false;

        scheduler_.reset();
        colour_.reset();
        tailBand_.reset();
        noiseTone_.reset();
        body_.reset();
        tail_.kill();
        gateEnv_.kill();
        releasing_ = false;
        shaper_.reset();

        for (auto& level : burstLevel_)
            level = 0.0;
    }

    /// Width's Q: geometric between the two ends, so the control is even.
    [[nodiscard]] static double qForWidth (double width) noexcept
    {
        const double w = std::clamp (width, 0.0, 1.0);
        return kNarrowQ * std::pow (kOpenQ / kNarrowQ, w);
    }

    /// Skew's gap ratio: exactly 1.0 at the centre, so an even pattern is
    /// even by construction and not by rounding.
    [[nodiscard]] static double ratioForSkew (double skew) noexcept
    {
        const double s = std::clamp (skew, -1.0, 1.0);

        if (dsp::isExactlyZero (s))
            return 1.0;

        return s < 0.0 ? 1.0 + s * (1.0 - kSkewLow)
                       : 1.0 + s * (kSkewHigh - 1.0);
    }

    /// Strikes. `velocity` 0..1; `seed` feeds the noise; `samplesToBoundary`
    /// as in the kick.
    void start (const ClapSettings& s, double velocity, std::uint64_t seed,
                int samplesToBoundary) noexcept
    {
        reset();

        const double v = std::clamp (velocity, 0.0, 1.0);
        const double amount = std::clamp (s.velocityLevel, 0.0, 1.0);

        // The gate first: it is the one thing a note-off can reach.
        gate_ = s.gate;
        release_ = std::max (kMinimumReleaseSeconds, std::clamp (s.releaseSeconds, 0.0, 2.0));

        bursts_ = std::clamp (s.bursts, 2, kMaxBursts);
        const double flam = std::clamp (s.flamSeconds, 0.004, 0.030);
        scheduler_.start (bursts_, flam * rate_, ratioForSkew (s.skew));

        burstCoefficient_ = std::exp (-6.907755278982137
                                      / (std::clamp (s.snapSeconds, 0.001, 0.020) * rate_));

        // ---- the two layers ----
        noise_ = std::clamp (s.noise, 0.0, 1.0);
        noiseTone_.setCutoffHz (std::clamp (s.noiseToneHz, 200.0, std::min (8000.0, rate_ * 0.4)));

        bodyLevel_ = std::clamp (s.body, 0.0, 1.0);
        bodyOn_ = ! dsp::isExactlyZero (bodyLevel_);

        if (bodyOn_)
        {
            const double pitch = std::clamp (s.bodyHz, 200.0, std::min (2500.0, rate_ * 0.2));
            const double ring = std::clamp (s.bodyRingSeconds, 0.01, 0.5);

            body_.setModeCount (kBodyModes);

            for (int mode = 0; mode < kBodyModes; ++mode)
                body_.setMode (mode, pitch * kBodyRatios[mode], ring * kBodyDecays[mode], 1.0);
        }

        // ---- what it is heard through ----
        const double colour = std::clamp (s.colourHz, 300.0, std::min (6000.0, rate_ * 0.4));
        const double q = dsp::SvfFilter::resonanceForQ (qForWidth (s.width));

        colour_.setResonance (q);
        colour_.setCutoffHz (colour);

        tailBand_.setResonance (q);
        tailBand_.setCutoffHz (std::clamp (colour * std::clamp (s.tailTone, 0.25, 1.5),
                                           100.0, rate_ * 0.4));

        const double drive = std::clamp (s.drive, 0.0, 1.0);
        driveOn_ = ! dsp::isExactlyZero (drive);
        driveGain_ = 1.0 + (kDriveRange - 1.0) * drive;
        driveTrim_ = std::pow (driveGain_, -kDriveTrimExponent);

        tailSeconds_ = std::clamp (s.tailSeconds, 0.03, 1.0);

        random_.seed (seed ^ kNoiseSalt);

        gain_ = std::clamp (s.level, 0.0, 1.0) * ((1.0 - amount) + amount * v);

        active_ = true;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// The control tick: the body's retirement check. The burst pattern is
    /// counted in samples and the tail is an envelope, so neither needs one.
    void advanceControl (int numSamples) noexcept
    {
        if (! active_ || numSamples <= 0)
            return;

        // Only once the pattern is finished. Checked before the first burst
        // had struck it, the bank is silent by definition and the check
        // retired the cavity before it had ever sounded -- which is exactly
        // what happened, and left a body-only clap rendering pure zeros.
        if (bodyOn_ && ! scheduler_.isPending() && body_.energy() < kBodyEnergyFloor)
        {
            body_.reset();
            bodyOn_ = false;
        }
    }

    /// Note-off. With Gate lit the WHOLE hit ramps out over the release from
    /// wherever it is -- the bursts still to come, the cavity's ring and the
    /// room alike -- so a long clap can be stopped by lifting the key. A
    /// one-shot ignores this entirely.
    ///
    /// A ramp on the output rather than a release on the envelopes, for the
    /// hat's reason: the layers are filtered after they are enveloped, so
    /// releasing the envelopes would leave the bands ringing past the key.
    void release() noexcept
    {
        if (! active_ || ! gate_ || releasing_)
            return;

        // A release envelope parked at 1.0: attack, hold and decay instant
        // with the sustain at 1, so three samples land it in its sustain
        // stage at exactly 1.0 and the note-off releases from there.
        gateEnv_.setAttackSeconds (0.0);
        gateEnv_.setHoldSeconds (0.0);
        gateEnv_.setDecaySeconds (0.0);
        gateEnv_.setSustain (1.0);
        gateEnv_.setReleaseSeconds (release_);
        gateEnv_.setReleaseTension (1.0);
        gateEnv_.noteOn();
        (void) gateEnv_.skip (3);
        gateEnv_.noteOff();

        releasing_ = true;
    }

    [[nodiscard]] bool isGated() const noexcept { return gate_; }

    /// One internal sample. Exactly 0.0 once every burst, the body and the
    /// tail have landed.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        if (const int fired = scheduler_.advance(); fired >= 0)
        {
            burstLevel_[static_cast<std::size_t> (fired)] = 1.0;

            // Every burst strikes the cavity as well as the air.
            if (bodyOn_)
                for (int mode = 0; mode < kBodyModes; ++mode)
                    body_.excite (mode, mode == 0 ? 1.0 : 0.6);

            // The last pulse is also the room's: the tail starts with the
            // burst that ends the pattern, not with the note.
            if (fired == bursts_ - 1)
            {
                tail_.setAttackSeconds (0.0);
                tail_.setAttackTension (0.0);
                tail_.setHoldSeconds (0.0);
                tail_.setDecaySeconds (tailSeconds_);
                tail_.setDecayTension (1.0);
                tail_.setSustain (0.0);
                tail_.noteOn();
            }
        }

        double burstEnvelope = 0.0;
        bool burstsSounding = false;

        for (auto& level : burstLevel_)
        {
            if (dsp::isExactlyZero (level))
                continue;

            burstEnvelope += level;
            level *= burstCoefficient_;

            if (level < kBurstFloor)
                level = 0.0;
            else
                burstsSounding = true;
        }

        double tailEnvelope = 0.0;

        if (tail_.isActive())
        {
            tailEnvelope = tail_.process();

            // Sustain is 0: arriving there IS the end.
            if (tail_.getStage() == dsp::AdsrStage::sustain)
                tail_.kill();
        }

        if (! burstsSounding && ! bodyOn_ && ! tail_.isActive() && ! scheduler_.isPending())
        {
            // Everything has landed; the filters' own ring is not a hit.
            reset();
            return 0.0;
        }

        // ---- the hiss, through its own tone, under the burst pattern ----
        const double hiss = noise_ * noiseTone_.process (random_.bipolar());

        // ---- the two bands: the hands, and the room behind them ----
        double x = colour_.process (hiss * burstEnvelope)
                 + tailBand_.process (hiss * tailEnvelope);

        // ---- the cavity, which rings on its own ----
        if (bodyOn_)
            x += kBodyGain * bodyLevel_ * body_.process();

        if (driveOn_)
        {
            // `SoftClipExcess` is what the clipper CHANGES -- clip(x) - x --
            // not the clipped signal, so it is ADDED back to the driven
            // signal to make one. Subtracting it, or worse taking it alone,
            // leaves only the clipping residue: exactly zero below the knee
            // and a harsh remnant above it. That was the first version, and
            // it read as a drive that muted the pad as it was turned down
            // and stripped the tail off as it was turned up.
            //
            // The trim is 1/g, so small signals pass at unity and the control
            // buys harmonics rather than loudness (CLAUDE.md section 7).
            // Measured over the whole range, the clap's RMS moves 0.023 ->
            // 0.023 / 0.022 / 0.021 / 0.019: 1.7 dB, while its peak falls
            // 0.320 -> 0.119, which is the clipping doing its job.
            const double driven = x * driveGain_;

            x = (driven + shaper_.process (driven, clip_)) * driveTrim_;
        }

        x *= gain_;

        // ---- the gate's release ramp ----
        if (releasing_)
        {
            x *= gateEnv_.process();

            if (! gateEnv_.isActive())
            {
                // Landed at exactly 0: the hit is over, whatever was ringing.
                reset();
                return 0.0;
            }
        }

        return x;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// How many bursts this hit was armed with -- what a test counts.
    [[nodiscard]] int getBurstCount() const noexcept { return bursts_; }

    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    double rate_ { 48000.0 };
    bool active_ { false };

    BurstScheduler scheduler_;
    int bursts_ { 4 };
    double burstLevel_[kMaxBursts] {};
    double burstCoefficient_ { 0.0 };

    dsp::Adsr tail_;
    double tailSeconds_ { 0.18 };

    dsp::Adsr gateEnv_;
    bool gate_ { false };
    bool releasing_ { false };
    double release_ { kMinimumReleaseSeconds };

    dsp::SvfFilter colour_, tailBand_, noiseTone_;
    dsp::SmallRandom random_;
    double noise_ { 1.0 };

    dsp::ModalResonator body_;
    bool bodyOn_ { false };
    double bodyLevel_ { 0.0 };

    dsp::Adaa1<dsp::SoftClipExcess> shaper_;
    dsp::SoftClipExcess clip_;
    bool driveOn_ { false };
    double driveGain_ { 1.0 };
    double driveTrim_ { 1.0 };

    double gain_ { 1.0 };
};

} // namespace tezla::ictus
