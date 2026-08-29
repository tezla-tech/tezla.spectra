// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The SoundFont generator table: numbers, defaults and clamp ranges.
//
// This is knowledge measurement cannot verify (CLAUDE.md section 9): the
// meanings are the format's definitions, and a subtle misremembering would be
// strictly worse than a faithful copy. The IDs and semantics follow the
// SoundFont 2.01 specification section 8.1; the defaults and ranges are taken
// from FluidSynth's fluid_gen.c table (LGPL-2.1-or-later, itself citing
// SFSpec21 section 8.1.3), cross-checked against tsf.h (MIT). Both are
// recorded in docs/DSP-REFERENCES.md, and copies of the consulted files sit
// in `technical references/svarayantra/`. The specification PDF itself could
// not be fetched from this container; that too is recorded there.

#include <cstdint>

namespace tezla::svarayantra {

/// Generator numbers, SF2.01 section 8.1.2. Only the ones this player reads
/// are named; the table below still carries defaults for the whole range so
/// resolution is uniform.
namespace gen
{
constexpr std::uint16_t startAddrsOffset = 0;
constexpr std::uint16_t endAddrsOffset = 1;
constexpr std::uint16_t startloopAddrsOffset = 2;
constexpr std::uint16_t endloopAddrsOffset = 3;
constexpr std::uint16_t startAddrsCoarseOffset = 4;
constexpr std::uint16_t vibLfoToPitch = 6;
constexpr std::uint16_t modEnvToPitch = 7;
constexpr std::uint16_t initialFilterFc = 8;
constexpr std::uint16_t initialFilterQ = 9;
constexpr std::uint16_t modEnvToFilterFc = 11;
constexpr std::uint16_t endAddrsCoarseOffset = 12;
constexpr std::uint16_t pan = 17;
constexpr std::uint16_t delayVibLfo = 23;
constexpr std::uint16_t freqVibLfo = 24;
constexpr std::uint16_t delayModEnv = 25;
constexpr std::uint16_t attackModEnv = 26;
constexpr std::uint16_t holdModEnv = 27;
constexpr std::uint16_t decayModEnv = 28;
constexpr std::uint16_t sustainModEnv = 29;
constexpr std::uint16_t releaseModEnv = 30;
constexpr std::uint16_t keynumToModEnvHold = 31;
constexpr std::uint16_t keynumToModEnvDecay = 32;
constexpr std::uint16_t delayVolEnv = 33;
constexpr std::uint16_t attackVolEnv = 34;
constexpr std::uint16_t holdVolEnv = 35;
constexpr std::uint16_t decayVolEnv = 36;
constexpr std::uint16_t sustainVolEnv = 37;
constexpr std::uint16_t releaseVolEnv = 38;
constexpr std::uint16_t keynumToVolEnvHold = 39;
constexpr std::uint16_t keynumToVolEnvDecay = 40;
constexpr std::uint16_t instrument = 41;
constexpr std::uint16_t keyRange = 43;
constexpr std::uint16_t velRange = 44;
constexpr std::uint16_t startloopAddrsCoarseOffset = 45;
constexpr std::uint16_t keynum = 46;
constexpr std::uint16_t velocity = 47;
constexpr std::uint16_t initialAttenuation = 48;
constexpr std::uint16_t endloopAddrsCoarseOffset = 50;
constexpr std::uint16_t coarseTune = 51;
constexpr std::uint16_t fineTune = 52;
constexpr std::uint16_t sampleId = 53;
constexpr std::uint16_t sampleModes = 54;
constexpr std::uint16_t scaleTuning = 56;
constexpr std::uint16_t exclusiveClass = 57;
constexpr std::uint16_t overridingRootKey = 58;

constexpr std::uint16_t count = 59;
} // namespace gen

/// Default value and clamp range for one generator, per SF2.01 section 8.1.3
/// (via FluidSynth's table).
struct GenRange
{
    double minimum;
    double maximum;
    double fallback;
};

/// Indexed by generator number. Generators the table marks with a huge range
/// are the sample offsets, whose real bound is the sample itself -- the model
/// clamps those against the sample header instead.
[[nodiscard]] constexpr GenRange genRange (std::uint16_t oper) noexcept
{
    constexpr double kBig = 1e10;

    switch (oper)
    {
        case gen::startAddrsOffset:          return { 0.0, kBig, 0.0 };
        case gen::endAddrsOffset:            return { -kBig, 0.0, 0.0 };
        case gen::startloopAddrsOffset:      return { -kBig, kBig, 0.0 };
        case gen::endloopAddrsOffset:        return { -kBig, kBig, 0.0 };
        case gen::startAddrsCoarseOffset:    return { 0.0, kBig, 0.0 };
        case 5:  /* modLfoToPitch */         return { -12000.0, 12000.0, 0.0 };
        case gen::vibLfoToPitch:             return { -12000.0, 12000.0, 0.0 };
        case gen::modEnvToPitch:             return { -12000.0, 12000.0, 0.0 };
        case gen::initialFilterFc:           return { 1500.0, 13500.0, 13500.0 };
        case gen::initialFilterQ:            return { 0.0, 960.0, 0.0 };
        case 10: /* modLfoToFilterFc */      return { -12000.0, 12000.0, 0.0 };
        case gen::modEnvToFilterFc:          return { -12000.0, 12000.0, 0.0 };
        case gen::endAddrsCoarseOffset:      return { -kBig, 0.0, 0.0 };
        case 13: /* modLfoToVolume */        return { -960.0, 960.0, 0.0 };
        case 15: /* chorusEffectsSend */     return { 0.0, 1000.0, 0.0 };
        case 16: /* reverbEffectsSend */     return { 0.0, 1000.0, 0.0 };
        case gen::pan:                       return { -500.0, 500.0, 0.0 };
        case 21: /* delayModLfo */           return { -12000.0, 5000.0, -12000.0 };
        case 22: /* freqModLfo */            return { -16000.0, 4500.0, 0.0 };
        case gen::delayVibLfo:               return { -12000.0, 5000.0, -12000.0 };
        case gen::freqVibLfo:                return { -16000.0, 4500.0, 0.0 };
        case gen::delayModEnv:               return { -12000.0, 5000.0, -12000.0 };
        case gen::attackModEnv:              return { -12000.0, 8000.0, -12000.0 };
        case gen::holdModEnv:                return { -12000.0, 5000.0, -12000.0 };
        case gen::decayModEnv:               return { -12000.0, 8000.0, -12000.0 };
        case gen::sustainModEnv:             return { 0.0, 1000.0, 0.0 };
        case gen::releaseModEnv:             return { -12000.0, 8000.0, -12000.0 };
        case gen::keynumToModEnvHold:        return { -1200.0, 1200.0, 0.0 };
        case gen::keynumToModEnvDecay:       return { -1200.0, 1200.0, 0.0 };
        case gen::delayVolEnv:               return { -12000.0, 5000.0, -12000.0 };
        case gen::attackVolEnv:              return { -12000.0, 8000.0, -12000.0 };
        case gen::holdVolEnv:                return { -12000.0, 5000.0, -12000.0 };
        case gen::decayVolEnv:               return { -12000.0, 8000.0, -12000.0 };
        case gen::sustainVolEnv:             return { 0.0, 1440.0, 0.0 };
        case gen::releaseVolEnv:             return { -12000.0, 8000.0, -12000.0 };
        case gen::keynumToVolEnvHold:        return { -1200.0, 1200.0, 0.0 };
        case gen::keynumToVolEnvDecay:       return { -1200.0, 1200.0, 0.0 };
        case gen::keynum:                    return { 0.0, 127.0, -1.0 };
        case gen::velocity:                  return { 0.0, 127.0, -1.0 };
        case gen::initialAttenuation:        return { 0.0, 1440.0, 0.0 };
        case gen::startloopAddrsCoarseOffset:return { -kBig, kBig, 0.0 };
        case gen::endloopAddrsCoarseOffset:  return { -kBig, kBig, 0.0 };
        case gen::coarseTune:                return { -120.0, 120.0, 0.0 };
        case gen::fineTune:                  return { -99.0, 99.0, 0.0 };
        case gen::scaleTuning:               return { 0.0, 1200.0, 100.0 };
        case gen::overridingRootKey:         return { 0.0, 127.0, -1.0 };
        default:                             return { 0.0, 0.0, 0.0 };
    }
}

/// Generators that only make sense inside an instrument. When one appears in
/// a preset zone the specification says it is ignored, not added.
[[nodiscard]] constexpr bool isInstrumentOnly (std::uint16_t oper) noexcept
{
    switch (oper)
    {
        case gen::startAddrsOffset:
        case gen::endAddrsOffset:
        case gen::startloopAddrsOffset:
        case gen::endloopAddrsOffset:
        case gen::startAddrsCoarseOffset:
        case gen::endAddrsCoarseOffset:
        case gen::startloopAddrsCoarseOffset:
        case gen::endloopAddrsCoarseOffset:
        case gen::keynum:
        case gen::velocity:
        case gen::sampleModes:
        case gen::exclusiveClass:
        case gen::overridingRootKey:
        case gen::sampleId:
            return true;
        default:
            return false;
    }
}

} // namespace tezla::svarayantra
