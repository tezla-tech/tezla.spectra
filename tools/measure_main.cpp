// tezla-measure -- offline analysis, so DSP claims can be checked with numbers
// before anything is loaded into a DAW.
//
//   tezla-measure selftest
//   tezla-measure filter-response [--fs 48000] [--freq 1000] [--q 0.707] [--out response.csv]
//   tezla-measure clip-aliasing   [--fs 48000] [--freq 1000] [--drive 4]
//   tezla-measure imd             [--fs 48000] [--freq 3000] [--drive 0.8]
//   tezla-measure naive-exciter   [--fs 48000] [--freq 5000]
//   tezla-measure halo            [--fs 48000]
//
// New commands get added here as plugins need them. Anything that measures a
// nonlinearity belongs in this tool, not in a DAW screenshot.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Version.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include "EmberdriveEngine.hpp"
#include "HaloEngine.hpp"

namespace {

struct Args
{
    double sampleRate   { 48000.0 };
    double frequency    { 1000.0 };
    double q            { 0.707 };
    double gainDb       { 0.0 };
    double drive        { 4.0 };
    std::string outPath;

    /// Whether --freq was actually given. Without this every command shares one
    /// default, and the naive-exciter baseline quietly measured a 1 kHz tone
    /// through a 3 kHz highpass -- which is silence, and duly reported -247 dB
    /// of aliasing for a structure that actually manages -28.
    bool frequencyGiven { false };

    [[nodiscard]] double frequencyOr (double fallback) const
    {
        return frequencyGiven ? frequency : fallback;
    }

    static Args parse (int argc, char** argv)
    {
        Args args;
        for (int i = 2; i + 1 < argc; i += 2)
        {
            const std::string key = argv[i];
            const std::string value = argv[i + 1];

            if      (key == "--fs")    args.sampleRate = std::atof (value.c_str());
            else if (key == "--freq")  { args.frequency = std::atof (value.c_str()); args.frequencyGiven = true; }
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

/// Lead-in long enough for Halo's band envelope to settle, in seconds rather
/// than in samples.
///
/// A two-pole 30 ms average needs about a second. A fixed 32768-sample lead-in
/// is 0.68 s at 48 kHz but only 0.17 s at 192 kHz, so with a sample count the
/// envelope was still drifting inside the measurement window at high rates --
/// which modulated the wet path and reported itself as 60 dB of aliasing that
/// was not there.
constexpr double kHaloSettleSeconds = 1.5;

std::vector<double> settlingSine (double frequency, double amplitude,
                                  double sampleRate, std::size_t windowLength)
{
    const auto preroll = static_cast<std::size_t> (sampleRate * kHaloSettleSeconds);
    return tezla::measure::sine (frequency, amplitude, sampleRate, preroll + windowLength);
}

/// Runs a mono signal through a Halo engine, block by block.
std::vector<double> runHalo (const tezla::halo::Parameters& parameters,
                             const std::vector<double>& input, double sampleRate)
{
    tezla::halo::Engine engine;
    engine.prepare (sampleRate, 271, 1);
    engine.setParameters (parameters);
    engine.reset();

    std::vector<double> output = input;

    for (std::size_t offset = 0; offset < output.size(); offset += 271)
    {
        const int numSamples = static_cast<int> (std::min<std::size_t> (271, output.size() - offset));
        double* pointer = output.data() + offset;
        engine.process (&pointer, 1, numSamples);
    }

    return output;
}

/// Two-tone intermodulation.
///
/// Harmonic distortion is the number everyone quotes, and for a harmonic
/// exciter it is the wrong one on its own: harmonics land on musically related
/// frequencies and mostly sound like tone, while intermodulation products land
/// on unrelated ones and sound like dirt. The virtual-bass literature grades
/// these devices on IMD for exactly that reason.
int runImd (const Args& args)
{
    using namespace tezla::measure;
    using namespace tezla::halo;

    constexpr std::size_t fftSize = 32768;
    const double sampleRate = args.sampleRate;

    // Two closely spaced tones, so the difference product lands well below both
    // and cannot be confused with a harmonic of either.
    const double centre = args.frequencyOr (3000.0);
    const double lower = binExactFrequency (centre, sampleRate, fftSize);
    const double upper = binExactFrequency (centre * 1.1, sampleRate, fftSize);
    const double difference = upper - lower;

    std::printf ("Halo intermodulation -- %.1f Hz + %.1f Hz at %.0f Hz, each at -6 dBFS\n",
                 lower, upper, sampleRate);
    std::printf ("Difference product at %.1f Hz, reported relative to one input tone.\n\n", difference);
    std::printf ("  %-8s %14s %14s %14s\n", "drive", "colour 0", "colour 0.5", "colour 1");

    for (const double drive : { 0.25, 0.5, 0.75, 1.0 })
    {
        std::printf ("  %6.2f  ", drive);

        for (const double colour : { 0.0, 0.5, 1.0 })
        {
            Parameters parameters;
            parameters.drive    = drive;
            parameters.colour   = colour;
            parameters.focusHz  = centre * 0.6;
            parameters.autoTrim = false;

            std::vector<double> input (static_cast<std::size_t> (sampleRate * kHaloSettleSeconds)
                                       + fftSize);
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                const double t = static_cast<double> (i) / sampleRate;
                input[i] = 0.5 * std::sin (2.0 * 3.14159265358979323846 * lower * t)
                         + 0.5 * std::sin (2.0 * 3.14159265358979323846 * upper * t);
            }

            const auto output = runHalo (parameters, input, sampleRate);
            const std::vector<double> settled (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                               output.end());

            const auto spectrum = fftOfReal (settled);
            const double binWidth = sampleRate / static_cast<double> (fftSize);

            const auto level = [&] (double hz)
            {
                const auto bin = static_cast<std::size_t> (std::llround (hz / binWidth));
                return 2.0 * std::abs (spectrum[bin]) / static_cast<double> (fftSize);
            };

            const double reference = level (lower);
            const double product   = level (difference);

            std::printf (" %14.1f", 20.0 * std::log10 (std::max (product / reference, 1.0e-30)));
        }
        std::printf ("\n");
    }

    std::printf ("\nThe difference tone is what a listener hears as roughness rather than as\n");
    std::printf ("brightness, so it matters more than THD on this kind of processor.\n");
    return 0;
}

/// The baseline every Halo figure is quoted against.
///
/// A conventional exciter is a highpass, a distortion, and a blend, all at the
/// host's own sample rate. That structure is built here in a few lines so the
/// comparison is honest -- our own code, measured the same way, rather than a
/// number claimed about somebody else's plugin.
int runNaiveExciter (const Args& args)
{
    using namespace tezla::dsp;
    using namespace tezla::measure;

    constexpr std::size_t fftSize = 32768;
    const double sampleRate = args.sampleRate;

    // The same 5 kHz tone `halo` uses. If the two commands measured different
    // tones the comparison between them would not be one.
    const double frequency = binExactFrequency (args.frequencyOr (5000.0), sampleRate, fftSize);

    std::printf ("Naive exciter baseline -- highpass, tanh, blend, all at %.0f Hz.\n", sampleRate);
    std::printf ("No oversampling and no antialiasing, which is the structure to beat.\n");
    std::printf ("Test tone %.1f Hz at -0.9 dBFS, the same one `halo` uses.\n\n", frequency);
    std::printf ("  %-10s %18s %18s\n", "focus", "audible alias dB", "full-band dB");

    for (const double focusHz : { 1000.0, 3000.0, 6000.0 })
    {
        Biquad<double> highA, highB;
        const auto coefficients = design::highpass (focusHz, 0.70710678118654752, sampleRate);
        highA.setCoefficients (coefficients);
        highB.setCoefficients (coefficients);

        auto signal = sine (frequency, 0.9, sampleRate, 2 * fftSize);

        for (double& sample : signal)
        {
            const double band = highB.process (highA.process (sample));
            sample += std::tanh (8.0 * band) * 0.25;
        }

        const std::vector<double> settled (signal.end() - static_cast<std::ptrdiff_t> (fftSize),
                                           signal.end());
        const auto report = analyseHarmonics (settled, sampleRate, frequency);

        std::printf ("  %7.0f Hz %18.1f %18.1f\n", focusHz, report.audibleAliasingDb, report.aliasingDb);
    }

    std::printf ("\nCompare against `tezla-measure halo`, which is the same measurement on the\n");
    std::printf ("real engine with ADAA inside an oversampled block.\n");
    return 0;
}

int runHalo (const Args& args)
{
    using namespace tezla::measure;
    using namespace tezla::halo;

    constexpr std::size_t fftSize = 32768;
    const double sampleRate = args.sampleRate;
    const double frequency = binExactFrequency (5000.0, sampleRate, fftSize);

    std::printf ("Halo -- audible-band aliasing (dB rel. fundamental) at %.0f Hz\n", sampleRate);
    std::printf ("5 kHz tone at -0.9 dBFS, Focus 3 kHz, Ceiling on at 16 kHz, Auto oversampling.\n\n");
    std::printf ("  %-8s %14s %14s %14s\n", "drive", "colour 0", "colour 0.5", "colour 1");

    for (const double drive : { 0.25, 0.5, 0.75, 1.0 })
    {
        std::printf ("  %6.2f  ", drive);

        for (const double colour : { 0.0, 0.5, 1.0 })
        {
            Parameters parameters;
            parameters.drive    = drive;
            parameters.colour   = colour;
            parameters.focusHz  = 3000.0;
            parameters.autoTrim = false;

            const auto output = runHalo (parameters, settlingSine (frequency, 0.9, sampleRate, fftSize),
                                         sampleRate);
            const std::vector<double> settled (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                               output.end());

            std::printf (" %14.1f", analyseHarmonics (settled, sampleRate, frequency).audibleAliasingDb);
        }
        std::printf ("\n");
    }

    std::printf ("\nHarmonic content of the wet path alone, Listen on, Drive 0.7, Focus 2 kHz,\n");
    std::printf ("Ceiling off, in dB relative to the input tone:\n\n");
    std::printf ("  %-10s %10s %10s %10s %10s\n", "colour", "H1", "H2", "H3", "H4");

    for (const double colour : { 0.0, 0.5, 1.0 })
    {
        Parameters parameters;
        parameters.listen    = true;
        parameters.drive     = 0.7;
        parameters.colour    = colour;
        parameters.focusHz   = 2000.0;
        parameters.ceilingOn = false;
        parameters.autoTrim  = false;

        const double tone = binExactFrequency (4000.0, sampleRate, fftSize);
        const auto output = runHalo (parameters, settlingSine (tone, 0.8, sampleRate, fftSize), sampleRate);
        const std::vector<double> settled (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                           output.end());

        const auto spectrum = fftOfReal (settled);
        const double binWidth = sampleRate / static_cast<double> (fftSize);

        std::printf ("  %8.2f  ", colour);

        for (int harmonic = 1; harmonic <= 4; ++harmonic)
        {
            const auto bin = static_cast<std::size_t> (std::llround (tone * harmonic / binWidth));
            const double amplitude = 2.0 * std::abs (spectrum[bin]) / static_cast<double> (fftSize);
            std::printf (" %10.1f", 20.0 * std::log10 (std::max (amplitude / 0.8, 1.0e-30)));
        }
        std::printf ("\n");
    }

    std::printf ("\nH1 is the fundamental leaking into the wet path, which a conventional exciter\n");
    std::printf ("mixes back at close to full level. Here it is what is left after the even half\n");
    std::printf ("cannot produce one at all and the odd half subtracts its own.\n");
    return 0;
}

/// Chebyshev precision mode, end to end.
///
/// Three questions, and the answer to the third is the interesting one.
/// Does asking for a harmonic give you that harmonic? Does the fundamental stay
/// out of the wet path? And what does the crazy end of Index actually cost --
/// which is a number worth having before shipping a control that invites people
/// to go there.
int runChebyshev (const Args& args)
{
    using namespace tezla::measure;
    using namespace tezla::halo;

    constexpr std::size_t fftSize = 32768;
    const double sampleRate = args.sampleRate;

    // Absolute levels, not levels relative to the fundamental. analyseHarmonics
    // scales everything to the fundamental, and here the fundamental is the
    // thing being claimed absent -- dividing by it turns every figure into noise
    // about noise.
    const auto levels = [] (const std::vector<double>& signal, double rate, double fundamentalHz)
    {
        const auto spectrum = fftOfReal (signal);
        const double binWidth = rate / static_cast<double> (signal.size());
        const auto bin = static_cast<std::size_t> (std::llround (fundamentalHz / binWidth));

        std::vector<double> db (9, -400.0);

        for (int n = 0; n <= 8; ++n)
        {
            const std::size_t centre = bin * static_cast<std::size_t> (n);
            double power = 0.0;

            for (std::size_t k = (centre > 0 ? centre - 1 : 0);
                 k <= centre + 1 && k < signal.size() / 2; ++k)
                power += std::norm (spectrum[k]);

            const double amplitude = 2.0 * std::sqrt (power) / static_cast<double> (signal.size());
            db[static_cast<std::size_t> (n)] = tezla::dsp::gainToDb (amplitude, -400.0);
        }

        return db;
    };

    /// A Chebyshev run on the low band, harmonics soloed.
    const auto measure = [&] (const std::array<double, 7>& gains, double index, double toneHz)
    {
        Parameters parameters;
        parameters.generator = Generator::Chebyshev;
        parameters.harmonics = gains;
        parameters.chebIndex = index;
        parameters.bandMode  = BandMode::Below;
        parameters.focusHz   = toneHz * 2.2;
        parameters.ceilingOn = false;
        parameters.listen    = true;
        parameters.autoTrim  = false;

        const double frequency = binExactFrequency (toneHz, sampleRate, fftSize);
        const auto output = runHalo (parameters,
                                     settlingSine (frequency, 0.5, sampleRate, fftSize),
                                     sampleRate);
        const std::vector<double> settled (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                           output.end());
        return levels (settled, sampleRate, frequency);
    };

    std::printf ("Halo -- Chebyshev precision mode at %.0f Hz\n", sampleRate);
    std::printf ("Le Brun, Digital Waveshaping Synthesis, JAES 27(4), 1979.\n\n");

    const double tone = args.frequencyOr (400.0);

    std::printf ("One harmonic requested at a time, %.0f Hz tone, harmonics soloed.\n", tone);
    std::printf ("Absolute dBFS -- DC and the fundamental are what must not be there.\n\n");
    std::printf ("  %-9s %8s %8s", "asked for", "DC", "H1");
    for (int n = 2; n <= 8; ++n)
        std::printf (" %7s%d", "H", n);
    std::printf ("\n");

    for (int harmonic = 2; harmonic <= 8; ++harmonic)
    {
        std::array<double, 7> gains {};
        gains[static_cast<std::size_t> (harmonic - 2)] = 1.0;

        const auto db = measure (gains, 1.0, tone);

        std::printf ("  H%-8d", harmonic);
        for (int n = 0; n <= 8; ++n)
            std::printf (" %8.1f", db[static_cast<std::size_t> (n)]);
        std::printf ("\n");
    }

    std::printf ("\nIndex, with every harmonic up. 1.0 is the exact point; above it the input\n");
    std::printf ("clamps and this stops being harmonic synthesis. Fundamental and DC are\n");
    std::printf ("relative to the loudest harmonic.\n\n");
    std::printf ("  %-8s %14s %14s\n", "index", "fundamental", "DC");

    for (const double index : { 0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 })
    {
        const std::array<double, 7> gains { 0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7 };
        const auto db = measure (gains, index, tone);

        double loudest = -400.0;
        for (int n = 2; n <= 8; ++n)
            loudest = std::max (loudest, db[static_cast<std::size_t> (n)]);

        // Index 0 is exactly the zero function, so there is no harmonic to be
        // relative to. Printing 0.0 dB there is a division by nothing dressed up
        // as a measurement.
        if (loudest < -300.0)
        {
            std::printf ("  %6.2f   %14s %14s\n", index, "silent", "silent");
            continue;
        }

        std::printf ("  %6.2f   %14.1f %14.1f\n", index,
                     db[1] - loudest, db[0] - loudest);
    }

    std::printf ("\nSweep debris: a 1 k -> 18 k sweep, worst inharmonic below 900 Hz, dBFS.\n");
    std::printf ("A sweep rather than a tone, because fold-back from a harmonically related\n");
    std::printf ("signal lands on that signal's own harmonics and hides there.\n\n");
    std::printf ("  %-14s %16s\n", "oversampling", "worst below 900 Hz");

    for (const auto mode : { tezla::dsp::OversamplingMode::Auto, tezla::dsp::OversamplingMode::Off })
    {
        Parameters parameters;
        parameters.generator    = Generator::Chebyshev;
        parameters.harmonics    = { 0.8, 0.8, 0.8, 0.8, 0.8, 0.8, 0.8 };
        parameters.oversampling = mode;
        parameters.focusHz      = 1000.0;
        parameters.ceilingOn    = false;
        parameters.listen       = true;
        parameters.autoTrim     = false;

        const auto preroll = static_cast<std::size_t> (sampleRate * kHaloSettleSeconds);
        const auto sweep = linearSweep (1000.0, 18000.0, 0.7, sampleRate, preroll + fftSize);
        const auto output = runHalo (parameters, sweep, sampleRate);
        const std::vector<double> settled (output.end() - static_cast<std::ptrdiff_t> (fftSize),
                                           output.end());

        const auto spectrum = fftOfReal (settled);
        const double binWidth = sampleRate / static_cast<double> (fftSize);

        double worst = 0.0;
        for (std::size_t k = static_cast<std::size_t> (20.0 / binWidth);
             k < static_cast<std::size_t> (900.0 / binWidth); ++k)
            worst = std::max (worst, 2.0 * std::abs (spectrum[k]) / static_cast<double> (fftSize));

        std::printf ("  %-14s %16.1f\n",
                     mode == tezla::dsp::OversamplingMode::Auto ? "Auto" : "Off",
                     tezla::dsp::gainToDb (worst, -400.0));
    }

    std::printf ("\nThe band limit is what makes these figures: content above\n");
    std::printf ("internalNyquist / n is removed before the polynomial, so nothing folds.\n");
    std::printf ("With oversampling off the internal Nyquist here is %.0f kHz, so eight\n",
                 sampleRate * 0.0005);
    std::printf ("harmonics may only be asked of content below %.1f kHz -- the limit is doing\n",
                 sampleRate * 0.0005 / 8.0);
    std::printf ("more of the work than the oversampler is.\n");
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
    std::printf ("  halo            [--fs]  exciter aliasing and harmonic content\n");
    std::printf ("  imd             [--fs --freq]  two-tone intermodulation (3 kHz default)\n");
    std::printf ("  naive-exciter   [--fs --freq]  host-rate baseline to beat (5 kHz default)\n");
    std::printf ("  chebyshev       [--fs --freq]  precision mode: selection, fundamental, index\n");
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
    if (command == "imd")             return runImd (args);
    if (command == "naive-exciter")   return runNaiveExciter (args);
    if (command == "halo")            return runHalo (args);
    if (command == "chebyshev")       return runChebyshev (args);

    printUsage();
    return 1;
}
