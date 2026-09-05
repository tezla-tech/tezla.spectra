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
//      keeps the latest parameters; a hit reads them once. Only the mixing
//      gestures are continuous, and they are smoothed: the master level, and
//      per pad the pan, the width and a room's level. A pan is not part of
//      the hit.
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
//
// I4.4: the pads are placed in the field. Each has a PAN on a BALANCE law --
// at centre both channels carry the pad at unity, exactly the dual mono the
// engine rendered before pans existed; hard left leaves the right channel
// exactly 0.0 -- rather than a constant-power law, whose centre would sit
// 3 dB under the render every saved project was mixed against.
//
// I4.5: the pads render MID and SIDE. Every engine's `process (double& side)`
// returns its mid and writes a side that is exactly 0.0 unless one of its
// spread controls is up (the hats' air and metal, the snare's wires, the
// clap's bursts), so a pad with nothing spread is the mono render bit for
// bit: the engine only forms `mid + side` and `mid - side` when the side is
// not exactly zero. On the side alone, per pad, sit WIDTH (a gain on the
// side, 1.0 exact) and MONO BELOW (a second-order high-pass on the side, so
// the low end stays in the centre whatever is spread above it); then the
// balance pan as before. A ROOM -- shared early reflections
// (dsp::EarlyReflections) fed by the pad's mid -- adds its own mid and side
// to the kick, the snare, the ghost and the clap; and the clap engine can
// play a LAYER under Snare 1, started with it after an offset, so the
// classic snare-plus-clap of the sampled drum machines is one pad.

#include <cstdint>

#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/EarlyReflections.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>
#include <tezla/dsp/SvfFilter.hpp>
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

/// The pads that have a room (I4.5). **Append-only** too: the parameter
/// tables are indexed by it.
enum class RoomIndex
{
    kick1 = 0,
    snare1,
    snare2,
    clap,

    count
};

constexpr int kRoomCount = static_cast<int> (RoomIndex::count);

/// Which pad each room belongs to, in RoomIndex order.
constexpr PadIndex kRoomPads[kRoomCount] = {
    PadIndex::kick1, PadIndex::snare1, PadIndex::snare2, PadIndex::clap
};

/// The room of a pad, or -1 for a pad that has none.
[[nodiscard]] constexpr int roomIndexFor (PadIndex pad) noexcept
{
    for (int room = 0; room < kRoomCount; ++room)
        if (kRoomPads[room] == pad)
            return room;

    return -1;
}

/// One pad's room: early reflections of its mid, added back as mid and side.
struct RoomSettings
{
    double level { 0.0 };     ///< 0..1; exactly nothing at 0 -- the room is not even run
    double seconds { 0.08 };  ///< the reflections' span, 0.01..0.25 -- a booth to a hall's first wall
    double toneHz { 4000.0 }; ///< a low-pass on the reflections, 500 Hz..20 kHz; 20 kHz is off exactly
};

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

    /// Per pad, -1 (hard left) .. +1 (hard right), indexed by PadIndex. A
    /// balance: 0 is both channels at unity, the dual mono the engine always
    /// rendered, bit for bit.
    double pan[kPadCount] {};

    /// Per pad, 0..2: a gain on the pad's SIDE signal only. 1.0 is the field
    /// the pad's own spread controls make, exactly (a multiply by 1.0); 0
    /// folds it to mono; 2 doubles the spread. A pad with nothing spread has
    /// no side and this does nothing at all.
    double width[kPadCount] { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

    /// Per pad, Hz: the pad's side is high-passed here (second order), so
    /// whatever is spread, the low end stays in the centre. 0 is off exactly.
    /// 150 Hz is the default because a club system cannot place anything
    /// below it and a folded sub must not lose level.
    double monoBelowHz[kPadCount] { 150.0, 150.0, 150.0, 150.0, 150.0, 150.0, 150.0, 150.0 };

    /// The rooms, indexed by RoomIndex.
    RoomSettings room[kRoomCount] {};

    /// The clap engine played under Snare 1 as a layer, 0..1: 0 starts no
    /// layer at all. It takes the CLAP page's sound and this level, the
    /// snare's velocity and the snare's placement.
    double snareClap { 0.0 };

    /// How far behind the snare the layer lands, 0..0.05 s: 0 is together.
    double snareClapOffsetSeconds { 0.0 };

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

    /// The clap layer's salt, past the eight pads', so its stream differs
    /// from the clap pad's on the same hit count.
    static constexpr std::uint64_t kSnareClapSalt = 9;

    /// A room's taps are drawn once, from a seed fixed per room, so a room
    /// keeps its shape from hit to hit and from session to session.
    static constexpr std::uint64_t kRoomSeeds[kRoomCount] = {
        0x243F6A8885A308D3ull, 0x13198A2E03707344ull, 0xA4093822299F31D0ull, 0x082EFA98EC4E6C89ull
    };

    /// After a pad's side has been exactly zero for this long its Mono
    /// below filter is reset and skipped, so a pad with nothing spread costs
    /// nothing on the side and renders its mono self bit for bit.
    static constexpr double kSideRingSeconds = 0.5;

    /// Mono below's Q: Butterworth, so the fold is flat above the corner.
    static constexpr double kMonoBelowQ = 0.7071067811865476;

    /// The oversampler's largest factor -- what the rooms' lines are sized
    /// for at prepare(), so a factor change later allocates nothing.
    static constexpr int kMaxFactor = 8;

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

    /// The balance law's two gains for a pan position: the near channel at
    /// exactly 1.0, the far one falling linearly to exactly 0.0 at the end
    /// stop. Centre is 1.0 and 1.0, so a pad at rest renders as it always
    /// did (a multiply by 1.0 is exact).
    [[nodiscard]] static double balanceLeft (double pan) noexcept { return pan > 0.0 ? 1.0 - pan : 1.0; }
    [[nodiscard]] static double balanceRight (double pan) noexcept { return pan < 0.0 ? 1.0 + pan : 1.0; }

    /// A pad's pan as the smoother currently has it, -1..+1.
    [[nodiscard]] double getPanNow (PadIndex pad) const noexcept
    {
        return pan_[static_cast<int> (pad)].getCurrent();
    }

    /// A pad's width as the smoother currently has it, 0..2.
    [[nodiscard]] double getWidthNow (PadIndex pad) const noexcept
    {
        return width_[static_cast<int> (pad)].getCurrent();
    }

    /// Whether a pad's Mono below filter is running this control chunk --
    /// false for a pad whose side has been exactly zero for kSideRingSeconds.
    [[nodiscard]] bool isSideRinging (PadIndex pad) const noexcept
    {
        return sideRing_[static_cast<int> (pad)] > 0;
    }

    [[nodiscard]] const dsp::EarlyReflections& room (RoomIndex index) const noexcept
    {
        return room_[static_cast<int> (index)];
    }

    /// A room's level as the smoother currently has it.
    [[nodiscard]] double getRoomLevelNow (RoomIndex index) const noexcept
    {
        return roomLevel_[static_cast<int> (index)].getCurrent();
    }

    /// The clap layer under Snare 1, and whether one is counting down to
    /// its start.
    [[nodiscard]] const Pad<ClapEngine>& snareClapLayer() const noexcept { return snareClap_; }
    [[nodiscard]] bool isSnareClapPending() const noexcept { return snareClapPending_ >= 0; }

    /// Hits sounding across every pad -- the activity count, not a silence.
    /// The clap layer counts as a hit of its own.
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

    /// Re-draws a pad's room at the length its settings ask for, if that
    /// differs from what it has: done at the pad's note-on, where the new
    /// taps land under a transient rather than across a ringing tail.
    void aimRoom (PadIndex pad) noexcept;

    /// The clap layer: scheduled by a Snare 1 hit, started `offset` internal
    /// samples later at a chunk edge the render loop cuts for it.
    void scheduleSnareClapLayer (int note, double velocity) noexcept;
    void startSnareClapLayer() noexcept;

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
    dsp::SmoothedValue<double> pan_[kPadCount];
    bool pansPrimed_ { false };

    // the field (I4.5)
    dsp::SmoothedValue<double> width_[kPadCount];
    dsp::SvfFilter monoBelow_[kPadCount];
    bool monoBelowOn_[kPadCount] {};
    int sideRing_[kPadCount] {};
    int sideRingSamples_ { 24000 };

    // the rooms
    dsp::EarlyReflections room_[kRoomCount];
    dsp::SmoothedValue<double> roomLevel_[kRoomCount];

    // the clap layer under Snare 1
    Pad<ClapEngine> snareClap_;
    int snareClapPending_ { -1 };
    int snareClapNote_ { -1 };
    double snareClapVelocity_ { 0.0 };

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
