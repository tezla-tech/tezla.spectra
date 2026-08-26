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

// ============================================================================
//  Phase 2: mangle, multiband, expert controls
// ============================================================================

TEZLA_TEST (engine_harmonic_profile_has_not_drifted)
{
    // The numbers published in the plugin README, pinned. Every one of them is
    // a promise about how the plugin sounds, and a refactor that quietly moves
    // any of them has changed the sound of every project that uses it.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    struct Expected { double driveDb, levelDbFs, thdDb, secondDb, thirdDb; };
    const Expected expected[] = {
        {  0.0, -12.04, -41.40, -41.40, -71.95 },
        { 12.0, -12.06, -29.53, -29.60, -48.01 },
        { 30.0, -12.37, -14.35, -19.87, -16.22 },
    };

    const double frequency = measure::binExactFrequency (1000.0, fs, fftSize);
    const auto input = measure::sine (frequency, dsp::dbToGain (-12.0), fs, 2 * fftSize);

    for (const auto& reference : expected)
    {
        Parameters parameters;
        parameters.driveDb   = reference.driveDb;
        parameters.character = 0.35;
        parameters.ceilingDb = 0.0;
        parameters.kneeDb    = 0.0;
        parameters.autoTrim  = true;

        const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

        CHECK_NEAR (report.fundamentalDbFs,  reference.levelDbFs, 0.05);
        CHECK_NEAR (report.thdDb,            reference.thdDb,     0.1);
        CHECK_NEAR (report.harmonicsDb[0],   reference.secondDb,  0.1);
        CHECK_NEAR (report.harmonicsDb[1],   reference.thirdDb,   0.1);
    }
}

TEZLA_TEST (fold_at_zero_changes_nothing)
{
    // Fold sits in the path permanently, so its zero setting has to be a true
    // bypass -- not "almost".
    constexpr double fs = 48000.0;
    const auto input = measure::sine (220.0, 0.4, fs, 16384);

    Parameters without;
    without.driveDb = 9.0;

    Parameters with = without;
    with.foldAmount = 0.0;
    with.foldRange  = 100.0;      // range means nothing when the amount is zero

    const auto a = run (without, input, fs);
    const auto b = run (with, input, fs);

    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK (a[i] == b[i]);
}

TEZLA_TEST (fold_range_multiplier_escalates_the_damage)
{
    // The point of the x10 and x100 ranges: a clipper converges on a square
    // wave and stops getting more interesting, a folder does not. Harmonic
    // energy should keep climbing well past the point where the fundamental
    // stops being the loudest thing present.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (110.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.5, fs, 2 * fftSize);

    double previousThd = -200.0;

    std::printf ("        %-8s %10s %12s\n", "range", "THD", "aliasing");

    for (const double range : { 1.0, 10.0, 100.0 })
    {
        Parameters parameters;
        parameters.driveDb    = 0.0;
        parameters.foldAmount = 1.0;
        parameters.foldRange  = range;
        parameters.ceilingDb  = 0.0;
        parameters.kneeDb     = 0.0;
        parameters.autoTrim   = true;

        const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

        std::printf ("        x%-7.0f %10.2f %12.1f\n", range, report.thdDb, report.audibleAliasingDb);

        CHECK (report.thdDb > previousThd + 3.0);
        previousThd = report.thdDb;

        // Still has to hold a sensible output level -- Range changes the sound,
        // not the volume.
        CHECK (report.fundamentalDbFs > -30.0);
        CHECK (report.fundamentalDbFs < 6.0);
    }

    // By x100 the harmonics are louder than what generated them.
    CHECK (previousThd > 0.0);
}

TEZLA_TEST (fold_is_stable_at_maximum_everything)
{
    // Fold x100 into full drive into the limiter, at full scale. Nothing here
    // may produce a NaN or run away.
    constexpr double fs = 48000.0;

    Parameters parameters;
    parameters.driveDb    = 30.0;
    parameters.foldAmount = 1.0;
    parameters.foldRange  = 100.0;
    parameters.character  = 1.0;

    std::vector<double> input (16384);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 2.0 * std::sin (2.0 * std::numbers::pi * 45.0 * static_cast<double> (i) / fs);

    const auto output = run (parameters, input, fs);

    for (const double sample : output)
    {
        CHECK (std::isfinite (sample));
        CHECK (std::abs (sample) < 4.0);
    }
}

TEZLA_TEST (multiband_is_level_flat_against_single_band)
{
    // A crossover that does not sum flat shows up as a dip or a bump at the
    // crossover point, which on a mix bus is instantly audible.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    for (const double testFrequency : { 60.0, 120.0, 500.0, 2500.0, 8000.0 })
    {
        const double frequency = measure::binExactFrequency (testFrequency, fs, fftSize);
        const auto input = measure::sine (frequency, 0.25, fs, 2 * fftSize);

        Parameters single;
        single.driveDb   = 0.0;
        single.character = 0.0;
        single.ceilingDb = 0.0;
        single.kneeDb    = 0.0;
        single.autoTrim  = false;

        Parameters multi = single;
        multi.multiband = true;

        const auto a = measure::analyseHarmonics (tail (run (single, input, fs), fftSize), fs, frequency);
        const auto b = measure::analyseHarmonics (tail (run (multi,  input, fs), fftSize), fs, frequency);

        CHECK_NEAR (b.fundamentalDbFs, a.fundamentalDbFs, 0.3);
    }
}

TEZLA_TEST (multiband_solo_and_mute_route_correctly)
{
    constexpr double fs = 48000.0;

    const auto bandLevelDb = [fs] (double testFrequency, BandState low, BandState mid, BandState high)
    {
        Parameters parameters;
        parameters.multiband = true;
        parameters.driveDb   = 0.0;
        parameters.ceilingDb = 0.0;
        parameters.kneeDb    = 0.0;
        parameters.autoTrim  = false;
        parameters.bands[0].state = low;
        parameters.bands[1].state = mid;
        parameters.bands[2].state = high;

        const auto input = measure::sine (testFrequency, 0.25, fs, 24000);
        const auto output = tail (run (parameters, input, fs), 8192);

        double sumOfSquares = 0.0;
        for (const double sample : output)
            sumOfSquares += sample * sample;

        return dsp::gainToDb (std::sqrt (sumOfSquares / static_cast<double> (output.size())));
    };

    // 30 Hz rather than 50: a crossover is a slope, not a wall, and 50 Hz is
    // only about 30 dB down through the mid band's 4th-order rolloff. That is
    // correct behaviour, so the test picks a frequency where the answer is
    // unambiguous instead of pretending the slope is steeper than it is.
    CHECK (bandLevelDb (30.0,   BandState::Solo, BandState::On, BandState::On) > -20.0);
    CHECK (bandLevelDb (5000.0, BandState::Solo, BandState::On, BandState::On) < -50.0);

    // Muting the low band does the opposite.
    CHECK (bandLevelDb (30.0,   BandState::Mute, BandState::On, BandState::On) < -50.0);
    CHECK (bandLevelDb (5000.0, BandState::Mute, BandState::On, BandState::On) > -20.0);

    // Mute wins over another band's solo.
    CHECK (bandLevelDb (30.0, BandState::Mute, BandState::Solo, BandState::On) < -50.0);
}

TEZLA_TEST (multiband_band_drive_trim_is_independent)
{
    // The reason multiband exists here: a clean sub under destroyed mids.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    Parameters parameters;
    parameters.multiband = true;
    parameters.driveDb   = 6.0;
    parameters.ceilingDb = 0.0;
    parameters.kneeDb    = 0.0;
    parameters.autoTrim  = true;
    parameters.bands[0].driveTrimDb = -24.0;   // sub stays clean
    parameters.bands[1].driveTrimDb =  24.0;   // mids get hammered

    const auto profileAt = [&] (double testFrequency)
    {
        const double frequency = measure::binExactFrequency (testFrequency, fs, fftSize);
        const auto input = measure::sine (frequency, 0.3, fs, 2 * fftSize);
        return measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);
    };

    const auto sub = profileAt (55.0);
    const auto mid = profileAt (700.0);

    CHECK (sub.thdDb < -50.0);          // genuinely clean down there
    CHECK (mid.thdDb > -20.0);          // and genuinely dirty up here
    CHECK (mid.thdDb > sub.thdDb + 25.0);
}

TEZLA_TEST (multiband_master_limiter_holds_the_ceiling)
{
    constexpr double fs = 48000.0;
    constexpr double ceilingDb = -3.0;

    Parameters parameters;
    parameters.multiband = true;
    parameters.driveDb   = 18.0;
    parameters.ceilingDb = ceilingDb;
    parameters.kneeDb    = 6.0;
    parameters.attackMs  = 1.0;
    parameters.autoTrim  = false;

    std::vector<double> input (96000);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.5 * std::sin (2.0 * std::numbers::pi *  60.0 * static_cast<double> (i) / fs)
                 + 0.5 * std::sin (2.0 * std::numbers::pi * 900.0 * static_cast<double> (i) / fs)
                 + 0.4 * std::sin (2.0 * std::numbers::pi * 6000.0 * static_cast<double> (i) / fs);

    const auto output = tail (run (parameters, input, fs), 48000);

    double peak = 0.0;
    for (const double sample : output)
        peak = std::max (peak, std::abs (sample));

    // Three bands summing can overshoot what any one of them was limited to;
    // the master limiter is what catches that. Its attack is fixed and fast
    // rather than following the Speed control, because catching that overshoot
    // is its only job.
    CHECK (dsp::gainToDb (peak) <= ceilingDb + 1.5);
}

TEZLA_TEST (expert_bias_overrides_character)
{
    // With the expert panel on, bias is set directly and Character stops
    // driving it -- otherwise the two controls would fight.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (500.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.25, fs, 2 * fftSize);

    Parameters parameters;
    parameters.driveDb   = 18.0;
    parameters.character = 0.0;          // would normally mean "no even harmonics"
    parameters.ceilingDb = 0.0;
    parameters.kneeDb    = 0.0;
    parameters.expert.enabled = true;
    parameters.expert.bias    = 1.2;     // but expert says otherwise

    const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

    CHECK (report.harmonicsDb[0] > -30.0);   // strong second harmonic despite Character 0

    // And with bias back at zero the second harmonic disappears again.
    parameters.expert.bias = 0.0;
    const auto symmetric = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);
    CHECK (symmetric.harmonicsDb[0] < symmetric.harmonicsDb[1] - 30.0);
}

TEZLA_TEST (expert_adaa_toggle_actually_changes_the_aliasing)
{
    // The switch exists so you can hear what antialiasing is doing -- and
    // sometimes want the grit. If turning it off changed nothing measurable it
    // would be a lie on the panel.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (2000.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.5, fs, 2 * fftSize);

    Parameters parameters;
    parameters.driveDb   = 30.0;
    parameters.kneeDb    = 0.0;
    parameters.autoTrim  = false;
    parameters.oversampling = dsp::OversamplingMode::Off;   // no oversampling to hide behind

    // Ceiling above the signal so the limiter sits idle. Its gain modulation
    // produces sidebands that the analysis counts as non-harmonic energy, and
    // at these levels that swamps the thing being measured -- with the limiter
    // working, ADAA on and off both read -93.6 dB and the toggle looks broken.
    parameters.ceilingDb = 24.0;
    parameters.expert.enabled = true;

    parameters.expert.adaaEnabled = true;
    const auto on = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

    parameters.expert.adaaEnabled = false;
    const auto off = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

    std::printf ("        ADAA on %.1f dB, off %.1f dB aliasing\n",
                 on.audibleAliasingDb, off.audibleAliasingDb);

    CHECK (on.audibleAliasingDb < off.audibleAliasingDb - 3.0);
}

TEZLA_TEST (expert_stereo_link_changes_the_image)
{
    // Fully linked, both channels get the same gain and the centre image holds.
    // Fully independent, a loud left pulls only the left down.
    constexpr double fs = 48000.0;
    const int numSamples = 48000;

    const auto rightLevelWithLink = [&] (double link)
    {
        Parameters parameters;
        parameters.driveDb   = 0.0;
        parameters.ceilingDb = -18.0;
        parameters.kneeDb    = 0.0;
        parameters.attackMs  = 1.0;
        parameters.autoTrim  = false;
        parameters.expert.enabled    = true;
        parameters.expert.stereoLink = link;

        Engine engine;
        engine.prepare (fs, 512, 2);
        engine.setParameters (parameters);
        engine.reset();

        std::vector<double> left (static_cast<std::size_t> (numSamples));
        std::vector<double> right (static_cast<std::size_t> (numSamples));

        for (int i = 0; i < numSamples; ++i)
        {
            const double phase = 2.0 * std::numbers::pi * 200.0 * static_cast<double> (i) / fs;
            left[static_cast<std::size_t> (i)]  = 0.9 * std::sin (phase);   // loud
            right[static_cast<std::size_t> (i)] = 0.05 * std::sin (phase);  // quiet
        }

        for (int offset = 0; offset < numSamples; offset += 512)
        {
            const int block = std::min (512, numSamples - offset);
            double* pointers[2] = { left.data() + offset, right.data() + offset };
            engine.process (pointers, 2, block);
        }

        double sumOfSquares = 0.0;
        for (int i = numSamples / 2; i < numSamples; ++i)
            sumOfSquares += right[static_cast<std::size_t> (i)] * right[static_cast<std::size_t> (i)];

        return dsp::gainToDb (std::sqrt (sumOfSquares / static_cast<double> (numSamples / 2)));
    };

    const double linked = rightLevelWithLink (1.0);
    const double independent = rightLevelWithLink (0.0);

    // Linked: the quiet right channel is pulled down with the loud left.
    // Independent: it is left alone, so it stays louder.
    CHECK (independent > linked + 3.0);
}

TEZLA_TEST (expert_headroom_moves_where_saturation_starts)
{
    // The constant that decides how soon the curve bends. Smaller headroom
    // means the same drive distorts far more.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (1000.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.25, fs, 2 * fftSize);

    double previousThd = -300.0;

    for (const double headroom : { 16.0, 8.0, 4.0, 2.0, 1.0 })
    {
        Parameters parameters;
        parameters.driveDb   = 6.0;
        parameters.ceilingDb = 0.0;
        parameters.kneeDb    = 0.0;
        parameters.expert.enabled = true;
        parameters.expert.shaperHeadroom = headroom;

        const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

        CHECK (report.thdDb > previousThd);
        previousThd = report.thdDb;
    }
}


TEZLA_TEST (oversampling_mode_applies_on_the_very_first_call)
{
    // prepare() necessarily runs before any parameters are known, so it uses
    // the defaults. The engine used to decide whether to rebuild by asking
    // "have parameters been set before?", which meant the first setParameters()
    // after prepare() was ignored -- so a project saved with oversampling on
    // anything but Auto reopened running Auto, and no test noticed because both
    // the measurement and the reference were wrong in the same way.
    constexpr double fs = 48000.0;

    struct Expected { dsp::OversamplingMode mode; int factor; int latency; };
    const Expected expected[] = {
        { dsp::OversamplingMode::Off,  1,  0 },
        { dsp::OversamplingMode::X2,   2, 47 },
        { dsp::OversamplingMode::X4,   4, 63 },
        { dsp::OversamplingMode::X8,   8, 71 },
        { dsp::OversamplingMode::Auto, 4, 63 },   // 48 kHz session
    };

    for (const auto& reference : expected)
    {
        Parameters parameters;
        parameters.oversampling = reference.mode;

        Engine engine;
        engine.prepare (fs, 512, 1);
        engine.setParameters (parameters);

        CHECK (engine.getOversamplingFactor() == reference.factor);
        CHECK (engine.getLatencySamples() == reference.latency);
    }
}

// ============================================================================
//  Phase 3: rectify, crush, downsample, feedback
// ============================================================================

TEZLA_TEST (new_mangle_stages_are_bypassed_at_their_zero_settings)
{
    // All four sit permanently in the path. Each one's neutral setting has to
    // be a true bypass, or existing projects change when the plugin updates.
    constexpr double fs = 48000.0;
    const auto input = measure::sine (220.0, 0.4, fs, 16384);

    Parameters neutral;
    neutral.driveDb = 9.0;

    const auto reference = run (neutral, input, fs);

    Parameters withStages = neutral;
    withStages.rectify    = 0.0;
    withStages.feedback   = 0.0;
    withStages.feedbackMs = 25.0;    // set, but inert while feedback is 0
    withStages.crush      = 0.0;
    withStages.downsample = 1.0;

    const auto result = run (withStages, input, fs);

    for (std::size_t i = 0; i < reference.size(); ++i)
        CHECK (reference[i] == result[i]);
}

TEZLA_TEST (rectify_adds_an_octave_through_the_whole_engine)
{
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (300.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.3, fs, 2 * fftSize);

    double previousSecond = -400.0;

    std::printf ("        %-10s %10s %10s\n", "rectify", "h2 (octave)", "THD");

    for (const double rectify : { 0.0, 0.3, 0.6, 1.0 })
    {
        Parameters parameters;
        parameters.driveDb   = 0.0;
        parameters.character = 0.0;      // symmetric, so h2 can only come from rectify
        parameters.ceilingDb = 24.0;
        parameters.kneeDb    = 0.0;
        parameters.rectify   = rectify;
        parameters.autoTrim  = true;

        const auto report = measure::analyseHarmonics (tail (run (parameters, input, fs), fftSize), fs, frequency);

        std::printf ("        %-10.1f %10.2f %10.2f\n", rectify, report.harmonicsDb[0], report.thdDb);

        CHECK (report.harmonicsDb[0] > previousSecond);
        previousSecond = report.harmonicsDb[0];
    }

    // Fully rectified, the octave is louder than what is left of the original.
    CHECK (previousSecond > 0.0);
}

TEZLA_TEST (crush_and_downsample_are_wet_only)
{
    // They live outside the oversampled block, which is also outside the
    // dry/wet mix. A fully dry setting must be untouched by either.
    constexpr double fs = 48000.0;

    std::vector<double> input (16384);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.4 * std::sin (2.0 * std::numbers::pi * 220.0 * static_cast<double> (i) / fs);

    Parameters parameters;
    parameters.mix        = 0.0;
    parameters.crush      = 1.0;
    parameters.downsample = 32.0;
    parameters.driveDb    = 24.0;

    const auto output = run (parameters, input, fs);
    const int latency = latencyOf (parameters, fs);

    double worstError = 0.0;
    for (std::size_t i = static_cast<std::size_t> (latency) + 512; i < input.size(); ++i)
        worstError = std::max (worstError, std::abs (output[i] - input[i - static_cast<std::size_t> (latency)]));

    CHECK (worstError < 1.0e-9);
}

TEZLA_TEST (crush_and_downsample_alias_on_purpose)
{
    // The one place in this plugin where more aliasing is the correct outcome.
    // If a future change quietly starts antialiasing these, the bit crusher
    // stops sounding like a bit crusher and this test says so.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 32768;

    const double frequency = measure::binExactFrequency (1000.0, fs, fftSize);
    const auto input = measure::sine (frequency, 0.5, fs, 2 * fftSize);

    Parameters clean;
    clean.ceilingDb = 24.0;
    clean.kneeDb    = 0.0;
    clean.autoTrim  = false;

    const auto reference = measure::analyseHarmonics (tail (run (clean, input, fs), fftSize), fs, frequency);
    CHECK (reference.audibleAliasingDb < -100.0);

    Parameters crushed = clean;
    crushed.crush = 0.8;
    const auto crushedReport = measure::analyseHarmonics (tail (run (crushed, input, fs), fftSize), fs, frequency);

    Parameters reduced = clean;
    reduced.downsample = 12.0;
    const auto reducedReport = measure::analyseHarmonics (tail (run (reduced, input, fs), fftSize), fs, frequency);

    std::printf ("        clean %.1f dB, crushed %.1f dB, downsampled %.1f dB\n",
                 reference.audibleAliasingDb, crushedReport.audibleAliasingDb,
                 reducedReport.audibleAliasingDb);

    CHECK (crushedReport.audibleAliasingDb > reference.audibleAliasingDb + 40.0);
    CHECK (reducedReport.audibleAliasingDb > reference.audibleAliasingDb + 40.0);
}

TEZLA_TEST (feedback_cannot_run_away_at_any_setting)
{
    // The guarantee. A feedback loop through a nonlinearity with drive in it is
    // exactly the arrangement that blows up, so this sweeps the whole parameter
    // space rather than sampling a couple of points.
    constexpr double fs = 48000.0;

    std::vector<double> impulseTrain (48000, 0.0);
    for (std::size_t i = 0; i < impulseTrain.size(); i += 4800)
        impulseTrain[i] = 1.0;

    for (const double feedback : { 0.3, 0.6, 0.9, 0.95, 2.0 })      // 2.0 must clamp
        for (const double delayMs : { 0.1, 1.0, 8.0, 50.0 })
            for (const double driveDb : { 0.0, 30.0 })
            {
                Parameters parameters;
                parameters.driveDb    = driveDb;
                parameters.feedback   = feedback;
                parameters.feedbackMs = delayMs;
                parameters.character  = 1.0;
                parameters.foldAmount = 1.0;
                parameters.foldRange  = 10.0;       // everything at once
                parameters.rectify    = 0.5;

                const auto output = run (parameters, impulseTrain, fs);

                for (const double sample : output)
                {
                    CHECK (std::isfinite (sample));
                    CHECK (std::abs (sample) < 8.0);
                }
            }
}

TEZLA_TEST (feedback_leaves_silence_silent)
{
    // A resonant loop that self-starts from nothing would make the plugin
    // unusable on a track with rests in it.
    constexpr double fs = 48000.0;

    Parameters parameters;
    parameters.feedback   = 0.95;
    parameters.feedbackMs = 4.0;
    parameters.driveDb    = 30.0;

    const auto output = run (parameters, std::vector<double> (24000, 0.0), fs);

    for (const double sample : output)
        CHECK (std::abs (sample) < 1.0e-12);
}

TEZLA_TEST (feedback_sustains_after_the_input_stops)
{
    // What the control is for: energy keeps circulating once the note has gone.
    constexpr double fs = 48000.0;

    std::vector<double> burst (48000, 0.0);
    for (std::size_t i = 0; i < 2400; ++i)
        burst[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 110.0 * static_cast<double> (i) / fs);

    const auto tailEnergyDb = [&] (double feedback)
    {
        Parameters parameters;
        parameters.driveDb    = 12.0;
        parameters.feedback   = feedback;
        parameters.feedbackMs = 6.0;
        parameters.ceilingDb  = 0.0;
        parameters.kneeDb     = 0.0;

        const auto output = run (parameters, burst, fs);

        // Well after the burst has finished.
        double sumOfSquares = 0.0;
        for (std::size_t i = 24000; i < output.size(); ++i)
            sumOfSquares += output[i] * output[i];

        return dsp::gainToDb (std::sqrt (sumOfSquares / 24000.0));
    };

    const double without = tailEnergyDb (0.0);
    const double with    = tailEnergyDb (0.9);

    std::printf ("        tail after the burst: %.1f dB without feedback, %.1f dB with\n",
                 without, with);

    CHECK (without < -80.0);          // nothing left once the note stops
    CHECK (with > without + 30.0);    // and plenty left with the loop running
}

TEZLA_TEST (feedback_repeats_at_the_delay_time)
{
    // What makes the loop playable rather than just noisy: the circulating
    // signal repeats at the delay period, so the resonance moves with the
    // control.
    //
    // Measured by autocorrelation rather than by looking for a spectral peak.
    // Spectrally the loop imposes a comb of 1/delay on whatever is circulating,
    // which shows up as sidebands around the sustained tone -- and at delays
    // like 4 ms those sidebands land exactly on the tone's own harmonics and
    // become invisible. Autocorrelation asks the question directly: does the
    // output repeat itself after this many samples?
    constexpr double fs = 48000.0;

    const auto bestLag = [&] (double delayMs, int searchFrom, int searchTo)
    {
        Parameters parameters;
        parameters.driveDb    = 18.0;
        parameters.feedback   = 0.9;
        parameters.feedbackMs = delayMs;
        parameters.ceilingDb  = 0.0;
        parameters.kneeDb     = 0.0;

        // A short burst of a frequency that is not commensurate with any of the
        // delays under test, so nothing lines up by accident.
        std::vector<double> excitation (48000, 0.0);
        for (std::size_t i = 0; i < 1200; ++i)
            excitation[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 733.0 * static_cast<double> (i) / fs);

        const auto output = run (parameters, excitation, fs);

        const std::size_t analysisStart = 24000;
        const std::size_t analysisLength = 12000;

        double bestScore = -2.0;
        int bestLagSamples = 0;

        for (int lag = searchFrom; lag <= searchTo; ++lag)
        {
            double correlation = 0.0, energyA = 0.0, energyB = 0.0;
            for (std::size_t i = 0; i < analysisLength; ++i)
            {
                const double a = output[analysisStart + i];
                const double b = output[analysisStart + i + static_cast<std::size_t> (lag)];
                correlation += a * b;
                energyA += a * a;
                energyB += b * b;
            }

            const double denominator = std::sqrt (energyA * energyB);
            const double score = denominator > 1.0e-20 ? correlation / denominator : 0.0;

            if (score > bestScore)
            {
                bestScore = score;
                bestLagSamples = lag;
            }
        }

        return std::pair { bestLagSamples, bestScore };
    };

    for (const double delayMs : { 2.0, 4.0, 8.0 })
    {
        const int expected = static_cast<int> (std::llround (delayMs * 0.001 * fs));
        const auto [lag, score] = bestLag (delayMs, expected / 2, expected * 2);

        std::printf ("        %.0f ms delay: strongest repeat at %d samples (expected %d), r = %.2f\n",
                     delayMs, lag, expected, score);

        // Within a couple of percent, and genuinely periodic rather than noise.
        CHECK (std::abs (lag - expected) <= std::max (2, expected / 40));
        CHECK (score > 0.5);
    }
}

TEZLA_TEST (emberdrive_settles_the_same_way_at_any_host_block_size)
{
    // The voicing -- four biquads, the shaper bias and the auto-trim -- is far
    // too expensive to rebuild per sample: the trim alone probes the whole
    // nonlinear chain 512 times per band. It used to be rebuilt once per
    // process() call instead, which made two things wrong at once.
    //
    // The output depended on the host's buffer size: a host on 64-sample blocks
    // rebuilt eight times as often as one on 512, so the same automation settled
    // along a different path and a bounce did not match what was heard.
    //
    // And modulation, which calls the engine every 32 samples, ran that probe
    // forty-eight times per output sample -- measured at 3.3x the CPU with eight
    // slots assigned, on a plugin meant to run twenty at a time.
    //
    // The rebuild is on a timer counted in samples now, carrying its remainder,
    // so it lands at the same absolute position however the block is cut up.
    const auto settle = [] (int blockSize)
    {
        constexpr double rate = 48000.0;
        constexpr int total = 24576;

        Engine engine;
        engine.prepare (rate, 1024, 1);

        Parameters parameters;
        parameters.driveDb = 0.0;
        parameters.toneTilt = 0.0;
        engine.setParameters (parameters);
        engine.reset();

        // Now move everything the voicing depends on, at once.
        parameters.driveDb  = 18.0;
        parameters.toneTilt = 0.6;
        parameters.foldAmount = 0.4;
        parameters.rectify = 0.3;

        std::vector<double> output;
        output.reserve (static_cast<std::size_t> (total));

        std::vector<double> block (static_cast<std::size_t> (blockSize));

        for (int written = 0; written < total; written += blockSize)
        {
            const int span = std::min (blockSize, total - written);

            for (int i = 0; i < span; ++i)
            {
                const double t = static_cast<double> (written + i) / rate;
                block[static_cast<std::size_t> (i)] =
                    0.5 * std::sin (2.0 * std::numbers::pi * 110.0 * t)
                  + 0.2 * std::sin (2.0 * std::numbers::pi * 1300.0 * t);
            }

            double* pointers[1] { block.data() };
            engine.setParameters (parameters);
            engine.process (pointers, 1, span);

            for (int i = 0; i < span; ++i)
                output.push_back (block[static_cast<std::size_t> (i)]);
        }

        return output;
    };

    const auto small = settle (64);
    const auto large = settle (512);
    const auto odd   = settle (100);          // deliberately not a power of two

    CHECK (small.size() == large.size());

    double worstAgainstLarge = 0.0;
    double worstAgainstOdd = 0.0;

    for (std::size_t i = 0; i < small.size(); ++i)
    {
        worstAgainstLarge = std::max (worstAgainstLarge, std::abs (small[i] - large[i]));
        worstAgainstOdd   = std::max (worstAgainstOdd,   std::abs (small[i] - odd[i]));
    }

    // Reverting the timer takes these to about 0.05 -- five percent of full
    // scale, which is what "the bounce does not match" sounds like.
    // Exactly zero, measured. Rebuilding once per call instead -- which is what
    // this did until the loop was cut at the voicing boundary -- takes them to
    // 0.296 and 0.310, a third of full scale.
    CHECK (worstAgainstLarge < 1.0e-9);
    CHECK (worstAgainstOdd < 1.0e-9);
}
