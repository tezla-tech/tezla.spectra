// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cstdint>
#include <vector>

#include "Sf2TestBuilder.hpp"

#include <Sf2File.hpp>
#include <Sf2Model.hpp>

using namespace tezla::svarayantra;

namespace
{
/// Parses the built bytes and resolves the model, asserting the parse held.
Sf2Model modelOf (const sf2test::FontBuilder& builder)
{
    const auto bytes = builder.build();

    Sf2File file;
    const auto result = file.parse (bytes.data(), bytes.size());
    CHECK (result.ok);

    Sf2Model model;
    model.build (file);
    return model;
}

sf2test::FontBuilder::Sample plainSample (int size = 256, std::uint8_t rootKey = 57)
{
    sf2test::FontBuilder::Sample sample;
    sample.data.assign (static_cast<std::size_t> (size), 1000);
    sample.loopStart = 8;
    sample.loopEnd = static_cast<std::uint32_t> (size - 8);
    sample.originalPitch = rootKey;
    return sample;
}
} // namespace

// ---------------------------------------------------------------------------
// Levels: defaults, absolute, relative
// ---------------------------------------------------------------------------

TEZLA_TEST (generator_defaults_apply_when_nothing_is_said)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample (256, 57) };
    font.instruments = { { "I", { { { { 53, 0 } } } } } };          // sampleID only
    font.presets = { { "P", 0, 0, { { { { 41, 0 } } } } } };        // instrument only

    const auto model = modelOf (font);

    CHECK (model.presets.size() == 1);
    CHECK (model.presets[0].zones.size() == 1);

    const auto& zone = model.presets[0].zones[0];
    CHECK (zone.filterCutoffCents == 13500.0);
    CHECK (zone.filterQCentibels == 0.0);
    CHECK (zone.scaleTuning == 100.0);
    CHECK (zone.pan == 0.0);
    CHECK (zone.attenuationCentibels == 0.0);
    CHECK (zone.rootKey == 57);                 // the sample's own pitch
    CHECK (zone.fixedKey == -1);
    CHECK (zone.fixedVelocity == -1);
    CHECK (zone.keyLow == 0);
    CHECK (zone.keyHigh == 127);
    CHECK (zone.velocityLow == 0);
    CHECK (zone.velocityHigh == 127);
    CHECK (zone.loopMode == LoopMode::none);
    CHECK (zone.start == 0);
    CHECK (zone.end == 256);
    CHECK (zone.volumeEnvelope.delayTimecents == -12000.0);
    CHECK (zone.volumeEnvelope.sustainLevel == 0.0);
    CHECK (zone.exclusiveClass == 0);
}

TEZLA_TEST (instrument_generators_replace_and_preset_generators_add)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample() };
    font.instruments = { { "I", { { {
        { 8, 9000 },     // initialFilterFc: absolute
        { 48, 100 },     // initialAttenuation
        { 17, -250 },    // pan
        { 53, 0 },
    } } } } };
    font.presets = { { "P", 0, 0, { { {
        { 8, 500 },      // +500 cents on top of the instrument's 9000
        { 48, 60 },      // +60 cB
        { 17, 100 },     // nudged right
        { 41, 0 },
    } } } } };

    const auto model = modelOf (font);
    const auto& zone = model.presets[0].zones[0];

    CHECK (zone.filterCutoffCents == 9500.0);
    CHECK (zone.attenuationCentibels == 160.0);
    CHECK (zone.pan == -150.0 / 500.0);
}

TEZLA_TEST (preset_additions_are_clamped_to_the_spec_range)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample() };
    font.instruments = { { "I", { { { { 8, 13000 }, { 53, 0 } } } } } };
    font.presets = { { "P", 0, 0, { { { { 8, 2000 }, { 41, 0 } } } } } };

    const auto model = modelOf (font);
    CHECK (model.presets[0].zones[0].filterCutoffCents == 13500.0);
}

TEZLA_TEST (instrument_only_generators_are_ignored_at_preset_level)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample (256, 57) };
    font.instruments = { { "I", { { { { 53, 0 } } } } } };
    font.presets = { { "P", 0, 0, { { {
        { 54, 1 },       // sampleModes: instrument-only
        { 58, 30 },      // overridingRootKey: instrument-only
        { 46, 99 },      // keynum: instrument-only
        { 41, 0 },
    } } } } };

    const auto model = modelOf (font);
    const auto& zone = model.presets[0].zones[0];

    CHECK (zone.loopMode == LoopMode::none);
    CHECK (zone.rootKey == 57);
    CHECK (zone.fixedKey == -1);
}

// ---------------------------------------------------------------------------
// Global zones
// ---------------------------------------------------------------------------

TEZLA_TEST (global_zones_layer_under_every_local_zone)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample() };
    font.instruments = { { "I", {
        { { { 17, -500 }, { 8, 7000 } } },       // global: no sampleID
        { { { 53, 0 } } },                       // inherits both
        { { { 17, 250 }, { 53, 0 } } },          // overrides pan locally
    } } };
    font.presets = { { "P", 0, 0, {
        { { { 48, 50 } } },                      // preset global: no instrument
        { { { 41, 0 } } },
    } } };

    const auto model = modelOf (font);
    const auto& zones = model.presets[0].zones;

    CHECK (zones.size() == 2);
    CHECK (zones[0].pan == -1.0);
    CHECK (zones[0].filterCutoffCents == 7000.0);
    CHECK (zones[0].attenuationCentibels == 50.0);
    CHECK (zones[1].pan == 0.5);
    CHECK (zones[1].filterCutoffCents == 7000.0);
    CHECK (zones[1].attenuationCentibels == 50.0);
}

// ---------------------------------------------------------------------------
// Ranges
// ---------------------------------------------------------------------------

TEZLA_TEST (key_and_velocity_ranges_intersect_across_levels)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample() };
    font.instruments = { { "I", {
        { { { 43, sf2test::range (40, 80) }, { 44, sf2test::range (0, 100) }, { 53, 0 } } },
        { { { 43, sf2test::range (0, 30) }, { 53, 0 } } },   // disjoint with the preset
    } } };
    font.presets = { { "P", 0, 0, { { {
        { 43, sf2test::range (60, 127) },
        { 44, sf2test::range (50, 127) },
        { 41, 0 },
    } } } } };

    const auto model = modelOf (font);
    const auto& zones = model.presets[0].zones;

    // The 0..30 pairing can never sound and is dropped outright.
    CHECK (zones.size() == 1);
    CHECK (zones[0].keyLow == 60);
    CHECK (zones[0].keyHigh == 80);
    CHECK (zones[0].velocityLow == 50);
    CHECK (zones[0].velocityHigh == 100);

    CHECK (zones[0].matches (60, 75));
    CHECK (zones[0].matches (80, 100));
    CHECK (! zones[0].matches (59, 75));
    CHECK (! zones[0].matches (81, 75));
    CHECK (! zones[0].matches (70, 49));
    CHECK (! zones[0].matches (70, 101));
}

// ---------------------------------------------------------------------------
// Addresses and pitch
// ---------------------------------------------------------------------------

TEZLA_TEST (sample_offsets_move_the_window_and_are_clamped_to_the_sample)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample (256, 57) };
    font.instruments = { { "I", { { {
        { 0, 10 },       // startAddrsOffset
        { 1, -5 },       // endAddrsOffset
        { 2, 2 },        // startloopAddrsOffset
        { 3, -2 },       // endloopAddrsOffset
        { 54, 1 },       // loop continuously
        { 58, 40 },      // overridingRootKey
        { 53, 0 },
    } } } } };
    font.presets = { { "P", 0, 0, { { { { 41, 0 } } } } } };

    const auto model = modelOf (font);
    const auto& zone = model.presets[0].zones[0];

    CHECK (zone.start == 10);
    CHECK (zone.end == 251);
    CHECK (zone.loopStart == 10);       // 8 + 2
    CHECK (zone.loopEnd == 246);        // 248 - 2
    CHECK (zone.loopMode == LoopMode::continuous);
    CHECK (zone.rootKey == 40);

    // An offset past the end of the sample clamps to it rather than walking
    // into the next sample in the pool.
    sf2test::FontBuilder wild;
    wild.samples = { plainSample (256, 57) };
    wild.instruments = { { "I", { { { { 0, 30000 }, { 53, 0 } } } } } };
    wild.presets = { { "P", 0, 0, { { { { 41, 0 } } } } } };

    const auto clamped = modelOf (wild);
    CHECK (clamped.presets[0].zones[0].start == 256);
}

TEZLA_TEST (an_unpitched_sample_roots_at_sixty_and_rom_samples_are_unplayable)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample (256, 255) };   // originalPitch 255: unpitched
    font.instruments = { { "I", { { { { 53, 0 } } } } } };
    font.presets = { { "P", 0, 0, { { { { 41, 0 } } } } } };

    const auto model = modelOf (font);
    CHECK (model.presets[0].zones[0].rootKey == 60);

    sf2test::FontBuilder rom;
    rom.samples = { plainSample() };
    rom.samples[0].sampleType = 0x8001;          // ROM mono
    rom.instruments = { { "I", { { { { 53, 0 } } } } } };
    rom.presets = { { "P", 0, 0, { { { { 41, 0 } } } } } };

    const auto romModel = modelOf (rom);
    CHECK (romModel.presets[0].zones.empty());
}

// ---------------------------------------------------------------------------
// Multiple presets
// ---------------------------------------------------------------------------

TEZLA_TEST (banks_programs_and_instrument_links_carry_through)
{
    sf2test::FontBuilder font;
    font.samples = { plainSample (256, 50), plainSample (128, 70) };
    font.instruments = {
        { "Low", { { { { 53, 0 } } } } },
        { "High", { { { { 53, 1 } } } } },
    };
    font.presets = {
        { "First", 0, 0, { { { { 41, 0 } } } } },
        { "Second", 8, 3, { { { { 41, 1 } } } } },
    };

    const auto model = modelOf (font);

    CHECK (model.presets.size() == 2);
    CHECK (model.presets[0].name == "First");
    CHECK (model.presets[0].bank == 0);
    CHECK (model.presets[0].program == 0);
    CHECK (model.presets[0].zones[0].sampleIndex == 0);
    CHECK (model.presets[0].zones[0].rootKey == 50);

    CHECK (model.presets[1].name == "Second");
    CHECK (model.presets[1].bank == 8);
    CHECK (model.presets[1].program == 3);
    CHECK (model.presets[1].zones[0].sampleIndex == 1);
    CHECK (model.presets[1].zones[0].rootKey == 70);

    // The second sample's window sits after the first in the pool.
    CHECK (model.presets[1].zones[0].start == 256);
    CHECK (model.presets[1].zones[0].end == 256 + 128);
}
