// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Builds SoundFont files in memory, so the tests own both ends of the parser
// and ship nobody's sample data. A soundfont the test wrote is a soundfont
// whose every byte the test can assert about -- and the sine font it builds
// is what the interpolation and tuning measurements play through.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sf2test {

/// Little-endian byte assembly, mirroring what the parser disassembles.
struct Bytes
{
    std::vector<std::uint8_t> data;

    void u8 (std::uint8_t value) { data.push_back (value); }

    void u16 (std::uint16_t value)
    {
        data.push_back (static_cast<std::uint8_t> (value & 0xff));
        data.push_back (static_cast<std::uint8_t> (value >> 8));
    }

    void u32 (std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            data.push_back (static_cast<std::uint8_t> ((value >> shift) & 0xff));
    }

    void fourCC (const char* id)
    {
        for (int i = 0; i < 4; ++i)
            data.push_back (static_cast<std::uint8_t> (id[i]));
    }

    void fixedName (const std::string& name, std::size_t width)
    {
        for (std::size_t i = 0; i < width; ++i)
            data.push_back (i < name.size() ? static_cast<std::uint8_t> (name[i]) : 0);
    }

    void append (const Bytes& other)
    {
        data.insert (data.end(), other.data.begin(), other.data.end());
    }
};

/// One generator to place in a zone.
struct Gen
{
    std::uint16_t oper;
    std::int16_t amount;
};

/// A key/velocity range generator's amount, packed low|high.
[[nodiscard]] inline std::int16_t range (int low, int high)
{
    return static_cast<std::int16_t> ((low & 0xff) | ((high & 0xff) << 8));
}

/// A complete, minimal, valid SoundFont: one preset, one instrument, one
/// sample. The generator lists are supplied so tests can exercise zones.
struct SimpleFont
{
    std::string name { "Test font" };
    std::vector<std::int16_t> sampleData;
    std::uint32_t sampleRate { 48000 };
    std::uint32_t loopStart { 0 };
    std::uint32_t loopEnd { 0 };
    std::uint8_t originalPitch { 60 };
    std::int8_t pitchCorrection { 0 };
    std::uint16_t sampleType { 1 };            // mono

    std::vector<Gen> instrumentGens;            // sampleID appended automatically
    std::vector<Gen> presetGens;                // instrument appended automatically

    [[nodiscard]] std::vector<std::uint8_t> build() const
    {
        Bytes info;
        info.fourCC ("ifil"); info.u32 (4); info.u16 (2); info.u16 (1);
        info.fourCC ("INAM");
        const std::uint32_t nameSize = static_cast<std::uint32_t> (name.size() + 1);
        info.u32 (nameSize);
        for (char c : name) info.u8 (static_cast<std::uint8_t> (c));
        info.u8 (0);
        if ((nameSize & 1u) != 0) info.u8 (0);

        Bytes smpl;
        for (std::int16_t s : sampleData) smpl.u16 (static_cast<std::uint16_t> (s));

        Bytes phdr;
        phdr.fixedName ("Preset", 20); phdr.u16 (0); phdr.u16 (0); phdr.u16 (0);
        phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
        phdr.fixedName ("EOP", 20); phdr.u16 (0); phdr.u16 (0); phdr.u16 (1);
        phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);

        const auto presetGenCount = static_cast<std::uint16_t> (presetGens.size() + 1);

        Bytes pbag;
        pbag.u16 (0); pbag.u16 (0);
        pbag.u16 (presetGenCount); pbag.u16 (0);   // terminal zone

        Bytes pmod;
        for (int i = 0; i < 5; ++i) pmod.u16 (0);   // terminal modulator

        Bytes pgen;
        for (const auto& gen : presetGens)
        {
            pgen.u16 (gen.oper);
            pgen.u16 (static_cast<std::uint16_t> (gen.amount));
        }
        pgen.u16 (41); pgen.u16 (0);                // instrument 0, last in the zone

        Bytes inst;
        inst.fixedName ("Instrument", 20); inst.u16 (0);
        inst.fixedName ("EOI", 20); inst.u16 (1);

        const auto instrumentGenCount = static_cast<std::uint16_t> (instrumentGens.size() + 1);

        Bytes ibag;
        ibag.u16 (0); ibag.u16 (0);
        ibag.u16 (instrumentGenCount); ibag.u16 (0);

        Bytes imod;
        for (int i = 0; i < 5; ++i) imod.u16 (0);

        Bytes igen;
        for (const auto& gen : instrumentGens)
        {
            igen.u16 (gen.oper);
            igen.u16 (static_cast<std::uint16_t> (gen.amount));
        }
        igen.u16 (53); igen.u16 (0);                // sampleID 0, last in the zone

        Bytes shdr;
        shdr.fixedName ("Sample", 20);
        shdr.u32 (0);
        shdr.u32 (static_cast<std::uint32_t> (sampleData.size()));
        shdr.u32 (loopStart);
        shdr.u32 (loopEnd);
        shdr.u32 (sampleRate);
        shdr.u8 (originalPitch);
        shdr.u8 (static_cast<std::uint8_t> (pitchCorrection));
        shdr.u16 (0);
        shdr.u16 (sampleType);
        shdr.fixedName ("EOS", 20);
        for (int i = 0; i < 5; ++i) shdr.u32 (0);
        shdr.u8 (0); shdr.u8 (0); shdr.u16 (0); shdr.u16 (0);

        auto chunk = [] (const char* id, const Bytes& payload)
        {
            Bytes out;
            out.fourCC (id);
            out.u32 (static_cast<std::uint32_t> (payload.data.size()));
            out.append (payload);
            if ((payload.data.size() & 1u) != 0) out.u8 (0);
            return out;
        };

        auto list = [] (const char* type, const Bytes& payload)
        {
            Bytes out;
            out.fourCC ("LIST");
            out.u32 (static_cast<std::uint32_t> (payload.data.size() + 4));
            out.fourCC (type);
            out.append (payload);
            return out;
        };

        Bytes pdta;
        pdta.append (chunk ("phdr", phdr)); pdta.append (chunk ("pbag", pbag));
        pdta.append (chunk ("pmod", pmod)); pdta.append (chunk ("pgen", pgen));
        pdta.append (chunk ("inst", inst)); pdta.append (chunk ("ibag", ibag));
        pdta.append (chunk ("imod", imod)); pdta.append (chunk ("igen", igen));
        pdta.append (chunk ("shdr", shdr));

        Bytes body;
        body.fourCC ("sfbk");
        body.append (list ("INFO", info));
        body.append (list ("sdta", chunk ("smpl", smpl)));
        body.append (list ("pdta", pdta));

        Bytes file;
        file.fourCC ("RIFF");
        file.u32 (static_cast<std::uint32_t> (body.data.size()));
        file.append (body);
        return file.data;
    }
};

/// The general builder: any number of presets, instruments and samples, each
/// zone an explicit generator list. Nothing is appended for you -- a zone
/// that should name a sample carries its own sampleID generator, a global
/// zone simply omits it -- so the tests own every byte of the hydra,
/// including deliberately wrong ones. Terminal records and index bookkeeping
/// are the only things build() adds.
struct FontBuilder
{
    struct Zone
    {
        std::vector<Gen> gens;
    };

    struct Sample
    {
        std::string name { "Sample" };
        std::vector<std::int16_t> data;
        std::uint32_t sampleRate { 48000 };
        std::uint32_t loopStart { 0 };      // relative to this sample
        std::uint32_t loopEnd { 0 };
        std::uint8_t originalPitch { 60 };
        std::int8_t pitchCorrection { 0 };
        std::uint16_t sampleType { 1 };
    };

    struct Instrument
    {
        std::string name { "Instrument" };
        std::vector<Zone> zones;
    };

    struct Preset
    {
        std::string name { "Preset" };
        std::uint16_t bank { 0 };
        std::uint16_t program { 0 };
        std::vector<Zone> zones;
    };

    std::string name { "Test font" };
    std::vector<Sample> samples;
    std::vector<Instrument> instruments;
    std::vector<Preset> presets;

    [[nodiscard]] std::vector<std::uint8_t> build() const
    {
        Bytes info;
        info.fourCC ("ifil"); info.u32 (4); info.u16 (2); info.u16 (1);
        info.fourCC ("INAM");
        const std::uint32_t nameSize = static_cast<std::uint32_t> (name.size() + 1);
        info.u32 (nameSize);
        for (char c : name) info.u8 (static_cast<std::uint8_t> (c));
        info.u8 (0);
        if ((nameSize & 1u) != 0) info.u8 (0);

        // The pool: samples end to end, with each one's base offset noted so
        // the headers can speak absolute positions.
        Bytes smpl;
        std::vector<std::uint32_t> base;

        for (const auto& sample : samples)
        {
            base.push_back (static_cast<std::uint32_t> (smpl.data.size() / 2));

            for (std::int16_t s : sample.data)
                smpl.u16 (static_cast<std::uint16_t> (s));
        }

        Bytes shdr;

        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto& sample = samples[i];
            shdr.fixedName (sample.name, 20);
            shdr.u32 (base[i]);
            shdr.u32 (base[i] + static_cast<std::uint32_t> (sample.data.size()));
            shdr.u32 (base[i] + sample.loopStart);
            shdr.u32 (base[i] + sample.loopEnd);
            shdr.u32 (sample.sampleRate);
            shdr.u8 (sample.originalPitch);
            shdr.u8 (static_cast<std::uint8_t> (sample.pitchCorrection));
            shdr.u16 (0);
            shdr.u16 (sample.sampleType);
        }

        shdr.fixedName ("EOS", 20);
        for (int i = 0; i < 5; ++i) shdr.u32 (0);
        shdr.u8 (0); shdr.u8 (0); shdr.u16 (0); shdr.u16 (0);

        // Presets: headers index bags, bags index generators.
        Bytes phdr, pbag, pgen;
        std::uint16_t presetZoneIndex = 0, presetGenIndex = 0;

        for (const auto& preset : presets)
        {
            phdr.fixedName (preset.name, 20);
            phdr.u16 (preset.program);
            phdr.u16 (preset.bank);
            phdr.u16 (presetZoneIndex);
            phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);

            for (const auto& zone : preset.zones)
            {
                pbag.u16 (presetGenIndex); pbag.u16 (0);
                ++presetZoneIndex;

                for (const auto& g : zone.gens)
                {
                    pgen.u16 (g.oper);
                    pgen.u16 (static_cast<std::uint16_t> (g.amount));
                    ++presetGenIndex;
                }
            }
        }

        phdr.fixedName ("EOP", 20); phdr.u16 (0); phdr.u16 (0);
        phdr.u16 (presetZoneIndex);
        phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
        pbag.u16 (presetGenIndex); pbag.u16 (0);

        Bytes pmod;
        for (int i = 0; i < 5; ++i) pmod.u16 (0);

        // Instruments, the same shape one level down.
        Bytes inst, ibag, igen;
        std::uint16_t instrumentZoneIndex = 0, instrumentGenIndex = 0;

        for (const auto& instrument : instruments)
        {
            inst.fixedName (instrument.name, 20);
            inst.u16 (instrumentZoneIndex);

            for (const auto& zone : instrument.zones)
            {
                ibag.u16 (instrumentGenIndex); ibag.u16 (0);
                ++instrumentZoneIndex;

                for (const auto& g : zone.gens)
                {
                    igen.u16 (g.oper);
                    igen.u16 (static_cast<std::uint16_t> (g.amount));
                    ++instrumentGenIndex;
                }
            }
        }

        inst.fixedName ("EOI", 20); inst.u16 (instrumentZoneIndex);
        ibag.u16 (instrumentGenIndex); ibag.u16 (0);

        Bytes imod;
        for (int i = 0; i < 5; ++i) imod.u16 (0);

        auto chunk = [] (const char* id, const Bytes& payload)
        {
            Bytes out;
            out.fourCC (id);
            out.u32 (static_cast<std::uint32_t> (payload.data.size()));
            out.append (payload);
            if ((payload.data.size() & 1u) != 0) out.u8 (0);
            return out;
        };

        auto list = [] (const char* type, const Bytes& payload)
        {
            Bytes out;
            out.fourCC ("LIST");
            out.u32 (static_cast<std::uint32_t> (payload.data.size() + 4));
            out.fourCC (type);
            out.append (payload);
            return out;
        };

        Bytes pdta;
        pdta.append (chunk ("phdr", phdr)); pdta.append (chunk ("pbag", pbag));
        pdta.append (chunk ("pmod", pmod)); pdta.append (chunk ("pgen", pgen));
        pdta.append (chunk ("inst", inst)); pdta.append (chunk ("ibag", ibag));
        pdta.append (chunk ("imod", imod)); pdta.append (chunk ("igen", igen));
        pdta.append (chunk ("shdr", shdr));

        Bytes body;
        body.fourCC ("sfbk");
        body.append (list ("INFO", info));
        body.append (list ("sdta", chunk ("smpl", smpl)));
        body.append (list ("pdta", pdta));

        Bytes file;
        file.fourCC ("RIFF");
        file.u32 (static_cast<std::uint32_t> (body.data.size()));
        file.append (body);
        return file.data;
    }
};

/// A one-cycle-looped sine font: `cycles` whole cycles of a sine whose
/// frequency is exactly `sampleRate / period` -- so looped playback at the
/// root key reproduces the sine bit-cleanly for ever. The loop spans the
/// middle cycles; 8 unlooped samples pad each end as the format expects.
[[nodiscard]] inline SimpleFont sineFont (int period, int cycles, std::uint32_t sampleRate,
                                          std::uint8_t rootKey)
{
    SimpleFont font;
    font.sampleRate = sampleRate;
    font.originalPitch = rootKey;

    const int total = period * cycles + 16;
    font.sampleData.resize (static_cast<std::size_t> (total));

    for (int i = 0; i < total; ++i)
        font.sampleData[static_cast<std::size_t> (i)] = static_cast<std::int16_t> (
            std::lround (30000.0 * std::sin (2.0 * 3.141592653589793 * (i - 8) / period)));

    font.loopStart = 8;
    font.loopEnd = static_cast<std::uint32_t> (8 + period * cycles);
    font.instrumentGens = { { 54, 1 } };   // sampleModes: loop continuously

    return font;
}

} // namespace sf2test
