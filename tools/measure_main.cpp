// tezla-measure -- offline analysis, so DSP claims can be checked with numbers
// before anything is loaded into a DAW.
//
//   tezla-measure selftest
//   tezla-measure filter-response [--fs 48000] [--freq 1000] [--q 0.707] [--out response.csv]
//   tezla-measure clip-aliasing   [--fs 48000] [--freq 1000] [--drive 4]
//
// New commands get added here as plugins need them. Anything that measures a
// nonlinearity belongs in this tool, not in a DAW screenshot.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Version.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include "EmberdriveEngine.hpp"

namespace {

struct Args
{
    double sampleRate   { 48000.0 };
    double frequency    { 1000.0 };
    double q            { 0.707 };
    double gainDb       { 0.0 };
    double drive        { 4.0 };
    std::string outPath;

    static Args parse (int argc, char** argv)
    {
        Args args;
        for (int i = 2; i + 1 < argc; i += 2)
        {
            const std::string key = argv[i];
            const std::string value = argv[i + 1];

            if      (key == "--fs")    args.sampleRate = std::atof (value.c_str());
            else if (key == "--freq")  args.frequency  = std::atof (value.c_str());
            else if (key == "--q")     args.q          = std::atof (value.c_str());
            else if (key == "--gain")  args.gainDb     = std::atof (value.c_str());
            else if (key == "--drive") args.drive      = std::atof (value.c_str());
            else if (key == "--out")   args.outPath    = value;
        }
        return args;
    }
};

int runSelfTest()
{
    using namespace tezla::measure;

    constexpr std::size_t fftSize = 32768;
    constexpr double sampleRate = 48000.0;

    // A pure sine must analyse as a pure sine: no harmonics, nothing else.
    const double frequency = binExactFrequency (1000.0, sampleRate, fftSize);
    const auto report = analyseHarmonics (sine (frequency, 0.5, sampleRate, fftSize), sampleRate, frequency);

    std::printf ("FFT self-test on a clean %.2f Hz sine at -6 dBFS:\n", frequency);
    std::printf ("  fundamental : %8.2f dBFS   (expect -6.02)\n", report.fundamentalDbFs);
    std::printf ("  THD         : %8.2f dB     (expect < -200)\n", report.thdDb);
    std::printf ("  non-harmonic: %8.2f dB     (expect < -200)\n", report.aliasingDb);

    const bool ok = std::abs (report.fundamentalDbFs + 6.02) < 0.1
                 && report.thdDb < -200.0
                 && report.aliasingDb < -200.0;

    std::printf ("\n  %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int runFilterResponse (const Args& args)
{
    const auto coefficients = tezla::dsp::design::lowpass (args.frequency, args.q, args.sampleRate);

    std::FILE* out = args.outPath.empty() ? stdout : std::fopen (args.outPath.c_str(), "w");
    if (out == nullptr)
    {
        std::fprintf (stderr, "could not open %s for writing\n", args.outPath.c_str());
        return 1;
    }

    std::fprintf (out, "frequency_hz,magnitude_db\n");
    for (int i = 0; i <= 400; ++i)
    {
        const double frequency = 10.0 * std::pow (10.0, 3.3 * static_cast<double> (i) / 400.0);
        if (frequency >= args.sampleRate * 0.5)
            break;

        std::fprintf (out, "%.4f,%.6f\n", frequency,
                      20.0 * std::log10 (std::max (coefficients.magnitudeAt (frequency, args.sampleRate), 1.0e-12)));
    }

    if (out != stdout)
    {
        std::fclose (out);
        std::printf ("wrote %s\n", args.outPath.c_str());
    }
    return 0;
}

int runClipAliasing (const Args& args)
{
    using namespace tezla::measure;

    // A naive hard clipper with no oversampling and no antiderivative trick.
    // This is the baseline every saturation stage in this repository has to
    // beat, and having the number written down makes "we improved it" checkable.
    constexpr std::size_t fftSize = 65536;
    const double frequency = binExactFrequency (args.frequency, args.sampleRate, fftSize);

    auto signal = sine (frequency, 0.5, args.sampleRate, fftSize);
    for (auto& sample : signal)
        sample = std::clamp (sample * args.drive, -1.0, 1.0);

    const auto report = analyseHarmonics (signal, args.sampleRate, frequency);

    std::printf ("naive hard clip, drive x%.1f, %.1f Hz at %.0f Hz sample rate\n",
                 args.drive, frequency, args.sampleRate);
    std::printf ("  fundamental  : %7.2f dBFS\n", report.fundamentalDbFs);
    std::printf ("  THD          : %7.2f dB  (%.3f %%, %d harmonics in band)\n",
                 report.thdDb, report.thdPercent, report.harmonicsCounted);
    std::printf ("  aliasing     : %7.2f dB  <-- the number to beat\n", report.aliasingDb);

    for (std::size_t i = 0; i < report.harmonicsDb.size() && i < 8; ++i)
        std::printf ("  harmonic %2zu  : %7.2f dB\n", i + 2, report.harmonicsDb[i]);

    return 0;
}

/// Sweeps Emberdrive's drive control at every session rate the rig uses and
/// reports what it does to harmonics and aliasing. This is the measurement that
/// backs up any claim about how the plugin sounds.
int runEmberdrive (const Args& args)
{
    using namespace tezla::measure;
    using namespace tezla::emberdrive;

    constexpr std::size_t fftSize = 32768;
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const double drives[] = { 0.0, 6.0, 12.0, 18.0, 24.0, 30.0 };

    std::FILE* csv = nullptr;
    if (! args.outPath.empty())
    {
        csv = std::fopen (args.outPath.c_str(), "w");
        if (csv == nullptr)
        {
            std::fprintf (stderr, "could not open %s for writing\n", args.outPath.c_str());
            return 1;
        }
        std::fprintf (csv, "sample_rate,oversampling,drive_db,character,level_dbfs,thd_db,h2_db,h3_db,audible_aliasing_db,latency_samples\n");
    }

    std::printf ("Emberdrive -- drive sweep, character %.2f, %.0f Hz test tone\n\n", args.gainDb, args.frequency);

    for (const double sampleRate : rates)
    {
        Parameters parameters;
        parameters.character = args.gainDb;    // --gain doubles as character here
        parameters.ceilingDb = 0.0;
        parameters.kneeDb    = 0.0;
        parameters.autoTrim  = true;

        Engine probe;
        probe.prepare (sampleRate, 512, 1);
        probe.setParameters (parameters);

        std::printf ("  %7.0f Hz session, Auto = x%d (%.0f kHz internal), latency %d samples (%.2f ms)\n",
                     sampleRate, probe.getOversamplingFactor(), probe.getOversampledRate() / 1000.0,
                     probe.getLatencySamples(),
                     1000.0 * probe.getLatencySamples() / sampleRate);
        std::printf ("    %-8s %10s %8s %9s %9s %12s\n", "drive", "level", "THD", "h2", "h3", "aliasing");

        for (const double driveDb : drives)
        {
            parameters.driveDb = driveDb;

            Engine engine;
            engine.prepare (sampleRate, 271, 1);
            engine.setParameters (parameters);
            engine.reset();

            const double frequency = binExactFrequency (args.frequency, sampleRate, fftSize);
            auto signal = sine (frequency, 0.25, sampleRate, 2 * fftSize);

            for (std::size_t offset = 0; offset < signal.size(); offset += 271)
            {
                const int numSamples = static_cast<int> (std::min<std::size_t> (271, signal.size() - offset));
                double* pointer = signal.data() + offset;
                engine.process (&pointer, 1, numSamples);
            }

            const std::vector<double> steadyState (signal.end() - static_cast<std::ptrdiff_t> (fftSize),
                                                   signal.end());
            const auto report = analyseHarmonics (steadyState, sampleRate, frequency);

            std::printf ("    +%-6.0f dB %9.2f %8.2f %9.2f %9.2f %12.1f\n",
                         driveDb, report.fundamentalDbFs, report.thdDb,
                         report.harmonicsDb[0], report.harmonicsDb[1], report.audibleAliasingDb);

            if (csv != nullptr)
                std::fprintf (csv, "%.0f,%d,%.1f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n",
                              sampleRate, engine.getOversamplingFactor(), driveDb, parameters.character,
                              report.fundamentalDbFs, report.thdDb,
                              report.harmonicsDb[0], report.harmonicsDb[1],
                              report.audibleAliasingDb, engine.getLatencySamples());
        }
        std::printf ("\n");
    }

    if (csv != nullptr)
    {
        std::fclose (csv);
        std::printf ("wrote %s\n", args.outPath.c_str());
    }
    return 0;
}

/// The wavefolder is a bass tool, and the reason is measurable: a folder's
/// harmonics extend to roughly (fold gain) x the fundamental, so on a 40 Hz sub
/// they all fit comfortably below Nyquist and on a 2 kHz lead they do not.
/// This prints where that boundary actually falls.
int runFold (const Args& args)
{
    using namespace tezla::measure;
    using namespace tezla::emberdrive;

    constexpr std::size_t fftSize = 32768;
    const double sampleRate = args.sampleRate;
    const double ranges[] = { 1.0, 10.0, 100.0 };
    const double frequencies[] = { 40.0, 80.0, 160.0, 330.0, 660.0, 1320.0, 2640.0 };

    std::printf ("Emberdrive fold -- audible-band aliasing (dB rel. fundamental) at %.0f Hz\n", sampleRate);
    std::printf ("Fold at maximum, drive 0 dB, Auto oversampling.\n\n");
    std::printf ("  %-10s %14s %14s %14s\n", "input", "Range x1", "Range x10", "Range x100");

    for (const double inputHz : frequencies)
    {
        std::printf ("  %7.0f Hz", inputHz);

        for (const double range : ranges)
        {
            Parameters parameters;
            parameters.driveDb    = 0.0;
            parameters.foldAmount = 1.0;
            parameters.foldRange  = range;
            parameters.ceilingDb  = 24.0;    // limiter idle, so it is the fold being measured
            parameters.kneeDb     = 0.0;
            parameters.autoTrim   = true;

            Engine engine;
            engine.prepare (sampleRate, 271, 1);
            engine.setParameters (parameters);
            engine.reset();

            const double frequency = binExactFrequency (inputHz, sampleRate, fftSize);
            auto signal = sine (frequency, 0.5, sampleRate, 2 * fftSize);

            for (std::size_t offset = 0; offset < signal.size(); offset += 271)
            {
                const int numSamples = static_cast<int> (std::min<std::size_t> (271, signal.size() - offset));
                double* pointer = signal.data() + offset;
                engine.process (&pointer, 1, numSamples);
            }

            const std::vector<double> steadyState (signal.end() - static_cast<std::ptrdiff_t> (fftSize),
                                                   signal.end());
            std::printf (" %14.1f", analyseHarmonics (steadyState, sampleRate, frequency).audibleAliasingDb);
        }
        std::printf ("\n");
    }

    std::printf ("\nBelow about -60 dB the folding is clean; above it the aliasing is part of\n");
    std::printf ("the sound, which on a sub or a reese is usually what you want anyway.\n");
    return 0;
}

void printUsage()
{
    std::printf ("tezla-measure (tezla-dsp %s)\n\n", tezla::dsp::kVersionString);
    std::printf ("  selftest                          verify the analysis chain itself\n");
    std::printf ("  filter-response [--fs --freq --q --out FILE]\n");
    std::printf ("  clip-aliasing   [--fs --freq --drive]\n");
    std::printf ("  emberdrive      [--freq --gain CHARACTER --out FILE]\n");
    std::printf ("  fold            [--fs]  wavefolder aliasing vs input frequency\n");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];
    const Args args = Args::parse (argc, argv);

    if (command == "selftest")        return runSelfTest();
    if (command == "filter-response") return runFilterResponse (args);
    if (command == "clip-aliasing")   return runClipAliasing (args);
    if (command == "emberdrive")      return runEmberdrive (args);
    if (command == "fold")            return runFold (args);

    printUsage();
    return 1;
}
