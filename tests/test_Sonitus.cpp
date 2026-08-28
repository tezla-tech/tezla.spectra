#include "TestFramework.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include <SonitusEngine.hpp>

using namespace tezla::sonitus;
using namespace tezla::dsp;

namespace
{
struct Buffers
{
    std::vector<double> left;
    std::vector<double> right;
    double* pointers[2] {};

    explicit Buffers (int samples)
        : left (static_cast<std::size_t> (samples), 0.0),
          right (static_cast<std::size_t> (samples), 0.0)
    {
        pointers[0] = left.data();
        pointers[1] = right.data();
    }
};

/// A patch with everything nonlinear turned up -- the case CLAUDE.md section 7
/// asks the aliasing figure to be measured at.
EngineParameters brutal()
{
    EngineParameters parameters;

    parameters.voice.shapeA = OscShape::saw;
    parameters.voice.levelA = 1.0;
    parameters.voice.shapeB = OscShape::saw;
    parameters.voice.levelB = 1.0;
    parameters.voice.semitonesB = 7.0;
    parameters.voice.syncB = true;
    parameters.voice.unisonA = 5;
    parameters.voice.unisonB = 5;
    parameters.voice.detuneA = 22.0;
    parameters.voice.detuneB = 22.0;
    parameters.voice.spreadA = 0.6;
    parameters.voice.pmIndex = 3.0;
    parameters.voice.ringAmount = 0.4;
    parameters.voice.foldAmount = 0.6;
    parameters.voice.subLevel = 0.7;
    parameters.voice.cutoffHz = 2000.0;
    parameters.voice.resonance = 0.7;
    parameters.voice.filterDrive = 0.7;
    parameters.voice.filterFm = 0.3;
    parameters.voice.ampAttack = 0.001;
    parameters.voice.ampSustain = 1.0;
    parameters.voice.ampRelease = 0.05;
    parameters.voice.ampVelocity = 0.0;
    parameters.voice.level = 0.5;

    parameters.keyboard = KeyboardMode::mono;
    parameters.tubeDriveDb = 24.0;
    parameters.combMode = CombMode::flange;
    parameters.combMix = 0.9;
    parameters.combFeedback = 0.72;
    parameters.combInverted = true;
    parameters.formantMix = 0.6;
    parameters.formantMorph = 0.4;
    parameters.tilt = 0.3;
    parameters.splitHz = 120.0;

    return parameters;
}

/// Renders one note and returns the left channel.
std::vector<double> play (Engine& engine, const EngineParameters& parameters, int note,
                          int samples, int blockSize = 256)
{
    engine.setParameters (parameters);
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

double peakOf (const std::vector<double>& signal)
{
    double peak = 0.0;

    for (const double sample : signal)
        peak = std::max (peak, std::abs (sample));

    return peak;
}

double amplitudeAt (const std::vector<double>& signal, double frequency, double sampleRate,
                    int from)
{
    double inPhase = 0.0;
    double quadrature = 0.0;

    const int count = static_cast<int> (signal.size()) - from;

    for (int i = 0; i < count; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * (i + from) / sampleRate;

        inPhase += signal[static_cast<std::size_t> (i + from)] * std::sin (phase);
        quadrature += signal[static_cast<std::size_t> (i + from)] * std::cos (phase);
    }

    return 2.0 * std::hypot (inPhase, quadrature) / std::max (count, 1);
}
} // namespace

// ---------------------------------------------------------------------------
// The things it must never do
// ---------------------------------------------------------------------------

TEZLA_TEST (an_instrument_with_no_note_is_exactly_silent)
{
    // Louder than the same rule for an effect: there is nothing upstream to
    // blame, so a leak here is the instrument's and only the instrument's.
    // Every nonlinear stage on, and the feedback loops running, with nothing
    // playing.
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Engine engine;
        engine.prepare (rate, 512);
        engine.setParameters (brutal());

        Buffers buffers (512);

        for (int block = 0; block < 40; ++block)
        {
            std::fill (buffers.left.begin(), buffers.left.end(), 0.0);
            std::fill (buffers.right.begin(), buffers.right.end(), 0.0);

            engine.process (buffers.pointers, 512);

            for (int i = 0; i < 512; ++i)
            {
                CHECK (buffers.left[static_cast<std::size_t> (i)] == 0.0);
                CHECK (buffers.right[static_cast<std::size_t> (i)] == 0.0);
            }
        }
    }
}

TEZLA_TEST (the_whole_instrument_stays_bounded)
{
    // Every nonlinearity at once, a chord rather than a note, and the note
    // released half way so the release tail runs through the same chain.
    Engine engine;
    engine.prepare (48000.0, 512);

    auto parameters = brutal();
    parameters.keyboard = KeyboardMode::poly;

    engine.setParameters (parameters);

    for (int note = 36; note < 44; ++note)
        engine.noteOn (note, 1.0);

    Buffers buffers (512);

    double worst = 0.0;

    for (int block = 0; block < 200; ++block)
    {
        if (block == 100)
            for (int note = 36; note < 44; ++note)
                engine.noteOff (note);

        engine.process (buffers.pointers, 512);

        for (int i = 0; i < 512; ++i)
        {
            CHECK (std::isfinite (buffers.left[static_cast<std::size_t> (i)]));
            CHECK (std::isfinite (buffers.right[static_cast<std::size_t> (i)]));

            worst = std::max ({ worst, std::abs (buffers.left[static_cast<std::size_t> (i)]),
                                std::abs (buffers.right[static_cast<std::size_t> (i)]) });
        }
    }

    // Eight voices of five-oscillator unison through a resonant filter, a
    // valve and a comb at 0.72 feedback. Pinned so a change to any of the
    // bounds inside shows up here rather than in a session.
    CHECK (worst < 40.0);
}

TEZLA_TEST (the_output_is_block_size_independent)
{
    // CLAUDE.md section 7, and the reason the render loop is cut at the control
    // boundary rather than at the callback's. Emberdrive measured 0.296 of full
    // scale between 64- and 512-sample blocks before that was done properly.
    //
    // Everything is moving here: an LFO on the cutoff, a sequencer on the LFO's
    // rate, and a glide between two notes.
    constexpr double rate = 48000.0;
    constexpr int samples = 24000;

    auto parameters = brutal();

    parameters.keyboard = KeyboardMode::legato;
    parameters.glideSeconds = 0.3;
    parameters.lfo1RateHz = 3.0;
    parameters.sequencerRateHz = 6.0;
    parameters.sequencerToLfo1Rate = 1.5;
    parameters.voice.slots[0] = { ModSource::lfo1, ModDestination::cutoff, 2.0 };
    parameters.voice.slots[1] = { ModSource::sequencer, ModDestination::foldAmount, 0.4 };

    // The global matrix moving too, and this half is the part that used to be
    // wrong: the mangle's controls were written once per *callback*, which is
    // the buffer-size dependence CLAUDE.md section 7 is about. Cutting them at
    // the control boundary with everything else is what makes this pass.
    parameters.globalSlots[0] = { GlobalSource::lfo1, GlobalDestination::combTime, 0.7 };
    parameters.globalSlots[1] = { GlobalSource::lfo2, GlobalDestination::tubeDrive, 0.5 };
    parameters.globalSlots[2] = { GlobalSource::sequencer, GlobalDestination::formantMorph, 0.6 };
    parameters.lfo2RateHz = 1.7;

    for (int step = 0; step < StepSequencer::kMaxSteps; ++step)
        parameters.sequencerSteps[static_cast<std::size_t> (step)]
          = std::sin (step * 1.1) * 0.8;

    const auto renderIn = [&] (int blockSize)
    {
        Engine engine;
        engine.prepare (rate, 512);
        engine.setParameters (parameters);

        engine.noteOn (40, 0.9);

        std::vector<double> out;
        Buffers buffers (512);

        int done = 0;

        while (done < samples)
        {
            // The second note must land on the **same sample** at every block
            // size, so the block is cut short at the boundary rather than
            // allowed to straddle it. Without this, a 64-sample block fires it
            // at 11968 and a 512-sample one at 11776, and the test measures its
            // own arithmetic rather than the engine's.
            int take = std::min (blockSize, samples - done);

            if (done < samples / 2)
                take = std::min (take, samples / 2 - done);

            if (done == samples / 2)
                engine.noteOn (52, 0.9);

            engine.process (buffers.pointers, take);

            for (int i = 0; i < take; ++i)
                out.push_back (buffers.left[static_cast<std::size_t> (i)]);

            done += take;
        }

        return out;
    };

    const auto small = renderIn (64);
    const auto large = renderIn (512);
    const auto odd = renderIn (97);

    CHECK (small.size() == large.size());
    CHECK (small.size() == odd.size());

    double worst = 0.0;

    for (std::size_t i = 0; i < small.size(); ++i)
    {
        worst = std::max (worst, std::abs (small[i] - large[i]));
        worst = std::max (worst, std::abs (small[i] - odd[i]));
    }

    CHECK (worst < 1.0e-9);
}

// ---------------------------------------------------------------------------
// Aliasing
// ---------------------------------------------------------------------------

TEZLA_TEST (aliasing_stays_below_sixty_decibels_across_the_bass_range)
{
    // CLAUDE.md section 7's threshold, and the honest scope of it.
    //
    // **The measurement has to be a harmonic patch.** A reese is dense and
    // inharmonic *on purpose* -- five detuned oscillators, a synced partner and
    // a ring modulator -- and `analyseHarmonics` counts everything that is not
    // a harmonic of the fundamental. Pointed at that, it reports the detuning
    // as aliasing and reads 0 dB. So this measures the nonlinear chain with one
    // oscillator, at a bin-exact fundamental: the wave folder, the filter's
    // rail and the tube, which are the stages that can fold energy back.
    //
    // Measured at 48 kHz, one saw at full fold, filter drive and 24 dB of tube:
    //
    //     note      x1       x2       x4       x8
    //     41 Hz  -69.20   -76.86   -82.18   -87.96
    //     55 Hz  -67.83   -72.83   -82.66   -85.20
    //     82 Hz  -66.76   -69.96   -74.12   -86.24
    //    110 Hz  -65.32   -67.62   -71.72   -78.34
    //    165 Hz  -55.71   -68.69   -76.61   -76.21
    //    220 Hz  -47.06   -55.94   -68.07   -75.03
    //    440 Hz  -37.08   -42.31   -56.40   -68.84
    //
    // So Auto's x4 clears -60 dB comfortably from E1 to A3, which is the whole
    // of what this instrument is for, and **does not** clear it at 440 Hz --
    // where the control offers x8 and gets -68.8. That limit is stated rather
    // than hidden: a bass synth played two octaves above where a bass lives is
    // outside what the default is tuned for.
    constexpr double rate = 48000.0;
    constexpr std::size_t window = 1 << 15;

    // A **harmonic** patch: one oscillator, no detune, no ring, no sub.
    EngineParameters parameters;

    parameters.voice.shapeA = OscShape::saw;
    parameters.voice.levelA = 1.0;
    parameters.voice.levelB = 0.0;
    parameters.voice.subLevel = 0.0;
    parameters.voice.unisonA = 1;
    parameters.voice.foldAmount = 0.6;
    parameters.voice.cutoffHz = 16000.0;
    parameters.voice.resonance = 0.7;
    parameters.voice.filterDrive = 0.7;
    parameters.voice.ampAttack = 0.001;
    parameters.voice.ampSustain = 1.0;
    parameters.voice.ampVelocity = 0.0;
    parameters.voice.level = 0.5;
    parameters.keyboard = KeyboardMode::mono;
    parameters.tubeDriveDb = 24.0;
    parameters.splitHz = 20.0;
    parameters.subMono = false;

    const auto aliasingAt = [&] (double wantHz, OversamplingMode mode)
    {
        auto local = parameters;
        local.oversampling = mode;

        const double hz = tezla::measure::binExactFrequency (wantHz, rate, window);

        // The parameters before prepare, so the graph is built at the right
        // factor rather than rebuilt on the first block -- which stops the note.
        Engine engine;
        engine.setParameters (local);
        engine.prepare (rate, 512);
        engine.tuning().setReference (60, hz);
        engine.noteOn (60, 1.0);

        // **A second of pre-roll, in time rather than samples.** The tube's
        // bias shift and supply sag have 33 and 45 ms time constants; 8192
        // samples is 171 ms at 48 kHz and 85 ms at 96 kHz, and the short one
        // read -52.99 dB against a settled -69.89. A settling time counted in
        // samples is a different settling time at every rate.
        const auto preRoll = static_cast<std::size_t> (rate);

        std::vector<double> out;
        out.reserve (window + preRoll);

        Buffers buffers (512);

        while (out.size() < window + preRoll)
        {
            engine.process (buffers.pointers, 512);

            for (int i = 0; i < 512; ++i)
                out.push_back (buffers.left[static_cast<std::size_t> (i)]);
        }

        std::vector<double> tail (out.end() - static_cast<long> (window), out.end());

        return tezla::measure::analyseHarmonics (tail, rate, hz).audibleAliasingDb;
    };

    // E1 to A3 at the Auto factor, which is what a bass instrument plays.
    for (const double note : { 41.2, 55.0, 82.4, 110.0, 164.8, 220.0 })
        CHECK (aliasingAt (note, OversamplingMode::Auto) < -60.0);

    // The limit, pinned rather than hidden.
    CHECK (aliasingAt (440.0, OversamplingMode::X4) > -60.0);
    CHECK (aliasingAt (440.0, OversamplingMode::X4) < -50.0);
    CHECK (aliasingAt (440.0, OversamplingMode::X8) < -60.0);

    // And more oversampling always buys something, at every note. If it did
    // not, the aliasing would be coming from somewhere the oversampling cannot
    // reach -- which is exactly what the naked wave folder was doing.
    for (const double note : { 110.0, 220.0, 440.0 })
    {
        CHECK (aliasingAt (note, OversamplingMode::X2) < aliasingAt (note, OversamplingMode::Off));
        CHECK (aliasingAt (note, OversamplingMode::X4) < aliasingAt (note, OversamplingMode::X2));
        CHECK (aliasingAt (note, OversamplingMode::X8) < aliasingAt (note, OversamplingMode::X4));
    }
}

TEZLA_TEST (the_wave_folder_is_antialiased_rather_than_merely_oversampled)
{
    // CLAUDE.md section 7: oversampling alone never gets there, because a
    // shaper with infinite bandwidth folds back whatever rate it is run at.
    // The folder was by a wide margin the loudest thing in the chain before it
    // was given antiderivative antialiasing -- measured at x4 and 110 Hz, one
    // stage at a time:
    //
    //     bare saw            -92.63 dB
    //     + fold at 0.6       -52.26      (was -43.82 naked)
    //     + fold at 1.0       -26.58      (was -17.08 naked)
    //     + filter drive      -67.14
    //     + tube at 24 dB     -87.71
    //     + tube at 36 dB     -75.34
    //     everything          -71.72      (was -63.59 naked)
    //
    // Eight to ten decibels, which is about what first-order ADAA is worth --
    // roughly a doubling of the internal rate, for a fraction of the cost. The
    // folder is still the loudest stage and that is inherent: a sine folder at
    // full drive folds a full-scale input thirty times per half cycle.
    constexpr double rate = 48000.0;
    constexpr std::size_t window = 1 << 15;

    const auto aliasingWithFold = [&] (double fold)
    {
        EngineParameters parameters;

        parameters.voice.shapeA = OscShape::saw;
        parameters.voice.levelA = 1.0;
        parameters.voice.levelB = 0.0;
        parameters.voice.subLevel = 0.0;
        parameters.voice.unisonA = 1;
        parameters.voice.foldAmount = fold;
        parameters.voice.cutoffHz = 16000.0;
        parameters.voice.ampAttack = 0.001;
        parameters.voice.ampSustain = 1.0;
        parameters.voice.ampVelocity = 0.0;
        parameters.voice.level = 0.5;
        parameters.keyboard = KeyboardMode::mono;
        parameters.splitHz = 20.0;
        parameters.subMono = false;
        parameters.oversampling = OversamplingMode::X4;

        const double hz = tezla::measure::binExactFrequency (110.0, rate, window);

        Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);
        engine.tuning().setReference (60, hz);
        engine.noteOn (60, 1.0);

        const auto preRoll = static_cast<std::size_t> (rate);

        std::vector<double> out;
        Buffers buffers (512);

        while (out.size() < window + preRoll)
        {
            engine.process (buffers.pointers, 512);

            for (int i = 0; i < 512; ++i)
                out.push_back (buffers.left[static_cast<std::size_t> (i)]);
        }

        std::vector<double> tail (out.end() - static_cast<long> (window), out.end());

        return tezla::measure::analyseHarmonics (tail, rate, hz).audibleAliasingDb;
    };

    // The bare oscillator, which is the floor everything else is measured
    // against.
    CHECK (aliasingWithFold (0.0) < -85.0);

    // With ADAA, half fold stays comfortably inside the threshold.
    CHECK (aliasingWithFold (0.6) < -50.0);

    // And full fold is the one place the instrument goes past it, which is
    // stated rather than hidden. A sine folder at full drive has no
    // band-limited version -- only a slower one.
    CHECK (aliasingWithFold (1.0) < -20.0);
    CHECK (aliasingWithFold (1.0) > -35.0);
}

TEZLA_TEST (the_oversampling_control_actually_does_something)
{
    // The bug this exists to catch is Emberdrive's, and it is worth restating
    // because it is invisible: `prepare()` runs before any parameter is known,
    // so anything it configures from a parameter has to be re-checked against
    // what it actually *built* rather than against a "have the parameters
    // arrived" flag. Getting that wrong made the oversampling control silently
    // inert on load, and no test caught it because the measurement and the
    // reference were wrong in the same way.
    //
    // So: the factor the engine reports, and the latency, both have to move.
    constexpr double rate = 48000.0;

    struct Case
    {
        OversamplingMode mode;
        int expectedFactor;
    };

    const Case cases[] = {
        { OversamplingMode::Off, 1 },
        { OversamplingMode::X2, 2 },
        { OversamplingMode::X4, 4 },
        { OversamplingMode::X8, 8 },
        { OversamplingMode::Auto, 4 },     // 48 kHz
    };

    for (const auto& item : cases)
    {
        Engine engine;
        engine.prepare (rate, 512);

        auto parameters = brutal();
        parameters.oversampling = item.mode;

        engine.setParameters (parameters);

        Buffers buffers (512);

        engine.noteOn (40, 1.0);
        engine.process (buffers.pointers, 512);

        CHECK (engine.getOversamplingFactor() == item.expectedFactor);
        CHECK (engine.getLatencySamples() == Oversampler::latencyForFactor (item.expectedFactor));
    }

    // And Auto follows the host rate rather than being a fixed number.
    for (const double hostRate : { 44100.0, 96000.0, 192000.0 })
    {
        Engine engine;
        engine.prepare (hostRate, 512);

        auto parameters = brutal();
        parameters.oversampling = OversamplingMode::Auto;

        engine.setParameters (parameters);

        Buffers buffers (512);

        engine.noteOn (40, 1.0);
        engine.process (buffers.pointers, 512);

        CHECK (engine.getOversamplingFactor() == autoOversamplingFactor (hostRate));
    }
}

// ---------------------------------------------------------------------------
// The mangle section
// ---------------------------------------------------------------------------

TEZLA_TEST (the_sub_band_bypasses_the_mangle_and_is_mono)
{
    // The clause that makes the instrument usable on a track. Below the split
    // nothing happens but a DC blocker and a fold to mono, so the low end stays
    // where it was put while everything above it is being destroyed.
    //
    // Measured on the **side signal at the note's own fundamental**, not on the
    // peak of L-R. The upper band is wide either way and dominates the peak, so
    // comparing peaks compares the part the control does not touch -- which is
    // what the first version of this did.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.splitHz = 150.0;
    parameters.combSpread = 1.0;
    parameters.voice.spreadA = 1.0;
    parameters.voice.spreadB = 1.0;
    parameters.voice.unisonA = 7;
    parameters.voice.unisonB = 7;

    const auto sideAt = [&] (bool mono)
    {
        auto local = parameters;
        local.subMono = mono;

        Engine engine;
        engine.prepare (rate, 512);
        engine.setParameters (local);
        engine.noteOn (33, 1.0);        // A1, 55 Hz -- well below the split

        Buffers buffers (512);

        std::vector<double> side;
        std::vector<double> mid;

        for (int block = 0; block < 80; ++block)
        {
            engine.process (buffers.pointers, 512);

            if (block < 20)
                continue;

            for (int i = 0; i < 512; ++i)
            {
                const double left = buffers.left[static_cast<std::size_t> (i)];
                const double right = buffers.right[static_cast<std::size_t> (i)];

                side.push_back (0.5 * (left - right));
                mid.push_back (0.5 * (left + right));
            }
        }

        struct Result { double side; double mid; };

        return Result { amplitudeAt (side, 55.0, rate, 0),
                        amplitudeAt (mid, 55.0, rate, 0) };
    };

    const auto folded = sideAt (true);
    const auto wide = sideAt (false);

    // The sub is there in both.
    CHECK (folded.mid > 0.001);
    CHECK (wide.mid > 0.001);

    // But with the fold on, there is essentially nothing of it in the side.
    CHECK (wide.side > folded.side * 5.0);
    CHECK (folded.side < folded.mid * 0.05);
}

TEZLA_TEST (the_order_switch_makes_a_different_instrument)
{
    // Tube before comb and comb before tube are not the same sound, for the
    // same reason a tone stack in front of a distortion is a different
    // amplifier from one behind it -- the comb decides what gets distorted, or
    // the tube fills the comb's notches with harmonics it made itself.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.tubeDriveDb = 24.0;
    parameters.combMode = CombMode::flange;
    parameters.combMix = 1.0;
    parameters.combFeedback = 0.72;
    parameters.formantMix = 0.0;

    const auto render = [&] (MangleOrder order)
    {
        auto local = parameters;
        local.order = order;

        Engine engine;
        engine.prepare (rate, 512);

        return play (engine, local, 40, 24000);
    };

    const auto first = render (MangleOrder::tubeThenComb);
    const auto second = render (MangleOrder::combThenTube);

    double worst = 0.0;
    double energy = 0.0;

    for (std::size_t i = 12000; i < first.size(); ++i)
    {
        worst = std::max (worst, std::abs (first[i] - second[i]));
        energy += first[i] * first[i];
    }

    const double rms = std::sqrt (energy / static_cast<double> (first.size() - 12000));

    CHECK (rms > 0.01);

    // Not a subtle difference: the two differ by more than the signal's own
    // RMS. If the order switch did nothing this would be zero.
    CHECK (worst > rms);
}

TEZLA_TEST (the_sequencer_can_step_the_lfos_rate)
{
    // The brief's old trick, built in. Pointing the sequencer at the LFO's rate
    // is what used to be an automation clip drawn on the rate knob.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.combMode = CombMode::off;
    parameters.formantMix = 0.0;
    parameters.tubeDriveDb = 0.0;
    parameters.lfo1RateHz = 1.0;
    parameters.sequencerRateHz = 2.0;
    parameters.sequencerLength = 2;
    parameters.sequencerSteps[0] = -1.0;
    parameters.sequencerSteps[1] = 1.0;

    const auto observe = [&] (double depth)
    {
        auto local = parameters;
        local.sequencerToLfo1Rate = depth;

        Engine engine;
        engine.prepare (rate, 512);
        engine.setParameters (local);
        engine.noteOn (40, 1.0);

        Buffers buffers (512);

        std::vector<double> lfo;

        for (int block = 0; block < 200; ++block)
        {
            engine.process (buffers.pointers, 512);
            lfo.push_back (engine.getGlobalSources().lfo1);
        }

        // How far the LFO travelled in total. A rate that steps between two
        // speeds covers a different distance from one that does not.
        double travelled = 0.0;

        for (std::size_t i = 1; i < lfo.size(); ++i)
            travelled += std::abs (lfo[i] - lfo[i - 1]);

        return travelled;
    };

    const double flat = observe (0.0);
    const double stepped = observe (2.0);

    CHECK (flat > 0.0);

    // Two octaves of swing either way, so half the time it runs at a quarter
    // speed and half at four times -- which covers a great deal more ground.
    CHECK (stepped > flat * 1.5);
}

TEZLA_TEST (the_tilt_is_a_balance_rather_than_a_volume)
{
    // One knob, two shelves moving in opposite directions: turning it moves
    // where the energy sits rather than how much of it there is.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.combMode = CombMode::off;
    parameters.formantMix = 0.0;
    parameters.tubeDriveDb = 0.0;
    parameters.voice.foldAmount = 0.0;
    parameters.voice.ringAmount = 0.0;
    parameters.voice.filterDrive = 0.0;
    parameters.voice.cutoffHz = 16000.0;
    parameters.voice.subLevel = 0.0;
    parameters.splitHz = 40.0;

    const auto render = [&] (double tilt)
    {
        auto local = parameters;
        local.tilt = tilt;

        Engine engine;
        engine.prepare (rate, 512);

        return play (engine, local, 45, 32000);
    };

    const auto flat = render (0.0);
    const auto dark = render (-1.0);
    const auto bright = render (1.0);

    const double low = 220.0;
    const double high = 3520.0;

    // The bass rises as the top falls, and the other way round.
    CHECK (amplitudeAt (dark, low, rate, 16000) > amplitudeAt (flat, low, rate, 16000) * 1.5);
    CHECK (amplitudeAt (dark, high, rate, 16000) < amplitudeAt (flat, high, rate, 16000) * 0.7);

    CHECK (amplitudeAt (bright, low, rate, 16000) < amplitudeAt (flat, low, rate, 16000) * 0.7);
    CHECK (amplitudeAt (bright, high, rate, 16000) > amplitudeAt (flat, high, rate, 16000) * 1.5);

    // And at zero it is out of the way.
    CHECK (peakOf (flat) > 0.0);
}

TEZLA_TEST (the_engine_survives_a_sample_rate_change_and_a_reset)
{
    // prepareToPlay clears all state -- CLAUDE.md section 7. No pops when the
    // transport restarts, no state leaking between projects, and a rate change
    // mid-life is a thing hosts do.
    Engine engine;

    for (const double rate : { 44100.0, 96000.0, 192000.0, 48000.0 })
    {
        engine.prepare (rate, 512);
        engine.setParameters (brutal());

        engine.noteOn (40, 1.0);

        Buffers buffers (512);

        for (int block = 0; block < 20; ++block)
        {
            engine.process (buffers.pointers, 512);

            for (int i = 0; i < 512; ++i)
                CHECK (std::isfinite (buffers.left[static_cast<std::size_t> (i)]));
        }

        engine.allNotesOff();
        engine.reset();

        // Silent immediately after a reset, with nothing left over.
        std::fill (buffers.left.begin(), buffers.left.end(), 0.0);
        std::fill (buffers.right.begin(), buffers.right.end(), 0.0);

        engine.process (buffers.pointers, 512);

        for (int i = 0; i < 512; ++i)
        {
            CHECK (buffers.left[static_cast<std::size_t> (i)] == 0.0);
            CHECK (buffers.right[static_cast<std::size_t> (i)] == 0.0);
        }
    }
}

// ---------------------------------------------------------------------------
// The global matrix
// ---------------------------------------------------------------------------

TEZLA_TEST (a_global_slot_sweeps_the_comb_over_octaves)
{
    // The instrument's headline claim. The brief's original trick was a flanger
    // with its rate pinned at zero so the *depth* knob became a direct comb
    // control, drawn by hand on an automation lane. This is that control with a
    // modulation source behind it, and the thing to prove is that it reaches --
    // a sweep of a few percent would be a chorus, not a flanger.
    //
    // The base delay is 2 ms rather than something longer for a reason worth
    // recording: the comb's own range is 20 us to 20 ms, so +-3 octaves only
    // fits between 160 us and 2.5 ms. At 8 ms the downward half runs into the
    // 20 ms ceiling and the sweep measures 18.9x instead of 64 -- which is the
    // clamp working correctly and the test measuring the wrong thing.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.combMode = CombMode::flange;
    parameters.combTimeMs = 2.0;
    parameters.combKeyTrack = 0.0;
    parameters.combInverted = false;
    parameters.lfo1Wave = Lfo::Wave::triangle;
    parameters.lfo1RateHz = 4.0;

    const auto sweepWith = [&] (double depth)
    {
        auto swept = parameters;
        swept.globalSlots[0] = { GlobalSource::lfo1, GlobalDestination::combTime, depth };

        Engine engine;
        engine.prepare (rate, 64);
        engine.setParameters (swept);
        engine.noteOn (40, 1.0);

        Buffers buffers (64);

        double lowest = 1.0e9;
        double highest = 0.0;

        // Two full LFO cycles at 4 Hz, read every 64 samples: the extremes of a
        // triangle are single instants, and a 256-sample stride misses them by
        // enough to matter.
        for (int block = 0; block < 375; ++block)
        {
            engine.process (buffers.pointers, 64);

            const double notch = engine.getCombNotchHz();

            lowest = std::min (lowest, notch);
            highest = std::max (highest, notch);
        }

        return std::pair { lowest, highest };
    };

    const auto [restLow, restHigh] = sweepWith (0.0);

    // Nothing pointed at it: the notch does not move at all, and it sits where
    // 1/(2D) puts it.
    CHECK (std::abs (restHigh - restLow) < 1.0e-9);
    CHECK (std::abs (restLow - 250.0) < 0.001);

    const auto [sweptLow, sweptHigh] = sweepWith (1.0);

    // +-3 octaves is a factor of 64. Measured: 31.250 Hz to 1977.9 Hz, a ratio
    // of 63.29 -- short of 64 only because a triangle's corners are single
    // samples and the reading is taken every 64.
    CHECK (sweptHigh / sweptLow > 55.0);
    CHECK (sweptHigh > 1900.0);
    CHECK (sweptLow < 33.0);
}

TEZLA_TEST (positive_global_modulation_raises_the_comb_rather_than_lowering_it)
{
    // A sign convention worth pinning, because the obvious implementation gets
    // it backwards: the control is a *delay*, and a longer delay is a lower
    // notch. A player turning a positive modulation up expects the sweep to go
    // up, so the delay has to move the other way.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.combMode = CombMode::flange;
    parameters.combTimeMs = 2.0;
    parameters.combKeyTrack = 0.0;
    parameters.combInverted = false;

    // A sequencer with every step at +1 and no glide is a constant +1: a
    // modulation source held at full, which is what makes this readable as a
    // single number rather than a sweep.
    parameters.sequencerRateHz = 1.0;
    parameters.sequencerGlide = 0.0;
    parameters.sequencerSteps.fill (1.0);

    const auto notchWith = [&] (double depth)
    {
        auto set = parameters;
        set.globalSlots[0] = { GlobalSource::sequencer, GlobalDestination::combTime, depth };

        Engine engine;
        engine.prepare (rate, 256);
        engine.setParameters (set);
        engine.noteOn (40, 1.0);

        Buffers buffers (256);
        engine.process (buffers.pointers, 256);

        return engine.getCombNotchHz();
    };

    const double rest = notchWith (0.0);
    const double up = notchWith (1.0);
    const double down = notchWith (-1.0);

    CHECK (std::abs (rest - 250.0) < 0.001);

    // Three octaves each way, exactly.
    CHECK (std::abs (up / rest - 8.0) < 0.001);
    CHECK (std::abs (rest / down - 8.0) < 0.001);
}

TEZLA_TEST (every_global_destination_reaches_its_control)
{
    // A destination list is exactly the kind of thing that silently grows a
    // dead entry: a `case` that was never written, or one written against the
    // wrong member. Each of the seven is checked here by measuring the thing it
    // is supposed to move -- so a destination that does nothing fails rather
    // than merely looking implemented.
    constexpr double rate = 48000.0;
    constexpr int samples = 16000;

    auto base = brutal();

    base.sequencerRateHz = 1.0;
    base.sequencerGlide = 0.0;
    base.sequencerSteps.fill (1.0);

    // Room for the destinations that add rather than scale -- and a comb that
    // is *audible*, which is the one thing this test got wrong first time
    // round. With the mix at zero the comb is bit-exactly transparent by
    // design, so modulating its feedback changed nothing and the reading was
    // exactly 0. That is the comb behaving correctly and the test measuring
    // the wrong thing.
    base.combMode = CombMode::flange;
    base.combTimeMs = 2.0;
    base.combKeyTrack = 0.0;
    base.combFeedback = 0.0;
    base.combMix = 0.5;
    base.combInverted = false;
    base.phaseFrequencyHz = 800.0;
    base.formantMorph = 0.0;
    base.formantMix = 1.0;
    base.tubeDriveDb = 0.0;
    base.outputDb = -30.0;

    const auto renderWith = [&] (GlobalDestination destination, double depth)
    {
        auto set = base;
        set.globalSlots[0] = { GlobalSource::sequencer, destination, depth };

        Engine engine;
        engine.prepare (rate, 256);

        const auto out = play (engine, set, 40, samples);

        // **RMS of the last quarter, not the peak of the whole render.** The
        // output gain starts where `prepare` left it -- 0 dB, because prepare
        // runs before any parameter is known -- and smooths towards whatever
        // the patch asks for, so the first tenth of a second of every render is
        // dominated by that ramp. Measuring the peak read the ramp rather than
        // the setting: +24 dB of modulation measured as 1.86x.
        double sum = 0.0;

        const std::size_t from = out.size() * 3 / 4;

        for (std::size_t i = from; i < out.size(); ++i)
            sum += out[i] * out[i];

        struct Result
        {
            double rms;
            double notch;
            double phase;
        };

        return Result { std::sqrt (sum / static_cast<double> (out.size() - from)),
                        engine.getCombNotchHz(), engine.getPhaseFrequencyHz() };
    };

    const auto rest = renderWith (GlobalDestination::none, 1.0);

    // comb time -- three octaves up.
    CHECK (std::abs (renderWith (GlobalDestination::combTime, 1.0).notch / rest.notch - 8.0) < 0.001);

    // phase centre -- four octaves up, clamped by the phaser's own ceiling well
    // above where this lands.
    CHECK (std::abs (renderWith (GlobalDestination::phaseFrequency, 1.0).phase / rest.phase - 16.0)
             < 0.01);

    // output -- +24 dB at full depth, which is a factor of 15.85 on the level.
    // Measured: 15.848, against the 15.849 the decibels predict.
    const double louder = renderWith (GlobalDestination::output, 1.0).rms / rest.rms;
    CHECK (louder > 15.0);
    CHECK (louder < 16.7);

    // The four that change the *sound* rather than a readable number: each is
    // checked by rendering with and without and requiring the output to differ
    // by more than a rounding. A destination wired to nothing renders bit for
    // bit the same, which is what the next test asserts from the other side.
    for (const auto destination : { GlobalDestination::combFeedback,
                                    GlobalDestination::combMix,
                                    GlobalDestination::formantMorph,
                                    GlobalDestination::tubeDrive })
    {
        const double changed = renderWith (destination, 1.0).rms;

        CHECK (std::abs (changed - rest.rms) > 1.0e-6);
    }
}

TEZLA_TEST (an_unpointed_global_matrix_changes_nothing_at_all)
{
    // The bit-exact half of the claim. Three slots with a source but no
    // destination, and three with a destination but no source, must render
    // identically to none at all -- otherwise the matrix is a tone control that
    // is on by default, and every patch saved before it existed changes.
    constexpr double rate = 48000.0;
    constexpr int samples = 8000;

    auto parameters = brutal();
    parameters.lfo1RateHz = 3.0;

    const auto render = [&] (const EngineParameters& set)
    {
        Engine engine;
        engine.prepare (rate, 256);

        return play (engine, set, 40, samples);
    };

    const auto plain = render (parameters);

    auto sourceOnly = parameters;
    sourceOnly.globalSlots[0] = { GlobalSource::lfo1, GlobalDestination::none, 1.0 };
    sourceOnly.globalSlots[1] = { GlobalSource::lfo2, GlobalDestination::none, -1.0 };
    sourceOnly.globalSlots[2] = { GlobalSource::sequencer, GlobalDestination::none, 1.0 };

    auto destinationOnly = parameters;
    destinationOnly.globalSlots[0] = { GlobalSource::none, GlobalDestination::combTime, 1.0 };
    destinationOnly.globalSlots[1] = { GlobalSource::none, GlobalDestination::tubeDrive, 1.0 };
    destinationOnly.globalSlots[2] = { GlobalSource::none, GlobalDestination::output, -1.0 };

    auto zeroDepth = parameters;
    zeroDepth.globalSlots[0] = { GlobalSource::lfo1, GlobalDestination::combTime, 0.0 };

    for (const auto& variant : { sourceOnly, destinationOnly, zeroDepth })
    {
        const auto other = render (variant);

        CHECK (other.size() == plain.size());

        for (std::size_t i = 0; i < plain.size(); ++i)
            CHECK (other[i] == plain[i]);
    }
}

// ---------------------------------------------------------------------------
// The idle skip
// ---------------------------------------------------------------------------

TEZLA_TEST (an_idle_instrument_stops_doing_arithmetic)
{
    // An instrument with nothing playing was not free: the mangle's filters and
    // the oversampler's decimation FIRs ran on silence, and that measured 17.9
    // ms/s -- 1.8% of a core -- with no note down at all. Ten idle instances in
    // a project is a fifth of a core spent on nothing.
    //
    // This is a timing test, which is unusual here and needs saying: the
    // *correctness* of the skip is what the next two tests check, and this one
    // only asserts that it saves something. The threshold is deliberately loose
    // -- a factor of four rather than the 25 measured -- because a shared build
    // machine is not a quiet one.
    constexpr double rate = 48000.0;

    EngineParameters parameters;

    parameters.voice.shapeA = OscShape::saw;
    parameters.voice.levelA = 1.0;
    parameters.voice.ampSustain = 1.0;
    parameters.oversampling = OversamplingMode::X4;

    Engine engine;
    engine.setParameters (parameters);
    engine.prepare (rate, 512);

    Buffers buffers (512);

    const auto secondOfSilence = [&]
    {
        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < static_cast<int> (rate); i += 512)
            engine.process (buffers.pointers, 512);

        return std::chrono::duration<double, std::milli> (
                   std::chrono::steady_clock::now() - started).count();
    };

    // The first second is rendered normally, because the chain has to be seen
    // to be silent before it can be skipped.
    const double working = secondOfSilence();

    // By the second it is skipping.
    const double idle = secondOfSilence();

    CHECK (idle * 4.0 < working);

    // And it comes straight back: a note after the skip has engaged sounds, and
    // sounds from the first block.
    engine.noteOn (40, 1.0);
    engine.process (buffers.pointers, 512);

    CHECK (peakOf (buffers.left) > 0.01);
}

TEZLA_TEST (the_idle_skip_never_cuts_a_tail)
{
    // The dangerous half. A comb at high feedback rings for a long time after
    // the note that fed it has gone, and a skip that fired on "no voices are
    // active" rather than on "the output has been exactly zero for a second"
    // would cut it off mid-ring -- a click, not a fade.
    //
    // The measurement is the **peak of the last block that was not silent**. A
    // truncated tail leaves that at whatever it was ringing at; a tail that
    // decayed leaves it at nothing. Measuring the largest sample-to-sample step
    // instead does not work and it is worth saying why: this patch steps 0.425
    // of full scale between neighbours all by itself, because a folded,
    // tube-driven saw at 48 kHz genuinely does that. The step tells you nothing
    // about whether the end was cut.
    constexpr double rate = 48000.0;

    const auto lastLiveBlockPeak = [&] (double feedback, int blocks)
    {
        auto parameters = brutal();

        parameters.combMode = CombMode::flange;
        parameters.combTimeMs = 18.0;
        parameters.combKeyTrack = 0.0;
        parameters.combFeedback = feedback;
        parameters.combMix = 1.0;
        parameters.combDamping = 0.0;
        parameters.voice.ampRelease = 0.02;

        Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);

        engine.noteOn (40, 1.0);

        Buffers buffers (512);

        for (int block = 0; block < 20; ++block)
            engine.process (buffers.pointers, 512);

        engine.noteOff (40);

        double lastLive = 0.0;
        bool everSilent = false;

        for (int block = 0; block < blocks; ++block)
        {
            engine.process (buffers.pointers, 512);

            const double peak = peakOf (buffers.left);

            // Exactly zero, which is what a skipped block writes -- so this
            // reads whether the skip engaged, not merely whether the tail got
            // quiet.
            if (peak > 0.0)
                lastLive = peak;
            else
                everSilent = true;
        }

        return std::pair { lastLive, everSilent };
    };

    // Half feedback: the ring falls under the -240 dBFS threshold inside two
    // seconds and the skip then engages. The last block that carried anything
    // at all peaked at 3.0e-26, thirteen decades below the threshold -- a fade
    // that ran well past the point of being cuttable, not a cut.
    const auto [gentleTail, gentleSilent] = lastLiveBlockPeak (0.5, 280);

    CHECK (gentleSilent);
    CHECK (gentleTail < 1.0e-6);

    // Full feedback: the comb is capped just under unity, so a 55-passes-a-
    // second delay takes about ten seconds to fall that far. The instrument is
    // *not* idle and must not be skipped -- so after four seconds it is still
    // ringing, at 1.8e-6 (-114.8 dBFS), and never went silent once. That is
    // the skip declining to fire rather than failing to.
    const auto [loudTail, loudSilent] = lastLiveBlockPeak (1.0, 380);

    CHECK (! loudSilent);
    CHECK (loudTail > 0.0);
    CHECK (loudTail < 1.0e-4);
}

TEZLA_TEST (the_lfos_keep_running_while_the_instrument_is_idle)
{
    // The skip freezes the audio path, not the clocks. A player who set a
    // two-second sweep and then stopped playing expects to find it where it
    // would have been, not where it was when the last note ended -- so the
    // global sources are advanced even on a skipped block.
    constexpr double rate = 48000.0;

    EngineParameters parameters;

    parameters.lfo1Wave = Lfo::Wave::sine;
    parameters.lfo1RateHz = 0.5;
    parameters.oversampling = OversamplingMode::Off;

    Engine engine;
    engine.setParameters (parameters);
    engine.prepare (rate, 512);

    Buffers buffers (512);

    // Well past the point the skip engages.
    for (int block = 0; block < 200; ++block)
        engine.process (buffers.pointers, 512);

    double lowest = 1.0;
    double highest = -1.0;

    // Two more seconds, entirely skipped. A frozen LFO would sit at one value.
    for (int block = 0; block < 190; ++block)
    {
        engine.process (buffers.pointers, 512);

        lowest = std::min (lowest, engine.getGlobalSources().lfo1);
        highest = std::max (highest, engine.getGlobalSources().lfo1);
    }

    CHECK (highest - lowest > 1.9);
}

TEZLA_TEST (the_harmonic_lock_follows_the_note_through_the_engine)
{
    // End to end: the lock has to track the note the *voice manager* is
    // playing, not a number the engine was told separately. That is the same
    // tracked frequency the comb uses -- which is what makes the two agree
    // rather than beat, since one locks to the note's period and the other to
    // its harmonics.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.combMode = CombMode::off;
    parameters.formantMix = 1.0;
    parameters.formantLock = 1.0;
    parameters.formantHarmonic = 6.0;
    parameters.keyboard = KeyboardMode::mono;

    Engine engine;
    engine.setParameters (parameters);
    engine.prepare (rate, 256);

    Buffers buffers (256);

    for (const int note : { 28, 40, 52 })
    {
        engine.noteOn (note, 1.0);
        engine.process (buffers.pointers, 256);

        const double played = 440.0 * std::pow (2.0, (note - 69) / 12.0);

        // The engine renders at the oversampled rate, so the formant's own
        // frequency is in those terms -- but a harmonic of the note is a
        // harmonic of the note at any rate.
        CHECK_NEAR (engine.getFormantHz (0) / played, 6.0, 1.0e-6);
        CHECK_NEAR (engine.getFormantHz (1) / played, 7.0, 1.0e-6);
        CHECK_NEAR (engine.getFormantHz (2) / played, 8.0, 1.0e-6);

        engine.noteOff (note);
        engine.allNotesOff();
    }
}

TEZLA_TEST (the_overtone_controls_are_neutral_by_default)
{
    // Every one of them was appended to a shipping plugin, so a patch saved
    // before they existed has to reopen sounding the same -- CLAUDE.md
    // section 8. Their defaults are the neutral settings, and this renders a
    // patch with them explicitly at those defaults against one that never
    // mentions them, bit for bit.
    constexpr double rate = 48000.0;
    constexpr int samples = 8000;

    auto parameters = brutal();
    parameters.formantMix = 1.0;

    const auto render = [&] (const EngineParameters& set)
    {
        Engine engine;
        engine.prepare (rate, 256);

        return play (engine, set, 40, samples);
    };

    const auto plain = render (parameters);

    auto explicitly = parameters;
    explicitly.formantLock = 0.0;
    explicitly.formantHarmonic = 1.0;
    explicitly.formantNotchHz = 1000.0;
    explicitly.formantNotchDepth = 0.0;

    const auto other = render (explicitly);

    CHECK (other.size() == plain.size());

    for (std::size_t i = 0; i < plain.size(); ++i)
        CHECK (other[i] == plain[i]);

    // And with the lock up but no note sounding there is nothing to lock to,
    // so it is still the vowel -- which is what a released key leaves behind.
    auto released = parameters;
    released.formantLock = 1.0;
    released.formantHarmonic = 9.0;

    Engine engine;
    engine.prepare (rate, 256);
    engine.setParameters (released);

    Buffers buffers (256);
    engine.process (buffers.pointers, 256);

    Engine reference;
    reference.prepare (rate, 256);
    reference.setParameters (parameters);
    reference.process (buffers.pointers, 256);

    for (int index = 0; index < Formant::kFormants; ++index)
        CHECK (engine.getFormantHz (index) == reference.getFormantHz (index));
}

// ---------------------------------------------------------------------------
// What playing the thing turned up
// ---------------------------------------------------------------------------

TEZLA_TEST (an_envelope_can_drive_a_global_destination)
{
    // The mangle used to take only LFOs and the sequencer, so the vowel, the
    // phase centre and the comb could not be enveloped at all. The objection
    // was that a per-voice source has one value per sounding note and the
    // mangle is one chain -- true, and the answer is the one the comb and the
    // formant already use: **follow the tracked note**, so all three stages
    // agree about which note they are following.
    constexpr double rate = 48000.0;

    auto parameters = brutal();

    parameters.combMode = CombMode::flange;
    parameters.combTimeMs = 2.0;
    parameters.combKeyTrack = 0.0;
    parameters.combInverted = false;
    parameters.keyboard = KeyboardMode::mono;

    // A slow attack, so the envelope's rise is visible in the notch rather than
    // being over before the first block ends.
    parameters.voice.ampAttack = 0.4;
    parameters.voice.ampSustain = 1.0;
    parameters.voice.ampVelocity = 0.0;

    parameters.globalSlots[0] = { GlobalSource::ampEnvelope, GlobalDestination::combTime, 1.0 };

    Engine engine;
    engine.setParameters (parameters);
    engine.prepare (rate, 256);

    Buffers buffers (256);

    // Before the note, nothing is sounding: an envelope with no note is closed,
    // so the comb sits exactly where the knob put it.
    engine.process (buffers.pointers, 256);

    const double atRest = engine.getCombNotchHz();

    CHECK_NEAR (atRest, 250.0, 0.001);

    engine.noteOn (40, 1.0);

    std::vector<double> notches;

    for (int block = 0; block < 90; ++block)
    {
        engine.process (buffers.pointers, 256);
        notches.push_back (engine.getCombNotchHz());
    }

    // The notch climbs with the envelope rather than jumping, and ends three
    // octaves up where full depth puts it.
    CHECK (notches.front() < notches.back());
    CHECK_NEAR (notches.back() / atRest, 8.0, 0.01);

    // Monotonic, which is what makes it an envelope rather than a wobble.
    int falls = 0;

    for (std::size_t i = 1; i < notches.size(); ++i)
        if (notches[i] < notches[i - 1] - 1.0e-9)
            ++falls;

    CHECK (falls == 0);
}

TEZLA_TEST (the_lfo_retriggers_on_a_note_and_free_runs_without_one)
{
    // Free-running is right for a pad, where the movement is ambient. It is
    // exactly wrong for a bass line: every note lands on a different part of
    // the cycle, so the same phrase played twice is two different sounds.
    constexpr double rate = 48000.0;

    const auto valueAfterNote = [&] (bool retrigger, int blocksBefore)
    {
        auto parameters = brutal();

        parameters.lfo1Wave = Lfo::Wave::sine;
        parameters.lfo1RateHz = 2.0;
        parameters.lfo1Retrigger = retrigger;
        parameters.keyboard = KeyboardMode::mono;

        Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 256);

        Buffers buffers (256);

        // Let the LFO run to an arbitrary point first.
        for (int block = 0; block < blocksBefore; ++block)
            engine.process (buffers.pointers, 256);

        engine.noteOn (40, 1.0);
        engine.process (buffers.pointers, 256);

        return engine.getGlobalSources().lfo1;
    };

    // Retriggered: the LFO is at the same place after the note however long it
    // had been running beforehand.
    const double a = valueAfterNote (true, 7);
    const double b = valueAfterNote (true, 23);

    CHECK_NEAR (a, b, 1.0e-9);

    // Free-running: it is not.
    const double c = valueAfterNote (false, 7);
    const double d = valueAfterNote (false, 23);

    CHECK (std::abs (c - d) > 0.05);
}

TEZLA_TEST (the_lfo_rate_can_follow_the_played_note)
{
    // The beating that gives a reese its character is a *fraction of the note*
    // rather than a fixed number of hertz, so a wobble that does not track
    // becomes a different sound as the line moves up the keyboard.
    constexpr double rate = 48000.0;

    const auto cyclesOverASecond = [&] (int note, double keyTrack)
    {
        auto parameters = brutal();

        parameters.lfo1Wave = Lfo::Wave::sine;
        parameters.lfo1RateHz = 4.0;
        parameters.lfo1KeyTrack = keyTrack;
        parameters.lfo1Retrigger = true;
        parameters.keyboard = KeyboardMode::mono;

        Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 64);

        engine.noteOn (note, 1.0);

        Buffers buffers (64);

        // Zero crossings, which are twice the cycles and need no phase
        // bookkeeping. **Counted over four seconds rather than one**, because
        // the count is an integer: at 4 Hz an octave down is two cycles a
        // second, so one stray crossing is a quarter of the reading. Four
        // seconds puts that under a twentieth.
        double previous = 0.0;
        int crossings = 0;

        for (int block = 0; block < 3000; ++block)
        {
            engine.process (buffers.pointers, 64);

            const double value = engine.getGlobalSources().lfo1;

            if ((previous < 0.0) != (value < 0.0))
                ++crossings;

            previous = value;
        }

        return crossings / 8.0;
    };

    // C4 is the reference, so tracking changes nothing there.
    CHECK_NEAR (cyclesOverASecond (60, 1.0), cyclesOverASecond (60, 0.0), 0.2);

    // An octave down halves the rate; an octave up doubles it.
    const double low = cyclesOverASecond (48, 1.0);
    const double middle = cyclesOverASecond (60, 1.0);
    const double high = cyclesOverASecond (72, 1.0);

    CHECK_NEAR (middle / low, 2.0, 0.06);
    CHECK_NEAR (high / middle, 2.0, 0.06);

    // And with tracking off it is the same rate at every note, which is the
    // behaviour that has to survive.
    CHECK_NEAR (cyclesOverASecond (48, 0.0), cyclesOverASecond (72, 0.0), 0.2);
}
