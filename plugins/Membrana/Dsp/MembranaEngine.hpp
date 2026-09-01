// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The microphone stage, assembled:
//
//     in -> CAPSULE+POSITION (per-channel EQ, linked settings)
//        -> PRESENCE (one decision, per-channel shelves)
//        -> DETAIL   (one decision, per-channel splits)
//        -> output trim
//
// Each stage is bypassed by its own predicate branch, so the all-neutral
// setting is bit-identical end to end -- asserted over 40001 samples with a
// denormal-hostile tail, the Phonoss pattern. The dynamics decisions are
// LINKED: one detector reads the larger of the two channels and one gain
// drives both, because an unlinked ride pulls the centre image toward
// whichever channel is momentarily quieter (CLAUDE.md section 7). Swap the
// channels and the outputs swap bit-for-bit; a test says so.
//
// Control cadence: the sample loop is cut at the engine's own
// kControlChunk boundary -- never the host's block edge -- and the capsule
// redesigns (FIR + coefficients, the expensive work) land exactly there.
// The presence stage keeps its own internal 64-sample gain timer and the
// detail stage prices its pow() per sample, so nothing in the signal path
// runs at a block-dependent cadence (Emberdrive's 0.296 lesson).
//
// The path is linear except for the smoothed gain rides, so there is no
// oversampling to manage and latency is 0 -- but section 7's sweep is still
// MEASURED, not assumed: at maximum dynamics settings the worst
// non-fundamental residue of a steady mid-knee tone is pinned by test in
// tezla-tests, and the number is quoted in the phase's commit.

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/SmoothedValue.hpp>

#include "CapsuleEq.hpp"
#include "DetailLift.hpp"
#include "PresenceTracker.hpp"

namespace tezla::membrana {

namespace dsp = tezla::dsp;

class MembranaEngine
{
public:
    static constexpr int kChannels = 2;
    static constexpr int kControlChunk = 64;

    /// Everything the panel controls, grouped the way the panel reads.
    struct Settings
    {
        struct Mic
        {
            bool on { true };
            double pattern01 { 0.5 };      ///< 0 omni, 0.5 cardioid, 1 fig-8
            double capsuleMm { 50.0 };
            double character01 { 0.35 };
            double grille01 { 0.0 };
            double grilleHz { 7000.0 };
            double distanceCm { 100.0 };   ///< 100 = the reference = neutral
            double axisDeg { 0.0 };
            bool autoLevel { true };
            double lowLimitHz { 40.0 };
        } mic;

        struct Presence
        {
            bool on { true };
            double amountDb { 0.0 };
            double frequencyHz { 4500.0 };
            double thresholdDb { -28.0 };
            double track01 { 0.65 };
        } presence;

        struct Detail
        {
            bool on { true };
            double amountDb { 0.0 };
            double splitHz { 3000.0 };
            double floorDb { -55.0 };
        } detail;

        double outputDb { 0.0 };
    };

    /// What the panel's activity lanes read: for each ride, what the curve
    /// ASKED for (the un-smoothed target) beside what is APPLIED (the
    /// smoothed lift) -- the pair that makes the lane show the mechanism
    /// rather than just activity.
    struct Meters
    {
        double presenceLiftDb { 0.0 };
        double presenceTargetDb { 0.0 };
        double detailLiftDb { 0.0 };
        double detailTargetDb { 0.0 };
        double capsuleTrimDb { 0.0 };
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        for (auto& capsule : capsule_)
            capsule.prepare (sampleRate_);

        presence_.prepare (sampleRate_);
        detail_.prepare (sampleRate_);
        outputGain_.prepare (sampleRate_, 0.020);
        outputGain_.setCurrentAndTarget (dsp::dbToGain (settings_.outputDb));
        chunkRemaining_ = 0;
    }

    void reset() noexcept
    {
        for (auto& capsule : capsule_)
            capsule.reset();

        presence_.reset();
        detail_.reset();
        outputGain_.setCurrentAndTarget (dsp::dbToGain (settings_.outputDb));
        chunkRemaining_ = 0;
    }

    void setSettings (const Settings& s) noexcept
    {
        for (auto& capsule : capsule_)
        {
            capsule.setPattern (s.mic.pattern01);
            capsule.setBodyMm (s.mic.capsuleMm);
            capsule.setCharacter (s.mic.character01);
            capsule.setGrille (s.mic.grille01, s.mic.grilleHz);
            capsule.setPosition (s.mic.distanceCm / 100.0, s.mic.axisDeg);
            capsule.setLowLimitHz (s.mic.lowLimitHz);
            capsule.setAutoLevel (s.mic.autoLevel);
        }

        presence_.setEnabled (s.presence.on);
        presence_.setAmountDb (s.presence.amountDb);
        presence_.setFrequencyHz (s.presence.frequencyHz);
        presence_.setThresholdDb (s.presence.thresholdDb);
        presence_.setTrack (s.presence.track01);

        detail_.setEnabled (s.detail.on);
        detail_.setAmountDb (s.detail.amountDb);
        detail_.setSplitHz (s.detail.splitHz);
        detail_.setFloorDb (s.detail.floorDb);

        if (! dsp::isExactly (s.outputDb, settings_.outputDb))
            outputGain_.setTarget (dsp::dbToGain (s.outputDb));

        settings_ = s;
    }

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings_; }

    void process (double* left, double* right, int numSamples) noexcept
    {
        dsp::ScopedNoDenormals noDenormals;

        int n = 0;

        while (n < numSamples)
        {
            // The engine's own control boundary, not the host's.
            if (chunkRemaining_ <= 0)
            {
                chunkRemaining_ = kControlChunk;

                for (auto& capsule : capsule_)
                    capsule.applyChanges();
            }

            const int runLength =
                std::min (numSamples - n, chunkRemaining_);

            const bool micEngaged = settings_.mic.on && ! capsule_[0].isNeutral();
            const bool presenceEngaged = ! presence_.isNeutral();
            const bool detailEngaged = ! detail_.isNeutral();
            const bool trimEngaged = outputGain_.isSmoothing()
                                     || ! dsp::isExactly (outputGain_.getCurrent(), 1.0);

            for (int i = 0; i < runLength; ++i)
            {
                double channel[kChannels] { left[n + i],
                                            right != nullptr ? right[n + i]
                                                             : left[n + i] };

                if (micEngaged)
                    for (int c = 0; c < kChannels; ++c)
                        channel[c] = capsule_[static_cast<std::size_t> (c)]
                                         .process (channel[c]);

                if (presenceEngaged)
                {
                    const double gain = presence_.computeGain (
                        std::max (std::abs (channel[0]), std::abs (channel[1])));

                    for (int c = 0; c < kChannels; ++c)
                        channel[c] = presence_.applyTo (c, channel[c], gain);
                }

                if (detailEngaged)
                {
                    const double gain = detail_.computeGain (
                        std::max (detail_.detectorMagnitude (0, channel[0]),
                                  detail_.detectorMagnitude (1, channel[1])));

                    for (int c = 0; c < kChannels; ++c)
                        channel[c] = detail_.applyTo (c, channel[c], gain);
                }

                if (trimEngaged)
                {
                    const double gain = outputGain_.next();

                    for (double& value : channel)
                        value *= gain;
                }

                left[n + i] = channel[0];

                if (right != nullptr)
                    right[n + i] = channel[1];
            }

            n += runLength;
            chunkRemaining_ -= runLength;
        }
    }

    [[nodiscard]] Meters getMeters() const noexcept
    {
        return { presence_.currentLiftDb(),
                 presence_.currentTargetDb(),
                 detail_.currentLiftDb(),
                 detail_.currentTargetDb(),
                 capsule_[0].trimDb() };
    }

    /// True when these settings make the whole engine the identity, bit for
    /// bit. Static, taking the settings rather than reading the engine, for
    /// the same reason PhonossEngine's is (a panel asking before the first
    /// callback must not answer from defaults).
    [[nodiscard]] static bool isIdentity (const Settings& s) noexcept
    {
        const bool micNeutral =
            ! s.mic.on
            || CapsuleEq::isNeutralFor (s.mic.pattern01, s.mic.distanceCm / 100.0,
                                        s.mic.axisDeg, s.mic.character01,
                                        s.mic.grille01, s.mic.autoLevel);

        const bool presenceNeutral =
            ! s.presence.on || dsp::isExactlyZero (s.presence.amountDb);

        const bool detailNeutral =
            ! s.detail.on || dsp::isExactlyZero (s.detail.amountDb);

        return micNeutral && presenceNeutral && detailNeutral
               && dsp::isExactly (dsp::dbToGain (s.outputDb), 1.0);
    }

    [[nodiscard]] bool isIdentity() const noexcept { return isIdentity (settings_); }

    /// For the editor's curve pane: the composed capsule response from the
    /// same coefficients that play (channel settings are linked, so channel
    /// 0 speaks for both).
    [[nodiscard]] double capsuleRenderedDbAt (double hz) const noexcept
    {
        return capsule_[0].renderedDbAt (hz);
    }

    [[nodiscard]] static int latencySamples() noexcept { return 0; }

private:
    double sampleRate_ { 48000.0 };

    CapsuleEq capsule_[kChannels];
    PresenceTracker presence_;
    DetailLift detail_;
    dsp::SmoothedValue<double> outputGain_;

    Settings settings_ {};
    int chunkRemaining_ { 0 };
};

} // namespace tezla::membrana
