// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A pad: one drum, monophonic, holding two hit slots so that a retrigger is
// a crossfade and never a cut.
//
// A drum does not chord with itself, but a fast roll retriggers it while it
// is still sounding, and cutting the old hit dead is a click (CLAUDE.md
// section 7). So the old slot ramps linearly to exactly 0.0 over 1 ms at the
// internal rate and is then reset, while the new hit starts in the other slot
// from its own reset state. Their sum IS the crossfade; no weights, no
// equal-power law. The same fade, over 5 ms, is how a choke group silences a
// pad (a closed hat shutting an open one) -- and a choke landing on a slot
// already fading takes the shorter remaining time, since a fade cannot be
// made longer without becoming a different sound.
//
// A third hit inside the 1 ms fade lands on the slot that is still fading
// and cuts it: the cut is bounded by that slot's remaining fade gain, which
// is where a 1 kHz roll would be inaudible anyway.
//
// `Engine` is the hit's synthesis (KickEngine today); the pad is templated
// rather than virtual so the sum stays a plain loop.

#include <cstdint>
#include <utility>

namespace tezla::ictus {

template <typename Engine>
class Pad
{
public:
    static constexpr double kRetriggerFadeSeconds = 0.001;
    static constexpr double kChokeFadeSeconds = 0.005;

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        for (auto& slot : slots_)
            slot.engine.prepare (rate_);

        reset();
    }

    void reset() noexcept
    {
        for (auto& slot : slots_)
            slot.cut();

        fresh_ = 0;
    }

    /// Starts a hit, fading whatever this pad was already playing. The
    /// arguments are forwarded to `Engine::start`.
    template <typename... Args>
    void start (Args&&... args) noexcept
    {
        Slot& old = slots_[static_cast<std::size_t> (fresh_)];

        if (old.isActive())
            old.fadeOut (kRetriggerFadeSeconds, rate_);

        fresh_ ^= 1;

        Slot& fresh = slots_[static_cast<std::size_t> (fresh_)];
        fresh.cut();
        fresh.engine.start (std::forward<Args> (args)...);
    }

    /// Fades every sounding hit over the choke time.
    void choke() noexcept
    {
        for (auto& slot : slots_)
            if (slot.isActive())
                slot.fadeOut (kChokeFadeSeconds, rate_);
    }

    /// The control tick, forwarded to the hits that are sounding.
    void advanceControl (int numSamples) noexcept
    {
        for (auto& slot : slots_)
            if (slot.engine.isActive())
                slot.engine.advanceControl (numSamples);
    }

    /// One internal sample: the sum of both slots. Exactly 0.0 when idle.
    [[nodiscard]] double process() noexcept
    {
        return slots_[0].process() + slots_[1].process();
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return slots_[0].isActive() || slots_[1].isActive();
    }

    /// How many hits are sounding -- the activity count the tests assert,
    /// rather than a silence they could not distinguish from a zombie.
    [[nodiscard]] int activeHits() const noexcept
    {
        return (slots_[0].isActive() ? 1 : 0) + (slots_[1].isActive() ? 1 : 0);
    }

    /// The engine of the most recently started hit.
    [[nodiscard]] const Engine& freshEngine() const noexcept
    {
        return slots_[static_cast<std::size_t> (fresh_)].engine;
    }

private:
    struct Slot
    {
        Engine engine;
        double fadeGain { 1.0 };
        double fadeStep { 0.0 };
        bool fading { false };

        [[nodiscard]] bool isActive() const noexcept { return engine.isActive() || fading; }

        /// Shortest remaining time wins: a step that would land sooner
        /// replaces one that would land later, never the reverse.
        void fadeOut (double seconds, double rate) noexcept
        {
            const double step = fadeGain / (seconds * rate);
            fadeStep = step > fadeStep ? step : fadeStep;
            fading = true;
        }

        void cut() noexcept
        {
            engine.reset();
            fadeGain = 1.0;
            fadeStep = 0.0;
            fading = false;
        }

        [[nodiscard]] double process() noexcept
        {
            if (! engine.isActive())
            {
                if (fading)
                    cut();

                return 0.0;
            }

            double y = engine.process();

            if (fading)
            {
                y *= fadeGain;
                fadeGain -= fadeStep;

                if (fadeGain <= 0.0)
                    cut();
            }

            return y;
        }
    };

    double rate_ { 48000.0 };
    Slot slots_[2];
    int fresh_ { 0 };
};

} // namespace tezla::ictus
