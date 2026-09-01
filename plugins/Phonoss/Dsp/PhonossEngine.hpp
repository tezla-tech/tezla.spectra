// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The strip, and the order is the product.
//
//     in -> trim -> HPF -> GATE -> DE-ESS -> COMP 1 -> COMP 2 -> EQ -> trim
//
// Five plugins in a row would give the same list of controls. What a strip
// gives that five plugins do not is that the order is decided, the gain
// staging between the stages is decided, and one set of presets makes the
// whole thing work at once. So the order is worth stating and defending:
//
//   **HPF first.** Proximity and rumble should never reach a detector. A
//   plosive that survives to the gate holds it open, and one that survives to
//   the compressor ducks the whole word.
//
//   **Gate before de-ess.** There is no reason to analyse the sibilance of
//   room tone, and a de-esser measuring a noise floor between lines finds
//   plenty of high-frequency energy in it.
//
//   **De-ess before compression.** This is the one that is usually got wrong.
//   A compressor that ducks on an "s" pulls down the word *after* it too,
//   which makes the sibilance louder in relative terms -- so a strip that
//   compresses first needs a harder de-esser to fix damage it caused itself,
//   and that is most of why de-essers get blamed for lisping.
//
//   **Two compressors.** A slow, low-ratio leveller and then a fast peak
//   catcher is how a vocal is actually compressed, and two instances of one
//   class is less code than one clever one.
//
//   **EQ last.** Tone-shaping after the dynamics, so it is not fighting them:
//   a boost before a compressor is an instruction to the compressor.
//
// ---------------------------------------------------------------------------
// Stereo is linked, and that is not a detail
// ---------------------------------------------------------------------------
//
// Every detector in the strip runs **once**, on the linked signal -- the
// larger of the two channels' magnitudes -- and the gain it decides is applied
// to both. CLAUDE.md section 7: independent per-channel dynamics move the
// image every time one channel is louder than the other, which on a doubled
// vocal is constantly, and the result is a lead that wanders. Filters stay per
// channel because a filter is state rather than a decision.
//
// ---------------------------------------------------------------------------
// Everything neutral is exact
// ---------------------------------------------------------------------------
//
// A channel strip is by definition permanently in the path, and section 7's
// rule bites hardest here: seven stages, each of which has to be the identity
// function at its neutral setting or the whole strip is "nearly" transparent
// seven times over. The trims are 0 dB (a gain of exactly 1.0), the HPF is off
// rather than at 1 Hz, the gate's Range is 0, the de-esser's reduction is 0,
// both ratios are 1:1, and every EQ band is at 0 dB -- where a peak or shelf
// biquad's numerator equals its denominator term for term, so the difference
// equation returns its input exactly. A test drives 40001 sample values
// through the assembled strip and compares bit for bit.

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/CompressorCore.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Exact.hpp>

#include "DeEsser.hpp"
#include "Gate.hpp"

namespace tezla::phonoss {

class PhonossEngine
{
public:
    static constexpr int kChannels = 2;

    /// Everything the strip needs, grouped by stage so a call site reads like
    /// the panel does.
    struct Settings
    {
        double inputTrimDb { 0.0 };

        /// 0 switches the high-pass off entirely -- no biquad at all, rather
        /// than one designed at some very low corner.
        double highpassHz { 0.0 };

        struct GateSettings
        {
            double thresholdDb { -45.0 };
            double hysteresisDb { 3.0 };
            double rangeDb { 0.0 };      ///< 0 is bit-exact identity
            double attackMs { 1.0 };
            double holdMs { 40.0 };
            double releaseMs { 120.0 };
            double sidechainHz { 0.0 };
        } gate;

        struct DeEssSettings
        {
            double cornerHz { 6000.0 };
            double thresholdDb { -6.0 };
            double ratio { 4.0 };
            double kneeDb { 3.0 };
            double rangeDb { 0.0 };      ///< 0 is bit-exact identity
            double attackMs { 0.5 };
            double releaseMs { 40.0 };
            bool listen { false };
        } deEss;

        struct CompSettings
        {
            double thresholdDb { -18.0 };
            double ratio { 1.0 };        ///< 1 is bit-exact identity
            double kneeDb { 6.0 };
            double attackMs { 10.0 };
            double releaseMs { 120.0 };
            double makeupDb { 0.0 };
            double mix { 1.0 };
            double sidechainHz { 0.0 };
            bool programDependent { false };
        };

        CompSettings leveller;   ///< COMP 1: slow, low ratio
        CompSettings peak;       ///< COMP 2: fast, catches what is left

        struct EqSettings
        {
            double lowShelfHz { 120.0 };
            double lowShelfDb { 0.0 };
            double midHz { 2500.0 };
            double midQ { 0.9 };
            double midDb { 0.0 };
            double highShelfHz { 8000.0 };
            double highShelfDb { 0.0 };
        } eq;

        double outputTrimDb { 0.0 };
    };

    /// What each stage is doing, for the panel's per-stage meters. The whole
    /// point of a strip's display is showing *which* stage is working.
    struct Meters
    {
        double gateDb { 0.0 };
        double deEssDb { 0.0 };
        double levellerDb { 0.0 };
        double peakDb { 0.0 };
        double sibilanceDb { SibilanceDetector::kSilentDb };
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        gate_.prepare (sampleRate_);
        deEsser_.prepare (sampleRate_);
        leveller_.prepare (sampleRate_);
        peak_.prepare (sampleRate_);

        designFilters();
        reset();
    }

    void reset() noexcept
    {
        gate_.reset();
        deEsser_.reset();
        leveller_.reset();
        peak_.reset();

        for (int channel = 0; channel < kChannels; ++channel)
        {
            highpass_[channel].reset();
            lowShelf_[channel].reset();
            mid_[channel].reset();
            highShelf_[channel].reset();
        }
    }

    void setSettings (const Settings& s) noexcept
    {
        inputGain_ = dsp::dbToGain (s.inputTrimDb);
        outputGain_ = dsp::dbToGain (s.outputTrimDb);

        gate_.setThresholdDb (s.gate.thresholdDb);
        gate_.setHysteresisDb (s.gate.hysteresisDb);
        gate_.setRangeDb (s.gate.rangeDb);
        gate_.setAttackMs (s.gate.attackMs);
        gate_.setHoldMs (s.gate.holdMs);
        gate_.setReleaseMs (s.gate.releaseMs);
        gate_.setSidechainHighpassHz (s.gate.sidechainHz);

        deEsser_.setCornerHz (s.deEss.cornerHz);
        deEsser_.setThresholdDb (s.deEss.thresholdDb);
        deEsser_.setRatio (s.deEss.ratio);
        deEsser_.setKneeDb (s.deEss.kneeDb);
        deEsser_.setRangeDb (s.deEss.rangeDb);
        deEsser_.setAttackMs (s.deEss.attackMs);
        deEsser_.setReleaseMs (s.deEss.releaseMs);
        deEsser_.setListen (s.deEss.listen);

        applyTo (leveller_, s.leveller);
        applyTo (peak_, s.peak);

        // The filters are rebuilt only when a corner or a gain actually moves.
        //
        // This is CPU hygiene and **not** a correctness guard, which is worth
        // stating precisely because CLAUDE.md section 7's rule looks like it
        // applies and does not: `Biquad::setCoefficients` does not clear state,
        // so redesigning a filter mid-stream is safe. Removing the guard leaves
        // every test in `test_PhonossEngine.cpp` green, which is the check that
        // says so.
        //
        // Its worth, micro-benched over two million calls: **173 ns per
        // `setSettings` with the guard against 314 ns without** -- four biquad
        // designs and their trig, saved. That is under 1% of a 480-sample block
        // and it does not show up in the strip's own CPU figure at all; the
        // number is here so the next person does not have to re-derive it
        // before deciding whether eight comparisons earn their place.
        if (! sameFilters (s))
        {
            filters_ = s;
            designFilters();
        }

        settings_ = s;
    }

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings_; }

    /// The strip, sample by sample, with every detector shared between the
    /// channels.
    void process (double* left, double* right, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            double channel[kChannels] { left[n], right != nullptr ? right[n] : left[n] };

            for (double& value : channel)
                value *= inputGain_;

            if (highpassOn_)
                for (int c = 0; c < kChannels; ++c)
                    channel[c] = dsp::snapToZero (highpass_[c].process (channel[c]));

            // GATE -- one detector, one gain, both channels.
            {
                const double gain = gate_.computeGain (linked (channel));

                for (double& value : channel)
                    value = Gate::applyTo (value, gain);
            }

            // DE-ESS -- one decision, per-channel filters.
            {
                const double gain = deEsser_.computeGain (linked (channel));

                for (int c = 0; c < kChannels; ++c)
                    channel[c] = deEsser_.applyTo (c, channel[c], gain);
            }

            // COMP 1, then COMP 2. Each detects from what reaches it, which is
            // what makes the second one a peak catcher rather than a second
            // opinion on the same signal.
            {
                const double gain = leveller_.computeGain (linked (channel));

                for (double& value : channel)
                    value = leveller_.applyTo (value, gain);
            }

            {
                const double gain = peak_.computeGain (linked (channel));

                for (double& value : channel)
                    value = peak_.applyTo (value, gain);
            }

            for (int c = 0; c < kChannels; ++c)
            {
                channel[c] = lowShelf_[c].process (channel[c]);
                channel[c] = mid_[c].process (channel[c]);
                channel[c] = dsp::snapToZero (highShelf_[c].process (channel[c]));
                channel[c] *= outputGain_;
            }

            left[n] = channel[0];

            if (right != nullptr)
                right[n] = channel[1];
        }
    }

    [[nodiscard]] Meters getMeters() const noexcept
    {
        return { gate_.getReductionDb(),
                 deEsser_.getReductionDb(),
                 leveller_.getReductionDb(),
                 peak_.getReductionDb(),
                 deEsser_.getSibilanceDb() };
    }

    /// True when a settings set makes the strip the identity, bit for bit --
    /// which is what the "everything off" preset has to be rather than merely
    /// sound like.
    ///
    /// **Static, and takes the settings rather than reading the engine**, so a
    /// caller can ask about settings that have not been pushed yet. A panel
    /// that asked the engine would answer from whatever the last audio
    /// callback installed, and before the transport has ever run that is the
    /// default-constructed neutral -- so the display would claim bit-exact
    /// transparency for a strip whose controls plainly say otherwise. Crossbar
    /// had this exact bug in its chain readout (CLAUDE.md section 7: what
    /// `prepare` built must be checked against what it actually built, not
    /// against whether anything has been set yet).
    [[nodiscard]] static bool isIdentity (const Settings& s) noexcept
    {
        const auto neutralCompressor = [] (const Settings::CompSettings& c)
        {
            return dsp::isExactly (c.ratio, 1.0)
                     && dsp::isExactlyZero (c.makeupDb)
                     && dsp::isExactly (c.mix, 1.0);
        };

        return dsp::isExactly (dsp::dbToGain (s.inputTrimDb), 1.0)
                 && dsp::isExactly (dsp::dbToGain (s.outputTrimDb), 1.0)
                 && s.highpassHz <= 0.0
                 && dsp::isExactlyZero (s.gate.rangeDb)
                 && dsp::isExactlyZero (s.deEss.rangeDb)
                 && neutralCompressor (s.leveller)
                 && neutralCompressor (s.peak)
                 && dsp::isExactlyZero (s.eq.lowShelfDb)
                 && dsp::isExactlyZero (s.eq.midDb)
                 && dsp::isExactlyZero (s.eq.highShelfDb);
    }

    /// The same question about what the engine is currently set to.
    [[nodiscard]] bool isIdentity() const noexcept { return isIdentity (settings_); }

private:
    /// What the detectors listen to: the larger of the two channels, so the
    /// strip reacts to the loudest thing present rather than to an average
    /// that a hard-panned double would halve.
    [[nodiscard]] static double linked (const double (&channel)[kChannels]) noexcept
    {
        return std::max (std::abs (channel[0]), std::abs (channel[1]));
    }

    static void applyTo (dsp::CompressorCore& compressor,
                         const Settings::CompSettings& s) noexcept
    {
        compressor.setThresholdDb (s.thresholdDb);
        compressor.setRatio (s.ratio);
        compressor.setKneeDb (s.kneeDb);
        compressor.setAttackMs (s.attackMs);
        compressor.setReleaseMs (s.releaseMs);
        compressor.setMakeupDb (s.makeupDb);
        compressor.setMix (s.mix);
        compressor.setSidechainHighpassHz (s.sidechainHz);
        compressor.setProgramDependent (s.programDependent);
    }

    [[nodiscard]] bool sameFilters (const Settings& s) const noexcept
    {
        return dsp::isExactly (s.highpassHz, filters_.highpassHz)
                 && dsp::isExactly (s.eq.lowShelfHz, filters_.eq.lowShelfHz)
                 && dsp::isExactly (s.eq.lowShelfDb, filters_.eq.lowShelfDb)
                 && dsp::isExactly (s.eq.midHz, filters_.eq.midHz)
                 && dsp::isExactly (s.eq.midQ, filters_.eq.midQ)
                 && dsp::isExactly (s.eq.midDb, filters_.eq.midDb)
                 && dsp::isExactly (s.eq.highShelfHz, filters_.eq.highShelfHz)
                 && dsp::isExactly (s.eq.highShelfDb, filters_.eq.highShelfDb);
    }

    void designFilters() noexcept
    {
        highpassOn_ = filters_.highpassHz > 0.0;

        const auto highpass = dsp::design::highpass (
            std::max (filters_.highpassHz, 1.0), 0.707, sampleRate_);

        // At 0 dB a shelf or a peak has a numerator equal to its denominator
        // term for term, so these are the exact identity rather than nearly
        // one -- which is the whole reason `Biquad::normalise` divides by a0
        // rather than multiplying by its reciprocal (CLAUDE.md section 7).
        const auto low = dsp::design::lowShelf (filters_.eq.lowShelfHz, 0.707,
                                                filters_.eq.lowShelfDb, sampleRate_);
        const auto peak = dsp::design::peak (filters_.eq.midHz, filters_.eq.midQ,
                                             filters_.eq.midDb, sampleRate_);
        const auto high = dsp::design::highShelf (filters_.eq.highShelfHz, 0.707,
                                                  filters_.eq.highShelfDb, sampleRate_);

        for (int channel = 0; channel < kChannels; ++channel)
        {
            highpass_[channel].setCoefficients (highpass);
            lowShelf_[channel].setCoefficients (low);
            mid_[channel].setCoefficients (peak);
            highShelf_[channel].setCoefficients (high);
        }
    }

    double sampleRate_ { 44100.0 };

    Gate gate_;
    DeEsser deEsser_;
    dsp::CompressorCore leveller_;
    dsp::CompressorCore peak_;

    dsp::Biquad<double> highpass_[kChannels];
    dsp::Biquad<double> lowShelf_[kChannels];
    dsp::Biquad<double> mid_[kChannels];
    dsp::Biquad<double> highShelf_[kChannels];
    bool highpassOn_ { false };

    double inputGain_ { 1.0 };
    double outputGain_ { 1.0 };

    Settings settings_;

    /// The settings the filters were last designed from.
    ///
    /// Deliberately poisoned so the defaults cannot match it and the first
    /// `setSettings` always designs the filters. A designated initialiser
    /// would say this in one line and warns about every field it leaves
    /// alone, so it is a constructed value instead -- the poisoning is the
    /// point, and a warning about it would be noise.
    Settings filters_ { poisonedFilterSettings() };

    [[nodiscard]] static Settings poisonedFilterSettings() noexcept
    {
        Settings poisoned;
        poisoned.highpassHz = -1.0;
        return poisoned;
    }
};

} // namespace tezla::phonoss
