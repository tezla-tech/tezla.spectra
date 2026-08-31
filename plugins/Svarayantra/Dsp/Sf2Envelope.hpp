// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The SoundFont DAHDSR envelope, in the format's own units.
//
// Conversions, per SF2.01 section 8.1.2 (provenance in Sf2Generators.hpp):
//
//   * Times are TIMECENTS: seconds = 2^(tc / 1200), so 0 is one second and
//     each 1200 halves or doubles. Anything at or below -11950 (~0.98 ms) is
//     pinned to instant -- the same pin both reference implementations use,
//     because the format's parsers see -32768 as "as fast as possible".
//   * Hold and decay stretch with key position: effective timecents gain
//     keynumTo{Hold,Decay} x (60 - key), so 100 units doubles the time one
//     octave below middle C and halves it one octave above.
//   * The VOLUME envelope's sustain is in CENTIBELS OF ATTENUATION (gain =
//     10^(-cB/200)); 1000 cB or more is conventionally silence. The
//     MODULATION envelope's sustain is in 0.1% units of its peak instead.
//
// Shape: the attack rises linearly in amplitude to the peak; volume decay
// and release fall LINEARLY IN dB -- at the rate that would traverse the
// format's full 100 dB range in the stated time, so a shallower sustain is
// reached proportionally sooner. The two reference implementations disagree
// here in detail (tsf inherits LinuxSampler's ~80 dB exponential constant,
// FluidSynth ramps a linear envelope against a 960 cB table) -- so this
// implementation follows the specification's own stated convention, and the
// tests pin the dB-per-second slope by measurement. The modulation envelope
// decays linearly in its own 0..1 value domain, full range in the stated
// time, which is what its consumers (pitch and filter cents) expect.
//
// The envelope is configured once, at start(); nothing re-aims a running
// phase (the Adsr no-op-guard lesson, avoided structurally). release() is
// idempotent, and a voice-steal can ask for a faster exit with
// quickRelease() -- ~10 ms by convention.
//
// A voice whose volume envelope lands in sustain at exactly zero gain can
// never become audible again (release only falls), so the envelope declares
// itself finished there rather than sustaining silence for ever -- the
// CPU-zombie lesson: assert activity, not silence.

#include <cmath>
#include <cstdint>

#include <tezla/dsp/Exact.hpp>

#include "Sf2Model.hpp"

namespace tezla::svarayantra {

class Sf2Envelope
{
public:
    enum class Kind
    {
        volume,       // next() is a gain factor
        modulation,   // next() is a 0..1 level
    };

    enum class Phase
    {
        delay,
        attack,
        hold,
        decay,
        sustain,
        release,
        finished,
    };

    /// Configures and restarts the envelope. `key` is the sounding MIDI key,
    /// for the keynum-scaled hold and decay.
    void start (const EnvelopeSpec& spec, Kind kind, int key, double sampleRate) noexcept
    {
        kind_ = kind;
        rate_ = sampleRate;

        delaySamples_ = toSamples (spec.delayTimecents);
        attackSamples_ = toSamples (spec.attackTimecents);
        holdSamples_ = toSamples (spec.holdTimecents
                                    + spec.keynumToHold * (60.0 - key));
        decaySeconds_ = toSeconds (spec.decayTimecents
                                     + spec.keynumToDecay * (60.0 - key));
        releaseSeconds_ = toSeconds (spec.releaseTimecents);

        if (kind == Kind::volume)
        {
            const double cb = spec.sustainLevel < 0.0 ? 0.0 : spec.sustainLevel;
            sustain_ = cb >= 1000.0 ? 0.0 : std::pow (10.0, -cb / 200.0);
        }
        else
        {
            const double fraction = spec.sustainLevel / 1000.0;
            sustain_ = fraction < 0.0 ? 1.0 : fraction > 1.0 ? 0.0 : 1.0 - fraction;
        }

        level_ = 0.0;
        remaining_ = delaySamples_;
        phase_ = Phase::delay;
        decayRatio_ = fallRatio (decaySeconds_);
        releaseStep_ = 0.0;
        advanceThroughEmptyPhases();
    }

    /// The key has gone up. From any phase, the level falls from wherever it
    /// is; calling again changes nothing.
    void release() noexcept
    {
        if (phase_ == Phase::release || phase_ == Phase::finished)
            return;

        beginRelease (releaseSeconds_);
    }

    /// A faster exit for voice stealing: releases over `seconds` from the
    /// current level, however far the normal release had or had not got.
    void quickRelease (double seconds = 0.01) noexcept
    {
        if (phase_ == Phase::finished)
            return;

        beginRelease (seconds);
    }

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] bool isFinished() const noexcept { return phase_ == Phase::finished; }

    /// The level as it stands, without advancing -- for control-rate readers
    /// that sample the envelope at timer boundaries while next() runs at the
    /// audio rate.
    [[nodiscard]] double currentLevel() const noexcept { return level_; }

    /// True once the envelope can never be heard again -- finished, or
    /// sustaining at exactly zero. The voice uses this to retire.
    [[nodiscard]] bool isEffectivelySilent() const noexcept
    {
        return phase_ == Phase::finished
            || (phase_ == Phase::sustain && dsp::isExactlyZero (level_));
    }

    /// One sample of gain (volume) or level (modulation).
    [[nodiscard]] double next() noexcept
    {
        switch (phase_)
        {
            case Phase::delay:
                if (--remaining_ <= 0)
                    enterAttack();
                return 0.0;

            case Phase::attack:
            {
                level_ += attackStep_;

                if (level_ > 1.0)
                    level_ = 1.0;

                if (--remaining_ <= 0)
                    enterHold();

                return level_;
            }

            case Phase::hold:
                if (--remaining_ <= 0)
                    enterDecay();
                return 1.0;

            case Phase::decay:
                if (kind_ == Kind::volume)
                {
                    // A multiplicative fall can never reach a sustain of
                    // exactly zero -- when the sustain sits below the
                    // silence floor the decay ends there, finished.
                    level_ *= decayRatio_;

                    if (level_ <= sustain_)
                    {
                        level_ = sustain_;
                        phase_ = Phase::sustain;
                    }
                    else if (level_ <= kSilence)
                    {
                        level_ = 0.0;
                        phase_ = Phase::finished;
                    }
                }
                else
                {
                    level_ -= decayStep_;

                    if (level_ <= sustain_)
                    {
                        level_ = sustain_;
                        phase_ = Phase::sustain;
                    }
                }

                return level_;

            case Phase::sustain:
                return level_;

            case Phase::release:
                if (kind_ == Kind::volume)
                    level_ *= releaseRatio_;
                else
                    level_ -= releaseStep_;

                if (level_ <= kSilence)
                {
                    level_ = 0.0;
                    phase_ = Phase::finished;
                }

                return level_;

            case Phase::finished:
                return 0.0;
        }

        return 0.0;
    }

private:
    // -100 dB: below the noise floor of the 16-bit data being enveloped.
    static constexpr double kSilence = 1.0e-5;

    [[nodiscard]] static double toSeconds (double timecents) noexcept
    {
        return timecents <= -11950.0 ? 0.0
                                     : std::pow (2.0, timecents / 1200.0);
    }

    [[nodiscard]] std::int64_t toSamples (double timecents) const noexcept
    {
        return static_cast<std::int64_t> (toSeconds (timecents) * rate_ + 0.5);
    }

    /// Per-sample amplitude ratio that traverses the format's full 100 dB
    /// range in `seconds` -- the linear-in-dB fall for decay and release.
    [[nodiscard]] double fallRatio (double seconds) const noexcept
    {
        if (seconds <= 0.0)
            return 0.0;

        return std::pow (10.0, -100.0 / (20.0 * seconds * rate_));
    }

    void enterAttack() noexcept
    {
        remaining_ = attackSamples_;

        if (remaining_ <= 0)
        {
            level_ = 1.0;
            enterHold();
            return;
        }

        phase_ = Phase::attack;
        level_ = 0.0;
        attackStep_ = 1.0 / static_cast<double> (remaining_);
    }

    void enterHold() noexcept
    {
        level_ = 1.0;
        remaining_ = holdSamples_;

        if (remaining_ <= 0)
        {
            enterDecay();
            return;
        }

        phase_ = Phase::hold;
    }

    void enterDecay() noexcept
    {
        level_ = 1.0;

        if (decaySeconds_ <= 0.0 || sustain_ >= 1.0)
        {
            level_ = sustain_;
            phase_ = Phase::sustain;

            if (kind_ == Kind::volume && dsp::isExactlyZero (level_))
                phase_ = Phase::finished;

            return;
        }

        phase_ = Phase::decay;
        decayStep_ = 1.0 / (decaySeconds_ * rate_);   // modulation kind only
    }

    void beginRelease (double seconds) noexcept
    {
        // Entering release during delay or attack releases from the level
        // actually reached -- 0 during delay, partway during attack.
        if (level_ <= kSilence && kind_ == Kind::volume)
        {
            level_ = 0.0;
            phase_ = Phase::finished;
            return;
        }

        if (seconds <= 0.0)
        {
            level_ = 0.0;
            phase_ = Phase::finished;
            return;
        }

        phase_ = Phase::release;
        releaseRatio_ = fallRatio (seconds);
        releaseStep_ = 1.0 / (seconds * rate_);       // modulation kind only
    }

    /// A start() whose delay is zero must not spend a sample emitting the
    /// delay phase's silence -- and likewise straight through a zero attack.
    void advanceThroughEmptyPhases() noexcept
    {
        if (phase_ == Phase::delay && remaining_ <= 0)
            enterAttack();
    }

    Kind kind_ { Kind::volume };
    Phase phase_ { Phase::finished };
    double rate_ { 48000.0 };

    std::int64_t delaySamples_ { 0 };
    std::int64_t attackSamples_ { 0 };
    std::int64_t holdSamples_ { 0 };
    double decaySeconds_ { 0.0 };
    double releaseSeconds_ { 0.0 };
    double sustain_ { 1.0 };

    std::int64_t remaining_ { 0 };
    double level_ { 0.0 };
    double attackStep_ { 0.0 };
    double decayRatio_ { 0.0 };
    double decayStep_ { 0.0 };
    double releaseRatio_ { 0.0 };
    double releaseStep_ { 0.0 };
};

} // namespace tezla::svarayantra
