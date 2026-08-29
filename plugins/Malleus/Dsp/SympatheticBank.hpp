// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The sympathetic strings under the instrument -- a sitar's taraf, a
// hardanger's understrings: long-ringing strings NOT played directly,
// tuned to degrees of the current scale, that pick up whatever the object
// does and answer in tune. This is where the tuning engine bleeds into the
// room: strike anything, and the scale itself shimmers back.
//
// Each string is modelled as its first three partials (1x, 2x, 3x, gains
// falling as 1/partial) in one shared ModalResonator, coupled to the
// object's OUTPUT through the bank's continuous input -- the physical
// arrangement: bridge vibration drives the taraf. The string frequencies
// come from the caller (the voice reads them off dsp::Tuning); this class
// is framework-free and takes Hz.
//
// DRONE closes a feedback loop around the bank: its own output, soft
// clipped, re-enters the input. That is a feedback loop around a
// nonlinearity, so it carries the full section 7 kit:
//
//   * tanh INSIDE the loop -- the level governor. Around resonant poles
//     the small-signal loop gain exceeds one by design (that is what makes
//     a drone sustain); tanh is what turns "grows" into "settles at a
//     level" instead of "diverges".
//   * kDroneCap = 0.9, a hard ceiling below unity on the fed-back
//     AMPLITUDE, applied after the clip, outside the user's reach.
//   * the parameter sweep in test_Sympathetic.cpp covers the whole
//     drone x coupling plane on maximum-length strings and pins the
//     output ceiling; the clip's removal is the break-check's red state.
//
// Silence contract: tanh(0) = 0, so an unexcited bank with full drone
// stays at exactly zero forever -- the loop cannot self-start from
// nothing (section 7).

#include <cmath>

#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/ModalResonator.hpp>

namespace tezla::malleus {

class SympatheticBank
{
public:
    static constexpr int kMaxStrings = 12;
    static constexpr int kPartialsPerString = 3;

    /// The fed-back amplitude never reaches unity, whatever the knob says.
    static constexpr double kDroneCap = 0.9;

    void prepare (double sampleRate) noexcept
    {
        bank_.prepare (sampleRate);
        previous_ = 0.0;
    }

    void reset() noexcept
    {
        bank_.reset();
        previous_ = 0.0;
    }

    /// Tunes the strings: `frequenciesHz[0..count)` are the fundamentals
    /// (the voice reads them off the current scale), `t60Seconds` how long
    /// they ring, `brightness` 0..1 how much of the upper partials speaks.
    /// State-preserving for strings that keep their pitch -- retuning is
    /// the resonator's glide, not a reset.
    void setStrings (const double* frequenciesHz, int count, double t60Seconds,
                     double brightness) noexcept
    {
        stringCount_ = count < 0 ? 0 : count > kMaxStrings ? kMaxStrings : count;

        const double bright = brightness < 0.0 ? 0.0
                            : brightness > 1.0 ? 1.0 : brightness;

        bank_.setModeCount (stringCount_ * kPartialsPerString);

        for (int s = 0; s < stringCount_; ++s)
            for (int p = 0; p < kPartialsPerString; ++p)
            {
                const int mode = s * kPartialsPerString + p;
                const double partial = static_cast<double> (p + 1);

                // Upper partials ring shorter (as string partials do) and
                // enter/leave through brightness -- on the OUTPUT gain
                // only. The drive coupling stays fixed: brightness is a
                // voicing EQ on what the strings say, not a change to how
                // hard the bridge shakes them.
                const double gain = (p == 0 ? 1.0 : bright / partial)
                                  / static_cast<double> (stringCount_);

                bank_.setMode (mode, frequenciesHz[s] * partial,
                               t60Seconds / partial, gain);
                bank_.setInputWeight (mode,
                    1.0 / (partial * static_cast<double> (stringCount_)));
            }
    }

    /// How strongly the object's output drives the strings, 0..1.
    void setCoupling (double coupling) noexcept
    {
        coupling_ = coupling < 0.0 ? 0.0 : coupling > 1.0 ? 1.0 : coupling;
    }

    /// The self-sustain amount, 0..1 of the (already sub-unity) cap.
    void setDrone (double drone) noexcept
    {
        drone_ = drone < 0.0 ? 0.0 : drone > 1.0 ? 1.0 : drone;
    }

    /// One sample: the object's output in, the strings' answer back.
    [[nodiscard]] double process (double objectSample) noexcept
    {
        double feed = coupling_ * objectSample;

        if (! dsp::isExactlyZero (drone_))
            feed += kDroneCap * drone_ * std::tanh (previous_);

        previous_ = bank_.process (feed);
        return previous_;
    }

    [[nodiscard]] double energy() const noexcept { return bank_.energy(); }

    [[nodiscard]] int getStringCount() const noexcept { return stringCount_; }

private:
    dsp::ModalResonator bank_;
    int stringCount_ { 0 };
    double coupling_ { 0.5 };
    double drone_ { 0.0 };
    double previous_ { 0.0 };
};

} // namespace tezla::malleus
