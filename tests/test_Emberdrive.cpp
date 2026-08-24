#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include "EmberdriveEngine.hpp"

using namespace tezla;
using namespace tezla::emberdrive;

namespace {

/// Runs a mono signal through the engine, block by block, and returns the
/// output. Block size deliberately does not divide the signal length evenly.
std::vector<double> run (const Parameters& parameters, const std::vector<double>& input,
                         double sampleRate, int blockSize = 271)
{
    Engine engine;
    engine.prepare (sampleRate, blockSize, 1);
    engine.setParameters (parameters);
    engine.reset();   // snap the smoothers, so the run starts at the stated settings

    std::vector<double> output = input;

    for (std::size_t offset = 0; offset < output.size(); offset += static_cast<std::size_t> (blockSize))
    {
        const int numSamples = static_cast<int> (std::min (static_cast<std::size_t> (blockSize),
                                                           output.size() - offset));
        double* pointer = output.data() + offset;
        engine.process (&pointer, 1, numSamples);
    }

    return output;
}

int latencyOf (const Parameters& parameters, double sampleRate)
{
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setParameters (parameters);
    return engine.getLatencySamples();
}

std::vector<double> tail (const std::vector<double>& v, std::size_t n)
{
    return { v.end() - static_cast<std::ptrdiff_t> (n), v.end() };
}

} // namespace

TEZLA_TEST (engine_silence_in_silence_out)
{
    // Non-negotiable: an idle track must be silent, at every setting. An
    // asymmetric shaper with a DC offset would fail this, which is exactly why
    // the check exists.
    for (const double character : { 0.0, 0.5, 1.0 })
        for (const double driveDb : { 0.0, 12.0, 30.0 })
        {
            Parameters parameters;
            parameters.driveDb = driveDb;
            parameters.character = character;

            const auto output = run (parameters, std::vector<double> (4096, 0.0), 48000.0);

            for (const double sample : output)
                CHECK (std::abs (sample) < 1.0e-12);
        }
}

TEZLA_TEST (engine_produces_no_dc_at_any_setting)
{
    // Asymmetry is how the valve end of Character makes even harmonics, and it
    // is also how a saturator quietly eats headroom and thumps on bypass.
    constexpr double fs = 48000.0;
    // Bin-exact, so the analysis window holds a whole number of cycles. A
    // partial cycle has a non-zero mean of its own -- about 1e-3 here -- which
    // would be blamed on the plugin as DC it never produced.
    const double frequency = measure::binExactFrequency (100.0, fs, 32768);
    const auto input = measure::sine (frequency, 0.5, fs, 65536);

    for (const double character : { 0.0, 0.5, 1.0 })
        for (const double driveDb : { 6.0, 18.0, 30.0 })
        {
            Parameters parameters;
            parameters.driveDb = driveDb;
            parameters.character = character;

            const auto output = tail (run (parameters, input, fs), 32768);

            double sum = 0.0;
            for (const double sample : output)
                sum += sample;

            const double dcOffset = std::abs (sum / static_cast<double> (output.size()));
            CHECK (dcOffset < 1.0e-4);
        }
}

TEZLA_TEST (engine_is_transparent_when_asked_to_be)
{
    // Priority two in CLAUDE.md: a clean setting has to be genuinely clean, not
    // a quieter version of the dirty one.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    Parameters parameters;
    parameters.driveDb   = 0.0;
    parameters.character = 0.0;
    parameters.toneTilt  = 0.0;
    parameters.ceilingDb = 0.0;
    parameters.kneeDb    = 0.0;

    const double frequency = measure::binExactFrequency (1000.0, fs, fftSize);
    const auto input = measure::sine (frequency, dsp::dbToGain (-20.0), fs, 2 * fftSize);
    const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

    CHECK_NEAR (report.fundamentalDbFs, -20.0, 0.2);
    CHECK (report.thdDb < -80.0);
    CHECK (report.audibleAliasingDb < -100.0);
}

TEZLA_TEST (engine_mix_at_zero_returns_the_dry_signal)
{
    // The dry path is the upsampled input, so a fully dry mix must come back as
    // the input delayed by exactly the reported latency. If this fails, partial
    // mix settings are combing.
    constexpr double fs = 48000.0;

    Parameters parameters;
    parameters.mix = 0.0;
    parameters.driveDb = 24.0;      // hard drive, to prove none of it leaks through

    std::vector<double> input (16384);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.4 * std::sin (2.0 * std::numbers::pi * 220.0 * static_cast<double> (i) / fs)
                 + 0.2 * std::sin (2.0 * std::numbers::pi * 3300.0 * static_cast<double> (i) / fs);

    const auto output = run (parameters, input, fs);
    const int latency = latencyOf (parameters, fs);

    double worstError = 0.0;
    for (std::size_t i = static_cast<std::size_t> (latency) + 512; i < input.size(); ++i)
        worstError = std::max (worstError, std::abs (output[i] - input[i - static_cast<std::size_t> (latency)]));

    CHECK (worstError < 1.0e-4);
}

TEZLA_TEST (engine_respects_the_ceiling)
{
    constexpr double fs = 48000.0;

    for (const double ceilingDb : { -12.0, -6.0, -0.3 })
    {
        Parameters parameters;
        parameters.driveDb   = 24.0;
        parameters.ceilingDb = ceilingDb;
        parameters.kneeDb    = 6.0;
        parameters.attackMs  = 1.0;
        parameters.outputDb  = 0.0;
        parameters.autoTrim  = false;

        const auto input = measure::sine (200.0, 0.9, fs, 96000);
        const auto output = tail (run (parameters, input, fs), 48000);

        double peak = 0.0;
        for (const double sample : output)
            peak = std::max (peak, std::abs (sample));

        // A feed-forward limiter with a finite attack tracks rather than
        // predicts, so a little ripple over the ceiling is honest. 1.5 dB is
        // the acceptance bar; more than that means it is not limiting.
        CHECK (dsp::gainToDb (peak) <= ceilingDb + 1.5);
    }
}

TEZLA_TEST (engine_latency_matches_the_reported_value)
{
    // The host compensates by whatever number we report. If the real delay is
    // different, every parallel track in the project is out of time.
    constexpr double fs = 48000.0;

    Parameters parameters;
    parameters.mix = 0.0;

    for (const auto mode : { dsp::OversamplingMode::Off, dsp::OversamplingMode::X2,
                             dsp::OversamplingMode::X4, dsp::OversamplingMode::X8 })
    {
        parameters.oversampling = mode;

        std::vector<double> input (8192, 0.0);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 500.0 * static_cast<double> (i) / fs);

        const auto output = run (parameters, input, fs);
        const int reported = latencyOf (parameters, fs);

        // Correlate to find the delay that actually lines up.
        int bestDelay = -1;
        double bestError = 1.0e30;
        for (int delay = 0; delay < 200; ++delay)
        {
            double error = 0.0;
            for (std::size_t i = 2000; i < 4000; ++i)
                error += std::abs (output[i] - input[i - static_cast<std::size_t> (delay)]);

            if (error < bestError)
            {
                bestError = error;
                bestDelay = delay;
            }
        }

        CHECK (bestDelay == reported);
    }
}

TEZLA_TEST (engine_sounds_the_same_at_every_session_rate)
{
    // The rule this rig cares about most. Same settings, four session rates:
    // the harmonic structure must match, because that is what "sounds the same"
    // means for a saturator.
    constexpr std::size_t fftSize = 32768;

    Parameters parameters;
    parameters.driveDb   = 18.0;
    parameters.character = 0.5;
    parameters.ceilingDb = 0.0;
    parameters.kneeDb    = 0.0;
    parameters.autoTrim  = false;

    std::vector<double> reference;

    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const double frequency = measure::binExactFrequency (1000.0, fs, fftSize);
        const auto input = measure::sine (frequency, 0.3, fs, 4 * fftSize);
        const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

        std::printf ("        %7.0f Hz: THD %6.2f dB  h2 %7.2f  h3 %7.2f  h4 %7.2f  h5 %7.2f\n",
                     fs, report.thdDb,
                     report.harmonicsDb[0], report.harmonicsDb[1],
                     report.harmonicsDb[2], report.harmonicsDb[3]);

        std::vector<double> profile (report.harmonicsDb.begin(), report.harmonicsDb.begin() + 5);

        if (reference.empty())
            reference = profile;
        else
            for (std::size_t h = 0; h < profile.size(); ++h)
                CHECK_NEAR (profile[h], reference[h], 1.0);
    }
}

TEZLA_TEST (engine_character_moves_even_harmonics_without_moving_the_level)
{
    // The Character control has one job: change the harmonic content. If it
    // also changes the level, the user is judging loudness, not tone.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (500.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.25, fs, 2 * fftSize);

    // The tape end is symmetric to the limits of double precision -- measured
    // at -282 dB, not merely "small" -- so the starting sentinel has to sit
    // below that rather than at a comfortable-looking round number.
    double previousSecondHarmonic = -400.0;

    for (const double character : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        Parameters parameters;
        parameters.driveDb   = 18.0;
        parameters.character = character;
        parameters.toneTilt  = 0.0;
        parameters.ceilingDb = 0.0;
        parameters.kneeDb    = 0.0;
        parameters.autoTrim  = true;

        const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

        std::printf ("        character %.2f: level %7.2f dBFS  h2 %8.2f  h3 %7.2f  THD %6.2f\n",
                     character, report.fundamentalDbFs,
                     report.harmonicsDb[0], report.harmonicsDb[1], report.thdDb);

        // Even harmonics rise monotonically towards the valve end.
        CHECK (report.harmonicsDb[0] > previousSecondHarmonic);
        previousSecondHarmonic = report.harmonicsDb[0];

        // And the level stays put: auto-trim exists so this control is a tone
        // control, not a volume control.
        CHECK_NEAR (report.fundamentalDbFs, dsp::gainToDb (0.25), 1.5);
    }

    // The tape end must be genuinely symmetric -- no even harmonics at all.
    Parameters tape;
    tape.driveDb = 18.0;
    tape.character = 0.0;
    tape.ceilingDb = 0.0;
    tape.kneeDb = 0.0;
    const auto tapeReport = measure::analyseHarmonics (tail (run (tape, input, fs), fftSize), fs, frequency);
    CHECK (tapeReport.harmonicsDb[0] < tapeReport.harmonicsDb[1] - 30.0);
}

TEZLA_TEST (engine_survives_extreme_input)
{
    // Dubstep sends things into plugins that were never meant to go there.
    // Nothing here may produce a NaN, an infinity, or a runaway.
    constexpr double fs = 48000.0;

    Parameters parameters;
    parameters.driveDb = 30.0;
    parameters.character = 1.0;

    std::vector<double> input (8192);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 40.0 * std::sin (2.0 * std::numbers::pi * 45.0 * static_cast<double> (i) / fs);

    const auto output = run (parameters, input, fs);

    for (const double sample : output)
    {
        CHECK (std::isfinite (sample));
        CHECK (std::abs (sample) < 10.0);
    }
}

TEZLA_TEST (engine_reset_leaves_no_state_behind)
{
    // Two runs from a clean engine must be bit-identical, or state is leaking
    // between projects and the plugin is not reproducible.
    constexpr double fs = 48000.0;

    Parameters parameters;
    parameters.driveDb = 20.0;

    const auto input = measure::sine (300.0, 0.5, fs, 4096);

    Engine engine;
    engine.prepare (fs, 512, 1);
    engine.setParameters (parameters);

    const auto once = [&]
    {
        engine.reset();
        std::vector<double> buffer = input;
        for (std::size_t offset = 0; offset < buffer.size(); offset += 512)
        {
            const int numSamples = static_cast<int> (std::min<std::size_t> (512, buffer.size() - offset));
            double* pointer = buffer.data() + offset;
            engine.process (&pointer, 1, numSamples);
        }
        return buffer;
    };

    const auto first  = once();
    const auto second = once();

    for (std::size_t i = 0; i < first.size(); ++i)
        CHECK (first[i] == second[i]);
}
