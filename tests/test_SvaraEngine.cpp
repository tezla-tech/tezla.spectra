// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <tezla/dsp/Scales.hpp>

#include "Sf2TestBuilder.hpp"

#include <Sf2File.hpp>
#include <Sf2Model.hpp>
#include <SvaraEngine.hpp>

using namespace tezla::svarayantra;
namespace dsp = tezla::dsp;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kSourcePeriod = 100;   // 480 Hz at 48 kHz
constexpr int kRootKey = 57;

/// A ready-to-play instrument around one looped sine, plus the buffers.
struct Rig
{
    Sf2File file;
    Sf2Model model;
    SvaraEngine engine;
    std::vector<double> left, right;

    explicit Rig (sf2test::SimpleFont font)
    {
        const auto bytes = font.build();
        CHECK (file.parse (bytes.data(), bytes.size()).ok);
        model.build (file);

        engine.prepare (kRate);
        engine.setFont (&file, &model);
    }

    void render (int count)
    {
        left.assign (static_cast<std::size_t> (count), -1.0);
        right.assign (static_cast<std::size_t> (count), -1.0);
        engine.process (left.data(), right.data(), count);
    }
};

[[nodiscard]] sf2test::SimpleFont sineFont()
{
    return sf2test::sineFont (kSourcePeriod, 4, static_cast<std::uint32_t> (kRate),
                              kRootKey);
}

/// Frequency from interpolated positive-going zero crossings: exact enough
/// (~0.01%) to tell 440 from 432 and a tritave from an octave.
[[nodiscard]] double estimateFrequency (const std::vector<double>& signal,
                                        double rate)
{
    double first = -1.0, last = -1.0;
    int cycles = 0;

    for (std::size_t i = 1; i < signal.size(); ++i)
    {
        if (! (signal[i - 1] < 0.0 && signal[i] >= 0.0))
            continue;

        const double frac = -signal[i - 1] / (signal[i] - signal[i - 1]);
        const double at = static_cast<double> (i - 1) + frac;

        if (first < 0.0)
            first = at;
        else
        {
            last = at;
            ++cycles;
        }
    }

    if (cycles < 1)
        return 0.0;

    return static_cast<double> (cycles) * rate / (last - first);
}

/// Renders a fresh note on a fresh rig and estimates its frequency.
[[nodiscard]] double playedFrequency (Rig& rig, int key)
{
    rig.engine.noteOn (key, 127);
    rig.render (2048);            // settle past the attack region
    rig.render (16384);
    rig.engine.allNotesOff();
    (void) rig.engine.activeVoiceCount();
    return estimateFrequency (rig.left, kRate);
}
} // namespace

// ---------------------------------------------------------------------------
// The default path is the sample, verbatim
// ---------------------------------------------------------------------------

TEZLA_TEST (a_note_at_its_root_key_reproduces_the_sample)
{
    // Root key, default 12-TET at A440, no filter (the font leaves it open),
    // instant envelope at 0 dB sustain: the output must be the sample data
    // through nothing but the centre pan law. The tuning's frequency and the
    // root's frequency are computed by two different code paths, so the rate
    // is 1.0 to within double rounding rather than bit-exactly -- the
    // tolerance says exactly that.
    Rig rig (sineFont());
    rig.engine.noteOn (kRootKey, 127);
    rig.render (400);

    const double pan = std::cos (0.25 * 3.141592653589793);
    double worst = 0.0;

    for (int i = 0; i < 400; ++i)
    {
        const double expected =
            static_cast<double> (rig.file.samples[static_cast<std::size_t> (i)])
              / 32768.0 * pan;
        worst = std::max (worst, std::abs (rig.left[static_cast<std::size_t> (i)]
                                             - expected));
        worst = std::max (worst, std::abs (rig.right[static_cast<std::size_t> (i)]
                                             - expected));
    }

    CHECK (worst < 1e-9);
}

TEZLA_TEST (silence_in_silence_out_with_and_without_a_font)
{
    Rig rig (sineFont());
    rig.render (512);

    bool allZero = true;

    for (double s : rig.left) allZero = allZero && s == 0.0;
    for (double s : rig.right) allZero = allZero && s == 0.0;

    CHECK (allZero);

    // No font at all: notes are refused, output stays exactly zero.
    rig.engine.setFont (nullptr, nullptr);
    rig.engine.noteOn (60, 127);
    rig.render (512);

    for (double s : rig.left) allZero = allZero && s == 0.0;
    CHECK (allZero);
    CHECK (rig.engine.activeVoiceCount() == 0);
}

// ---------------------------------------------------------------------------
// The tuning owns the pitch
// ---------------------------------------------------------------------------

TEZLA_TEST (the_tuning_decides_the_played_frequency)
{
    // An octave above the root in 12-TET doubles the rate: 960 Hz from the
    // 480 Hz source.
    {
        Rig rig (sineFont());
        const double hz = playedFrequency (rig, kRootKey + 12);
        CHECK (std::abs (hz - 960.0) < 1.0);
    }

    // Concert pitch A432 lowers everything by the ratio 432/440 -- the
    // sample plays slower, nothing else changes.
    {
        Rig rig (sineFont());
        rig.engine.tuning().setConcertPitch (432.0);
        const double hz = playedFrequency (rig, kRootKey + 12);
        CHECK (std::abs (hz - 960.0 * 432.0 / 440.0) < 1.0);
    }
}

TEZLA_TEST (bohlen_pierce_repeats_at_the_tritave_through_samples)
{
    // Thirteen Bohlen-Pierce steps are a 3/1, not an octave: the whole point
    // of putting the microtuning inside the sampler. Both notes ride the
    // same sample, pitched by the tuning.
    Rig rigLow (sineFont());
    CHECK (rigLow.engine.tuning().setScale (dsp::scales::bohlenPierce()));
    const double low = playedFrequency (rigLow, 60);

    Rig rigHigh (sineFont());
    CHECK (rigHigh.engine.tuning().setScale (dsp::scales::bohlenPierce()));
    const double high = playedFrequency (rigHigh, 60 + 13);

    CHECK (std::abs (high / low - 3.0) < 0.01);
}

TEZLA_TEST (scale_tuning_zero_plays_every_key_at_the_recording)
{
    // A drum zone: scaleTuning 0 pins the pitch to the recording, so the
    // tuning -- microtonal or not -- moves nothing.
    auto font = sineFont();
    font.instrumentGens.push_back ({ 56, 0 });   // scaleTuning

    Rig rigLow (font);
    const double low = playedFrequency (rigLow, 45);

    Rig rigHigh (font);
    rigHigh.engine.tuning().setConcertPitch (432.0);
    const double high = playedFrequency (rigHigh, 81);

    CHECK (std::abs (low - 480.0) < 0.5);
    CHECK (std::abs (high - 480.0) < 0.5);
}

TEZLA_TEST (pitch_bend_moves_by_the_bend_range)
{
    // Full bend at the default +-2 semitone range: the root plays a whole
    // tone up. The bend is set before the note here; the mid-note path goes
    // through the same control heads and is covered by the block-size test.
    Rig rig (sineFont());
    rig.engine.setPitchBend (1.0);
    const double hz = playedFrequency (rig, kRootKey);

    CHECK (std::abs (hz - 480.0 * std::exp2 (200.0 / 1200.0)) < 1.0);
}

// ---------------------------------------------------------------------------
// Voices live and die by activity
// ---------------------------------------------------------------------------

TEZLA_TEST (voices_retire_on_note_off_and_the_count_says_so)
{
    Rig rig (sineFont());
    rig.engine.noteOn (60, 127);
    rig.render (512);
    CHECK (rig.engine.activeVoiceCount() == 1);

    // The font's release is the instant default: one render later the voice
    // is gone and the output is exactly zero again -- not merely quiet.
    rig.engine.noteOff (60);
    rig.render (64);
    CHECK (rig.engine.activeVoiceCount() == 0);

    rig.render (256);

    bool allZero = true;

    for (double s : rig.left) allZero = allZero && s == 0.0;

    CHECK (allZero);
}

TEZLA_TEST (the_sustain_pedal_defers_note_off_until_it_lifts)
{
    Rig rig (sineFont());
    rig.engine.setSustainPedal (true);
    rig.engine.noteOn (60, 127);
    rig.render (256);

    rig.engine.noteOff (60);
    rig.render (4096);
    CHECK (rig.engine.activeVoiceCount() == 1);   // held by the pedal

    rig.engine.setSustainPedal (false);
    rig.render (64);
    CHECK (rig.engine.activeVoiceCount() == 0);
}

TEZLA_TEST (an_exclusive_class_chokes_the_previous_note_quickly_not_instantly)
{
    auto font = sineFont();
    font.instrumentGens.push_back ({ 57, 1 });   // exclusiveClass 1

    Rig rig (font);
    rig.engine.noteOn (60, 127);
    rig.render (256);
    CHECK (rig.engine.activeVoiceCount() == 1);

    // The choke starts a ~10 ms quick release: both voices sound for that
    // moment (a choke that cut dead would pop), then the old one retires.
    rig.engine.noteOn (62, 127);
    rig.render (64);
    CHECK (rig.engine.activeVoiceCount() == 2);

    rig.render (700);                            // past 480 samples of fade
    CHECK (rig.engine.activeVoiceCount() == 1);
}

TEZLA_TEST (stealing_at_the_ceiling_holds_the_count_and_stays_finite)
{
    Rig rig (sineFont());

    for (int key = 20; key < 20 + SvaraEngine::kMaxVoices + 8; ++key)
    {
        rig.engine.noteOn (key, 100);
        rig.render (32);
    }

    CHECK (rig.engine.activeVoiceCount() == SvaraEngine::kMaxVoices);

    rig.render (1024);

    bool finite = true;

    for (double s : rig.left) finite = finite && std::isfinite (s);
    for (double s : rig.right) finite = finite && std::isfinite (s);

    CHECK (finite);
}

TEZLA_TEST (one_voice_starts_per_matching_zone)
{
    // Two instrument zones split at key 60: a note picks only the zone whose
    // range holds it; a layered pair (both full-range) starts two voices.
    sf2test::FontBuilder split;
    split.samples = { [] {
        sf2test::FontBuilder::Sample s;
        s.data.assign (4096, 8000);
        s.loopStart = 8;
        s.loopEnd = 4088;
        s.originalPitch = 57;
        return s;
    }() };
    split.instruments = { { "Split", {
        { { { 43, sf2test::range (0, 59) }, { 54, 1 }, { 53, 0 } } },
        { { { 43, sf2test::range (60, 127) }, { 54, 1 }, { 53, 0 } } },
    } } };
    split.presets = { { "P", 0, 0, { { { { 41, 0 } } } } } };

    const auto bytes = split.build();

    Sf2File file;
    CHECK (file.parse (bytes.data(), bytes.size()).ok);

    Sf2Model model;
    model.build (file);

    SvaraEngine engine;
    engine.prepare (kRate);
    engine.setFont (&file, &model);

    engine.noteOn (40, 100);
    CHECK (engine.activeVoiceCount() == 1);

    engine.noteOn (100, 100);
    CHECK (engine.activeVoiceCount() == 2);
}

// ---------------------------------------------------------------------------
// Block-size independence
// ---------------------------------------------------------------------------

TEZLA_TEST (the_output_does_not_depend_on_the_host_buffer_size)
{
    // Same note, vibrato running (a control that MOVES between every
    // control head), same mid-note bend, rendered in one call against
    // 48-sample blocks -- 48 chosen because its block edges do NOT all sit
    // on the engine's 32-sample grid, which is exactly where a lazy
    // implementation would fire extra control updates. The bend changes at
    // sample 960, a boundary both chunkings reach. Bit-for-bit equality or
    // the engine's output depends on the host's buffer size.
    auto renderChunked = [] (int blockSize)
    {
        Rig rig (sineFont());
        rig.engine.setModWheel (1.0);
        rig.engine.noteOn (kRootKey + 7, 127);

        std::vector<double> all;

        int rendered = 0;

        while (rendered < 4032)
        {
            if (rendered == 960)
                rig.engine.setPitchBend (-0.5);

            const int n = std::min (blockSize, 4032 - rendered);
            rig.render (n);
            all.insert (all.end(), rig.left.begin(), rig.left.end());
            rendered += n;
        }

        return all;
    };

    const auto big = renderChunked (960);
    const auto small = renderChunked (48);

    double worst = 0.0;

    for (std::size_t i = 0; i < big.size(); ++i)
        worst = std::max (worst, std::abs (big[i] - small[i]));

    CHECK (worst == 0.0);
}

// ---------------------------------------------------------------------------
// The mod wheel's vibrato
// ---------------------------------------------------------------------------

TEZLA_TEST (the_mod_wheel_adds_vibrato_and_stillness_returns_without_it)
{
    // Wheel full: +-50 cents of triangle vibrato at the format's default
    // 8.176 Hz. Measured as the spread of short-window frequency estimates:
    // +-50 cents is +-2.9%, so the spread comfortably clears 2%; with the
    // wheel at rest it stays within measurement noise.
    auto spreadWithWheel = [] (double wheel)
    {
        Rig rig (sineFont());
        rig.engine.setModWheel (wheel);
        rig.engine.noteOn (kRootKey, 127);
        rig.render (2048);

        double lowest = 1e9, highest = 0.0;

        for (int window = 0; window < 24; ++window)
        {
            rig.render (480);   // 10 ms windows across ~4 vibrato cycles
            const double hz = estimateFrequency (rig.left, kRate);
            lowest = std::min (lowest, hz);
            highest = std::max (highest, hz);
        }

        return (highest - lowest) / 480.0;
    };

    CHECK (spreadWithWheel (1.0) > 0.02);
    CHECK (spreadWithWheel (0.0) < 0.005);
}
