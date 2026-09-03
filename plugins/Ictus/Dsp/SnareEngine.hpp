// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The snare engine -- one hit of a synthesised snare drum, and with the wires
// at 0 and a lower, longer shell, a tom or a perc.
//
// From the published analysis rather than any product (Reid, "Synthesizing
// Drums: The Snare Drum", Sound On Sound; docs/DSP-REFERENCES.md, "Drum
// synthesis -- Ictus"): a snare's tuned part is two quasi-harmonic series
// plus the (0,1) pair near 180 and 330 Hz that decays far faster than the
// rest; its wires are noise under an envelope; and velocity moves the noise
// filter's cutoff and the tuned/noise mix. Here:
//
//   * The SHELL is three modes of a `ModalResonator` at the ratios
//     1 + (r0 - 1) * spread with r0 = {1, 1.6, 2.2} -- the fundamental, the
//     series' next partial and the fast pair, the article's measurements
//     rounded -- with T60s of Decay x {1, 0.7, 0.5}, so the upper pair dies
//     first as it does on the drum. Spread 0 is one tone (a tom); Tone is how
//     hard the upper two are struck, and at 0 they are not run at all. A
//     `TensionDrop` glides all three down from Start semitones over Drop
//     time, retuning the bank once per control chunk while it moves and once
//     more as it lands, then never again: the landed drum costs three
//     complex multiplies a sample and no transcendentals.
//   * The WIRES are seeded white noise through a state-variable filter --
//     Snappy is its corner, Snap morphs it from high-pass to band-pass --
//     under an AHD envelope killed the moment it lands. RATTLE is the one
//     nonlinearity kept from the physical models (Bilbao's point that the
//     wire interaction is what the linear models miss): the shell's own
//     motion throws the wires, so a second drive on the wires FOLLOWS the
//     shell -- |shell| through a 1 ms one-pole, normalised by the strike,
//     times the amount -- added to the stick's burst rather than scaling
//     it, so with Rattle up the wires buzz for as long as the drum rings
//     and not merely as long as their own envelope lasts. Exact at 0 by
//     branch: the follower is not evaluated and nothing is added.
//   * The CRACK is the kick's click pair (Click.hpp): the stick's contact.
//   * Body and Wires are two levels, summed. Velocity moves the level, the
//     wires' level and corner together (the article's recipe), the crack,
//     and the drop's depth.
//
// EVERYTHING IS SNAPSHOTTED AT `start()`, as in the kick. The gate is the
// kick's idea with one difference: a snare has no amplitude envelope of its
// own -- the shell rings down on its modes' T60s -- so the release is a ramp
// on the whole hit, engaged only at note-off and absent from the ungated
// path. The shell retires when its energy has fallen 120 dB below a unit
// strike, checked once per control chunk, and the cut leaves exact zeros.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/TensionDrop.hpp>
#include <tezla/dsp/UnisonBank.hpp>

#include "Click.hpp"

namespace tezla::ictus {

/// Every snare control, in the units the user thinks in. Copied into the hit
/// at note-on; see the header comment.
struct SnareSettings
{
    // ---- shell ----------------------------------------------------------
    double tuneHz { 180.0 };             ///< the fundamental, 60..800 Hz
    bool   followKey { false };          ///< take the fundamental from the MIDI note instead
    double spread { 1.0 };               ///< 0 = one tone, 1 = the snare's set 1 : 1.6 : 2.2
    double tone { 0.6 };                 ///< how hard the upper two modes are struck, 0..1; 0 runs one mode
    double decaySeconds { 0.25 };        ///< the fundamental's T60, 0.05..2; the others at x0.7 and x0.5
    double startSemitones { 4.0 };       ///< the drop starts this far above, 0..24
    double dropSeconds { 0.02 };         ///< landing time of the drop, 0.002..0.2
    double body { 0.8 };                 ///< the shell's level, 0..1

    // ---- wires ----------------------------------------------------------
    double wires { 0.6 };                ///< the wires' level, 0..1; exactly nothing at 0
    double snappyHz { 3000.0 };          ///< the wires' filter corner, 1000..8000
    double snap { 0.0 };                 ///< 0 = high-pass above the corner, 1 = band-pass at it
    double wiresDecaySeconds { 0.15 };   ///< 0.05..0.4
    double rattle { 0.0 };               ///< how much the shell's motion drives the wires, 0..1

    // ---- crack ----------------------------------------------------------
    double crack { 0.0 };                ///< the stick's contact: resonator level, 0..1
    double crackToneHz { 4000.0 };       ///< its pitch and the burst's corner, 200..8000
    double crackNoise { 0.0 };           ///< noise burst level, 0..1
    double crackNoiseSeconds { 0.0015 }; ///< the burst's fall to -60 dB, 0.0005..0.008

    // ---- level and gate -------------------------------------------------
    double level { 0.8 };                ///< 0..1
    bool   gate { false };               ///< lit: a note-off fades the hit out over `releaseSeconds`
    double releaseSeconds { 0.0 };       ///< 0..2; 0 is a 1 ms cut

    // ---- velocity amounts, all of the form x * ((1 - a) + a * v) --------
    double velocityLevel { 1.0 };
    double velocityWires { 0.4 };        ///< the wires' level AND their corner
    double velocityCrack { 0.6 };
    double velocityDrop { 0.3 };
};

/// The snare engine with the wires off and a lower, longer shell: the Perc
/// pad's defaults, a tom.
[[nodiscard]] inline SnareSettings tomSettings() noexcept
{
    SnareSettings s;
    s.tuneHz = 110.0;
    s.spread = 0.35;
    s.tone = 0.4;
    s.decaySeconds = 0.45;
    s.startSemitones = 7.0;
    s.dropSeconds = 0.04;
    s.wires = 0.0;
    s.crack = 0.15;
    s.crackToneHz = 2500.0;
    return s;
}

class SnareEngine
{
public:
    static constexpr int kModes = 3;

    /// The three modes' frequency ratios at Spread 1 and their T60s as a
    /// fraction of Decay: the SOS article's measured snare, rounded.
    static constexpr double kModeRatios[kModes] { 1.0, 1.6, 2.2 };
    static constexpr double kModeDecays[kModes] { 1.0, 0.7, 0.5 };

    /// The shell is cut exactly once its energy is this far below a unit
    /// strike: -120 dB in amplitude, a millionth. Checked per control chunk.
    static constexpr double kShellEnergyFloor = 1.0e-12;

    /// The rattle follower's time constant.
    static constexpr double kFollowerSeconds = 0.001;

    /// How hard the shell drives the wires at Rattle 1: the follower of a
    /// unit strike, normalised by the strike's sum, peaks near 0.24, so this
    /// puts the shell's drive at about half the stick's burst at the strike
    /// (measured: x1.5 to x1.8 RMS over the first 20 ms, by the burst's own decay).
    static constexpr double kRattleGain = 2.0;

    /// The wires' filter resonance: a little peak at the corner, so a
    /// band-passed snap has a pitch and a high-passed one a presence.
    static constexpr double kWiresResonance = 0.3;

    /// The shortest release -- a note-off with Release at 0 ramps out over
    /// this rather than stepping to zero (the kick's constant).
    static constexpr double kMinimumReleaseSeconds = 0.001;

    /// The wires' random stream is salted so it differs from the crack's,
    /// which gets the hit's seed as it is.
    static constexpr std::uint64_t kWiresSalt = 0x5851F42D4C957F2Dull;

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        shell_.prepare (rate_);
        shell_.setModeCount (kModes);
        drop_.prepare (rate_);

        wiresEnv_.prepare (rate_);
        wiresFilter_.prepare (rate_);
        wiresFilter_.setMode (dsp::SvfMode::highpass);
        wiresFilter_.setResonance (kWiresResonance);

        gateEnv_.prepare (rate_);
        crack_.prepare (rate_);

        followerCoefficient_ = 1.0 - std::exp (-1.0 / (kFollowerSeconds * rate_));

        reset();
    }

    /// Silent and idle; everything cleared so the next hit starts from
    /// nothing.
    void reset() noexcept
    {
        active_ = false;
        shellOn_ = false;
        wiresOn_ = false;
        rattleOn_ = false;
        dropMoving_ = false;
        releasing_ = false;

        shell_.reset();
        drop_.reset();
        wiresEnv_.kill();
        wiresFilter_.reset();
        gateEnv_.kill();
        crack_.reset();

        follower_ = 0.0;
        retunes_ = 0;
    }

    /// Strikes. `fundamentalHz` is the landed pitch of the lowest mode (the
    /// caller resolves Follow key); `velocity` 0..1; `seed` feeds the wires
    /// and the crack; `samplesToBoundary` as in the kick.
    void start (const SnareSettings& s, double fundamentalHz, double velocity,
                std::uint64_t seed, int samplesToBoundary) noexcept
    {
        reset();

        const double v = std::clamp (velocity, 0.0, 1.0);

        // `x * ((1 - a) + a * v)`: exactly x at amount 0 (the kick's form).
        const auto scaled = [v] (double x, double amount) noexcept
        {
            const double a = std::clamp (amount, 0.0, 1.0);
            return x * ((1.0 - a) + a * v);
        };

        // ---- gate, first: the release feeds the envelopes below ----
        gate_ = s.gate;
        release_ = std::max (kMinimumReleaseSeconds, std::clamp (s.releaseSeconds, 0.0, 2.0));

        // ---- shell ----
        fundamental_ = std::clamp (fundamentalHz, 20.0, rate_ * 0.2);

        const double spread = std::clamp (s.spread, 0.0, 1.0);
        const double tone = std::clamp (s.tone, 0.0, 1.0);
        const double decay = std::clamp (s.decaySeconds, 0.05, 2.0);

        for (int mode = 0; mode < kModes; ++mode)
        {
            base_[mode] = fundamental_ * (1.0 + (kModeRatios[mode] - 1.0) * spread);
            t60_[mode] = decay * kModeDecays[mode];
        }

        // Tone 0 strikes only the fundamental, so only it is run.
        shell_.setModeCount (dsp::isExactlyZero (tone) ? 1 : kModes);

        const double start = scaled (std::clamp (s.startSemitones, 0.0, 24.0), s.velocityDrop);
        drop_.trigger (start, std::clamp (s.dropSeconds, 0.002, 0.2));
        dropMoving_ = true;
        applyDrop();

        // A unit strike on the fundamental, `tone` on the upper two: the
        // output level is the Body control's job, below.
        shell_.excite (0, 1.0);

        if (! dsp::isExactlyZero (tone))
        {
            shell_.excite (1, tone);
            shell_.excite (2, tone);
        }

        bodyGain_ = std::clamp (s.body, 0.0, 1.0);
        strikeNorm_ = 1.0 / (1.0 + 2.0 * tone);
        shellOn_ = true;

        // ---- wires ----
        wiresLevel_ = scaled (std::clamp (s.wires, 0.0, 1.0), s.velocityWires);
        wiresOn_ = ! dsp::isExactlyZero (wiresLevel_);

        if (wiresOn_)
        {
            random_.seed (seed ^ kWiresSalt);

            // The corner moves with velocity by the same amount as the level
            // (the SOS mapping): a soft hit is quieter AND duller.
            const double corner = scaled (std::clamp (s.snappyHz, 1000.0, std::min (8000.0, rate_ * 0.4)),
                                          s.velocityWires);
            wiresFilter_.setCutoffHz (corner);

            // From the high-pass (position 1.0) back toward the band-pass
            // (0.5): a morph of -0.5 lands exactly on the band-pass.
            wiresFilter_.setMorph (-0.5 * std::clamp (s.snap, 0.0, 1.0));

            wiresEnv_.setAttackSeconds (0.0);
            wiresEnv_.setAttackTension (0.0);
            wiresEnv_.setHoldSeconds (0.0);
            wiresEnv_.setDecaySeconds (std::clamp (s.wiresDecaySeconds, 0.05, 0.4));
            wiresEnv_.setDecayTension (1.0);
            wiresEnv_.setSustain (0.0);
            wiresEnv_.setReleaseSeconds (release_);
            wiresEnv_.setReleaseTension (1.0);
            wiresEnv_.noteOn();

            rattle_ = std::clamp (s.rattle, 0.0, 1.0);
            rattleOn_ = ! dsp::isExactlyZero (rattle_);
        }

        // ---- crack ----
        crack_.start (scaled (std::clamp (s.crack, 0.0, 1.0), s.velocityCrack), s.crackToneHz,
                      scaled (std::clamp (s.crackNoise, 0.0, 1.0), s.velocityCrack),
                      s.crackNoiseSeconds, seed);

        gain_ = scaled (std::clamp (s.level, 0.0, 1.0), s.velocityLevel);

        active_ = true;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// The control tick: the drop's retune for the chunk that starts here,
    /// and the shell's retirement check.
    void advanceControl (int numSamples) noexcept
    {
        if (! active_ || numSamples <= 0)
            return;

        if (dropMoving_)
        {
            // The modes at THIS chunk's start (a no-op inside the bank when
            // nothing moved), then the drop walks on to where the next chunk
            // begins. Once it has snapped to exactly 1.0 the modes are set
            // to their landed frequencies one last time, and never again.
            applyDrop();

            if (drop_.isActive())
                drop_.advance (numSamples);
            else
                dropMoving_ = false;
        }

        if (shellOn_ && shell_.energy() < kShellEnergyFloor)
        {
            shell_.reset();
            shellOn_ = false;
        }
    }

    /// Note-off. A gated hit ramps the whole thing out over the release
    /// from wherever it is; a one-shot ignores this entirely.
    void release() noexcept
    {
        if (! active_ || ! gate_ || releasing_)
            return;

        // A release envelope parked at 1.0: attack, hold and decay are
        // instant with the sustain at 1, so three samples land it in its
        // sustain stage at exactly 1.0, and the note-off releases from there.
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

    /// One internal sample. Exactly 0.0 when the hit is over.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        double x = 0.0;

        // ---- shell ----
        if (shellOn_)
        {
            const double shell = shell_.process();
            x = shell * bodyGain_;

            if (rattleOn_)
            {
                // The drum's motion, which is what throws the wires.
                const double magnitude = shell < 0.0 ? -shell : shell;
                follower_ += followerCoefficient_ * (magnitude - follower_);
            }
        }

        // ---- wires ----
        if (wiresOn_)
        {
            double gain = 0.0;

            if (wiresEnv_.isActive())
            {
                const double env = wiresEnv_.process();

                // Sustain is 0: arriving there IS the end (the zombie lesson).
                if (wiresEnv_.getStage() == dsp::AdsrStage::sustain)
                    wiresEnv_.kill();

                gain = wiresLevel_ * env;
            }

            // The shell's drive on the wires, on top of the stick's.
            if (rattleOn_)
                gain += wiresLevel_ * rattle_ * kRattleGain * (follower_ * strikeNorm_);

            x += gain * wiresFilter_.process (random_.bipolar());

            // Over when the burst has landed -- unless the shell is still
            // throwing them.
            if (! wiresEnv_.isActive() && ! (rattleOn_ && shellOn_))
                wiresOn_ = false;
        }

        // ---- crack ----
        crack_.addTo (x);

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

        if (! shellOn_ && ! wiresOn_ && ! crack_.isActive())
            active_ = false;

        return x * gain_;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// The fundamental at the last control boundary: the landed pitch times
    /// the drop's multiplier, exactly the landed pitch once it has snapped.
    [[nodiscard]] double currentHz() const noexcept { return base_[0] * drop_.multiplier(); }

    /// How many times the bank has been retuned since the hit started --
    /// the cost claim: it grows while the drop moves and stops when it lands.
    [[nodiscard]] int getRetuneCount() const noexcept { return retunes_; }

    /// The rattle follower's level: exactly 0.0 for a hit with Rattle at 0,
    /// because it is never evaluated.
    [[nodiscard]] double getFollowerLevel() const noexcept { return follower_; }

    [[nodiscard]] bool isShellSounding() const noexcept { return shellOn_; }
    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    void applyDrop() noexcept
    {
        const double multiplier = drop_.multiplier();
        const int modes = shell_.getModeCount();

        for (int mode = 0; mode < modes; ++mode)
            shell_.setMode (mode, base_[mode] * multiplier, t60_[mode], 1.0);

        ++retunes_;
    }

    double rate_ { 48000.0 };
    bool active_ { false };

    // shell
    dsp::ModalResonator shell_;
    dsp::TensionDrop drop_;
    double fundamental_ { 180.0 };
    double base_[kModes] {};
    double t60_[kModes] {};
    double bodyGain_ { 0.8 };
    bool shellOn_ { false };
    bool dropMoving_ { false };
    int retunes_ { 0 };

    // wires
    bool wiresOn_ { false };
    double wiresLevel_ { 0.0 };
    dsp::Adsr wiresEnv_;
    dsp::SvfFilter wiresFilter_;
    dsp::SmallRandom random_;
    bool rattleOn_ { false };
    double rattle_ { 0.0 };
    double follower_ { 0.0 };
    double followerCoefficient_ { 0.0 };
    double strikeNorm_ { 1.0 };

    // crack
    ClickPair crack_;

    // level and gate
    double gain_ { 1.0 };
    bool gate_ { false };
    bool releasing_ { false };
    double release_ { kMinimumReleaseSeconds };
    dsp::Adsr gateEnv_;
};

} // namespace tezla::ictus
