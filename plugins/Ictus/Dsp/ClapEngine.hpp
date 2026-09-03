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
// same time, so the ear hears three or four bursts a few milliseconds apart
// and then the room's answer to all of them at once. That structure IS the
// sound, and it is why a clap made from a single noise burst never convinces:
// synthesise the burst pattern and the tail separately and the thing lands.
//
// The recipe is Clark's, from the Nord Modular percussion chapter (read
// first-hand, docs/DSP-REFERENCES.md): "white noise amplitude-modulated by a
// fast envelope fired by four pulses about 11 ms apart". Here:
//
//   * `BurstScheduler` counts down to each of the four pulses, in samples,
//     so the spacing is in milliseconds and identical at every host rate.
//   * The first three fire a short exponential burst each; the fourth fires
//     one as well AND starts the TAIL, which is the room.
//   * Their envelopes SUM -- overlapping claps add, they do not replace --
//     and the sum amplitude-modulates one seeded noise source.
//   * The result goes through one band-pass: COLOUR is where the clap sits,
//     and a clap is a mid-band event, all smack and no weight.
//
// EVERYTHING IS SNAPSHOTTED AT `start()`. The tail is a `dsp::Adsr` killed
// the moment it reaches its zero sustain, and the bursts are cut to exactly
// zero at a floor, so a finished clap is exact zeros and the pad's activity
// count is honest (CLAUDE.md section 7).
//
// Humanising the spacing -- which is what makes a real clap different every
// time -- waits for I6, where every pad's deviations are one knob.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/UnisonBank.hpp>

namespace tezla::ictus {

/// Counts down, in samples, to each pulse of a burst pattern.
///
/// Sample counts rather than a wall clock: the spacing is set in
/// milliseconds and converted once at note-on, so a clap fired at 44.1 kHz
/// and at 192 kHz has its bursts at the same instants and not merely at
/// similar ones.
class BurstScheduler
{
public:
    static constexpr int kMaxBursts = 4;

    /// Arms `count` bursts, the first immediately and the rest `spacing`
    /// samples apart.
    void start (int count, double spacingSamples) noexcept
    {
        count_ = std::clamp (count, 0, kMaxBursts);
        next_ = 0;
        countdown_ = 0;
        spacing_ = std::max (1, static_cast<int> (std::lround (spacingSamples)));
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
        countdown_ = spacing_ - 1;

        return fired;
    }

    /// Whether any burst is still to come.
    [[nodiscard]] bool isPending() const noexcept { return next_ < count_; }

private:
    int count_ { 0 };
    int next_ { 0 };
    int countdown_ { 0 };
    int spacing_ { 1 };
};

/// Every clap control.
struct ClapSettings
{
    double flamSeconds { 0.011 };   ///< between the bursts, 0.004..0.030; the chapter's 11 ms
    double colourHz { 1200.0 };     ///< the band-pass's centre, 300..5000 Hz
    double tailSeconds { 0.18 };    ///< the room's fall, 0.03..1
    double level { 0.8 };           ///< 0..1

    double velocityLevel { 1.0 };   ///< x * ((1 - a) + a * v), as everywhere here
};

class ClapEngine
{
public:
    /// Three quick bursts and the fourth that starts the tail: the chapter's
    /// four pulses.
    static constexpr int kBursts = BurstScheduler::kMaxBursts;

    /// How fast one burst falls. Short enough that the bursts read as
    /// separate slaps at the shortest Flam, long enough not to click.
    static constexpr double kBurstSeconds = 0.0035;

    /// A burst is cut to exactly zero once it is this far down, so a finished
    /// clap leaves exact zeros rather than a denormal trickle.
    static constexpr double kBurstFloor = 1.0e-6;

    /// The band-pass's Q: a clap is a band, not a resonance.
    ///
    /// Named as Q for the reason `SvfFilter::resonanceForQ` gives: the first
    /// draft set the CONTROL to 0.8, which is Q 125 and rings for 33 ms at
    /// 1.2 kHz. The four bursts smeared into one another and the spacing test
    /// found twenty-five onsets. Q 1 is a band an octave and a half wide,
    /// which is a clap.
    static constexpr double kColourQ = 1.0;

    static constexpr std::uint64_t kNoiseSalt = 0x14057B7EF767814Full;

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        colour_.prepare (rate_);
        colour_.setMode (dsp::SvfMode::bandpass);
        colour_.setResonance (dsp::SvfFilter::resonanceForQ (kColourQ));

        tail_.prepare (rate_);

        // exp(-1 / (tau * fs)) with tau chosen so the burst falls 60 dB in
        // kBurstSeconds: the same convention the rest of the kit uses.
        burstCoefficient_ = std::exp (-6.907755278982137 / (kBurstSeconds * rate_));

        reset();
    }

    void reset() noexcept
    {
        active_ = false;

        scheduler_.reset();
        colour_.reset();
        tail_.kill();

        for (auto& level : burstLevel_)
            level = 0.0;
    }

    /// Strikes. `velocity` 0..1; `seed` feeds the noise; `samplesToBoundary`
    /// as in the kick.
    void start (const ClapSettings& s, double velocity, std::uint64_t seed,
                int samplesToBoundary) noexcept
    {
        reset();

        const double v = std::clamp (velocity, 0.0, 1.0);
        const double amount = std::clamp (s.velocityLevel, 0.0, 1.0);

        const double flam = std::clamp (s.flamSeconds, 0.004, 0.030);
        scheduler_.start (kBursts, flam * rate_);

        colour_.setCutoffHz (std::clamp (s.colourHz, 300.0, std::min (5000.0, rate_ * 0.4)));

        tailSeconds_ = std::clamp (s.tailSeconds, 0.03, 1.0);

        random_.seed (seed ^ kNoiseSalt);

        gain_ = std::clamp (s.level, 0.0, 1.0) * ((1.0 - amount) + amount * v);

        active_ = true;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// Nothing moves inside a clap hit that is not sample-accurate already:
    /// the burst pattern is counted in samples and the tail is an envelope.
    /// The tick exists because the pad calls it.
    void advanceControl (int) noexcept {}

    /// A clap is a one-shot; a note-off does not shorten it.
    void release() noexcept {}

    /// One internal sample. Exactly 0.0 once the last burst and the tail
    /// have landed.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        if (const int fired = scheduler_.advance(); fired >= 0)
        {
            burstLevel_[static_cast<std::size_t> (fired)] = 1.0;

            // The last pulse is also the room's: the tail starts with the
            // burst that ends the pattern, not with the note.
            if (fired == kBursts - 1)
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

        double envelope = 0.0;
        bool burstsSounding = false;

        for (auto& level : burstLevel_)
        {
            if (dsp::isExactlyZero (level))
                continue;

            envelope += level;
            level *= burstCoefficient_;

            if (level < kBurstFloor)
                level = 0.0;
            else
                burstsSounding = true;
        }

        if (tail_.isActive())
        {
            envelope += tail_.process();

            // Sustain is 0: arriving there IS the end.
            if (tail_.getStage() == dsp::AdsrStage::sustain)
                tail_.kill();
        }

        if (! burstsSounding && ! tail_.isActive() && ! scheduler_.isPending())
        {
            // Everything has landed; the filter's own ring is not a hit.
            reset();
            return 0.0;
        }

        return colour_.process (envelope * random_.bipolar()) * gain_;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    double rate_ { 48000.0 };
    bool active_ { false };

    BurstScheduler scheduler_;
    double burstLevel_[kBursts] {};
    double burstCoefficient_ { 0.0 };

    dsp::Adsr tail_;
    double tailSeconds_ { 0.18 };

    dsp::SvfFilter colour_;
    dsp::SmallRandom random_;

    double gain_ { 1.0 };
};

} // namespace tezla::ictus
