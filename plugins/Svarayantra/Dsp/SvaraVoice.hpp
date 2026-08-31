// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// One sounding zone: sample player, filter, envelopes, pan.
//
// ---------------------------------------------------------------------------
// Pitch: the tuning names the target, the zone names the recording
// ---------------------------------------------------------------------------
//
// A soundfont sample was recorded playing its root key's pitch in standard
// 12-TET at A440 -- that is what the format's originalPitch field asserts.
// The microtuning decides what frequency the played key SHOULD be. So:
//
//     rootHz      = 440 * 2^((rootKey - 69) / 12)     [what the file holds]
//     targetHz    = tuning.frequencyFor(key)          [what the music wants]
//     cents       = 1200 * log2(targetHz / rootHz) * scaleTuning/100
//                   + coarseTune*100 + fineTune + pitchCorrection
//     rate        = 2^(cents/1200) * fileRate / hostRate
//
// scaleTuning scales the tuning's whole displacement from the root, concert
// pitch offset included: at scaleTuning 0 (drum kits) every key plays the
// recording untransposed and A432 does not shift it either -- an unpitched
// drum has no pitch to move. At the default 100 the played frequency IS the
// tuning's frequency, which is the whole point of this instrument.
//
// ---------------------------------------------------------------------------
// Velocity: the spec's default concave curve is the square law
// ---------------------------------------------------------------------------
//
// The format's default velocity-to-attenuation modulator uses its concave
// curve; the equivalent DLS default states it directly as attenuation
// = 20 log10(127^2 / vel^2) dB, i.e. gain = (vel/127)^2. That square law is
// what is implemented here. The SF2 specification PDF could not be fetched
// to verify its concave table matches exactly (recorded in
// docs/DSP-REFERENCES.md); the square law is the derived stand-in and both
// references reach 0 dB at full velocity.
//
// The filter is the shared TPT state-variable, which keeps its corner exact
// at every host rate by construction. A zone whose cutoff sits at the
// format's open-filter ceiling (13500 cents ~ 19.9 kHz) and has no filter
// modulation skips the filter entirely -- most zones, in most fonts, and it
// is what keeps the root-key playback path bit-exact.

#include <cmath>
#include <cstdint>

#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/SvfFilter.hpp>

#include "SamplePlayer.hpp"
#include "Sf2Envelope.hpp"
#include "Sf2Model.hpp"

namespace tezla::svarayantra {

class SvaraVoice
{
public:
    void prepare (double sampleRate) noexcept
    {
        hostRate_ = sampleRate;
        filter_.prepare (sampleRate);
        filter_.setMode (tezla::dsp::SvfMode::lowpass);
        active_ = false;
    }

    /// Starts the zone sounding. `targetHz` is the tuning's frequency for
    /// the played key; `serial` orders voices for stealing; bend and wheel
    /// are the controller state at the moment of the note, so the first
    /// samples already play at the right pitch rather than waiting for the
    /// next control boundary.
    void start (const std::int16_t* pool, const PlayableZone& zone, int key,
                int velocity, double targetHz, std::uint64_t serial,
                double bendCents, double modWheel) noexcept
    {
        zone_ = &zone;
        key_ = key;
        serial_ = serial;
        pendingRelease_ = false;

        const int soundingKey = zone.fixedKey >= 0 ? zone.fixedKey : key;
        const int soundingVelocity =
            zone.fixedVelocity >= 0 ? zone.fixedVelocity : velocity;

        // The fixed pitch mathematics, once per note.
        const double rootHz =
            440.0 * std::exp2 ((zone.rootKey - 69) / 12.0);
        const double tuningCents = 1200.0 * std::log2 (targetHz / rootHz);

        baseCents_ = tuningCents * (zone.scaleTuning / 100.0)
                       + zone.coarseTuneSemitones * 100.0
                       + zone.fineTuneCents
                       + zone.pitchCorrectionCents;
        rateFactor_ = zone.sampleRate / hostRate_;

        player_.start (pool, zone.start, zone.end, zone.loopStart, zone.loopEnd,
                       zone.loopMode);

        volumeEnvelope_.start (zone.volumeEnvelope, Sf2Envelope::Kind::volume,
                               soundingKey, hostRate_);

        // The modulation envelope only runs when something consumes it.
        useModEnv_ = ! dsp::isExactlyZero (zone.modEnvToPitchCents)
                  || ! dsp::isExactlyZero (zone.modEnvToFilterCents);

        if (useModEnv_)
            modulationEnvelope_.start (zone.modulationEnvelope,
                                       Sf2Envelope::Kind::modulation,
                                       soundingKey, hostRate_);

        // Gain: zone attenuation plus the square-law velocity curve.
        const double velocityGain =
            (soundingVelocity / 127.0) * (soundingVelocity / 127.0);
        gain_ = std::pow (10.0, -zone.attenuationCentibels / 200.0) * velocityGain;

        // Equal-power pan, fixed for the note.
        const double angle = (zone.pan + 1.0) * 0.25 * 3.141592653589793;
        panLeft_ = std::cos (angle);
        panRight_ = std::sin (angle);

        // The filter only exists when the font asked for one.
        useFilter_ = zone.filterCutoffCents < 13499.0
                  || ! dsp::isExactlyZero (zone.modEnvToFilterCents);

        if (useFilter_)
        {
            filter_.reset();
            filter_.setResonance (resonanceFor (zone.filterQCentibels));
            filter_.setCutoffHz (centsToHz (zone.filterCutoffCents));
        }

        // Vibrato: the font's own depth plus up to 50 cents from the mod
        // wheel (the format's default modulator), triangle LFO. The phase is
        // derived from the voice's absolute age at each control head -- one
        // multiply and an fmod -- never accumulated chunk by chunk, because
        // accumulation order is chunking order and the output must not
        // depend on the host's buffer size even in the last ulp.
        age_ = 0;
        vibratoDelaySamples_ = static_cast<std::int64_t> (
            timecentsToSeconds (zone.vibLfoDelayTimecents) * hostRate_);
        vibratoIncrement_ = centsToHz (zone.vibLfoFrequencyCents) / hostRate_;

        active_ = true;
        updateTargets (bendCents, modWheel);
    }

    /// The key has gone up (or the pedal that was holding it has). The
    /// sample's until-release loop opens and the volume envelope falls.
    void release() noexcept
    {
        player_.release();
        volumeEnvelope_.release();
        modulationEnvelope_.release();
        pendingRelease_ = false;
    }

    /// The fast exit for an exclusive-class choke: ~10 ms, click-free.
    void quickRelease() noexcept
    {
        player_.release();
        volumeEnvelope_.quickRelease();
        modulationEnvelope_.quickRelease();
        pendingRelease_ = false;
    }

    /// The instant exit for a stolen slot.
    void kill() noexcept { active_ = false; }

    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] bool isReleasing() const noexcept
    {
        return volumeEnvelope_.phase() == Sf2Envelope::Phase::release;
    }
    [[nodiscard]] int key() const noexcept { return key_; }
    [[nodiscard]] std::uint64_t serial() const noexcept { return serial_; }
    [[nodiscard]] int exclusiveClass() const noexcept
    {
        return zone_ != nullptr ? zone_->exclusiveClass : 0;
    }

    /// Marks a note-off deferred by the sustain pedal.
    void deferRelease() noexcept { pendingRelease_ = true; }
    [[nodiscard]] bool hasDeferredRelease() const noexcept { return pendingRelease_; }

    /// Renders one chunk, adding into the buffers. `controlHead` is true
    /// only when the chunk begins at the engine's control-timer boundary --
    /// a chunk cut short by a block edge continues with the targets it has,
    /// so the output cannot depend on the host's buffer size. Pitch and
    /// filter targets move at control heads; the envelopes run per sample.
    void renderAdd (double* left, double* right, int count, bool controlHead,
                    double bendCents, double modWheel) noexcept
    {
        if (! active_)
            return;

        if (controlHead)
            updateTargets (bendCents, modWheel);

        age_ += count;

        // --- the samples.
        for (int i = 0; i < count; ++i)
        {
            double sample = player_.next();

            if (useFilter_)
                sample = filter_.process (sample);

            sample *= volumeEnvelope_.next() * gain_;

            if (useModEnv_)
                (void) modulationEnvelope_.next();

            left[i] += sample * panLeft_;
            right[i] += sample * panRight_;
        }

        // Retire by activity, not silence: a finished player, a finished
        // envelope, or an envelope parked in inaudible sustain all end the
        // voice -- nothing inaudible is allowed to keep costing.
        if (player_.isFinished() || volumeEnvelope_.isEffectivelySilent())
            active_ = false;
    }

private:
    /// Pitch and filter targets from the current controller and modulation
    /// state. Runs at note start and at every control-timer boundary.
    void updateTargets (double bendCents, double modWheel) noexcept
    {
        const double modLevel =
            useModEnv_ ? modulationEnvelope_.currentLevel() : 0.0;

        double vibratoCents = 0.0;
        const double vibratoDepth = zone_->vibLfoToPitchCents + 50.0 * modWheel;

        if (age_ >= vibratoDelaySamples_ && ! dsp::isExactlyZero (vibratoDepth))
        {
            const double turns =
                static_cast<double> (age_ - vibratoDelaySamples_) * vibratoIncrement_;
            vibratoCents = vibratoDepth * triangle (turns - std::floor (turns));
        }

        const double cents = baseCents_ + bendCents + vibratoCents
                               + zone_->modEnvToPitchCents * modLevel;
        player_.setRate (std::exp2 (cents / 1200.0) * rateFactor_);

        if (useFilter_)
            filter_.setCutoffHz (centsToHz (
                zone_->filterCutoffCents + zone_->modEnvToFilterCents * modLevel));
    }

    [[nodiscard]] static double timecentsToSeconds (double timecents) noexcept
    {
        return timecents <= -11950.0 ? 0.0 : std::exp2 (timecents / 1200.0);
    }

    /// Absolute cents to Hz, anchored at 8.176 Hz (MIDI key 0) per the spec.
    [[nodiscard]] static double centsToHz (double cents) noexcept
    {
        return 8.176 * std::exp2 (cents / 1200.0);
    }

    /// SF2 resonance (centibels above the passband) to the shared filter's
    /// geometric resonance control: Q = 10^(cB/200), then inverted through
    /// the filter's own Q map.
    [[nodiscard]] static double resonanceFor (double qCentibels) noexcept
    {
        const double q = std::pow (10.0, qCentibels / 200.0);
        const double clamped = q < 0.5 ? 0.5 : q > 500.0 ? 500.0 : q;
        return std::log (clamped / 0.5) / std::log (500.0 / 0.5);
    }

    /// Triangle in [-1, 1] from phase in [0, 1), starting at 0 rising.
    [[nodiscard]] static double triangle (double phase) noexcept
    {
        const double folded = phase < 0.25 ? phase
                            : phase < 0.75 ? 0.5 - phase
                                           : phase - 1.0;
        return 4.0 * folded;
    }

    SamplePlayer player_;
    Sf2Envelope volumeEnvelope_;
    Sf2Envelope modulationEnvelope_;
    tezla::dsp::SvfFilter filter_;

    const PlayableZone* zone_ { nullptr };
    double hostRate_ { 48000.0 };

    bool active_ { false };
    bool pendingRelease_ { false };
    bool useFilter_ { false };
    bool useModEnv_ { false };
    int key_ { -1 };
    std::uint64_t serial_ { 0 };

    double baseCents_ { 0.0 };
    double rateFactor_ { 1.0 };
    double gain_ { 1.0 };
    double panLeft_ { 0.7071067811865476 };
    double panRight_ { 0.7071067811865476 };

    double vibratoIncrement_ { 0.0 };
    std::int64_t vibratoDelaySamples_ { 0 };
    std::int64_t age_ { 0 };
};

} // namespace tezla::svarayantra
