// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The SoundFont 2 file, parsed -- and refused whole if it cannot be parsed
// whole.
//
// ---------------------------------------------------------------------------
// What an .sf2 is
// ---------------------------------------------------------------------------
//
// A RIFF file with three LIST blocks:
//
//   INFO   version and titles. `ifil` is mandatory in the format and here.
//   sdta   the sample data: `smpl`, 16-bit little-endian PCM. (`sm24`, the
//          optional extension to 24 bits, is recognised and ignored -- the
//          README says so.)
//   pdta   the nine cross-referencing tables the spec calls the hydra:
//
//            phdr (38 B)  presets: name, bank:program, first preset-zone
//            pbag (4 B)   preset zones: first generator, first modulator
//            pmod (10 B)  preset modulators
//            pgen (4 B)   preset generators
//            inst (22 B)  instruments: name, first instrument-zone
//            ibag (4 B)   instrument zones
//            imod (10 B)  instrument modulators
//            igen (4 B)   instrument generators
//            shdr (46 B)  samples: offsets into smpl, loop, root key, type
//
// Every list is terminated by one sentinel record, and every record points at
// a *first* index whose span runs to the next record's first index -- so the
// arrays only mean anything if those indices never run backwards and never
// run off the end. That is what `parse` checks, span by span.
//
// ---------------------------------------------------------------------------
// Refused whole, with the failing chunk named
// ---------------------------------------------------------------------------
//
// The `.tzref`/`.scl` lesson, third application: a tuning that half-loads is
// worse than one that will not load, and a soundfont is a hundred times more
// structure. A truncated smpl, a bag index pointing backwards, a generator
// span past the end -- any of it refuses the whole file with the chunk and
// the reason, and the previous state stays live. Nothing half-loads.
//
// Record layouts and sizes verified against two independent implementations
// -- TinySoundFont (MIT) and FluidSynth's sfloader (LGPL-2.1+), saved under
// `technical references/svarayantra/` -- because the specification PDF itself
// could not be fetched; docs/DSP-REFERENCES.md records that honestly.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tezla::svarayantra {

// ---------------------------------------------------------------------------
// The records, as parsed
// ---------------------------------------------------------------------------

/// One sample header. Offsets index the mono 16-bit sample pool, in samples.
struct Sf2Sample
{
    std::string name;
    std::uint32_t start { 0 };
    std::uint32_t end { 0 };
    std::uint32_t startLoop { 0 };
    std::uint32_t endLoop { 0 };
    std::uint32_t sampleRate { 0 };
    std::uint8_t originalPitch { 60 };
    std::int8_t pitchCorrection { 0 };
    std::uint16_t sampleLink { 0 };
    std::uint16_t sampleType { 0 };

    static constexpr std::uint16_t kMono = 1;
    static constexpr std::uint16_t kRight = 2;
    static constexpr std::uint16_t kLeft = 4;
    static constexpr std::uint16_t kLinked = 8;
    static constexpr std::uint16_t kRomFlag = 0x8000;

    [[nodiscard]] bool isRom() const noexcept { return (sampleType & kRomFlag) != 0; }
};

/// One generator: an operator and its amount. The amount is one 16-bit field
/// that different operators read differently -- as a signed value, or as a
/// low/high byte pair for the range operators -- so both views are kept.
struct Sf2Generator
{
    std::uint16_t oper { 0 };
    std::int16_t amount { 0 };
    std::uint8_t rangeLow { 0 };
    std::uint8_t rangeHigh { 0 };
};

/// One modulator, exactly as stored. Interpretation lives in Sf2Model.
struct Sf2Modulator
{
    std::uint16_t sourceOper { 0 };
    std::uint16_t destOper { 0 };
    std::int16_t amount { 0 };
    std::uint16_t amountSourceOper { 0 };
    std::uint16_t transformOper { 0 };
};

/// One zone: a span of generators and a span of modulators. Spans are
/// half-open [first, last) into the file's generator/modulator arrays.
struct Sf2Zone
{
    std::uint32_t generatorFirst { 0 };
    std::uint32_t generatorLast { 0 };
    std::uint32_t modulatorFirst { 0 };
    std::uint32_t modulatorLast { 0 };
};

/// One instrument: a name and a half-open span of instrument zones.
struct Sf2Instrument
{
    std::string name;
    std::uint32_t zoneFirst { 0 };
    std::uint32_t zoneLast { 0 };
};

/// One preset: bank:program, and a half-open span of preset zones.
struct Sf2Preset
{
    std::string name;
    std::uint16_t program { 0 };
    std::uint16_t bank { 0 };
    std::uint32_t zoneFirst { 0 };
    std::uint32_t zoneLast { 0 };
};

/// What `parse` hands back. `ok` false means nothing was written to the
/// output -- the caller's previous soundfont, if any, is untouched.
struct Sf2ParseResult
{
    bool ok { false };
    std::string chunk;     ///< which chunk refused the file
    std::string message;   ///< why
};

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------

class Sf2File
{
public:
    /// The INFO the panel shows.
    std::string name;              ///< INAM, or empty
    int versionMajor { 0 };        ///< ifil
    int versionMinor { 0 };

    /// The sample pool: every sample of every instrument, 16-bit as stored.
    /// Kept as int16 on purpose -- it is the biggest thing in memory by far,
    /// and doubling it to hold doubles would buy one multiply per fetch.
    std::vector<std::int16_t> samples;

    std::vector<Sf2Sample> sampleHeaders;
    std::vector<Sf2Generator> instrumentGenerators;
    std::vector<Sf2Modulator> instrumentModulators;
    std::vector<Sf2Zone> instrumentZones;
    std::vector<Sf2Instrument> instruments;
    std::vector<Sf2Generator> presetGenerators;
    std::vector<Sf2Modulator> presetModulators;
    std::vector<Sf2Zone> presetZones;
    std::vector<Sf2Preset> presets;

    [[nodiscard]] int presetCount() const noexcept { return static_cast<int> (presets.size()); }

    /// Parses `size` bytes of .sf2. On failure the object is untouched.
    [[nodiscard]] Sf2ParseResult parse (const std::uint8_t* data, std::size_t size)
    {
        Sf2File fresh;
        auto result = fresh.parseInto (data, size);

        if (result.ok)
            *this = std::move (fresh);

        return result;
    }

private:
    // -----------------------------------------------------------------------
    // Little-endian readers. Byte assembly rather than struct punning, so the
    // same code is correct on every architecture and alignment -- CLAUDE.md
    // section 2.3 defers *running* on ARM, not writing portably for it.
    // -----------------------------------------------------------------------

    struct Cursor
    {
        const std::uint8_t* data { nullptr };
        std::size_t size { 0 };
        std::size_t at { 0 };

        [[nodiscard]] std::size_t remaining() const noexcept { return size - at; }
        [[nodiscard]] bool has (std::size_t count) const noexcept { return remaining() >= count; }

        std::uint8_t u8() noexcept { return data[at++]; }

        std::uint16_t u16() noexcept
        {
            const std::uint16_t value = static_cast<std::uint16_t> (
                static_cast<std::uint16_t> (data[at]) | (static_cast<std::uint16_t> (data[at + 1]) << 8));
            at += 2;
            return value;
        }

        std::uint32_t u32() noexcept
        {
            const std::uint32_t value = static_cast<std::uint32_t> (data[at])
                                          | (static_cast<std::uint32_t> (data[at + 1]) << 8)
                                          | (static_cast<std::uint32_t> (data[at + 2]) << 16)
                                          | (static_cast<std::uint32_t> (data[at + 3]) << 24);
            at += 4;
            return value;
        }

        /// A four-character chunk id.
        std::string fourCC() noexcept
        {
            std::string id (reinterpret_cast<const char*> (data + at), 4);
            at += 4;
            return id;
        }

        /// A fixed-width, zero-padded name field.
        std::string fixedName (std::size_t width) noexcept
        {
            const char* begin = reinterpret_cast<const char*> (data + at);
            std::size_t length = 0;

            while (length < width && begin[length] != '\0')
                ++length;

            at += width;
            return std::string (begin, length);
        }
    };

    [[nodiscard]] static Sf2ParseResult refuse (std::string chunk, std::string message)
    {
        Sf2ParseResult result;
        result.chunk = std::move (chunk);
        result.message = std::move (message);
        return result;
    }

    [[nodiscard]] static Sf2ParseResult accept()
    {
        Sf2ParseResult result;
        result.ok = true;
        return result;
    }

    // -----------------------------------------------------------------------
    // The parse
    // -----------------------------------------------------------------------

    [[nodiscard]] Sf2ParseResult parseInto (const std::uint8_t* data, std::size_t size)
    {
        if (data == nullptr || size < 12)
            return refuse ("RIFF", "not a RIFF file: shorter than a RIFF header");

        Cursor cursor { data, size, 0 };

        if (cursor.fourCC() != "RIFF")
            return refuse ("RIFF", "not a RIFF file");

        const std::uint32_t riffSize = cursor.u32();

        if (riffSize < 4 || riffSize > size - 8)
            return refuse ("RIFF", "RIFF size runs past the end of the file");

        // Everything below stays inside the RIFF payload.
        cursor.size = 8 + riffSize;

        if (cursor.fourCC() != "sfbk")
            return refuse ("RIFF", "not a SoundFont: the form is not 'sfbk'");

        bool seenInfo = false, seenSdta = false, seenPdta = false;

        while (cursor.remaining() > 0)
        {
            if (! cursor.has (8))
                return refuse ("RIFF", "truncated inside a chunk header");

            const std::string id = cursor.fourCC();
            const std::uint32_t chunkSize = cursor.u32();

            if (! cursor.has (chunkSize))
                return refuse (id, "chunk size runs past the end of the file");

            if (id != "LIST")
                return refuse (id, "expected a LIST chunk at the top level");

            Cursor list { cursor.data, cursor.at + chunkSize, cursor.at };

            if (chunkSize < 4)
                return refuse ("LIST", "LIST too small to carry a type");

            const std::string type = list.fourCC();

            Sf2ParseResult result = accept();

            if (type == "INFO")      { result = parseInfo (list); seenInfo = true; }
            else if (type == "sdta") { result = parseSdta (list); seenSdta = true; }
            else if (type == "pdta") { result = parsePdta (list); seenPdta = true; }
            // Unknown LISTs are skipped: the format allows extension.

            if (! result.ok)
                return result;

            // Chunks are word-aligned: an odd size carries one pad byte.
            cursor.at += chunkSize + (chunkSize & 1u);
        }

        if (! seenInfo) return refuse ("INFO", "no INFO list -- every SoundFont has one");
        if (! seenSdta) return refuse ("sdta", "no sample data list");
        if (! seenPdta) return refuse ("pdta", "no hydra: this file has no presets to give");

        return validate();
    }

    [[nodiscard]] Sf2ParseResult parseInfo (Cursor list)
    {
        bool seenVersion = false;

        while (list.remaining() > 0)
        {
            if (! list.has (8))
                return refuse ("INFO", "truncated inside a sub-chunk header");

            const std::string id = list.fourCC();
            const std::uint32_t chunkSize = list.u32();

            if (! list.has (chunkSize))
                return refuse ("INFO", "sub-chunk '" + id + "' runs past the end");

            if (id == "ifil")
            {
                if (chunkSize != 4)
                    return refuse ("ifil", "version chunk is not 4 bytes");

                versionMajor = list.u16();
                versionMinor = list.u16();
                seenVersion = true;
            }
            else
            {
                if (id == "INAM")
                    name = std::string (reinterpret_cast<const char*> (list.data + list.at),
                                        strnlen (reinterpret_cast<const char*> (list.data + list.at),
                                                 chunkSize));

                list.at += chunkSize;
            }

            list.at += (chunkSize & 1u);
        }

        if (! seenVersion)
            return refuse ("ifil", "no version chunk -- mandatory in the format");

        return accept();
    }

    [[nodiscard]] Sf2ParseResult parseSdta (Cursor list)
    {
        while (list.remaining() > 0)
        {
            if (! list.has (8))
                return refuse ("sdta", "truncated inside a sub-chunk header");

            const std::string id = list.fourCC();
            const std::uint32_t chunkSize = list.u32();

            if (! list.has (chunkSize))
                return refuse ("sdta", "sub-chunk '" + id + "' runs past the end");

            if (id == "smpl")
            {
                if ((chunkSize & 1u) != 0)
                    return refuse ("smpl", "sample data has an odd byte count");

                const std::size_t count = chunkSize / 2;
                samples.resize (count);

                for (std::size_t i = 0; i < count; ++i)
                    samples[i] = static_cast<std::int16_t> (list.u16());
            }
            else
            {
                // sm24 and anything else: recognised, skipped, documented.
                list.at += chunkSize;
            }

            list.at += (chunkSize & 1u);
        }

        if (samples.empty())
            return refuse ("smpl", "no sample data");

        return accept();
    }

    [[nodiscard]] Sf2ParseResult parsePdta (Cursor list)
    {
        // The nine, in the order the format fixes. Each is (id, record size).
        struct Expected { const char* id; std::size_t record; };

        static constexpr Expected kOrder[] = {
            { "phdr", 38 }, { "pbag", 4 }, { "pmod", 10 }, { "pgen", 4 },
            { "inst", 22 }, { "ibag", 4 }, { "imod", 10 }, { "igen", 4 },
            { "shdr", 46 },
        };

        for (const auto& expected : kOrder)
        {
            if (! list.has (8))
                return refuse (expected.id, "hydra ends before this chunk");

            const std::string id = list.fourCC();
            const std::uint32_t chunkSize = list.u32();

            if (id != expected.id)
                return refuse (id, std::string ("out of order in the hydra -- expected '")
                                     + expected.id + "'");

            if (! list.has (chunkSize))
                return refuse (id, "chunk runs past the end of the hydra");

            if (chunkSize % expected.record != 0)
                return refuse (id, "size is not a whole number of records");

            const std::size_t count = chunkSize / expected.record;

            if (count < 1)
                return refuse (id, "empty -- even an empty list carries its terminal record");

            Cursor records { list.data, list.at + chunkSize, list.at };

            if (id == "phdr")
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    Sf2Preset preset;
                    preset.name = records.fixedName (20);
                    preset.program = records.u16();
                    preset.bank = records.u16();
                    preset.zoneFirst = records.u16();
                    records.u32(); records.u32(); records.u32();   // library, genre, morphology
                    presets.push_back (std::move (preset));
                }
            }
            else if (id == "pbag" || id == "ibag")
            {
                auto& zones = (id[0] == 'p') ? presetZones : instrumentZones;

                for (std::size_t i = 0; i < count; ++i)
                {
                    Sf2Zone zone;
                    zone.generatorFirst = records.u16();
                    zone.modulatorFirst = records.u16();
                    zones.push_back (zone);
                }
            }
            else if (id == "pmod" || id == "imod")
            {
                auto& modulators = (id[0] == 'p') ? presetModulators : instrumentModulators;

                for (std::size_t i = 0; i < count; ++i)
                {
                    Sf2Modulator modulator;
                    modulator.sourceOper = records.u16();
                    modulator.destOper = records.u16();
                    modulator.amount = static_cast<std::int16_t> (records.u16());
                    modulator.amountSourceOper = records.u16();
                    modulator.transformOper = records.u16();
                    modulators.push_back (modulator);
                }
            }
            else if (id == "pgen" || id == "igen")
            {
                auto& generators = (id[0] == 'p') ? presetGenerators : instrumentGenerators;

                for (std::size_t i = 0; i < count; ++i)
                {
                    Sf2Generator generator;
                    generator.oper = records.u16();

                    const std::uint16_t raw = records.u16();
                    generator.amount = static_cast<std::int16_t> (raw);
                    generator.rangeLow = static_cast<std::uint8_t> (raw & 0xff);
                    generator.rangeHigh = static_cast<std::uint8_t> (raw >> 8);

                    generators.push_back (generator);
                }
            }
            else if (id == "inst")
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    Sf2Instrument instrument;
                    instrument.name = records.fixedName (20);
                    instrument.zoneFirst = records.u16();
                    instruments.push_back (std::move (instrument));
                }
            }
            else if (id == "shdr")
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    Sf2Sample sample;
                    sample.name = records.fixedName (20);
                    sample.start = records.u32();
                    sample.end = records.u32();
                    sample.startLoop = records.u32();
                    sample.endLoop = records.u32();
                    sample.sampleRate = records.u32();
                    sample.originalPitch = records.u8();
                    sample.pitchCorrection = static_cast<std::int8_t> (records.u8());
                    sample.sampleLink = records.u16();
                    sample.sampleType = records.u16();
                    sampleHeaders.push_back (std::move (sample));
                }
            }

            list.at += chunkSize + (chunkSize & 1u);
        }

        return accept();
    }

    // -----------------------------------------------------------------------
    // Cross-checks: the part that makes half-loading impossible
    // -----------------------------------------------------------------------

    [[nodiscard]] Sf2ParseResult validate()
    {
        // Terminal records come off the ends; spans come from index deltas.
        // Every span must be monotonic and in range, or an index somewhere is
        // lying and everything built on it would be garbage that plays.
        auto spanZones = [] (auto& owners, std::size_t zoneCount,
                             const char* chunk) -> Sf2ParseResult
        {
            for (std::size_t i = 0; i + 1 < owners.size(); ++i)
            {
                owners[i].zoneLast = owners[i + 1].zoneFirst;

                if (owners[i].zoneLast < owners[i].zoneFirst)
                    return refuse (chunk, "zone indices run backwards");

                if (owners[i].zoneLast > zoneCount)
                    return refuse (chunk, "zone index points past the zone list");
            }

            if (! owners.empty())
                owners.pop_back();   // the terminal record

            return accept();
        };

        auto spanGenerators = [] (std::vector<Sf2Zone>& zones, std::size_t generatorCount,
                                  std::size_t modulatorCount, const char* chunk) -> Sf2ParseResult
        {
            for (std::size_t i = 0; i + 1 < zones.size(); ++i)
            {
                zones[i].generatorLast = zones[i + 1].generatorFirst;
                zones[i].modulatorLast = zones[i + 1].modulatorFirst;

                if (zones[i].generatorLast < zones[i].generatorFirst
                    || zones[i].modulatorLast < zones[i].modulatorFirst)
                    return refuse (chunk, "generator or modulator indices run backwards");

                if (zones[i].generatorLast > generatorCount
                    || zones[i].modulatorLast > modulatorCount)
                    return refuse (chunk, "generator or modulator index points past its list");
            }

            if (! zones.empty())
                zones.pop_back();   // the terminal zone

            return accept();
        };

        if (auto r = spanZones (presets, presetZones.size(), "phdr"); ! r.ok) return r;
        if (auto r = spanZones (instruments, instrumentZones.size(), "inst"); ! r.ok) return r;

        if (auto r = spanGenerators (presetZones, presetGenerators.size(),
                                     presetModulators.size(), "pbag"); ! r.ok) return r;
        if (auto r = spanGenerators (instrumentZones, instrumentGenerators.size(),
                                     instrumentModulators.size(), "ibag"); ! r.ok) return r;

        // The terminal sample header.
        if (sampleHeaders.empty())
            return refuse ("shdr", "no sample headers");

        sampleHeaders.pop_back();

        // Sample headers must stay inside the pool they index. ROM samples are
        // allowed to point anywhere -- they index memory this file does not
        // carry, and the model marks them unplayable rather than lying.
        for (const auto& sample : sampleHeaders)
        {
            if (sample.isRom())
                continue;

            if (sample.start > sample.end || sample.end > samples.size())
                return refuse ("shdr", "sample '" + sample.name + "' points past the sample data");

            if (sample.startLoop > sample.endLoop
                || sample.endLoop > sample.end || sample.startLoop < sample.start)
                return refuse ("shdr", "sample '" + sample.name + "' has a loop outside itself");
        }

        // Every instrument generator that names a sample must name a real one,
        // and every preset generator that names an instrument likewise. These
        // are the two cross-table pointers everything hangs from.
        constexpr std::uint16_t kGenInstrument = 41;
        constexpr std::uint16_t kGenSampleId = 53;

        for (const auto& generator : instrumentGenerators)
            if (generator.oper == kGenSampleId
                && static_cast<std::size_t> (static_cast<std::uint16_t> (generator.amount))
                     >= sampleHeaders.size())
                return refuse ("igen", "a zone names a sample that does not exist");

        for (const auto& generator : presetGenerators)
            if (generator.oper == kGenInstrument
                && static_cast<std::size_t> (static_cast<std::uint16_t> (generator.amount))
                     >= instruments.size())
                return refuse ("pgen", "a zone names an instrument that does not exist");

        return accept();
    }
};

} // namespace tezla::svarayantra
