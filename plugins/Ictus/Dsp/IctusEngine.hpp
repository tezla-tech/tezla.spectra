// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The Ictus engine: eight pads on a control grid, rendered at the internal
// (oversampled) rate straight into the oversampler's own buffers, decimated
// once to the host. Framework-free; the JUCE layer unpacks buffers and MIDI
// and calls this.
//
// Two decisions shape everything here (plugins/Ictus/PLAN.md):
//
//   1. Every engine knob is snapshotted into the hit at note-on. The engine
//      keeps the latest parameters; a hit reads them once. Only the master
//      level is continuous, and it is smoothed.
//   2. Everything renders at the internal rate. A drum's click is an 8 kHz
//      resonance and its harmonics come from nonlinear curves; both belong
//      inside the oversampled section, and an instrument has nothing to
//      upsample anyway -- it makes its audio at the internal rate and only
//      the decimation filters run (the Sonitus arrangement).
//
// The render loop is cut at the control boundary, never at the callback's
// (CLAUDE.md section 7): the control grid is the engine's, persists across
// calls, and a note-on between calls lands at whatever offset it lands at --
// the hit gets its own exact pitch endpoints for the partial chunk, so the
// block size cannot bend a sweep and a test holds 64-, 97- and 512-sample
// blocks bit-identical.
//
// I4 shape: every one of the eight pads has an engine -- two kicks, three on
// the snare engine (Snare 1, the ghost, Perc), the two hats sharing one set
// of controls with a decay each, and the clap. The Main bus is still the only
// bus; the four others are declared so that I7 adds them without
// restructuring.

#include <cstdint>

#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/Tuning.hpp>

#include "ClapEngine.hpp"
#include "HatEngine.hpp"
#include "KickEngine.hpp"
#include "Pad.hpp"
#include "SnareEngine.hpp"

namespace tezla::ictus {

/// The eight pads. **Append-only** (CLAUDE.md section 8): a saved routing or
/// choke group stores these as indices.
enum class PadIndex
{
    kick1 = 0,
    snare1,
    hatClosed,
    hatOpen,
    clap,
    perc,
    kick2,
    snare2,

    count
};

constexpr int kPadCount = static_cast<int> (PadIndex::count);

/// General MIDI, so any drum pattern plays without setup: kick 36 (C1),
/// snare 38, closed hat 42, open hat 46, clap 39, rim 37, kick 2 on 35,
/// snare 2 on 40.
constexpr int kDefaultPadNotes[kPadCount] = { 36, 38, 42, 46, 39, 37, 35, 40 };

struct EngineParameters
{
    KickSettings kick1;
    KickSettings kick2;

    SnareSettings snare1;
    SnareSettings snare2;

    /// The Perc pad: the snare engine with tom defaults and the wires off.
    SnareSettings perc { tomSettings() };

    /// One pair of cymbals struck two ways: the closed and open pads share
    /// every control but their decay, which `HatSettings` carries both of.
    HatSettings hat;

    ClapSettings clap;

    /// MIDI note per pad. Held here for the engine; the plugin stores them
    /// as state-tree properties, not parameters (plugins/Ictus/PLAN.md).
    int padNotes[kPadCount] { 36, 38, 42, 46, 39, 37, 35, 40 };

    double masterDb { 0.0 };

    /// Bass mode: the whole keyboard plays Kick 1, tuned to the key through
    /// the tuning (12-TET at A4 = 440 Hz unless a scale is loaded), and the
    /// other pads are silent -- a tuned sub-bass instrument out of the kick.
    bool bassMode { false };

    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };
    dsp::RenderOversampling renderOversampling { dsp::RenderOversampling::sameAsLive };
};

class Engine
{
public:
    /// Control grid, in internal samples: 6 kHz at 192 k, 1.4 kHz at 44.1 k
    /// with oversampling off.
    static constexpr int kControlIntervalSamples = 32;

    /// Once the whole output has been exactly zero for this long with no hit
    /// sounding, rendering and decimation are skipped and the host gets
    /// zeros. The decimators' delay lines are all zeros long before then.
    static constexpr double kIdleSecondsBeforeSkipping = 1.0;
    static constexpr double kIdleThreshold = 1.0e-12;

    /// Per-hit seeds, Malleus's rule: a base, a per-pad salt, and the hit
    /// counter times a golden-ratio constant. Reproducible from prepare(),
    /// never repeating within a session.
    static constexpr std::uint64_t kSeedBase   = 0x1C7A5D2B9E4F6013ull;
    static constexpr std::uint64_t kPadSalt    = 0x9E3779B97F4A7C15ull;
    static constexpr std::uint64_t kHitGolden  = 0xD1B54A32D192ED03ull;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void setParameters (const EngineParameters& parameters) noexcept { parameters_ = parameters; }
    [[nodiscard]] const EngineParameters& getParameters() const noexcept { return parameters_; }

    /// A note strikes every pad mapped to it -- or, in Bass mode, Kick 1 at
    /// the key's pitch. Velocity 0..1.
    void noteOn (int note, double velocity) noexcept;

    /// A note-off releases the gated hit that note started (Gate lit on the
    /// pad); a one-shot pad ignores it.
    void noteOff (int note) noexcept;

    /// Fades every sounding hit over the choke time.
    void allNotesOff() noexcept;

    /// Chokes one pad: every hit it is playing fades over
    /// `Pad::kChokeFadeSeconds`. The one group that fires by itself is the
    /// hats -- a closed hit silences the open pad, which is a foot on the
    /// pedal -- and `HatSettings::choke` is what arms it.
    void choke (PadIndex pad) noexcept;

    void process (double* const* output, int numSamples) noexcept;

    /// The latency this instrument actually has, in host samples.
    ///
    /// `Oversampler::getLatencySamples()` is the ROUND trip -- up and down,
    /// what an effect incurs -- and its tap counts are chosen so that sum is
    /// whole. An instrument writes straight into the internal buffers and
    /// runs only the decimation half, so its true delay is half of that: 23.5
    /// host samples at x2, 31.5 at x4, 35.5 at x8. Measured here on the
    /// first kick (a x4 onset arrived 0.66 ms earlier than the round-trip
    /// figure said). A fractional latency cannot be declared honestly, so
    /// the engine delays its internal signal by half a host sample
    /// (`factor / 2` internal samples) before decimating, and declares the
    /// whole number that results: 24, 32, 36. Zero with oversampling off.
    [[nodiscard]] int getLatencySamples() const noexcept
    {
        return oversampler_.getFactor() > 1 ? (oversampler_.getLatencySamples() + 1) / 2 : 0;
    }
    [[nodiscard]] int getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }
    [[nodiscard]] double getInternalRate() const noexcept { return internalRate_; }

    /// Whether the host is rendering offline, for the render-quality
    /// override. Taken at the next process() like the factor itself.
    void setOffline (bool offline) noexcept { offline_ = offline; }
    [[nodiscard]] bool isOffline() const noexcept { return offline_; }

    [[nodiscard]] dsp::OversamplingMode effectiveOversampling() const noexcept
    {
        return dsp::effectiveOversamplingMode (parameters_.oversampling,
                                               parameters_.renderOversampling, offline_);
    }

    /// Hits sounding across every pad -- the activity count, not a silence.
    [[nodiscard]] int activeHitCount() const noexcept;

    [[nodiscard]] std::uint64_t getHitCount() const noexcept { return hitCount_; }

    /// How many hits a pad has had since prepare(), and the velocity of the
    /// last -- what the editor's pad lamps light from.
    [[nodiscard]] std::uint32_t getPadHitCount (PadIndex pad) const noexcept
    {
        return padHits_[static_cast<int> (pad)];
    }

    [[nodiscard]] double getPadLastVelocity (PadIndex pad) const noexcept
    {
        return padVelocity_[static_cast<int> (pad)];
    }

    [[nodiscard]] const Pad<KickEngine>& kick1() const noexcept { return kick1_; }
    [[nodiscard]] const Pad<KickEngine>& kick2() const noexcept { return kick2_; }
    [[nodiscard]] const Pad<SnareEngine>& snare1() const noexcept { return snare1_; }
    [[nodiscard]] const Pad<SnareEngine>& snare2() const noexcept { return snare2_; }
    [[nodiscard]] const Pad<SnareEngine>& perc() const noexcept { return perc_; }
    [[nodiscard]] const Pad<HatEngine>& hatClosed() const noexcept { return hatClosed_; }
    [[nodiscard]] const Pad<HatEngine>& hatOpen() const noexcept { return hatOpen_; }
    [[nodiscard]] const Pad<ClapEngine>& clap() const noexcept { return clap_; }

    /// The tuning Follow key and Bass mode read the landed pitch from. The
    /// scale swap allocates nothing (the Malleus and Sonitus arrangement).
    [[nodiscard]] dsp::Tuning& tuning() noexcept { return tuning_; }
    [[nodiscard]] const dsp::Tuning& tuning() const noexcept { return tuning_; }
    bool swapScale (dsp::Scale& other) noexcept { return tuning_.swapScale (other); }

private:
    void reconcileFactor() noexcept;
    void rebuildForRate() noexcept;
    void controlTick() noexcept;
    void renderChunk (double* left, double* right, int numSamples) noexcept;

    void startKick (Pad<KickEngine>& pad, PadIndex index, const KickSettings& settings,
                    int note, double velocity, bool keyed) noexcept;
    void startSnare (Pad<SnareEngine>& pad, PadIndex index, const SnareSettings& settings,
                     int note, double velocity) noexcept;
    void startHat (Pad<HatEngine>& pad, PadIndex index, bool open,
                   int note, double velocity) noexcept;
    void startClap (int note, double velocity) noexcept;

    /// The seed rule shared by every pad: a base, a per-pad salt, and the hit
    /// counter times a golden-ratio constant.
    [[nodiscard]] std::uint64_t nextSeed (PadIndex index) noexcept;

    double sampleRate_ { 48000.0 };
    double internalRate_ { 48000.0 };
    int maxBlockSize_ { 512 };
    bool offline_ { false };

    EngineParameters parameters_;

    dsp::Oversampler oversampler_;      // the Main bus

    Pad<KickEngine> kick1_;
    Pad<KickEngine> kick2_;
    Pad<SnareEngine> snare1_;
    Pad<SnareEngine> snare2_;
    Pad<SnareEngine> perc_;
    Pad<HatEngine> hatClosed_;
    Pad<HatEngine> hatOpen_;
    Pad<ClapEngine> clap_;

    dsp::Tuning tuning_;

    dsp::SmoothedValue<double> masterGain_;

    int sinceControl_ { 0 };
    int idleSamples_ { 0 };
    std::uint64_t hitCount_ { 0 };
    std::uint32_t padHits_[kPadCount] {};
    double padVelocity_[kPadCount] {};

    /// The half-host-sample alignment delay: the last `factor / 2` internal
    /// samples of the previous block, per channel. At most 4 (x8).
    double carry_[2][4] {};
};

} // namespace tezla::ictus
