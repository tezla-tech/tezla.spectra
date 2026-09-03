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
                const Tuning* tuning = nullptr, int note = 60)
{
    static const Tuning twelveEqual {};

    Ranks r;
    r.gains.fill (1.0);

    stackRanks (mode, copies, step, octaves,
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
