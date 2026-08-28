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

using namespace tezla::svarayantra;

// ---------------------------------------------------------------------------
// The round trip: the tests own both ends
// ---------------------------------------------------------------------------

TEZLA_TEST (a_built_soundfont_parses_back_to_what_was_built)
{
    auto font = sf2test::sineFont (100, 4, 44100, 57);
    font.name = "Round trip";
    font.pitchCorrection = -7;
    font.instrumentGens.push_back ({ 43, sf2test::range (36, 96) });   // keyRange
    font.presetGens = { { 48, 60 } };                                  // attenuation 60 cB

    const auto bytes = font.build();

    Sf2File file;
    const auto result = file.parse (bytes.data(), bytes.size());

    CHECK (result.ok);
    CHECK (file.name == "Round trip");
    CHECK (file.versionMajor == 2);
    CHECK (file.versionMinor == 1);

    // One of each, with the terminal records consumed rather than reported.
    CHECK (file.presets.size() == 1);
    CHECK (file.instruments.size() == 1);
    CHECK (file.sampleHeaders.size() == 1);
    CHECK (file.presetZones.size() == 1);
    CHECK (file.instrumentZones.size() == 1);

    // The sample header, field for field.
    const auto& sample = file.sampleHeaders[0];
    CHECK (sample.name == "Sample");
    CHECK (sample.start == 0);
    CHECK (sample.end == font.sampleData.size());
    CHECK (sample.startLoop == 8);
    CHECK (sample.endLoop == 8 + 400);
    CHECK (sample.sampleRate == 44100);
    CHECK (sample.originalPitch == 57);
    CHECK (sample.pitchCorrection == -7);
    CHECK (sample.sampleType == Sf2Sample::kMono);
    CHECK (! sample.isRom());

    // The sample data survives bit for bit.
    CHECK (file.samples.size() == font.sampleData.size());

    bool identical = true;

    for (std::size_t i = 0; i < file.samples.size(); ++i)
        identical = identical && file.samples[i] == font.sampleData[i];

    CHECK (identical);

    // The spans resolved from the index deltas: the instrument zone holds
    // sampleModes, keyRange and the sampleID; the preset zone holds the
    // attenuation and the instrument pointer.
    const auto& instrumentZone = file.instrumentZones[0];
    CHECK (instrumentZone.generatorFirst == 0);
    CHECK (instrumentZone.generatorLast == 3);

    const auto& keyRange = file.instrumentGenerators[1];
    CHECK (keyRange.oper == 43);
    CHECK (keyRange.rangeLow == 36);
    CHECK (keyRange.rangeHigh == 96);

    CHECK (file.instrumentGenerators[2].oper == 53);   // sampleID, last

    const auto& presetZone = file.presetZones[0];
    CHECK (presetZone.generatorFirst == 0);
    CHECK (presetZone.generatorLast == 2);
    CHECK (file.presetGenerators[0].oper == 48);
    CHECK (file.presetGenerators[0].amount == 60);
    CHECK (file.presetGenerators[1].oper == 41);       // instrument, last
}

// ---------------------------------------------------------------------------
// Refusals: never half-loaded
// ---------------------------------------------------------------------------

TEZLA_TEST (every_truncation_is_refused_and_leaves_the_previous_font_alive)
{
    // The .tzref lesson with a hundred structures instead of one: any prefix
    // of a valid file must refuse cleanly -- and a parser that half-loaded
    // before refusing would leave garbage that PLAYS. So each refusal is also
    // checked to have left the previously loaded font untouched.
    auto good = sf2test::sineFont (64, 2, 48000, 60);
    const auto bytes = good.build();

    Sf2File file;
    CHECK (file.parse (bytes.data(), bytes.size()).ok);

    const auto presetName = file.presets[0].name;
    const auto sampleCount = file.samples.size();

    int refused = 0;

    for (std::size_t cut = 0; cut < bytes.size(); cut += 7)
    {
        const auto result = file.parse (bytes.data(), cut);

        if (! result.ok)
            ++refused;

        // Untouched, whatever happened.
        if (file.presets.size() != 1 || file.presets[0].name != presetName
            || file.samples.size() != sampleCount)
        {
            CHECK (false);
            return;
        }
    }

    // Every single prefix refused: the RIFF size check alone guarantees it,
    // and everything after it double-checks its own chunk.
    CHECK (refused == static_cast<int> ((bytes.size() + 6) / 7));
}

TEZLA_TEST (corruption_is_refused_with_the_guilty_chunk_named)
{
    auto font = sf2test::sineFont (64, 2, 48000, 60);
    const auto clean = font.build();

    auto parseExpecting = [] (std::vector<std::uint8_t> bytes, const char* chunk)
    {
        Sf2File file;
        const auto result = file.parse (bytes.data(), bytes.size());

        CHECK (! result.ok);
        CHECK (result.chunk == chunk);
    };

    // Not a RIFF at all.
    {
        auto bytes = clean;
        bytes[0] = 'X';
        parseExpecting (bytes, "RIFF");
    }

    // Not a SoundFont.
    {
        auto bytes = clean;
        bytes[8] = 'x';
        parseExpecting (bytes, "RIFF");
    }

    // A generator naming a sample that does not exist. The sampleID is the
    // last instrument generator; its amount sits two bytes after its oper.
    // Find the igen chunk and break its sampleID.
    {
        auto bytes = clean;

        for (std::size_t i = 0; i + 4 < bytes.size(); ++i)
            if (bytes[i] == 'i' && bytes[i + 1] == 'g' && bytes[i + 2] == 'e'
                && bytes[i + 3] == 'n')
            {
                // chunk header (8) + first record: sampleModes(4) then the
                // sampleID record: oper at +8+4, amount at +8+4+2.
                bytes[i + 8 + 4 + 2] = 99;   // sample 99 of 1
                break;
            }

        parseExpecting (bytes, "igen");
    }

    // A loop outside its sample: endLoop beyond end. shdr's loop fields sit
    // after the 20-byte name and two u32s.
    {
        auto bytes = clean;

        for (std::size_t i = 0; i + 4 < bytes.size(); ++i)
            if (bytes[i] == 's' && bytes[i + 1] == 'h' && bytes[i + 2] == 'd'
                && bytes[i + 3] == 'r')
            {
                const std::size_t endLoopAt = i + 8 + 20 + 4 + 4 + 4;
                bytes[endLoopAt + 2] = 0x7f;   // enormous endLoop
                break;
            }

        parseExpecting (bytes, "shdr");
    }

    // Zone indices running backwards: pbag's terminal record pointing at a
    // generator index below the first zone's.
    {
        auto bytes = clean;

        for (std::size_t i = 0; i + 4 < bytes.size(); ++i)
            if (bytes[i] == 'p' && bytes[i + 1] == 'b' && bytes[i + 2] == 'a'
                && bytes[i + 3] == 'g')
            {
                bytes[i + 8] = 200;   // first zone's genNdx far past the terminal's
                break;
            }

        parseExpecting (bytes, "pbag");
    }

    // And the hydra out of order.
    {
        auto bytes = clean;

        for (std::size_t i = 0; i + 4 < bytes.size(); ++i)
            if (bytes[i] == 'p' && bytes[i + 1] == 'h' && bytes[i + 2] == 'd'
                && bytes[i + 3] == 'r')
            {
                bytes[i + 1] = 'x';
                break;
            }

        Sf2File file;
        const auto result = file.parse (bytes.data(), bytes.size());
        CHECK (! result.ok);
    }
}

TEZLA_TEST (a_rom_sample_is_carried_but_marked_rather_than_bounds_checked)
{
    // ROM samples index memory the file does not carry, so their offsets are
    // allowed to point anywhere -- the model must refuse to *play* them, and
    // the parser must not refuse the whole file over them.
    auto font = sf2test::sineFont (64, 2, 48000, 60);
    font.sampleType = static_cast<std::uint16_t> (Sf2Sample::kMono | Sf2Sample::kRomFlag);

    const auto bytes = font.build();

    Sf2File file;
    CHECK (file.parse (bytes.data(), bytes.size()).ok);
    CHECK (file.sampleHeaders.size() == 1);
    CHECK (file.sampleHeaders[0].isRom());
}
