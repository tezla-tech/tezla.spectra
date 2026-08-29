// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// From parsed chunks to playable zones: the SoundFont generator model.
//
// The format's resolution rules, per SF2.01 sections 7 and 9.4 (semantics
// cross-checked against FluidSynth and tsf.h -- see Sf2Generators.hpp for the
// provenance note):
//
//   * An INSTRUMENT zone's generators are ABSOLUTE: each one replaces the
//     generator's default outright.
//   * A PRESET zone's generators are RELATIVE: each one is ADDED to the
//     instrument-level result -- a preset saying `initialFilterFc 500` means
//     "500 cents brighter than the instrument says", not 500 cents. The
//     handful of instrument-only generators (sample offsets, sampleModes,
//     exclusiveClass, overridingRootKey, keynum, velocity) are ignored at
//     preset level rather than added.
//   * The FIRST zone of an instrument is GLOBAL if it names no sample; its
//     generators become the base for every following zone unless a zone sets
//     the same generator locally. The first zone of a preset is global the
//     same way if it names no instrument.
//   * Key and velocity ranges INTERSECT across the two levels: a note plays
//     a zone only if it is inside both the instrument zone's range and the
//     preset zone's. An empty intersection means the pairing can never
//     sound, and it is dropped here rather than tested per note-on.
//
// Everything is resolved once, at load time, into flat PlayableZone records
// -- the audio thread never walks the hydra.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Sf2File.hpp"
#include "Sf2Generators.hpp"
#include "SamplePlayer.hpp"

namespace tezla::svarayantra {

/// Raw envelope generators for one zone, still in the format's units
/// (timecents, centibels or 0.1% for sustain, timecents-per-key scaling).
/// Sf2Envelope owns the conversion to seconds and gain at note-on.
struct EnvelopeSpec
{
    double delayTimecents { -12000.0 };
    double attackTimecents { -12000.0 };
    double holdTimecents { -12000.0 };
    double decayTimecents { -12000.0 };
    double sustainLevel { 0.0 };          // volume: cB attenuation; mod: 0.1%
    double releaseTimecents { -12000.0 };
    double keynumToHold { 0.0 };          // timecents per key below 60
    double keynumToDecay { 0.0 };
};

/// One fully resolved playable region: sample, ranges, and every generator
/// the voice consumes, in the format's units, defaults applied.
struct PlayableZone
{
    std::uint32_t sampleIndex { 0 };

    int keyLow { 0 };
    int keyHigh { 127 };
    int velocityLow { 0 };
    int velocityHigh { 127 };

    // Absolute sample-pool offsets with the zone's address offsets applied
    // and clamped into the sample's own bounds.
    std::uint32_t start { 0 };
    std::uint32_t end { 0 };
    std::uint32_t loopStart { 0 };
    std::uint32_t loopEnd { 0 };
    LoopMode loopMode { LoopMode::none };

    double sampleRate { 48000.0 };
    int rootKey { 60 };                   // override, else the sample's pitch
    double pitchCorrectionCents { 0.0 };  // from the sample header
    double coarseTuneSemitones { 0.0 };
    double fineTuneCents { 0.0 };
    double scaleTuning { 100.0 };         // cents of pitch per key step

    double attenuationCentibels { 0.0 };
    double pan { 0.0 };                   // -1 full left .. +1 full right

    double filterCutoffCents { 13500.0 }; // absolute cents, 8.176 Hz origin
    double filterQCentibels { 0.0 };

    EnvelopeSpec volumeEnvelope;
    EnvelopeSpec modulationEnvelope;
    double modEnvToPitchCents { 0.0 };
    double modEnvToFilterCents { 0.0 };

    double vibLfoToPitchCents { 0.0 };
    double vibLfoDelayTimecents { -12000.0 };
    double vibLfoFrequencyCents { 0.0 };

    int exclusiveClass { 0 };
    int fixedKey { -1 };                  // keynum override, -1 = none
    int fixedVelocity { -1 };

    [[nodiscard]] bool matches (int key, int velocity) const noexcept
    {
        return key >= keyLow && key <= keyHigh
            && velocity >= velocityLow && velocity <= velocityHigh;
    }
};

/// One preset as the host sees it: bank, program, name, and the flat list of
/// zones a note-on searches.
struct ModelPreset
{
    std::string name;
    int bank { 0 };
    int program { 0 };
    std::vector<PlayableZone> zones;
};

/// Builds the playable model from a parsed file. The file must have passed
/// its own validation (Sf2File::parse refuses otherwise), so instrument and
/// sample references are trusted to be in range here.
class Sf2Model
{
public:
    std::vector<ModelPreset> presets;

    void build (const Sf2File& file)
    {
        presets.clear();
        presets.reserve (file.presets.size());

        for (const auto& preset : file.presets)
        {
            ModelPreset out;
            out.name = preset.name;
            out.bank = preset.bank;
            out.program = preset.program;

            GenSet presetGlobal;
            bool first = true;

            for (auto z = preset.zoneFirst; z < preset.zoneLast; ++z)
            {
                GenSet pgens = presetGlobal;
                double instrumentIndex = -1.0;
                collect (file.presetGenerators, file.presetZones[z], pgens,
                         gen::instrument, instrumentIndex);

                if (instrumentIndex < 0.0)
                {
                    // No instrument named: global if it is the first zone,
                    // dead weight otherwise (per spec, ignored).
                    if (first)
                        presetGlobal = pgens;

                    first = false;
                    continue;
                }

                first = false;
                resolveInstrument (file, static_cast<std::uint32_t> (instrumentIndex),
                                   pgens, out.zones);
            }

            presets.push_back (std::move (out));
        }
    }

private:
    /// Sparse generator assignment: which are set, and to what.
    struct GenSet
    {
        double value[gen::count] {};
        bool present[gen::count] {};

        void set (std::uint16_t oper, double v) noexcept
        {
            if (oper < gen::count)
            {
                value[oper] = v;
                present[oper] = true;
            }
        }
    };

    /// Reads one zone's generators into `into` (later entries override
    /// earlier ones, so a local zone layered over a global copy wins).
    /// Ranges are packed low|high in the amount and land as encoded.
    /// `linkOper`'s amount (instrument or sampleID) is reported separately
    /// rather than stored.
    static void collect (const std::vector<Sf2Generator>& generators,
                         const Sf2Zone& zone, GenSet& into,
                         std::uint16_t linkOper, double& linkValue)
    {
        for (auto g = zone.generatorFirst; g < zone.generatorLast; ++g)
        {
            const auto& generator = generators[g];

            if (generator.oper == linkOper)
                linkValue = static_cast<double> (
                    static_cast<std::uint16_t> (generator.amount));
            else if (generator.oper == gen::keyRange || generator.oper == gen::velRange)
                into.set (generator.oper, static_cast<double> (
                    generator.rangeLow | (generator.rangeHigh << 8)));
            else
                into.set (generator.oper, static_cast<double> (generator.amount));
        }
    }

    void resolveInstrument (const Sf2File& file, std::uint32_t instrumentIndex,
                            const GenSet& pgens, std::vector<PlayableZone>& zones)
    {
        const auto& instrument = file.instruments[instrumentIndex];

        GenSet instrumentGlobal;
        bool first = true;

        for (auto z = instrument.zoneFirst; z < instrument.zoneLast; ++z)
        {
            GenSet igens = instrumentGlobal;
            double sampleIndex = -1.0;
            collect (file.instrumentGenerators, file.instrumentZones[z], igens,
                     gen::sampleId, sampleIndex);

            if (sampleIndex < 0.0)
            {
                if (first)
                    instrumentGlobal = igens;

                first = false;
                continue;
            }

            first = false;

            PlayableZone zone;

            if (! resolveRanges (igens, pgens, zone))
                continue;   // ranges never intersect: unplayable pairing

            const auto& sample = file.sampleHeaders[static_cast<std::uint32_t> (sampleIndex)];

            if (sample.isRom())
                continue;   // indexes memory the file does not carry

            zone.sampleIndex = static_cast<std::uint32_t> (sampleIndex);
            resolveGenerators (igens, pgens, sample, zone);
            zones.push_back (zone);
        }
    }

    /// Intersects key and velocity ranges across the two levels. False means
    /// the intersection is empty.
    static bool resolveRanges (const GenSet& igens, const GenSet& pgens,
                               PlayableZone& zone)
    {
        auto apply = [] (const GenSet& gens, std::uint16_t oper, int& low, int& high)
        {
            if (! gens.present[oper])
                return;

            const auto packed = static_cast<int> (gens.value[oper]);
            low = std::max (low, packed & 0xff);
            high = std::min (high, (packed >> 8) & 0xff);
        };

        apply (igens, gen::keyRange, zone.keyLow, zone.keyHigh);
        apply (pgens, gen::keyRange, zone.keyLow, zone.keyHigh);
        apply (igens, gen::velRange, zone.velocityLow, zone.velocityHigh);
        apply (pgens, gen::velRange, zone.velocityLow, zone.velocityHigh);

        return zone.keyLow <= zone.keyHigh && zone.velocityLow <= zone.velocityHigh;
    }

    /// One generator's final value: default, replaced by the instrument
    /// level if set, plus the preset level if set and allowed, clamped to
    /// the specification's range. The clamp applies only when something was
    /// actually set: three generators (overridingRootKey, keynum, velocity)
    /// use a fallback of -1 meaning "none", which sits *outside* their own
    /// 0..127 range -- clamping the untouched default would turn "no
    /// override" into "override with zero".
    [[nodiscard]] static double finalValue (std::uint16_t oper, const GenSet& igens,
                                            const GenSet& pgens) noexcept
    {
        const auto range = genRange (oper);
        const bool presetSets = pgens.present[oper] && ! isInstrumentOnly (oper);

        if (! igens.present[oper] && ! presetSets)
            return range.fallback;

        double value = igens.present[oper] ? igens.value[oper] : range.fallback;

        if (presetSets)
            value += pgens.value[oper];

        return value < range.minimum ? range.minimum
             : value > range.maximum ? range.maximum
                                     : value;
    }

    static void resolveGenerators (const GenSet& igens, const GenSet& pgens,
                                   const Sf2Sample& sample, PlayableZone& zone)
    {
        auto value = [&] (std::uint16_t oper) { return finalValue (oper, igens, pgens); };

        // --- addresses: sample bounds plus offsets, clamped to the sample.
        const auto offset = [&] (std::uint16_t fine, std::uint16_t coarse)
        {
            return value (fine) + 32768.0 * value (coarse);
        };

        auto clampToSample = [&] (double at)
        {
            const auto lo = static_cast<double> (sample.start);
            const auto hi = static_cast<double> (sample.end);
            return static_cast<std::uint32_t> (at < lo ? lo : at > hi ? hi : at);
        };

        zone.start = clampToSample (
            static_cast<double> (sample.start)
              + offset (gen::startAddrsOffset, gen::startAddrsCoarseOffset));
        zone.end = clampToSample (
            static_cast<double> (sample.end)
              + offset (gen::endAddrsOffset, gen::endAddrsCoarseOffset));
        zone.loopStart = clampToSample (
            static_cast<double> (sample.startLoop)
              + offset (gen::startloopAddrsOffset, gen::startloopAddrsCoarseOffset));
        zone.loopEnd = clampToSample (
            static_cast<double> (sample.endLoop)
              + offset (gen::endloopAddrsOffset, gen::endloopAddrsCoarseOffset));

        // --- loop mode: 0 none, 1 continuous, 3 until release; 2 is
        // reserved-and-unused in the format and lands as none.
        const auto modes = static_cast<int> (
            igens.present[gen::sampleModes] ? igens.value[gen::sampleModes] : 0.0);
        zone.loopMode = modes == 1 ? LoopMode::continuous
                      : modes == 3 ? LoopMode::untilRelease
                                   : LoopMode::none;

        // --- pitch. An originalPitch above 127 means "unpitched",
        // conventionally played as 60.
        zone.sampleRate = static_cast<double> (sample.sampleRate);
        const auto rootOverride = static_cast<int> (value (gen::overridingRootKey));
        zone.rootKey = rootOverride >= 0 ? rootOverride
                     : sample.originalPitch <= 127 ? sample.originalPitch
                                                   : 60;
        zone.pitchCorrectionCents = static_cast<double> (sample.pitchCorrection);
        zone.coarseTuneSemitones = value (gen::coarseTune);
        zone.fineTuneCents = value (gen::fineTune);
        zone.scaleTuning = value (gen::scaleTuning);

        // --- level and placement.
        zone.attenuationCentibels = value (gen::initialAttenuation);
        zone.pan = value (gen::pan) / 500.0;

        // --- filter.
        zone.filterCutoffCents = value (gen::initialFilterFc);
        zone.filterQCentibels = value (gen::initialFilterQ);

        // --- envelopes.
        auto envelope = [&] (std::uint16_t delay, std::uint16_t attack,
                             std::uint16_t hold, std::uint16_t decay,
                             std::uint16_t sustain, std::uint16_t release,
                             std::uint16_t keyHold, std::uint16_t keyDecay)
        {
            EnvelopeSpec spec;
            spec.delayTimecents = value (delay);
            spec.attackTimecents = value (attack);
            spec.holdTimecents = value (hold);
            spec.decayTimecents = value (decay);
            spec.sustainLevel = value (sustain);
            spec.releaseTimecents = value (release);
            spec.keynumToHold = value (keyHold);
            spec.keynumToDecay = value (keyDecay);
            return spec;
        };

        zone.volumeEnvelope = envelope (gen::delayVolEnv, gen::attackVolEnv,
                                        gen::holdVolEnv, gen::decayVolEnv,
                                        gen::sustainVolEnv, gen::releaseVolEnv,
                                        gen::keynumToVolEnvHold, gen::keynumToVolEnvDecay);
        zone.modulationEnvelope = envelope (gen::delayModEnv, gen::attackModEnv,
                                            gen::holdModEnv, gen::decayModEnv,
                                            gen::sustainModEnv, gen::releaseModEnv,
                                            gen::keynumToModEnvHold, gen::keynumToModEnvDecay);
        zone.modEnvToPitchCents = value (gen::modEnvToPitch);
        zone.modEnvToFilterCents = value (gen::modEnvToFilterFc);

        // --- vibrato LFO (driven from the mod wheel by the engine).
        zone.vibLfoToPitchCents = value (gen::vibLfoToPitch);
        zone.vibLfoDelayTimecents = value (gen::delayVibLfo);
        zone.vibLfoFrequencyCents = value (gen::freqVibLfo);

        // --- voice management.
        zone.exclusiveClass = static_cast<int> (
            igens.present[gen::exclusiveClass] ? igens.value[gen::exclusiveClass] : 0.0);
        zone.fixedKey = static_cast<int> (value (gen::keynum));
        zone.fixedVelocity = static_cast<int> (value (gen::velocity));
    }
};

} // namespace tezla::svarayantra
