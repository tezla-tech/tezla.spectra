#include "TestFramework.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/measure/Fft.hpp>
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
    parameters.voice.amp.attack = 0.001;
    parameters.voice.amp.sustain = 1.0;
    parameters.voice.amp.release = 0.05;
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
    parameters.voice.amp.attack = 0.001;
    parameters.voice.amp.sustain = 1.0;
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
        parameters.voice.amp.attack = 0.001;
        parameters.voice.amp.sustain = 1.0;
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

    CHECK (std::abs (rest - 250.0) < 0.001);

    // **Measured at half the maximum, not at the maximum**, and the reason is
    // worth recording: full depth is six octaves each way, which is a range of
    // twelve -- and the comb's own delay only spans 20 us to 20 ms, which is
    // 9.97. So a full-depth sweep deliberately runs into the ends. That is the
    // extremity being asked for rather than a fault, but it means the *ratio*
    // has to be read somewhere it is not clipping.
    //
    // The depth law is squared, so a depth of 1/sqrt(2) is half the maximum:
    // three octaves, a factor of eight.
    const double half = 1.0 / std::sqrt (2.0);

    CHECK (std::abs (notchWith (half) / rest - 8.0) < 0.01);
    CHECK (std::abs (rest / notchWith (-half) - 8.0) < 0.01);

    // And at full depth it reaches the clamps rather than running away: the
    // delay bottoms out at 20 us and tops out at 20 ms whatever it is asked
    // for, which is what keeps the interpolator inside its own buffer.
    CHECK (notchWith (1.0) > rest * 20.0);
    CHECK (notchWith (-1.0) < rest / 8.0);
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

    // **Half the maximum**, because at full depth both of these run into a
    // clamp and a ratio read against a clamp measures the clamp. The depth law
    // is squared, so 1/sqrt(2) is half: three octaves, a factor of eight.
    const double half = 1.0 / std::sqrt (2.0);

    CHECK (std::abs (renderWith (GlobalDestination::combTime, half).notch / rest.notch - 8.0) < 0.01);
    CHECK (std::abs (renderWith (GlobalDestination::phaseFrequency, half).phase / rest.phase - 8.0)
             < 0.05);

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
    parameters.voice.amp.sustain = 1.0;
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
        parameters.voice.amp.release = 0.02;

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
    parameters.voice.amp.attack = 0.4;
    parameters.voice.amp.sustain = 1.0;
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

    // The notch climbs with the envelope rather than jumping, and ends six
    // octaves up -- a factor of sixty-four -- where full depth puts it.
    CHECK (notches.front() < notches.back());
    CHECK_NEAR (notches.back() / atRest, 64.0, 0.05);

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

TEZLA_TEST (an_lfo_reaches_the_top_of_its_range_without_being_clamped)
{
    // The rate control goes to 160 Hz, and two clamps used to stop it well
    // short of that: `Lfo::setRateHz` capped at 100, and the engine capped the
    // *effective* rate at 100 again after key tracking and the sequencer had
    // multiplied it. Neither was reachable from the old 40 Hz knob without key
    // tracking, which is exactly why nothing caught it -- and key tracking is
    // what the instrument is for.
    //
    // Measured by counting the LFO's own zero crossings rather than by looking
    // at audio: this is a claim about the modulation source, and reading it
    // through a filter would confound the source with what the destination did
    // with it.
    constexpr double rate = 48000.0;
    constexpr double seconds = 0.5;

    const auto crossingsAt = [] (double hz, double keyTrack, int note)
    {
        auto parameters = brutal();

        parameters.lfo1Wave = Lfo::Wave::sine;
        parameters.lfo1RateHz = hz;
        parameters.lfo1KeyTrack = keyTrack;
        parameters.lfo1Smooth = 0.0;
        parameters.sequencerToLfo1Rate = 0.0;

        // Nothing assigned: the reading comes from the engine's published
        // source, not from anything downstream of it.
        parameters.globalSlots[0] = { GlobalSource::none, GlobalDestination::none, 0.0 };

        Engine engine;
        engine.prepare (rate, 64);
        engine.setParameters (parameters);
        engine.noteOn (note, 1.0);

        Buffers buffers (64);

        int crossings = 0;
        double previous = 0.0;
        bool first = true;

        const int blocks = static_cast<int> (rate * seconds) / 64;

        for (int block = 0; block < blocks; ++block)
        {
            std::fill (buffers.left.begin(), buffers.left.end(), 0.0);
            std::fill (buffers.right.begin(), buffers.right.end(), 0.0);

            engine.process (buffers.pointers, 64);

            const double value = engine.readouts().lfo1.load();

            if (! first && previous < 0.0 && value >= 0.0)
                ++crossings;

            previous = value;
            first = false;
        }

        return crossings;
    };

    // A sine crosses zero upwards once per cycle, so the count over half a
    // second is half the rate in hertz. Read at 750 Hz here -- 48 kHz through
    // the x4 oversampler is 192 kHz internal, and the sources are published
    // every 64-sample block -- so the tolerance is a couple of cycles rather
    // than exact.
    const int slow = crossingsAt (2.0, 0.0, 60);
    const int fast = crossingsAt (160.0, 0.0, 60);

    CHECK (std::abs (slow - 1) <= 1);

    // The claim with teeth: at 160 Hz the count must be near 80, not the 50 a
    // 100 Hz clamp would give.
    CHECK (fast >= 72 && fast <= 88);

    // And key tracking on top of a fast rate is not clamped away either. Two
    // octaves above middle C at full tracking is four times the rate; the
    // engine's own ceiling is half the control rate, which at 192 kHz internal
    // is 3000 Hz, so 40 x 4 = 160 passes untouched.
    const int tracked = crossingsAt (40.0, 1.0, 84);

    CHECK (tracked >= 72 && tracked <= 88);
}

/// A voice with nothing downstream of it: one sine oscillator, no mangle. What
/// is being measured in the two tests below is what the modulation does to the
/// *pitch*, and the comb, the tube and the split all move a waveform about for
/// reasons that have nothing to do with it.
EngineParameters bareSine()
{
    EngineParameters parameters;

    parameters.voice.shapeA = OscShape::sine;
    parameters.voice.levelA = 1.0;
    parameters.voice.levelB = 0.0;
    parameters.voice.subLevel = 0.0;
    parameters.voice.unisonA = 1;
    parameters.voice.unisonB = 1;
    parameters.voice.cutoffHz = 18000.0;
    parameters.voice.amp.attack = 0.001;
    parameters.voice.amp.sustain = 1.0;
    parameters.voice.level = 0.5;

    parameters.keyboard = KeyboardMode::mono;

    parameters.splitHz = 40.0;
    parameters.combMode = CombMode::off;
    parameters.combMix = 0.0;
    parameters.formantMix = 0.0;
    parameters.tubeDriveDb = 0.0;
    parameters.tilt = 0.0;
    parameters.outputDb = 0.0;

    parameters.lfo1Wave = Lfo::Wave::sine;
    parameters.lfo1Smooth = 0.0;

    return parameters;
}

TEZLA_TEST (a_slow_lfo_on_pitch_sweeps_the_period)
{
    // The pitch destination, measured directly: a sine oscillator crosses zero
    // twice per cycle and nowhere else, so the gap between crossings *is* the
    // period and its spread is the depth of the sweep.
    //
    // **Peak to peak, not standard deviation.** Over a quarter of a second a
    // 2 Hz LFO covers half a cycle, so most of the sweep is monotonic and the
    // deviation reads far smaller than the swing -- 1.4% against a real 4.8%.
    // The range is the honest statistic for a ramp.
    //
    // The first attempt at this counted crossings of the house saw and read a
    // spread of 0.89 with the LFO switched *off*: a saw crosses zero many times
    // per cycle once its harmonics have been through a comb and a decimation
    // filter, so the gaps measured the harmonic structure rather than the pitch.
    constexpr double rate = 48000.0;
    constexpr int samples = 12000;

    const auto spreadAt = [] (double hz)
    {
        auto parameters = bareSine();

        parameters.lfo1RateHz = hz;
        parameters.voice.slots[0] = { ModSource::lfo1, ModDestination::pitch, 100.0 };

        Engine engine;
        engine.prepare (rate, 64);

        const auto out = play (engine, parameters, 45, samples, 64);

        // The first eighth is the amp attack and the output smoother settling.
        const std::size_t from = out.size() / 8;

        int shortest = 1 << 30;
        int longest = 0;
        int last = -1;
        double total = 0.0;
        int counted = 0;

        for (std::size_t i = from + 1; i < out.size(); ++i)
            if (out[i - 1] < 0.0 && out[i] >= 0.0)
            {
                if (last >= 0)
                {
                    const int gap = static_cast<int> (i) - last;

                    shortest = std::min (shortest, gap);
                    longest = std::max (longest, gap);
                    total += gap;
                    ++counted;
                }

                last = static_cast<int> (i);
            }

        if (counted < 8)
            return 0.0;

        return (longest - shortest) / (total / counted);
    };

    // At rest the only variation is the sample grid's own rounding: one sample
    // in a 436-sample period.
    CHECK (spreadAt (0.0) < 0.01);

    // A semitone either way is about 6% of the period, and the measurement sees
    // most of it.
    CHECK (spreadAt (2.0) > 0.03);
}

TEZLA_TEST (a_fast_lfo_on_pitch_makes_sidebands)
{
    // **The top of the rate range does something different in kind**, and this
    // is the test that says what. At 160 Hz an LFO cycle is shorter than the
    // period it is modulating, so there is no sweep to measure -- the pitch
    // never settles anywhere long enough to have one. What there is instead is
    // frequency modulation: energy at the carrier plus and minus the modulator,
    // which is the same mechanism as the FILTER page's own FM control reached
    // from the modulation matrix.
    //
    // Measuring this as a period spread would read almost nothing and look like
    // a failure. Measuring it as a spectrum shows the sidebands arriving.
    constexpr double rate = 48000.0;
    constexpr std::size_t fftSize = 8192;

    const auto spectrumWith = [] (double hz)
    {
        auto parameters = bareSine();

        parameters.lfo1RateHz = hz;
        parameters.voice.slots[0] = { ModSource::lfo1, ModDestination::pitch, 100.0 };

        Engine engine;
        engine.prepare (rate, 64);

        // A4, so both sidebands land well clear of zero and of each other.
        const auto out = play (engine, parameters, 69, 3 * static_cast<int> (fftSize), 64);

        // From the second half, past the attack and the gain smoother.
        std::vector<double> window (fftSize);

        for (std::size_t i = 0; i < fftSize; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi
                                                        * static_cast<double> (i)
                                                        / static_cast<double> (fftSize));

            window[i] = out[out.size() - fftSize + i] * hann;
        }

        return fftOfReal (window);
    };

    const auto energyNear = [] (const Spectrum& spectrum, double hz)
    {
        // Three bins either side: at 5.86 Hz a bin, a Hann main lobe is four
        // bins wide and the sideband is not bin-exact.
        const auto centre = static_cast<int> (std::llround (hz * fftSize / rate));

        double peak = 0.0;

        for (int bin = centre - 3; bin <= centre + 3; ++bin)
            if (bin > 0 && bin < static_cast<int> (fftSize / 2))
                peak = std::max (peak, std::abs (spectrum[static_cast<std::size_t> (bin)]));

        return peak;
    };

    const auto still = spectrumWith (0.0);
    const auto fast = spectrumWith (160.0);

    // The carrier is there either way, and is the reference everything else is
    // measured against.
    const double carrier = energyNear (still, 440.0);

    CHECK (carrier > 0.0);

    // 440 + 160 and 440 - 160. Held still there is only the window's own
    // leakage this far out; modulated there is a sideband.
    const double upperStill = energyNear (still, 600.0) / carrier;
    const double lowerStill = energyNear (still, 280.0) / carrier;

    const double upperFast = energyNear (fast, 600.0) / energyNear (fast, 440.0);
    const double lowerFast = energyNear (fast, 280.0) / energyNear (fast, 440.0);

    // Leakage three bins out of a Hann window is far below a percent.
    CHECK (upperStill < 0.01);
    CHECK (lowerStill < 0.01);

    // The sidebands. A 100-cent swing at 160 Hz is a modulation index near 0.4,
    // which puts each first-order sideband about 14 dB below the carrier -- so
    // the claim is that they are unmistakably present, not that they are loud.
    CHECK (upperFast > 0.05);
    CHECK (lowerFast > 0.05);

    // And the thing this whole range change was about: against the old clamp at
    // 100 Hz the sidebands would sit at 340 and 540 Hz instead, so the bins at
    // 280 and 600 would read leakage. They do not.
    CHECK (upperFast > 10.0 * upperStill);
    CHECK (lowerFast > 10.0 * lowerStill);
}


TEZLA_TEST (kargyraa_puts_a_subharmonic_where_the_throat_puts_one)
{
    // Kargyraa is period doubling, not an octave divider: alternate cycles of
    // the waveform that is already there are damped, so what appears is the
    // **half-integer series** -- f/2, 3f/2, 5f/2 -- around the fundamental,
    // rather than a separate tone an octave down.
    //
    // That distinction is the whole test. A sub oscillator would put energy at
    // f/2 and nothing at 3f/2; this must put energy at both.
    constexpr double rate = 48000.0;
    constexpr std::size_t fftSize = 16384;

    const auto spectrumAt = [] (double depth, int divisor)
    {
        auto parameters = bareSine();

        // A saw, because a sine has one harmonic to modulate and the series
        // being looked for sits around all of them.
        parameters.voice.shapeA = OscShape::saw;
        parameters.voice.kargyraaDepth = depth;
        parameters.voice.kargyraaRasp = 0.5;
        parameters.voice.kargyraaDivisor = divisor;

        Engine engine;
        engine.prepare (rate, 64);

        // A2 at 110 Hz, so f/2 is 55 Hz and the split at 40 Hz leaves it alone.
        const auto out = play (engine, parameters, 45, 3 * static_cast<int> (fftSize), 64);

        std::vector<double> window (fftSize);

        for (std::size_t i = 0; i < fftSize; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi
                                                        * static_cast<double> (i)
                                                        / static_cast<double> (fftSize));

            window[i] = out[out.size() - fftSize + i] * hann;
        }

        return fftOfReal (window);
    };

    const auto energyNear = [] (const Spectrum& spectrum, double hz)
    {
        const auto centre = static_cast<int> (std::llround (hz * fftSize / rate));

        double peak = 0.0;

        for (int bin = centre - 3; bin <= centre + 3; ++bin)
            if (bin > 0 && bin < static_cast<int> (fftSize / 2))
                peak = std::max (peak, std::abs (spectrum[static_cast<std::size_t> (bin)]));

        return peak;
    };

    const auto off = spectrumAt (0.0, 2);
    const auto on = spectrumAt (0.8, 2);

    const double carrierOff = energyNear (off, 110.0);
    const double carrierOn = energyNear (on, 110.0);

    CHECK (carrierOff > 0.0);
    CHECK (carrierOn > 0.0);

    // Nothing at the half-integers with the control down: a saw has harmonics
    // at 110, 220, 330 and nowhere else.
    CHECK (energyNear (off, 55.0) / carrierOff < 0.01);
    CHECK (energyNear (off, 165.0) / carrierOff < 0.01);
    CHECK (energyNear (off, 275.0) / carrierOff < 0.01);

    // And the series arrives with it. **3f/2 is the one that separates this
    // from a sub oscillator** -- an added tone an octave down would leave it
    // empty.
    CHECK (energyNear (on, 55.0) / carrierOn > 0.05);
    CHECK (energyNear (on, 165.0) / carrierOn > 0.05);
    CHECK (energyNear (on, 275.0) / carrierOn > 0.02);

    // A divisor of three subdivides by three instead: f/3 and 2f/3 appear, and
    // f/2 does not.
    const auto third = spectrumAt (0.8, 3);
    const double carrierThird = energyNear (third, 110.0);

    CHECK (energyNear (third, 110.0 / 3.0) / carrierThird > 0.05);
    CHECK (energyNear (third, 220.0 / 3.0) / carrierThird > 0.05);
    CHECK (energyNear (third, 55.0) / carrierThird < 0.02);
}

TEZLA_TEST (kargyraa_is_locked_to_the_note_and_cannot_drift)
{
    // The lock is by construction -- the modulator's phase is derived from the
    // oscillator's own cycle counter rather than accumulated from a clock of
    // its own -- and this is what that buys: the subharmonic lands on exactly
    // half the played frequency at every pitch, with no tuning to get wrong.
    //
    // A free-running modulator would pass at one note and fail at the others,
    // which is why three are measured rather than one.
    constexpr double rate = 48000.0;
    constexpr std::size_t fftSize = 16384;

    const auto subharmonicRatio = [] (int note, double frequency)
    {
        auto parameters = bareSine();

        parameters.voice.shapeA = OscShape::saw;
        parameters.voice.kargyraaDepth = 0.9;
        parameters.voice.kargyraaRasp = 0.5;
        parameters.voice.kargyraaDivisor = 2;

        Engine engine;
        engine.prepare (rate, 64);

        const auto out = play (engine, parameters, note, 3 * static_cast<int> (fftSize), 64);

        std::vector<double> window (fftSize);

        for (std::size_t i = 0; i < fftSize; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi
                                                        * static_cast<double> (i)
                                                        / static_cast<double> (fftSize));

            window[i] = out[out.size() - fftSize + i] * hann;
        }

        const auto spectrum = fftOfReal (window);

        // The loudest bin below three quarters of the fundamental *is* the
        // subharmonic, whatever it turns out to be -- so a modulator running at
        // the wrong rate is found rather than assumed away.
        const auto limit = static_cast<std::size_t> (0.75 * frequency * fftSize / rate);

        std::size_t loudest = 1;
        double best = 0.0;

        // From 30 Hz up: below that is the split's own high-pass skirt.
        for (std::size_t bin = static_cast<std::size_t> (30.0 * fftSize / rate); bin < limit; ++bin)
            if (std::abs (spectrum[bin]) > best)
            {
                best = std::abs (spectrum[bin]);
                loudest = bin;
            }

        return static_cast<double> (loudest) * rate / fftSize / frequency;
    };

    // A1, A2 and A3. Each must find its subharmonic at half its own pitch --
    // within one FFT bin, which at 16384 points is 2.9 Hz.
    CHECK_NEAR (subharmonicRatio (33, 55.0), 0.5, 0.06);
    CHECK_NEAR (subharmonicRatio (45, 110.0), 0.5, 0.03);
    CHECK_NEAR (subharmonicRatio (57, 220.0), 0.5, 0.02);
}

TEZLA_TEST (kargyraa_at_zero_is_bit_exact)
{
    // CLAUDE.md section 7: anything permanently in the signal path needs a
    // bit-exact bypass at its neutral setting, not merely a transparent one.
    //
    // Here it is exact twice over -- the branch skips the multiply, and the
    // modulator at zero depth is `1 - 0 * shape`, which is exactly 1.0, and
    // `x * 1.0` is exactly x. The branch is a fast path rather than the
    // mechanism, and removing it would correctly fail nothing. Worth saying
    // out loud, because a test that cannot fail is a decoration.
    constexpr double rate = 48000.0;
    constexpr int samples = 8000;

    auto parameters = brutal();
    parameters.voice.kargyraaDepth = 0.0;

    Engine reference;
    reference.prepare (rate, 128);
    const auto without = play (reference, parameters, 40, samples, 128);

    // The clock still runs at zero depth -- engaging the control mid-note has
    // to start from the note's own phase rather than from wherever a stopped
    // counter left off -- so this also says the clock costs nothing.
    parameters.voice.kargyraaRasp = 1.0;
    parameters.voice.kargyraaDivisor = 4;

    Engine engine;
    engine.prepare (rate, 128);
    const auto with = play (engine, parameters, 40, samples, 128);

    std::size_t differences = 0;

    for (std::size_t i = 0; i < without.size(); ++i)
        if (without[i] != with[i])
            ++differences;

    CHECK (differences == 0);
}

TEZLA_TEST (kargyraa_does_not_alias)
{
    // The modulator is band-limited *by construction*: `(0.5 - 0.5 cos t)^k` is
    // `sin^2k(t/2)`, which expands into exactly k harmonics of t and nothing
    // above them. So the product widens the carrier by k * f/N and no further,
    // and no antiderivative or oversampling argument is needed to know it.
    //
    // The claim still gets measured, because "band-limited by construction" is
    // an argument about the modulator and this is a statement about the
    // instrument -- and CLAUDE.md section 7 wants the number.
    //
    // A sine carrier, so every component in the output is either the carrier,
    // a subharmonic sideband the modulator is entitled to, or aliasing.
    constexpr double rate = 48000.0;
    constexpr std::size_t fftSize = 16384;

    auto parameters = bareSine();

    // The worst case the control offers: full depth, sharpest rasp, and a note
    // high enough that k * f/N is a long way up the spectrum.
    parameters.voice.kargyraaDepth = 1.0;
    parameters.voice.kargyraaRasp = 1.0;
    parameters.voice.kargyraaDivisor = 2;

    Engine engine;
    engine.prepare (rate, 128);

    // A5, 880 Hz. The modulator's eight harmonics of 440 Hz reach 3520 Hz, so
    // everything the modulation is allowed to make sits below 4400 Hz.
    const auto out = play (engine, parameters, 81, 3 * static_cast<int> (fftSize), 128);

    std::vector<double> window (fftSize);

    for (std::size_t i = 0; i < fftSize; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi
                                                    * static_cast<double> (i)
                                                    / static_cast<double> (fftSize));

        window[i] = out[out.size() - fftSize + i] * hann;
    }

    const auto spectrum = fftOfReal (window);

    double carrier = 0.0;
    double worst = 0.0;
    double worstHz = 0.0;

    for (std::size_t bin = 1; bin < fftSize / 2; ++bin)
    {
        const double hz = static_cast<double> (bin) * rate / fftSize;
        const double magnitude = std::abs (spectrum[bin]);

        if (std::abs (hz - 880.0) < 20.0)
            carrier = std::max (carrier, magnitude);

        // Everything the modulation is entitled to lies below 4400 Hz. Above
        // it, in the audible band, there should be nothing.
        if (hz > 5000.0 && hz < 18000.0 && magnitude > worst)
        {
            worst = magnitude;
            worstHz = hz;
        }
    }

    CHECK (carrier > 0.0);

    const double relativeDb = 20.0 * std::log10 (worst / carrier + 1.0e-30);

    // CLAUDE.md section 7's threshold.
    (void) worstHz;

    // **Measured: -200.5 dB**, which is the numerical floor -- there is nothing
    // up there at all, which is what "band-limited by construction" predicts
    // rather than merely permits.
    //
    // Seen red by replacing the shape with the obvious implementation, a hard
    // gate on the alternate cycle: **-24.4 dB at 5.7 kHz**, 176 dB worse and
    // grossly audible. That is the difference between a modulator with a finite
    // Fourier series and one without, and it is why the shape is what it is.
    CHECK (relativeDb < -60.0);
}

TEZLA_TEST (an_lfo_attack_fades_its_depth_in_from_the_note)
{
    // A delayed vibrato: the note arrives steady and the movement creeps in
    // after it. Measured as the peak swing of the source itself over the first
    // and last thirds of a render, because a fade is a claim about *depth* over
    // time and a single peak reading cannot see it.
    constexpr double rate = 48000.0;
    constexpr double seconds = 1.2;

    const auto swings = [] (double attackSeconds)
    {
        auto parameters = brutal();

        parameters.lfo1Wave = Lfo::Wave::sine;
        parameters.lfo1RateHz = 20.0;
        parameters.lfo1Smooth = 0.0;
        parameters.lfo1AttackSeconds = attackSeconds;
        parameters.globalSlots[0] = { GlobalSource::none, GlobalDestination::none, 0.0 };

        Engine engine;
        engine.prepare (rate, 64);
        engine.setParameters (parameters);
        engine.noteOn (60, 1.0);

        Buffers buffers (64);

        const int blocks = static_cast<int> (rate * seconds) / 64;

        double early = 0.0;
        double late = 0.0;

        for (int block = 0; block < blocks; ++block)
        {
            std::fill (buffers.left.begin(), buffers.left.end(), 0.0);
            std::fill (buffers.right.begin(), buffers.right.end(), 0.0);

            engine.process (buffers.pointers, 64);

            const double value = std::abs (engine.readouts().lfo1.load());

            if (block < blocks / 6)
                early = std::max (early, value);
            else if (block > blocks * 5 / 6)
                late = std::max (late, value);
        }

        struct Result { double early, late; };

        return Result { early, late };
    };

    // With no attack the depth is there from the first cycle, and the two
    // thirds read the same.
    const auto none = swings (0.0);

    CHECK (none.early > 0.9);
    CHECK (none.late > 0.9);

    // With a one-second attack the first sixth is a long way down and the last
    // sixth is at full depth.
    const auto slow = swings (1.0);

    CHECK (slow.early < 0.2);
    CHECK (slow.late > 0.9);

    // And it is a fade rather than a gate -- the early reading is small but not
    // zero, because the LFO is running the whole time.
    CHECK (slow.early > 0.0);
}
