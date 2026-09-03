// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <tezla/dsp/Denormals.hpp>
#include <tezla/dsp/Scales.hpp>
#include <tezla/measure/Fft.hpp>

#include <SonitusEngine.hpp>
#include <StackShapes.hpp>

using namespace tezla::sonitus;
using namespace tezla::dsp;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 48000.0;

struct Ranks
{
    std::array<double, kMaxStackCopies> cents {};
    std::array<double, kMaxStackCopies> gains {};
};

Ranks ranksFor (StackMode mode, int copies, int step = 1, double octaves = 0.0,
                const Tuning* tuning = nullptr, int note = 60,
                StackOrigin origin = StackOrigin::centre)
{
    static const Tuning twelveEqual {};

    Ranks r;
    r.gains.fill (1.0);

    stackRanks (mode, copies, step, octaves, origin,
                tuning != nullptr ? *tuning : twelveEqual, note,
                r.cents.data(), r.gains.data());

    return r;
}

struct Buffers
{
    std::vector<double> left, right;
    double* pointers[2] {};

    explicit Buffers (int samples)
        : left (static_cast<std::size_t> (samples), 0.0),
          right (static_cast<std::size_t> (samples), 0.0)
    {
        pointers[0] = left.data();
        pointers[1] = right.data();
    }
};

/// A patch that is nothing but one oscillator's stack, so what is measured is
/// the stack and not the filter's opinion of it.
EngineParameters bareStack (StackMode mode, int copies)
{
    EngineParameters p;

    p.voice.shapeA = OscShape::sine;
    p.voice.levelA = 1.0;
    p.voice.levelB = 0.0;
    p.voice.unisonA = copies;
    p.voice.stackA = mode;
    p.voice.detuneA = 0.0;
    p.voice.spreadA = 0.0;
    p.voice.subLevel = 0.0;

    p.voice.amp.attack = 0.001;
    p.voice.amp.decay = 0.05;
    p.voice.amp.sustain = 1.0;
    p.voice.amp.release = 0.05;

    p.voice.cutoffHz = 20000.0;
    p.voice.resonance = 0.0;
    p.voice.filterDrive = 0.0;
    p.formantMix = 0.0;
    p.tubeDriveDb = 0.0;
    p.tilt = 0.0;
    p.combMode = CombMode::off;

    return p;
}

/// Plays one note and returns the left channel.
///
/// **The parameters are pushed and a block is run before the note.** Changing
/// the oversampling factor is a graph rebuild, and `Engine::process` cuts every
/// sounding note when it sees one -- deliberately, so the player hears the stop
/// rather than a voice that is "playing" silence. A test that set the factor
/// and played immediately measured pure zeros and read it as a broken stack;
/// the settle block is what a plugin does anyway, since its parameters arrive
/// before its first note. Fixed at 64 samples whatever the render block is, so
/// two runs at different block sizes start from the identical state.
std::vector<double> play (Engine& engine, const EngineParameters& parameters, int note,
                          int samples, int blockSize = 256)
{
    engine.setParameters (parameters);

    {
        Buffers settle (64);
        engine.process (settle.pointers, 64);
    }

    engine.noteOn (note, 1.0);

    std::vector<double> out;
    out.reserve (static_cast<std::size_t> (samples));

    Buffers buffers (blockSize);
    int done = 0;

    while (done < samples)
    {
        const int take = std::min (blockSize, samples - done);

        std::fill (buffers.left.begin(), buffers.left.end(), 0.0);
        std::fill (buffers.right.begin(), buffers.right.end(), 0.0);

        engine.process (buffers.pointers, take);

        for (int i = 0; i < take; ++i)
            out.push_back (buffers.left[static_cast<std::size_t> (i)]);

        done += take;
    }

    return out;
}

/// Plays one note and returns both channels, for the tests that are about
/// where a copy *is* rather than what pitch it is.
struct Stereo
{
    std::vector<double> left, right;
};

Stereo playStereo (Engine& engine, const EngineParameters& parameters, int note,
                   int samples, int blockSize = 256)
{
    engine.setParameters (parameters);

    {
        Buffers settle (64);
        engine.process (settle.pointers, 64);
    }

    engine.noteOn (note, 1.0);

    Stereo out;
    out.left.reserve (static_cast<std::size_t> (samples));
    out.right.reserve (static_cast<std::size_t> (samples));

    Buffers buffers (blockSize);
    int done = 0;

    while (done < samples)
    {
        const int take = std::min (blockSize, samples - done);

        std::fill (buffers.left.begin(), buffers.left.end(), 0.0);
        std::fill (buffers.right.begin(), buffers.right.end(), 0.0);

        engine.process (buffers.pointers, take);

        for (int i = 0; i < take; ++i)
        {
            out.left.push_back (buffers.left[static_cast<std::size_t> (i)]);
            out.right.push_back (buffers.right[static_cast<std::size_t> (i)]);
        }

        done += take;
    }

    return out;
}

/// Where **one band** sits in the image over a window: +1 hard right, -1 hard
/// left, 0 centred.
///
/// Per band rather than broadband, and that is the point rather than a detail.
/// A Shepard stack is symmetric by construction -- for every copy leaving one
/// edge there is one arriving at the other -- so its *broadband* balance is
/// near zero and near constant whatever the panning does, and a broadband
/// measurement reports "nothing is happening" for both modes. What differs is
/// where a given frequency comes from, which needs the spectrum.
double bandBalance (const Stereo& s, std::size_t from, double low, double high)
{
    constexpr std::size_t kSize = 8192;

    if (from + kSize > s.left.size())
        return 0.0;

    const auto energy = [&] (const std::vector<double>& x)
    {
        std::vector<double> block (x.begin() + static_cast<std::ptrdiff_t> (from),
                                   x.begin() + static_cast<std::ptrdiff_t> (from + kSize));

        // Blackman-Harris, as everywhere else here: the bands are a couple of
        // octaves wide and leakage from a loud neighbour would land in them.
        for (std::size_t i = 0; i < kSize; ++i)
        {
            const double t = static_cast<double> (i) / static_cast<double> (kSize - 1);

            block[i] *= 0.35875 - 0.48829 * std::cos (2.0 * std::numbers::pi * t)
                                + 0.14128 * std::cos (4.0 * std::numbers::pi * t)
                                - 0.01168 * std::cos (6.0 * std::numbers::pi * t);
        }

        const auto spectrum = measure::fftOfReal (block);

        double sum = 0.0;

        // The upper half of the transform is the mirror, so only the first
        // half is summed -- doubling the energy would not change a ratio, but
        // reading a band that straddles Nyquist's mirror would.
        for (std::size_t k = 1; k < kSize / 2; ++k)
        {
            const double hz = kRate * static_cast<double> (k) / static_cast<double> (kSize);

            if (hz >= low && hz < high)
                sum += std::norm (spectrum[k]);
        }

        return sum;
    };

    const double l = std::sqrt (energy (s.left));
    const double r = std::sqrt (energy (s.right));

    return l + r > 0.0 ? (r - l) / (r + l) : 0.0;
}

/// The spectral centroid of a window, in Hz. What "the pitch is climbing"
/// looks like to a measurement when the thing climbing is seven sines at once.
double centroidHz (const std::vector<double>& x, std::size_t from, std::size_t count)
{
    constexpr std::size_t kSize = 4096;

    if (from + kSize > x.size())
        return 0.0;

    (void) count;

    std::vector<double> block (x.begin() + static_cast<std::ptrdiff_t> (from),
                               x.begin() + static_cast<std::ptrdiff_t> (from + kSize));

    // Blackman-Harris: the sidelobes of anything cheaper pile up under a stack
    // of seven partials and drag the centroid around on their own.
    for (std::size_t i = 0; i < kSize; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (kSize - 1);
        block[i] *= 0.35875 - 0.48829 * std::cos (6.283185307179586 * t)
                  + 0.14128 * std::cos (12.566370614359172 * t)
                  - 0.01168 * std::cos (18.84955592153876 * t);
    }

    const auto spectrum = measure::fftOfReal (block);

    double weighted = 0.0;
    double total = 0.0;

    // The upper half of the transform is the mirror -- counting it puts the
    // centroid at Nyquist/2 for every signal, which is how this measurement
    // was wrong the first time it was written for Ictus.
    for (std::size_t bin = 1; bin < kSize / 2; ++bin)
    {
        const double magnitude = std::abs (spectrum[bin]);
        const double hz = static_cast<double> (bin) * kRate / static_cast<double> (kSize);

        weighted += magnitude * hz;
        total += magnitude;
    }

    return total > 0.0 ? weighted / total : 0.0;
}

/// The frequency of the strongest partial within an octave of `aroundHz`.
///
/// Shepard's components are an octave apart, so a half-octave window either
/// side picks exactly one of them and follows it as it climbs.
double partialNear (const std::vector<double>& x, std::size_t from, double aroundHz)
{
    constexpr std::size_t kSize = 4096;

    if (from + kSize > x.size())
        return 0.0;

    std::vector<double> block (x.begin() + static_cast<std::ptrdiff_t> (from),
                               x.begin() + static_cast<std::ptrdiff_t> (from + kSize));

    for (std::size_t i = 0; i < kSize; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (kSize - 1);
        block[i] *= 0.35875 - 0.48829 * std::cos (6.283185307179586 * t)
                  + 0.14128 * std::cos (12.566370614359172 * t)
                  - 0.01168 * std::cos (18.84955592153876 * t);
    }

    const auto spectrum = measure::fftOfReal (block);

    const double perBin = kRate / static_cast<double> (kSize);

    const auto lowest = static_cast<std::size_t> (std::max (1.0, aroundHz / std::sqrt (2.0) / perBin));
    const auto highest = std::min (kSize / 2,
                                   static_cast<std::size_t> (aroundHz * std::sqrt (2.0) / perBin));

    std::size_t peak = lowest;
    double best = -1.0;

    for (std::size_t bin = lowest; bin < highest; ++bin)
    {
        const double magnitude = std::abs (spectrum[bin]);

        if (magnitude > best)
        {
            best = magnitude;
            peak = bin;
        }
    }

    // Parabolic interpolation on the log magnitude, so the answer is not
    // quantised to 11.7 Hz -- which is 5% at 235 Hz and would swamp what is
    // being measured.
    if (peak == 0 || peak + 1 >= kSize / 2)
        return static_cast<double> (peak) * perBin;

    const double left = std::log (std::max (std::abs (spectrum[peak - 1]), 1.0e-30));
    const double centre = std::log (std::max (std::abs (spectrum[peak]), 1.0e-30));
    const double right = std::log (std::max (std::abs (spectrum[peak + 1]), 1.0e-30));

    const double denominator = left - 2.0 * centre + right;
    const double offset = denominator != 0.0 ? 0.5 * (left - right) / denominator : 0.0;

    return (static_cast<double> (peak) + std::clamp (offset, -0.5, 0.5)) * perBin;
}
} // namespace

// ---------------------------------------------------------------------------
// Ranks
// ---------------------------------------------------------------------------

TEZLA_TEST (every_stack_mode_puts_one_copy_exactly_on_the_played_pitch)
{
    // The rule that makes Stack safe to turn up: exactly one copy sits at 0
    // cents, so adding copies adds notes *around* the note rather than moving
    // it. An instrument that detuned itself as the Unison knob went up would be
    // unusable whatever it sounded like.
    for (int copies = 1; copies <= kMaxStackCopies; ++copies)
        for (const auto mode : { StackMode::octaves, StackMode::fifths, StackMode::tritones,
                                 StackMode::cluster, StackMode::diminished, StackMode::scale })
        {
            const auto r = ranksFor (mode, copies);

            int atPitch = 0;

            for (int i = 0; i < copies; ++i)
                if (r.cents[static_cast<std::size_t> (i)] == 0.0)
                    ++atPitch;

            if (atPitch != 1)
                std::printf ("    mode %d, %d copies: %d copies at the played pitch\n",
                             static_cast<int> (mode), copies, atPitch);

            CHECK (atPitch == 1);
        }
}

TEZLA_TEST (interval_modes_land_on_the_intervals_they_name)
{
    // Checked against the interval each mode is *named* for rather than against
    // the table it is built from -- computing the expectation from the same
    // array being tested is how the Ictus cavity test passed while the ratios
    // were wrong.
    const struct { StackMode mode; double cents; const char* name; } cases[] {
        { StackMode::octaves,    1200.0, "octaves"    },
        { StackMode::fifths,      700.0, "fifths"     },
        { StackMode::tritones,    600.0, "tritones"   },
        { StackMode::cluster,     100.0, "cluster"    },
        { StackMode::diminished,  300.0, "diminished" },
    };

    for (const auto& c : cases)
    {
        const auto r = ranksFor (c.mode, 5);

        // Ranks -2..+2 for five copies.
        for (int i = 0; i < 5; ++i)
            CHECK_NEAR (r.cents[static_cast<std::size_t> (i)], c.cents * (i - 2), 1.0e-12);

        // ...and every copy sounds.
        for (int i = 0; i < 5; ++i)
            CHECK (r.gains[static_cast<std::size_t> (i)] == 1.0);
    }

    // A tritone stack is symmetric, which is the whole reason it has no root:
    // the interval up and the interval down are the same one.
    const auto tritone = ranksFor (StackMode::tritones, 3);
    CHECK_NEAR (tritone.cents[0], -tritone.cents[2], 1.0e-12);
}

TEZLA_TEST (scale_mode_copies_play_the_keys_the_step_names)
{
    // The expectation comes from `Tuning::frequencyFor` -- the thing Scale mode
    // is *defined* as -- and not from a table of intervals, so a tuning with no
    // octave has to come out right without anything here knowing about it.
    Tuning tuning;

    constexpr int kNote = 60;

    for (const int step : { 1, 2, 4, 7 })
    {
        const auto r = ranksFor (StackMode::scale, 5, step, 0.0, &tuning, kNote);

        const double root = tuning.frequencyFor (kNote);

        for (int i = 0; i < 5; ++i)
        {
            const int rank = stackRank (i, 5);
            const double expected = 1200.0 * std::log2 (tuning.frequencyFor (kNote + rank * step)
                                                          / root);

            CHECK_NEAR (r.cents[static_cast<std::size_t> (i)], expected, 1.0e-9);
        }
    }

    // In twelve-tone equal temperament a step of 7 keys is a fifth, so Scale
    // and Fifths agree -- which is the sanity check that says the mechanism is
    // doing arithmetic rather than reading the interval table by accident.
    const auto scaled = ranksFor (StackMode::scale, 5, 7, 0.0, &tuning, kNote);
    const auto fifths = ranksFor (StackMode::fifths, 5);

    for (int i = 0; i < 5; ++i)
        CHECK_NEAR (scaled.cents[static_cast<std::size_t> (i)],
                    fifths.cents[static_cast<std::size_t> (i)], 1.0e-9);
}

TEZLA_TEST (scale_mode_follows_a_scale_that_has_no_octave)
{
    // Bohlen-Pierce repeats at 3/1 rather than at 2/1, so "a step" there is a
    // degree of a scale with nothing to resolve to -- and the whole reason
    // Scale mode goes through the tuning rather than through a cents table.
    Tuning tuning;
    tuning.setScale (scales::bohlenPierce());

    constexpr int kNote = 60;

    const auto r = ranksFor (StackMode::scale, 3, 1, 0.0, &tuning, kNote);

    const double root = tuning.frequencyFor (kNote);

    for (int i = 0; i < 3; ++i)
    {
        const int rank = stackRank (i, 3);
        const double expected = 1200.0 * std::log2 (tuning.frequencyFor (kNote + rank) / root);

        CHECK_NEAR (r.cents[static_cast<std::size_t> (i)], expected, 1.0e-9);
    }

    // Thirteen equal steps of 3/1 is 146.3 cents a step -- neither a semitone
    // nor anything else twelve-tone has a name for.
    std::printf ("    Bohlen-Pierce step: %.2f cents (12-EDO semitone is 100)\n",
                 r.cents[2] - r.cents[1]);

    CHECK_NEAR (r.cents[2] - r.cents[1], 1200.0 * std::log2 (3.0) / 13.0, 1.0e-6);
}

TEZLA_TEST (scale_mode_silences_a_copy_that_falls_off_the_keyboard)
{
    // A copy with no honest pitch goes quiet rather than being clamped onto one
    // another copy is already playing. Two copies on one pitch is a level error
    // nobody would ever trace back to a stack mode.
    Tuning tuning;

    // Note 120 with seven copies at seven keys apart reaches 141, which is off
    // the end -- `frequencyFor` returns 0 Hz above 127.
    const auto r = ranksFor (StackMode::scale, 7, 7, 0.0, &tuning, 120);

    int silent = 0;

    for (int i = 0; i < 7; ++i)
    {
        const double gain = r.gains[static_cast<std::size_t> (i)];

        CHECK (gain == 0.0 || gain == 1.0);
        CHECK (std::isfinite (r.cents[static_cast<std::size_t> (i)]));

        if (gain == 0.0)
            ++silent;
    }

    std::printf ("    note 120, step 7, 7 copies: %d silenced\n", silent);

    CHECK (silent == 2);   // ranks +2 (134) and +3 (141)
}

// ---------------------------------------------------------------------------
// Shepard, in the instrument
// ---------------------------------------------------------------------------

TEZLA_TEST (the_shepard_glide_is_the_same_speed_at_every_copy_count)
{
    // The units question, and the one that would be inaudible until two
    // oscillators disagreed: `shepardRanks` takes **turns** and the engine
    // counts **octaves travelled**, so the conversion is octaves/count. Get it
    // wrong and an oscillator with seven copies climbs faster than one with
    // three purely because it has more of them.
    for (const double octaves : { 0.25, 1.0, 3.7 })
        for (const int copies : { 3, 5, 7 })
        {
            const auto now = ranksFor (StackMode::shepard, copies, 1, octaves);
            const auto later = ranksFor (StackMode::shepard, copies, 1, octaves + 0.5);

            // Copy 0 has climbed half an octave in half an octave of travel,
            // whatever the count -- unless it wrapped, in which case it has
            // fallen the whole span.
            double climbed = later.cents[0] - now.cents[0];

            if (climbed < 0.0)
                climbed += 1200.0 * copies;

            CHECK_NEAR (climbed, 600.0, 1.0e-9);
        }
}

TEZLA_TEST (the_shepard_accumulator_wraps_without_a_jump)
{
    // 420 octaves is divisible by every copy count from 1 to 7, so the wrap is
    // an exact whole number of turns at any of them -- which is why it is 420
    // and not, say, 100. Bit-exact rather than merely small.
    for (int copies = 1; copies <= kMaxStackCopies; ++copies)
    {
        const auto low = ranksFor (StackMode::shepard, copies, 1, 0.317);
        const auto wrapped = ranksFor (StackMode::shepard, copies, 1,
                                       0.317 + kShepardWrapOctaves);

        for (int i = 0; i < copies; ++i)
        {
            CHECK_NEAR (low.cents[static_cast<std::size_t> (i)],
                        wrapped.cents[static_cast<std::size_t> (i)], 1.0e-9);
            CHECK_NEAR (low.gains[static_cast<std::size_t> (i)],
                        wrapped.gains[static_cast<std::size_t> (i)], 1.0e-12);
        }
    }
}

TEZLA_TEST (a_shepard_patch_climbs_while_its_spectrum_stands_still)
{
    // **Both halves of the illusion, and the first draft only asked for one.**
    //
    // It asserted that the spectral centroid *moves*, which is exactly
    // backwards: a Shepard tone's whole trick is that the spectrum is
    // stationary while every component inside it climbs. The centroid read
    // 402, 398, 399, 396, 401, 403, 404, 402, 400 Hz over a full octave of
    // travel -- the theorem working perfectly -- and the test called it a
    // failure. What climbs is the partials, and that is what to measure.
    ScopedNoDenormals guard;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);

    auto p = bareStack (StackMode::shepard, 7);
    p.voice.amp.decay = 4.0;
    p.voice.amp.sustain = 1.0;
    p.shepardRate = 1.0;          // one octave per second

    const auto rendered = play (*engine, p, 60, static_cast<int> (kRate * 2.5));

    // ---- the components climb --------------------------------------------
    //
    // Followed one at a time: at each step the partial is looked for where the
    // *previous* measurement plus the elapsed climb says it should be, so the
    // tracker cannot silently jump to the neighbour an octave up.
    constexpr double kStep = 0.2;                       // seconds
    constexpr double kExpected = 1.148698354997035;     // 2^0.2

    double where = partialNear (rendered, static_cast<std::size_t> (kRate * 0.6), 300.0);

    std::printf ("    shepard partial: %.1f", where);

    for (int step = 1; step <= 4; ++step)
    {
        const auto at = static_cast<std::size_t> (kRate * (0.6 + kStep * step));
        const double found = partialNear (rendered, at, where * kExpected);

        std::printf (" -> %.1f", found);

        const double climbed = found / where;

        CHECK_NEAR (climbed, kExpected, 0.03);

        where = found;
    }

    std::printf (" Hz (x%.3f per %.1f s, expected x%.3f)\n",
                 std::pow (where / partialNear (rendered,
                                                static_cast<std::size_t> (kRate * 0.6), 300.0),
                           0.25),
                 kStep, kExpected);

    // ---- and the spectrum does not --------------------------------------
    //
    // The identity from `Shepard.hpp` seen through the whole instrument: the
    // summed power is flat, so the centre of gravity does not move even though
    // everything under it is rising.
    std::vector<double> centroids;

    for (int step = 0; step <= 8; ++step)
    {
        const auto at = static_cast<std::size_t> (kRate * (0.5 + 0.125 * step));
        centroids.push_back (centroidHz (rendered, at, 4096));
    }

    double lowest = 1.0e300;
    double highest = -1.0e300;

    for (const double c : centroids)
    {
        lowest = std::min (lowest, c);
        highest = std::max (highest, c);
    }

    std::printf ("    centroid over one octave of travel: %.0f .. %.0f Hz (%.1f%% swing)\n",
                 lowest, highest, 100.0 * (highest - lowest) / lowest);

    CHECK ((highest - lowest) / lowest < 0.05);

    // ...and after a whole octave of travel it is where it began, which is the
    // Risset claim itself: the sound has returned to itself having risen.
    const double returned = std::abs (centroids.back() - centroids.front()) / centroids.front();

    std::printf ("    returns to within %.2f%% after one octave\n", 100.0 * returned);

    CHECK (returned < 0.02);
}

TEZLA_TEST (a_stack_is_the_same_at_every_block_size)
{
    // CLAUDE.md section 7: the control grid is counted in samples and the loop
    // is cut at its boundary, so the host's buffer size cannot reach the
    // output. Shepard is the case that would break it -- it is the one mode
    // that recomputes every chunk.
    ScopedNoDenormals guard;

    auto first = std::make_unique<Engine>();
    auto second = std::make_unique<Engine>();

    first->prepare (kRate, 512);
    second->prepare (kRate, 512);

    auto p = bareStack (StackMode::shepard, 7);
    p.voice.amp.decay = 2.0;
    p.voice.amp.sustain = 1.0;
    p.shepardRate = 2.0;
    p.oversampling = OversamplingMode::Off;

    const int samples = static_cast<int> (kRate * 0.5);

    const auto small = play (*first, p, 55, samples, 64);
    const auto large = play (*second, p, 55, samples, 512);

    std::size_t differing = 0;

    for (std::size_t i = 0; i < small.size(); ++i)
        if (small[i] != large[i])
            ++differing;

    std::printf ("    64 vs 512 samples: %zu of %zu differ\n", differing, small.size());

    CHECK (differing == 0);
}

TEZLA_TEST (a_shepard_stack_costs_what_the_tooltip_says)
{
    // The only mode with a running cost: it recomputes seven pitches and seven
    // gains per bank per control chunk, where every other mode pushes the same
    // numbers and is refused as a no-op. The tooltip claims "a percent or two
    // of a core at thirty-two voices" and this is that claim.
    ScopedNoDenormals guard;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);

    auto p = bareStack (StackMode::shepard, 7);
    p.voice.unisonB = 7;
    p.voice.stackB = StackMode::shepard;
    p.voice.levelB = 1.0;
    p.voice.amp.decay = 30.0;
    p.voice.amp.sustain = 1.0;
    p.shepardRate = 1.0;
    p.oversampling = OversamplingMode::X4;

    // Measured against the identical patch in Detune mode, because the figure
    // that matters is what the *mode* costs -- 112 oscillators at x4 is
    // expensive before any of this, and quoting the total as Shepard's price
    // would be dishonest in the direction that flatters it.
    const auto measure = [&] (StackMode mode)
    {
        auto patch = p;
        patch.voice.stackA = mode;
        patch.voice.stackB = mode;

        auto e = std::make_unique<Engine>();
        e->prepare (kRate, 512);
        e->setParameters (patch);

        for (int note = 40; note < 56; ++note)
            e->noteOn (note, 1.0);

        Buffers buffers (512);

        for (int i = 0; i < 20; ++i)
            e->process (buffers.pointers, 512);

        constexpr int kBlocks = 200;

        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < kBlocks; ++i)
            e->process (buffers.pointers, 512);

        return std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    };

    // **Best of five, interleaved.** A single pass at each mode swung by ten
    // points of a core between two runs of this very test on this container,
    // which is more than the difference being measured -- so the first draft's
    // conclusion was noise wearing a number's clothes. The minimum is the right
    // statistic for "how fast can this go": contention only ever adds time.
    const auto best = [&measure] (StackMode mode)
    {
        double lowest = 1.0e300;

        for (int pass = 0; pass < 5; ++pass)
            lowest = std::min (lowest, measure (mode));

        return lowest;
    };

    const double detune = best (StackMode::detune);
    const double octaves = best (StackMode::octaves);
    const double shepard = best (StackMode::shepard);

    const double audioSeconds = 200 * 512.0 / kRate;

    std::printf ("    16 notes x 14 stacks of 7 at x4:  detune %.1f%%   octaves %.1f%%"
                 "   shepard %.1f%% of a core\n",
                 100.0 * detune / audioSeconds, 100.0 * octaves / audioSeconds,
                 100.0 * shepard / audioSeconds);

    std::printf ("    shepard costs %+.1f%% of a core over detune (%.2fx)\n",
                 100.0 * (shepard - detune) / audioSeconds, shepard / detune);

    // **Both budgets are ratios against the same patch**, not wall-clock
    // fractions of a core. An absolute figure measures the machine as much as
    // the code -- this container's own baseline moved four points between the
    // two runs that settled these numbers -- and the claim being made is about
    // what the *mode* costs.
    //
    // A fixed-interval mode recomputes nothing after the first chunk of a note,
    // so it must cost what Detune costs.
    CHECK_CPU_BUDGET (octaves, detune * 1.25, "an interval stack against a detune stack");

    // Shepard recomputes seven pitches and seven gains per bank per chunk, and
    // that measured at 1.23x and 1.26x on the two runs the tooltip's figure
    // comes from. The ceiling is set above those with room for a slower
    // machine, and well below the 2x that would mean something had gone wrong.
    CHECK_CPU_BUDGET (shepard, detune * 1.50, "a shepard stack against a detune stack");
}

// ---------------------------------------------------------------------------
// Sag -- one slow instability, shared
// ---------------------------------------------------------------------------

namespace
{
/// A patch with nothing moving but the sag, so what is measured is the sag.
EngineParameters bareSag (double depth, double period)
{
    auto p = bareStack (StackMode::detune, 1);

    p.voice.amp.attack = 0.005;
    p.voice.amp.decay = 60.0;
    p.voice.amp.sustain = 1.0;

    p.voice.sagDepth = depth;
    p.sagDepth = depth;
    p.sagPeriodSeconds = period;

    return p;
}
} // namespace

TEZLA_TEST (sag_at_zero_is_bit_exactly_out_of_the_path)
{
    // The walk keeps walking -- it is still a modulation source at depth 0 --
    // but nothing it does reaches the audio.
    //
    // **The `isExactlyZero` guards on the three shares are a cost saving, not
    // what makes this true**, and a break-check said so: removing the one on
    // the pitch left this passing, because `pow(2, 40 * 0 * sag / 1200)` is
    // `pow(2, 0)` and that is exactly 1.0 by IEEE, as are the level's `pow(10,
    // 0)` and the cutoff's. Exactly the shape of Ictus's Air branch, which was
    // mis-described the same way. What this test actually holds is that the
    // depth is the only thing standing between the walk and the audio -- which
    // is the claim worth having, and which a share reading some other control
    // by mistake would break.
    ScopedNoDenormals guard;

    auto still = std::make_unique<Engine>();
    auto walking = std::make_unique<Engine>();

    still->prepare (kRate, 512);
    walking->prepare (kRate, 512);

    // Identical patches; one has a sag *rate* set, which moves the walk without
    // moving the depth.
    auto a = bareSag (0.0, 20.0);
    auto b = bareSag (0.0, 2.0);

    const int samples = static_cast<int> (kRate * 1.0);

    const auto quiet = play (*still, a, 57, samples);
    const auto lively = play (*walking, b, 57, samples);

    std::size_t differing = 0;

    for (std::size_t i = 0; i < quiet.size(); ++i)
        if (quiet[i] != lively[i])
            ++differing;

    std::printf ("    depth 0, two walk rates: %zu of %zu samples differ\n",
                 differing, quiet.size());

    CHECK (differing == 0);

    // ...and the walk really was moving, or the test above proves nothing.
    std::printf ("    the walk reached %+.4f while contributing nothing\n", walking->getSag());

    CHECK (std::abs (walking->getSag()) > 0.01);
}

TEZLA_TEST (sag_moves_every_voice_by_the_same_cents)
{
    // The whole point, and what separates it from the two drifts already here:
    // common-mode. A chord stays in tune with itself and the instrument goes
    // flat as one machine. `voiceDrift` is the control that must *not* behave
    // like this, so it is measured beside it.
    ScopedNoDenormals guard;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);

    auto p = bareSag (1.0, 3.0);
    engine->setParameters (p);

    Buffers buffers (512);
    engine->process (buffers.pointers, 512);

    for (const int note : { 40, 52, 64 })
        engine->noteOn (note, 1.0);

    for (int block = 0; block < 200; ++block)
        engine->process (buffers.pointers, 512);

    const double cents = engine->getSagCents();

    std::printf ("    three voices sagging by %+.2f cents, from a walk at %+.4f\n",
                 cents, engine->getSag());

    // One number for the instrument, and it is the walk times the depth times
    // the documented share.
    CHECK_NEAR (cents, kSagPitchCents * engine->getSag(), 1.0e-9);
    CHECK (std::abs (cents) > 0.5);
}

TEZLA_TEST (sag_reaches_the_sub_as_well_as_the_top)
{
    // The bug this arrangement exists to avoid: the sub reads
    // `frequency_ * pitchRatio` and ignores `centsA`, so sagging through the
    // cents field -- the obvious place, beside the pitch bend -- would leave
    // the sub perfectly in tune underneath a sagging top. Half the instrument
    // failing is not the effect.
    //
    // Measured with the sub *alone*: if it does not move, nothing here does.
    ScopedNoDenormals guard;

    const auto subPitch = [] (double depth)
    {
        auto engine = std::make_unique<Engine>();
        engine->prepare (kRate, 512);

        auto p = bareSag (depth, 3.0);
        p.voice.levelA = 0.0;
        p.voice.levelB = 0.0;
        p.voice.subLevel = 1.0;
        p.voice.subShape = SubShape::sine;
        p.voice.subOctave = 0;
        p.subSplit = false;

        engine->setParameters (p);

        Buffers buffers (512);
        engine->process (buffers.pointers, 512);

        // Let the walk run somewhere well away from zero before the note, so
        // the note itself is rendered at a settled offset.
        for (int block = 0; block < 400; ++block)
            engine->process (buffers.pointers, 512);

        engine->noteOn (57, 1.0);       // A3, 220 Hz

        std::vector<double> out;
        Buffers render (512);

        for (int block = 0; block < 24; ++block)
        {
            std::fill (render.left.begin(), render.left.end(), 0.0);
            std::fill (render.right.begin(), render.right.end(), 0.0);

            engine->process (render.pointers, 512);

            for (double v : render.left)
                out.push_back (v);
        }

        return std::pair { partialNear (out, 4096, 220.0), engine->getSagCents() };
    };

    const auto [neutral, neutralCents] = subPitch (0.0);
    const auto [sagged, saggedCents] = subPitch (1.0);

    const double measured = 1200.0 * std::log2 (sagged / neutral);

    std::printf ("    sub at %.2f Hz neutral, %.2f Hz sagging: %+.1f cents measured,"
                 " %+.1f expected\n",
                 neutral, sagged, measured, saggedCents - neutralCents);

    CHECK (std::abs (saggedCents) > 5.0);
    CHECK_NEAR (measured, saggedCents - neutralCents, 4.0);
}

TEZLA_TEST (sag_does_not_restart_on_a_note)
{
    // A key going down does not reset the temperature of a transistor, and it
    // does not reset this either -- the same rule the unison drift and the
    // voice card already follow. A sag that restarted per note would be a
    // per-note effect wearing an instrument-wide name.
    ScopedNoDenormals guard;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);

    engine->setParameters (bareSag (1.0, 4.0));

    Buffers buffers (512);

    for (int block = 0; block < 300; ++block)
        engine->process (buffers.pointers, 512);

    const double before = engine->getSag();

    engine->noteOn (60, 1.0);
    engine->process (buffers.pointers, 512);

    const double after = engine->getSag();

    std::printf ("    walk %+.6f before a note, %+.6f after\n", before, after);

    // It has stepped on, because a control chunk went by -- but by a chunk's
    // worth, not back to zero.
    CHECK (std::abs (after - before) < 0.02);
    CHECK (std::abs (before) > 0.02);
}

TEZLA_TEST (sag_is_the_same_at_every_block_size)
{
    ScopedNoDenormals guard;

    auto first = std::make_unique<Engine>();
    auto second = std::make_unique<Engine>();

    first->prepare (kRate, 512);
    second->prepare (kRate, 512);

    const auto p = bareSag (1.0, 2.0);
    const int samples = static_cast<int> (kRate * 0.5);

    const auto small = play (*first, p, 55, samples, 64);
    const auto large = play (*second, p, 55, samples, 512);

    std::size_t differing = 0;

    for (std::size_t i = 0; i < small.size(); ++i)
        if (small[i] != large[i])
            ++differing;

    std::printf ("    sag at 64 vs 512 samples: %zu of %zu differ\n", differing, small.size());

    CHECK (differing == 0);
}

// ---------------------------------------------------------------------------
// Phase 6 -- origin, shear and phase panning
// ---------------------------------------------------------------------------

TEZLA_TEST (every_origin_still_puts_exactly_one_copy_on_the_played_pitch)
{
    // The rule the whole Stack control rests on, re-checked for the two new
    // origins. Up and Down move the *weight* of the stack; if either of them
    // could also move the note, the control would be a tuning error waiting to
    // happen -- and with `stackRank` returning `index` and `index - last`, an
    // off-by-one either way is exactly what it would look like.
    for (int copies = 1; copies <= kMaxStackCopies; ++copies)
        for (const auto origin : { StackOrigin::centre, StackOrigin::up, StackOrigin::down })
            for (const auto mode : { StackMode::octaves, StackMode::fifths, StackMode::tritones,
                                     StackMode::cluster, StackMode::diminished, StackMode::scale })
            {
                const auto r = ranksFor (mode, copies, 1, 0.0, nullptr, 60, origin);

                int atPitch = 0;

                for (int i = 0; i < copies; ++i)
                    if (r.cents[static_cast<std::size_t> (i)] == 0.0)
                        ++atPitch;

                if (atPitch != 1)
                    std::printf ("    mode %d, origin %d, %d copies: %d at the played pitch\n",
                                 static_cast<int> (mode), static_cast<int> (origin),
                                 copies, atPitch);

                CHECK (atPitch == 1);
            }
}

TEZLA_TEST (origin_puts_the_stack_where_it_says_and_nowhere_else)
{
    // Up is every copy at or above the note, Down every copy at or below it,
    // and Centre is what shipped -- which is asserted by comparing against the
    // shipped placement rather than by recomputing it, so a change to the
    // centring rule is caught here too.
    for (int copies = 2; copies <= kMaxStackCopies; ++copies)
    {
        const auto up = ranksFor (StackMode::octaves, copies, 1, 0.0, nullptr, 60, StackOrigin::up);
        const auto down = ranksFor (StackMode::octaves, copies, 1, 0.0, nullptr, 60, StackOrigin::down);

        double highestUp = 0.0, lowestDown = 0.0;

        for (int i = 0; i < copies; ++i)
        {
            const auto index = static_cast<std::size_t> (i);

            CHECK (up.cents[index] >= 0.0);
            CHECK (down.cents[index] <= 0.0);

            highestUp = std::max (highestUp, up.cents[index]);
            lowestDown = std::min (lowestDown, down.cents[index]);
        }

        // And they span the same distance in opposite directions: n copies at
        // octaves reach n-1 octaves away either way.
        const double span = 1200.0 * static_cast<double> (copies - 1);

        CHECK_NEAR (highestUp, span, 1.0e-9);
        CHECK_NEAR (lowestDown, -span, 1.0e-9);
    }
}

TEZLA_TEST (a_shepard_stack_pans_by_where_a_copy_is_rather_than_by_its_rank)
{
    // Panning by rank leaves a rising stack standing still in the image: the
    // ranks never change, so neither does the picture. Panning by phase means
    // each copy crosses the field once per turn.
    //
    // What is asserted is that a *single* copy sweeps monotonically from one
    // side to the other over one turn, which is the claim the tooltip makes,
    // and that the set of copies always spans the field -- so the image is
    // never momentarily collapsed while one copy is mid-crossing.
    constexpr int kCopies = 5;

    double pans[kMaxStackCopies] {};
    double previous = -2.0;

    for (int step = 0; step <= 40; ++step)
    {
        // One turn is `count` octaves of travel, so this walks exactly one.
        const double octaves = static_cast<double> (kCopies) * static_cast<double> (step) / 41.0;

        shepardPans (octaves, kCopies, pans);

        CHECK (pans[0] > previous);
        previous = pans[0];

        double lowest = 2.0, highest = -2.0;

        for (int k = 0; k < kCopies; ++k)
        {
            CHECK (pans[k] >= -1.0 && pans[k] <= 1.0);
            lowest = std::min (lowest, pans[k]);
            highest = std::max (highest, pans[k]);
        }

        // Five copies evenly spaced around the cycle: the gap between the
        // outermost two is never less than (count-1)/count of the whole field.
        CHECK (highest - lowest >= 2.0 * (kCopies - 1) / static_cast<double> (kCopies) - 1.0e-9);
    }
}

TEZLA_TEST (phase_panning_fixes_where_a_frequency_comes_from_and_rank_panning_does_not)
{
    // **The first version of this test asserted the opposite** and failed, which
    // is the whole reason it is worth reading. The intuition was "phase panning
    // sweeps the image"; what a Shepard stack actually does is tie a copy's
    // pitch, its window gain *and* its position to one phase, so the copies
    // move and the ensemble does not. One arrives where another leaves.
    //
    // Panning by *rank* is the one that churns, because rank is fixed while
    // pitch climbs through it, so position and pitch are out of step.
    //
    // So what is measured is where a given **band** comes from, over time.
    auto p = bareStack (StackMode::shepard, 5);

    p.voice.spreadA = 1.0;
    p.shepardRate = 0.5;
    p.voice.amp.decay = 8.0;
    p.voice.amp.sustain = 1.0;

    const int samples = static_cast<int> (kRate * 6.0);

    auto byRank = std::make_unique<Engine>();
    auto byPhase = std::make_unique<Engine>();

    byRank->prepare (kRate, 512);
    byPhase->prepare (kRate, 512);

    auto q = p;
    q.voice.shepardPanA = true;

    const auto fixed = playStereo (*byRank, p, 60, samples);
    const auto swept = playStereo (*byPhase, q, 60, samples);

    struct Band { double low, high; const char* name; };

    const Band bands[] = { { 60.0, 130.0, "  60-130" }, { 260.0, 520.0, " 260-520" },
                           { 1040.0, 2080.0, "1040-2080" } };

    std::printf ("    band Hz      by rank: 1s / 3s / 5s        by phase: 1s / 3s / 5s\n");

    double rankSwing = 0.0;
    double phaseSwing = 0.0;
    double lowest = 2.0, highest = -2.0;

    for (const auto& band : bands)
    {
        double rank[3] {}, phase[3] {};
        int slot = 0;

        for (const double seconds : { 1.0, 3.0, 5.0 })
        {
            const auto from = static_cast<std::size_t> (kRate * seconds);

            rank[slot] = bandBalance (fixed, from, band.low, band.high);
            phase[slot] = bandBalance (swept, from, band.low, band.high);
            ++slot;
        }

        std::printf ("    %-9s  %+7.3f %+7.3f %+7.3f      %+7.3f %+7.3f %+7.3f\n",
                     band.name, rank[0], rank[1], rank[2], phase[0], phase[1], phase[2]);

        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j)
            {
                rankSwing = std::max (rankSwing, std::abs (rank[i] - rank[j]));
                phaseSwing = std::max (phaseSwing, std::abs (phase[i] - phase[j]));
            }

        lowest = std::min (lowest, phase[0]);
        highest = std::max (highest, phase[0]);
    }

    // By phase, a band comes from the same place all the way through --
    // measured at 0.001 across five seconds, so 0.02 is twenty times the
    // observed drift and still an order of magnitude below the rank figure.
    CHECK (phaseSwing < 0.02);

    // By rank, it does not: measured swings past 1.0 of the whole field.
    CHECK (rankSwing > 0.5);

    // And the fan really is a fan: the lowest band and the highest sit in
    // different places. Measured -0.182 against +0.747.
    CHECK (highest - lowest > 0.5);
}

TEZLA_TEST (shear_runs_the_second_stack_against_the_first)
{
    // Shear is arithmetic on the second accumulator: B advances by
    // `(1 - 2 * shear)` of A's step, so 0 locks them, 0.5 holds B still and 1
    // makes B fall exactly as fast as A rises. Asserted on the accumulators
    // themselves through the engine's own advance, because the audible effect
    // -- two stacks beating against each other -- is not a number a spectrum
    // can hand back cleanly.
    struct Case { double shear, expected; };

    for (const auto c : { Case { 0.0, 1.0 }, Case { 0.25, 0.5 },
                          Case { 0.5, 0.0 }, Case { 1.0, -1.0 } })
    {
        auto p = bareStack (StackMode::shepard, 5);

        p.voice.stackB = StackMode::shepard;
        p.voice.unisonB = 5;
        p.voice.levelB = 1.0;
        p.shepardRate = 1.0;
        p.shepardShear = c.shear;

        auto engine = std::make_unique<Engine>();
        engine->prepare (kRate, 512);

        const auto rendered = play (*engine, p, 60, static_cast<int> (kRate * 2.0));

        (void) rendered;

        const double a = engine->getShepardOctaves();

        // **Unwrapped first.** Both accumulators wrap at kShepardWrapOctaves,
        // and a falling one wraps immediately -- two octaves down reads as 418
        // up. That is the wrap working (420 is divisible by every copy count,
        // so the window's phase is identical either side of it), but it is not
        // a ratio, and reading it as one is how a test reports a working
        // mechanism as broken.
        double b = engine->getShepardOctavesB();

        if (b > 0.5 * kShepardWrapOctaves)
            b -= kShepardWrapOctaves;

        std::printf ("    shear %.2f:  A %+.4f octaves,  B %+.4f  (ratio %+.4f)\n",
                     c.shear, a, b, a != 0.0 ? b / a : 0.0);

        CHECK (a > 0.5);                       // A really did climb
        CHECK_NEAR (b / a, c.expected, 1.0e-9);
    }
}

TEZLA_TEST (shear_at_zero_leaves_the_two_stacks_bit_identical)
{
    // The default has to be what shipped: one accumulator's worth of climb,
    // shared. Compared as numbers rather than as audio, because "the two
    // stacks sound the same" is exactly the claim a shared accumulator makes
    // trivially true and a sheared one does not.
    auto p = bareStack (StackMode::shepard, 4);

    p.voice.stackB = StackMode::shepard;
    p.voice.unisonB = 4;
    p.voice.levelB = 1.0;
    p.shepardRate = 0.7;
    p.shepardShear = 0.0;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);

    for (int i = 0; i < 8; ++i)
    {
        const auto rendered = play (*engine, p, 60, static_cast<int> (kRate * 0.25));

        (void) rendered;

        CHECK (engine->getShepardOctaves() == engine->getShepardOctavesB());
    }
}

// ---------------------------------------------------------------------------
// Shepard retrigger
// ---------------------------------------------------------------------------

TEZLA_TEST (shepard_retrigger_off_leaves_every_voice_on_the_one_shared_glide)
{
    // The default has to be what shipped, and "what shipped" is specifically
    // that two notes started seconds apart are at the *same* point of the same
    // climb -- one clock for the instrument. Measured as the two voices'
    // spectra rather than as an internal number, because the shared clock is
    // the audible claim.
    auto p = bareStack (StackMode::shepard, 5);

    p.shepardRate = 0.5;
    p.voice.amp.decay = 8.0;
    p.voice.amp.sustain = 1.0;
    p.voice.shepardRetrigger = false;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);
    engine->setParameters (p);

    Buffers settle (64);
    engine->process (settle.pointers, 64);

    // Two notes an octave apart, the second started a second after the first.
    // With one shared clock they are at the same phase, so the stacks land on
    // the same set of frequencies and the pair beats rather than chorusing.
    engine->noteOn (48, 1.0);

    Buffers gap (512);

    for (int i = 0; i < static_cast<int> (kRate / 512.0); ++i)
        engine->process (gap.pointers, 512);

    const double before = engine->getShepardOctaves();

    engine->noteOn (60, 1.0);

    // Nothing about the clock moved because a note arrived.
    CHECK_NEAR (engine->getShepardOctaves(), before, 1.0e-12);
}

TEZLA_TEST (a_note_on_never_moves_the_shared_shepard_clock_even_with_retrigger_on)
{
    // **This is the test that defends the design**, and it exists because the
    // obvious alternative -- reset the accumulator on a note-on -- was tried as
    // a break-check and the click test did not catch it. It would not: the
    // oscillators' phases stay continuous through a reset, so there is no
    // sample-level step to find, only every sounding voice's stack silently
    // jumping to a different set of pitches. A discontinuity test cannot see a
    // pitch jump, and saying so is the point.
    //
    // So the claim is asserted where it lives: retrigger is an offset **per
    // voice**, and the clock the whole instrument shares is never touched by a
    // note arriving.
    for (const bool retrigger : { false, true })
    {
        auto p = bareStack (StackMode::shepard, 5);

        p.shepardRate = 1.5;
        p.voice.amp.decay = 8.0;
        p.voice.amp.sustain = 1.0;
        p.voice.shepardRetrigger = retrigger;

        auto engine = std::make_unique<Engine>();

        engine->prepare (kRate, 512);
        engine->setParameters (p);

        Buffers block (512);

        engine->process (block.pointers, 64);
        engine->noteOn (48, 1.0);

        for (int i = 0; i < static_cast<int> (kRate * 2.0 / 512.0); ++i)
            engine->process (block.pointers, 512);

        const double beforeA = engine->getShepardOctaves();
        const double beforeB = engine->getShepardOctavesB();

        CHECK (beforeA > 1.0);            // it really has been climbing

        engine->noteOn (60, 1.0);
        engine->noteOn (67, 1.0);

        // Not "close to": the note-on path must not write to it at all.
        CHECK (engine->getShepardOctaves() == beforeA);
        CHECK (engine->getShepardOctavesB() == beforeB);

        // And one more block, to catch a reset deferred to the next control
        // chunk rather than done in the note-on itself.
        engine->process (block.pointers, 512);

        CHECK (engine->getShepardOctaves() > beforeA);
    }
}

TEZLA_TEST (shepard_retrigger_starts_a_new_note_at_the_bottom_and_leaves_the_others)
{
    // The claim, and the reason it is an offset per voice rather than a reset
    // of the clock: a note started later climbs its own line, and the notes
    // already sounding do not move at all.
    //
    // Measured through the placement function, which is where the offset lands
    // -- a voice at offset `o` computes its ranks from `global - o`, so a note
    // taken at global = 2.5 with retrigger on sees 0 at its own note-on and
    // the identical ranks a note taken at global = 0 would have seen.
    double firstCents[kMaxStackCopies] {}, firstGains[kMaxStackCopies] {};
    double laterCents[kMaxStackCopies] {}, laterGains[kMaxStackCopies] {};

    for (int i = 0; i < kMaxStackCopies; ++i)
    {
        firstGains[i] = 1.0;
        laterGains[i] = 1.0;
    }

    static const Tuning twelveEqual {};

    // A note taken when the clock read 0, half a turn later.
    const double halfTurn = 5.0 * 0.5;   // five copies, so a turn is five octaves

    stackRanks (StackMode::shepard, 5, 1, halfTurn - 0.0, StackOrigin::centre,
                twelveEqual, 60, firstCents, firstGains);

    // A note taken when the clock read 2.5, at the same instant -- so its own
    // elapsed travel is zero and it must see what the first note saw at zero.
    double atZeroCents[kMaxStackCopies] {}, atZeroGains[kMaxStackCopies] {};

    for (int i = 0; i < kMaxStackCopies; ++i)
        atZeroGains[i] = 1.0;

    stackRanks (StackMode::shepard, 5, 1, 0.0, StackOrigin::centre,
                twelveEqual, 60, atZeroCents, atZeroGains);

    stackRanks (StackMode::shepard, 5, 1, halfTurn - halfTurn, StackOrigin::centre,
                twelveEqual, 60, laterCents, laterGains);

    for (int i = 0; i < 5; ++i)
    {
        const auto index = static_cast<std::size_t> (i);

        // The retriggered note is at the bottom of its climb, bit for bit.
        CHECK (laterCents[index] == atZeroCents[index]);
        CHECK (laterGains[index] == atZeroGains[index]);
    }

    // And it is genuinely somewhere else from the note that has been climbing:
    // half a turn of five copies is two and a half octaves of travel.
    bool moved = false;

    for (int i = 0; i < 5; ++i)
        if (firstCents[static_cast<std::size_t> (i)] != atZeroCents[static_cast<std::size_t> (i)])
            moved = true;

    CHECK (moved);
}

TEZLA_TEST (a_retriggered_note_lands_on_the_same_partials_however_late_it_is_played)
{
    // **The claim retrigger actually makes**, measured spectrally -- and the
    // first version of this test measured it as a waveform difference and was
    // wrong to.
    //
    // Worth the paragraph, because the mistake looks like a tolerance that
    // needs loosening and is not. Two Shepard stacks whose phases differ by
    // 0.000187 of a turn are 1.12 cents apart, which at 200 Hz is a 0.13 Hz
    // beat: after three seconds they are half a cycle out and a sample-by-
    // sample comparison reads 0.754 of full scale. That is two nearly
    // identical sounds, reported as completely different ones. A pitch claim
    // has to be measured as pitch.
    //
    // So: play a note immediately, and the same note two seconds into the
    // climb, both with retrigger on. Both start their own climb at zero, so
    // both must put their partials in the same places.
    auto p = bareStack (StackMode::shepard, 5);

    p.shepardRate = 0.7;
    p.voice.amp.attack = 0.001;
    p.voice.amp.decay = 6.0;
    p.voice.amp.sustain = 1.0;
    p.voice.shepardRetrigger = true;

    const auto renderAfter = [] (const EngineParameters& parameters, double waitSeconds)
    {
        auto engine = std::make_unique<Engine>();

        engine->prepare (kRate, 512);
        engine->setParameters (parameters);

        Buffers block (512);

        engine->process (block.pointers, 64);

        for (int i = 0; i < static_cast<int> (kRate * waitSeconds / 512.0); ++i)
            engine->process (block.pointers, 512);

        engine->noteOn (55, 1.0);

        std::vector<double> out;

        while (out.size() < 16384)
        {
            std::fill (block.left.begin(), block.left.end(), 0.0);
            std::fill (block.right.begin(), block.right.end(), 0.0);
            engine->process (block.pointers, 512);

            for (int n = 0; n < 512; ++n)
                out.push_back (block.left[static_cast<std::size_t> (n)]);
        }

        return out;
    };

    const auto immediate = renderAfter (p, 0.0);
    const auto late = renderAfter (p, 2.0);

    // The strongest partial of each, interpolated -- the stack's whole point is
    // that the set of partials is what moves, so the loudest one moving is the
    // thing retrigger has to reset.
    // The stack sits an octave or two around the played note, so the search
    // is anchored where a five-copy Shepard stack at note 55 starts.
    const double first = partialNear (immediate, 2048, 200.0);
    const double second = partialNear (late, 2048, 200.0);

    std::printf ("    note at 0 s: %.2f Hz    note at 2 s: %.2f Hz    %+.2f cents\n",
                 first, second,
                 1200.0 * std::log2 (second / std::max (first, 1.0e-9)));

    // Within two cents. Without retrigger the late note is 1.4 octaves up the
    // climb, which is 1680 cents away.
    CHECK (std::abs (1200.0 * std::log2 (second / std::max (first, 1.0e-9))) < 2.0);
}

TEZLA_TEST (shepard_retrigger_moves_a_note_that_starts_late_and_nothing_else)
{
    // The other half: a note taken well into the climb must differ, and by a
    // lot. Without this the test above would pass on a retrigger that did
    // nothing at all.
    auto p = bareStack (StackMode::shepard, 5);

    p.shepardRate = 0.7;
    p.voice.amp.decay = 6.0;
    p.voice.amp.sustain = 1.0;

    auto off = std::make_unique<Engine>();
    auto on = std::make_unique<Engine>();

    off->prepare (kRate, 512);
    on->prepare (kRate, 512);

    auto q = p;
    p.voice.shepardRetrigger = false;
    q.voice.shepardRetrigger = true;

    const auto render = [] (Engine& engine, const EngineParameters& parameters)
    {
        engine.setParameters (parameters);

        Buffers block (512);

        // Two seconds of clock before the note, so the shared glide is 1.4
        // octaves in and the retriggered one is not.
        for (int i = 0; i < static_cast<int> (kRate * 2.0 / 512.0); ++i)
            engine.process (block.pointers, 512);

        engine.noteOn (55, 1.0);

        std::vector<double> out;

        for (int i = 0; i < static_cast<int> (kRate * 1.0 / 512.0); ++i)
        {
            std::fill (block.left.begin(), block.left.end(), 0.0);
            std::fill (block.right.begin(), block.right.end(), 0.0);
            engine.process (block.pointers, 512);

            for (int n = 0; n < 512; ++n)
                out.push_back (block.left[static_cast<std::size_t> (n)]);
        }

        return out;
    };

    const auto a = render (*off, p);
    const auto b = render (*on, q);

    double worst = 0.0;

    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));

    std::printf ("    late note, retrigger off against on:   worst |difference| %.3g\n", worst);

    CHECK (worst > 0.05);
}

TEZLA_TEST (shepard_retrigger_is_the_same_at_every_block_size)
{
    // The offset is captured at a note-on and then held, so it must not be a
    // per-block quantity -- CLAUDE.md section 7's buffer-size rule, which
    // Emberdrive failed by 0.296 of full scale before the control loop was cut
    // at the chunk boundary rather than the callback's.
    auto p = bareStack (StackMode::shepard, 5);

    p.shepardRate = 0.7;
    p.voice.amp.decay = 6.0;
    p.voice.amp.sustain = 1.0;
    p.voice.spreadA = 0.6;
    p.voice.shepardRetrigger = true;

    const int samples = static_cast<int> (kRate * 3.0);

    auto small = std::make_unique<Engine>();
    auto large = std::make_unique<Engine>();

    small->prepare (kRate, 512);
    large->prepare (kRate, 512);

    const auto a = play (*small, p, 55, samples, 64);
    const auto b = play (*large, p, 55, samples, 512);

    double worst = 0.0;

    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));

    std::printf ("    64 against 512 samples, retrigger on:  worst |difference| %.3g\n", worst);

    CHECK (worst == 0.0);
}

TEZLA_TEST (a_retriggered_shepard_note_starts_quietly_enough_not_to_click)
{
    // Playing a second note into a held one must not produce a discontinuity.
    //
    // **This does not discriminate the design**, and the comment used to claim
    // it did. Break-checked: resetting the shared clock on a note-on -- the
    // alternative this design rejects -- leaves this test green, because the
    // oscillators' phases stay continuous through a reset and only their
    // *pitches* jump. What defends the design is
    // `a_note_on_never_moves_the_shared_shepard_clock_even_with_retrigger_on`.
    // This one is still worth having for what it does cover: that the join
    // itself is clean.
    auto p = bareStack (StackMode::shepard, 5);

    p.shepardRate = 1.5;                 // fast, so the clock is far from zero
    p.voice.amp.attack = 0.001;
    p.voice.amp.decay = 8.0;
    p.voice.amp.sustain = 1.0;
    p.voice.shepardRetrigger = true;

    auto engine = std::make_unique<Engine>();
    engine->prepare (kRate, 512);
    engine->setParameters (p);

    Buffers settle (64);
    engine->process (settle.pointers, 64);

    engine->noteOn (48, 1.0);

    Buffers block (128);

    // Two seconds of climbing, so the clock is nowhere near a turn boundary.
    for (int i = 0; i < static_cast<int> (kRate * 2.0 / 128.0); ++i)
        engine->process (block.pointers, 128);

    // The largest step the held note takes on its own, as the reference.
    double heldStep = 0.0;
    double previous = 0.0;

    for (int i = 0; i < 40; ++i)
    {
        std::fill (block.left.begin(), block.left.end(), 0.0);
        std::fill (block.right.begin(), block.right.end(), 0.0);
        engine->process (block.pointers, 128);

        for (int n = 0; n < 128; ++n)
        {
            const double value = block.left[static_cast<std::size_t> (n)];
            heldStep = std::max (heldStep, std::abs (value - previous));
            previous = value;
        }
    }

    // Now add a note, and measure the same thing across the join.
    engine->noteOn (60, 1.0);

    double joinStep = 0.0;

    for (int i = 0; i < 40; ++i)
    {
        std::fill (block.left.begin(), block.left.end(), 0.0);
        std::fill (block.right.begin(), block.right.end(), 0.0);
        engine->process (block.pointers, 128);

        for (int n = 0; n < 128; ++n)
        {
            const double value = block.left[static_cast<std::size_t> (n)];
            joinStep = std::max (joinStep, std::abs (value - previous));
            previous = value;
        }
    }

    std::printf ("    held note max step %.4f,  across a retriggered note-on %.4f\n",
                 heldStep, joinStep);

    // The new note legitimately adds signal, so the step grows -- what must
    // not happen is a discontinuity, which for a stack jerked back to phase 0
    // would be a large multiple rather than a small one.
    CHECK (joinStep < heldStep * 4.0);
}
