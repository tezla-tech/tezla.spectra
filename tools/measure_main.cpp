// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// tezla-measure -- offline analysis, so DSP claims can be checked with numbers
// before anything is loaded into a DAW.
//
//   tezla-measure selftest
//   tezla-measure filter-response [--fs 48000] [--freq 1000] [--q 0.707] [--out response.csv]
//   tezla-measure clip-aliasing   [--fs 48000] [--freq 1000] [--drive 4]
//   tezla-measure imd             [--fs 48000] [--freq 3000] [--drive 0.8]
//   tezla-measure naive-exciter   [--fs 48000] [--freq 5000]
//   tezla-measure halo            [--fs 48000]
//   tezla-measure capstone        [--fs 48000]
//   tezla-measure loudness        [--fs 48000]
//
// New commands get added here as plugins need them. Anything that measures a
// nonlinearity belongs in this tool, not in a DAW screenshot.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>
#include <vector>

#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Scales.hpp>
#include <tezla/dsp/TruePeakDetector.hpp>
#include <tezla/dsp/Version.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include "AnvilEngine.hpp"
#include "CapstoneEngine.hpp"
#include "EmberdriveEngine.hpp"
#include "HaloEngine.hpp"
#include "SonitusEngine.hpp"
#include "TranspectusEngine.hpp"

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

/// fopen for writing, without MSVC's C4996.
///
/// The warning text suggests _CRT_SECURE_NO_WARNINGS, which switches the whole
/// deprecation category off across the translation unit -- and that category is
/// not all noise. One #ifdef in one place is cheaper than losing the rest of it.
std::FILE* openForWriting (const std::string& path)
{
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return fopen_s (&file, path.c_str(), "w") == 0 ? file : nullptr;
#else
    return std::fopen (path.c_str(), "w");
#endif
}

int runFilterResponse (const Args& args)
{
    const auto coefficients = tezla::dsp::design::lowpass (args.frequency, args.q, args.sampleRate);

    std::FILE* out = args.outPath.empty() ? stdout : openForWriting (args.outPath);
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
        csv = openForWriting (args.outPath);
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


// ---------------------------------------------------------------------------
// Capstone: the four numbers the limiter's claims rest on.
//
//   1. true peak at the output, per detector setting
//   2. clipper aliasing, per oversampling factor
//   3. distortion against look-ahead -- what the attack control actually costs
//   4. CPU, on one core, so "20 of these in a project" is a measured claim
// ---------------------------------------------------------------------------

/// Renders a signal through a configured engine in host-sized blocks.
std::vector<std::vector<double>> renderCapstone (tezla::capstone::Engine& engine,
                                                 std::vector<std::vector<double>> input,
                                                 int blockSize)
{
    const int channels = static_cast<int> (input.size());
    const int total = static_cast<int> (input[0].size());

    for (int offset = 0; offset < total; )
    {
        const int span = std::min (blockSize, total - offset);
        double* pointers[2] { nullptr, nullptr };

        for (int c = 0; c < channels; ++c)
            pointers[c] = input[static_cast<std::size_t> (c)].data() + offset;

        engine.process (pointers, channels, span);
        offset += span;
    }

    return input;
}

double capstoneTruePeak (const std::vector<std::vector<double>>& x, int from)
{
    double peak = 0.0;

    for (const auto& channel : x)
    {
        tezla::dsp::TruePeakDetector detector;
        detector.prepare (tezla::dsp::TruePeakDetector::kMaxFactor);
        detector.setFactor (16);
        detector.reset();

        for (std::size_t i = 0; i < channel.size(); ++i)
        {
            const double reading = detector.process (channel[i]);

            if (static_cast<int> (i) >= from)
                peak = std::max (peak, reading);
        }
    }

    return peak;
}

int runCapstone (const Args& args)
{
    using namespace tezla;

    const double sampleRate = args.sampleRate;
    constexpr double ceilingDb = -1.0;

    std::printf ("Capstone -- true-peak brickwall limiter and clipper, at %.0f Hz\n\n", sampleRate);

    // ---- 1. true peak at the output -----------------------------------------

    std::printf ("True peak at the output, ceiling %.1f dBFS\n", ceilingDb);
    std::printf ("  Content dense near Nyquist, which is where a sample meter is worst.\n\n");
    std::printf ("  %-10s %-7s %14s %14s\n", "detector", "ratio", "sample peak", "true peak");

    const char* detectorNames[] = { "Off", "Standard", "Strict" };
    int detectorIndex = 0;

    for (const auto mode : { dsp::TruePeakMode::Off, dsp::TruePeakMode::Standard,
                             dsp::TruePeakMode::Strict })
    {
        capstone::Engine engine;
        engine.prepare (sampleRate, 512, 2);

        capstone::Parameters parameters;
        parameters.ceilingDb = ceilingDb;
        parameters.thresholdDb = -12.0;
        parameters.clipOn = false;
        parameters.limitOn = true;
        parameters.attackMs = 1.0;
        parameters.truePeak = mode;
        engine.setParameters (parameters);
        engine.reset();

        std::vector<std::vector<double>> x (2, std::vector<double> (20000));

        for (int i = 0; i < 20000; ++i)
        {
            const double v = 0.90 * ((i / 3) % 2 != 0 ? 1.0 : -1.0);
            x[0][static_cast<std::size_t> (i)] = v;
            x[1][static_cast<std::size_t> (i)] = v;
        }

        const auto y = renderCapstone (engine, x, 128);

        double samplePeak = 0.0;

        for (const auto& channel : y)
            for (std::size_t i = 2000; i < channel.size(); ++i)
                samplePeak = std::max (samplePeak, std::abs (channel[i]));

        std::printf ("  %-10s x%-6d %11.3f dB %11.3f dB\n",
                     detectorNames[detectorIndex++],
                     dsp::truePeakFactorFor (mode, sampleRate),
                     dsp::gainToDb (samplePeak, -200.0) - ceilingDb,
                     dsp::gainToDb (capstoneTruePeak (y, 2000), -200.0) - ceilingDb);
    }

    std::printf ("\n  Both columns relative to the ceiling. Read them together: Off holds the\n");
    std::printf ("  samples exactly on the ceiling and reconstructs 1.5 dB above it, while\n");
    std::printf ("  Strict deliberately holds the samples 1.5 dB *below* the ceiling so that\n");
    std::printf ("  what the converter actually produces lands on it. That is the trade --\n");
    std::printf ("  a true-peak limiter is quieter on the meter and correct in the air.\n\n");

    // ---- 2. clipper aliasing --------------------------------------------------

    constexpr std::size_t fftSize = 1 << 14;
    const double frequency = measure::binExactFrequency (1000.0, sampleRate, fftSize);

    std::printf ("Clipper aliasing, hard corner, driven 12 dB into it\n\n");
    std::printf ("  %-12s %20s %20s\n", "oversampling", "audible aliasing", "THD");

    const std::pair<const char*, dsp::OversamplingMode> modes[] {
        { "Off", dsp::OversamplingMode::Off },
        { "x2",  dsp::OversamplingMode::X2 },
        { "x4",  dsp::OversamplingMode::X4 },
        { "x8",  dsp::OversamplingMode::X8 }
    };

    for (const auto& [name, mode] : modes)
    {
        capstone::Engine engine;
        engine.prepare (sampleRate, 1024, 2);

        capstone::Parameters parameters;
        parameters.clipOn = true;
        parameters.clipThresholdDb = -12.0;
        parameters.clipShape = 0.0;
        parameters.clipOversampling = mode;
        parameters.limitOn = false;
        engine.setParameters (parameters);
        engine.reset();

        std::vector<std::vector<double>> x (2, std::vector<double> (2 * fftSize));

        for (std::size_t i = 0; i < 2 * fftSize; ++i)
        {
            const double v = 0.9 * std::sin (2.0 * 3.14159265358979323846 * frequency
                                             * static_cast<double> (i) / sampleRate);
            x[0][i] = v;
            x[1][i] = v;
        }

        const auto y = renderCapstone (engine, x, 512);

        // The second half only: the first carries the oversampler's fill, and
        // the DFT treats its block as circular.
        std::vector<double> settled (y[0].begin() + static_cast<long> (fftSize), y[0].end());
        const auto report = measure::analyseHarmonics (settled, sampleRate, frequency);

        std::printf ("  %-12s %17.1f dB %17.1f dB\n", name, report.audibleAliasingDb, report.thdDb);
    }

    std::printf ("\n  The house target is nothing inharmonic above -60 dB in the audible band.\n");
    std::printf ("  Auto picks x%d at this rate.\n\n", dsp::autoOversamplingFactor (sampleRate));

    // ---- 3. distortion against look-ahead -------------------------------------

    std::printf ("What look-ahead buys, on a 60 Hz tone limited 12 dB\n\n");
    std::printf ("  %-12s %14s %16s %14s\n", "attack", "latency", "THD", "gain reduction");

    for (const double attackMs : { 0.0, 0.05, 0.2, 1.0, 5.0, 20.0 })
    {
        capstone::Engine engine;
        engine.prepare (sampleRate, 1024, 2);

        capstone::Parameters parameters;
        parameters.ceilingDb = -0.3;
        parameters.thresholdDb = -12.0;
        parameters.clipOn = false;
        parameters.limitOn = true;
        parameters.lookaheadOn = attackMs > 0.0;
        parameters.attackMs = attackMs;
        parameters.holdMs = 0.0;
        parameters.releaseMs = 50.0;
        parameters.truePeak = dsp::TruePeakMode::Off;
        engine.setParameters (parameters);
        engine.reset();

        // A low tone is the hard case: the gain has to move slowly enough not
        // to distort a cycle that lasts 800 samples.
        const double low = measure::binExactFrequency (60.0, sampleRate, fftSize);

        std::vector<std::vector<double>> x (2, std::vector<double> (2 * fftSize));

        for (std::size_t i = 0; i < 2 * fftSize; ++i)
        {
            const double v = 0.9 * std::sin (2.0 * 3.14159265358979323846 * low
                                             * static_cast<double> (i) / sampleRate);
            x[0][i] = v;
            x[1][i] = v;
        }

        const auto y = renderCapstone (engine, x, 512);

        std::vector<double> settled (y[0].begin() + static_cast<long> (fftSize), y[0].end());
        const auto report = measure::analyseHarmonics (settled, sampleRate, low);

        std::printf ("  %8.2f ms %11d sm %13.1f dB %11.2f dB\n",
                     attackMs, engine.getLatencySamples(), report.thdDb,
                     engine.getLimiterReductionDb());
    }

    std::printf ("\n  Zero look-ahead cannot bring the gain down before the peak, so it cuts\n");
    std::printf ("  the waveform instead. That is what the Clip stage is for -- it does the\n");
    std::printf ("  same job deliberately, band-limited, and with a shape control.\n\n");

    // ---- 4. CPU ---------------------------------------------------------------

    std::printf ("CPU, one core, 60 seconds of stereo audio in 128-sample blocks\n\n");
    std::printf ("  %-34s %12s %12s\n", "setting", "seconds", "x realtime");

    const auto timeIt = [&] (const char* name, const capstone::Parameters& parameters)
    {
        capstone::Engine engine;
        engine.prepare (sampleRate, 128, 2);
        engine.setParameters (parameters);
        engine.reset();

        const int blocks = static_cast<int> (60.0 * sampleRate / 128.0);

        std::vector<double> left (128), right (128);

        for (int i = 0; i < 128; ++i)
        {
            left[static_cast<std::size_t> (i)] = 0.7 * std::sin (0.05 * i);
            right[static_cast<std::size_t> (i)] = 0.7 * std::cos (0.07 * i);
        }

        const auto start = std::chrono::steady_clock::now();

        for (int b = 0; b < blocks; ++b)
        {
            double* pointers[2] { left.data(), right.data() };
            engine.process (pointers, 2, 128);
        }

        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

        std::printf ("  %-34s %10.3f s %11.0fx\n", name, elapsed.count(), 60.0 / elapsed.count());
    };

    {
        capstone::Parameters parameters;
        parameters.clipOn = false;
        parameters.limitOn = true;
        parameters.truePeak = dsp::TruePeakMode::Off;
        timeIt ("limit only, sample peak", parameters);

        parameters.truePeak = dsp::TruePeakMode::Standard;
        timeIt ("limit only, Standard", parameters);

        parameters.truePeak = dsp::TruePeakMode::Strict;
        timeIt ("limit only, Strict", parameters);

        parameters.truePeak = dsp::TruePeakMode::Standard;
        parameters.clipOn = true;
        parameters.clipOversampling = dsp::OversamplingMode::Auto;
        timeIt ("clip Auto + limit Standard", parameters);

        parameters.clipOversampling = dsp::OversamplingMode::X8;
        parameters.truePeak = dsp::TruePeakMode::Strict;
        timeIt ("clip x8 + limit Strict (worst)", parameters);
    }

    std::printf ("\n  Twenty instances need 20x realtime or better with headroom to spare.\n");
    return 0;
}

// ---------------------------------------------------------------- loudness --
//
// The Transpectus engine measured through its own public readings. Every number
// here is one the panel shows, produced by the same code path, so a claim in
// the README can be checked rather than believed.

/// Feeds an engine a stereo signal in host-sized blocks, the way a DAW would.
void runThrough (tezla::transpectus::Engine& engine,
                 const std::vector<std::vector<double>>& x,
                 int blockSize)
{
    const auto total = x[0].size();
    std::vector<const double*> pointers (x.size());

    for (std::size_t start = 0; start < total; start += static_cast<std::size_t> (blockSize))
    {
        const auto n = std::min (static_cast<std::size_t> (blockSize), total - start);

        for (std::size_t channel = 0; channel < x.size(); ++channel)
            pointers[channel] = x[channel].data() + start;

        engine.process (pointers.data(), static_cast<int> (x.size()), static_cast<int> (n));
    }
}

/// A stereo sine at a stated dBFS, `seconds` long.
std::vector<std::vector<double>> stereoSine (double frequency, double dbfs,
                                             double sampleRate, double seconds)
{
    const auto n = static_cast<std::size_t> (sampleRate * seconds);
    const double amplitude = tezla::dsp::dbToGain (dbfs);

    std::vector<std::vector<double>> x (2, std::vector<double> (n));

    for (std::size_t i = 0; i < n; ++i)
    {
        const double v = amplitude * std::sin (2.0 * 3.14159265358979323846 * frequency
                                               * static_cast<double> (i) / sampleRate);
        x[0][i] = v;
        x[1][i] = v;
    }

    return x;
}

int runLoudness (const Args& args)
{
    using namespace tezla;

    // ---- 1. the 48 kHz coefficient trap ---------------------------------------

    std::printf ("A -23 dBFS 1 kHz sine, read at four host rates\n\n");
    std::printf ("  %-10s %14s %14s %14s\n", "rate", "integrated", "short term", "momentary");

    double worstRateError = 0.0;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        transpectus::Engine engine;
        engine.prepare (rate, 1024, 2);

        const auto x = stereoSine (1000.0, -23.0, rate, 10.0);
        runThrough (engine, x, 512);

        std::printf ("  %-10.0f %14.3f %14.3f %14.3f\n",
                     rate, engine.getIntegratedLufs(), engine.getShortTermLufs(),
                     engine.getMomentaryLufs());

        worstRateError = std::max (worstRateError, std::abs (engine.getIntegratedLufs() + 23.0));
    }

    std::printf ("\n  worst deviation from -23.000: %.4f LU  (EBU Tech 3341 case 1 allows 0.1)\n",
                 worstRateError);
    std::printf ("  BS.1770-5 tabulates its coefficients at 48 kHz only. These four agree\n");
    std::printf ("  because the filter is designed from the analogue prototype at the actual\n");
    std::printf ("  rate, not read off the table -- see CLAUDE.md section 6.\n\n");

    // ---- 2. gating -------------------------------------------------------------

    const double sampleRate = args.sampleRate;

    std::printf ("Gating: what the silence around a take does to the integrated reading\n\n");

    {
        transpectus::Engine engine;
        engine.prepare (sampleRate, 1024, 2);

        // Ten seconds of programme, then ten of digital black.
        auto x = stereoSine (1000.0, -23.0, sampleRate, 10.0);
        const auto silence = static_cast<std::size_t> (sampleRate * 10.0);

        for (auto& channel : x)
            channel.resize (channel.size() + silence, 0.0);

        runThrough (engine, x, 512);

        std::printf ("  10 s at -23 dBFS + 10 s of silence : %8.3f LUFS  (ungated would read -26.0)\n",
                     engine.getIntegratedLufs());
    }

    {
        // The relative gate excludes the quieter half only once the two are far
        // enough apart, and the boundary is not the 10 LU the control is named
        // after: the gate sits 10 LU below a mean the quiet half has already
        // dragged down. Both sides of it, measured.
        for (const double separationDb : { 10.0, 15.0 })
        {
            transpectus::Engine engine;
            engine.prepare (sampleRate, 1024, 2);

            auto loud  = stereoSine (1000.0, -23.0, sampleRate, 10.0);
            const auto quiet = stereoSine (1000.0, -23.0 - separationDb, sampleRate, 10.0);

            for (std::size_t channel = 0; channel < loud.size(); ++channel)
                loud[channel].insert (loud[channel].end(),
                                      quiet[channel].begin(), quiet[channel].end());

            runThrough (engine, loud, 512);

            std::printf ("  half at -23, half %.0f dB below      : %8.3f LUFS  (%s)\n",
                         separationDb, engine.getIntegratedLufs(),
                         separationDb > 12.79 ? "quiet half gated out"
                                              : "both halves counted");
        }
    }

    std::printf ("\n  The boundary is 12.79 dB, not 10: z1/z2 > 19 is where the quiet half\n");
    std::printf ("  stops reaching a gate that its own presence lowered.\n\n");

    // ---- 3. PLR, and what limiting costs it ------------------------------------

    // A stereo sine is useless here: its 3.01 dB of crest factor is cancelled
    // exactly by the 3.01 dB of summing two correlated channels, so it reads
    // PLR 0.00 at every level and says nothing. The question is what *limiting*
    // costs, so the signal has to have transients and the limiter has to be
    // real -- which it is, three folders over.

    std::printf ("PLR -- true peak minus integrated loudness, on a drum pattern through Capstone\n\n");
    std::printf ("  %-22s %10s %10s %10s %10s\n",
                 "limiting", "dBTP", "LUFS-I", "PLR", "vs clean");

    {
        const auto n = static_cast<std::size_t> (sampleRate * 8.0);
        std::vector<std::vector<double>> dry (2, std::vector<double> (n));

        // Kick every 500 ms, snare offset by 250, hats on the eighths: a decaying
        // sine per hit, which is the shape a drum bus actually presents to a
        // limiter -- a high crest factor over a low mean.
        const double twoPi = 2.0 * 3.14159265358979323846;

        auto strike = [&] (double atSeconds, double frequency, double decay, double amplitude)
        {
            const auto start = static_cast<std::size_t> (atSeconds * sampleRate);

            for (std::size_t i = start; i < n; ++i)
            {
                const double t = static_cast<double> (i - start) / sampleRate;
                const double envelope = std::exp (-t / decay);

                if (envelope < 1.0e-4)
                    break;

                const double v = amplitude * envelope * std::sin (twoPi * frequency * t);
                dry[0][i] += v;
                dry[1][i] += v;
            }
        };

        for (double t = 0.0; t < 8.0; t += 0.5)
        {
            strike (t,         55.0, 0.120, 0.85);   // kick
            strike (t + 0.25, 190.0, 0.090, 0.55);   // snare body
            strike (t + 0.25, 3200.0, 0.045, 0.30);  // snare crack

            for (double eighth = 0.0; eighth < 0.5; eighth += 0.125)
                strike (t + eighth, 8000.0, 0.020, 0.18);
        }

        double clean = 0.0;

        for (const double reductionDb : { 0.0, 3.0, 6.0, 12.0 })
        {
            capstone::Engine limiter;
            limiter.prepare (sampleRate, 1024, 2);

            capstone::Parameters parameters;
            parameters.clipOn = false;
            parameters.limitOn = reductionDb > 0.0;
            parameters.ceilingDb = -1.0;
            parameters.thresholdDb = -reductionDb;
            parameters.attackMs = 1.0;
            parameters.releaseMs = 100.0;
            limiter.setParameters (parameters);

            const auto wet = renderCapstone (limiter, dry, 512);

            transpectus::Engine engine;
            engine.prepare (sampleRate, 1024, 2);
            runThrough (engine, wet, 512);

            const double plr = engine.getPlr();

            if (reductionDb == 0.0)
                clean = plr;

            char label[48];
            std::snprintf (label, sizeof (label), "%s", reductionDb > 0.0 ? "limited" : "clean");
            char full[64];
            std::snprintf (full, sizeof (full), "%s %.0f dB", label, reductionDb);

            std::printf ("  %-22s %10.2f %10.2f %10.2f %10.2f\n",
                         reductionDb > 0.0 ? full : "clean",
                         engine.getTruePeakDb(), engine.getIntegratedLufs(), plr,
                         plr - clean);
        }
    }

    std::printf ("\n  The last column is the whole point: it is how much transient the limiter\n");
    std::printf ("  removed to buy that loudness. Below about 5 dB of PLR the transients are\n");
    std::printf ("  gone, and pushing further buys level that every platform then undoes.\n\n");

    // ---- 4. what each platform actually does -----------------------------------

    std::printf ("What each platform does to a -8 LUFS master, and to a -20 LUFS one\n\n");
    std::printf ("  %-14s %8s %16s %16s\n", "target", "LUFS-I", "-8 LUFS master", "-20 LUFS master");

    for (int index = 0; index < transpectus::kNumLoudnessTargets; ++index)
    {
        double deltas[2] {};

        int slot = 0;

        for (const double programmeDb : { -8.0, -20.0 })
        {
            transpectus::Engine engine;
            engine.prepare (sampleRate, 1024, 2);

            transpectus::Parameters parameters;
            parameters.targetIndex = index;
            engine.setParameters (parameters);

            // A sine whose integrated loudness lands on the number we want.
            // -23 dBFS reads -23 LUFS, so the offset is the whole adjustment.
            const auto x = stereoSine (1000.0, programmeDb, sampleRate, 10.0);
            runThrough (engine, x, 512);

            deltas[slot++] = engine.getTargetDeltaDb();
        }

        const auto& target = transpectus::kLoudnessTargets[index];

        // + 0.0 so an exact zero prints as "+0.0" rather than "-0.0", which
        // reads like a rounded-away turn-down and is the opposite of the point.
        char loud[32], quiet[32];
        std::snprintf (loud,  sizeof (loud),  "%+.1f dB", -deltas[0] + 0.0);
        std::snprintf (quiet, sizeof (quiet), "%+.1f dB", -deltas[1] + 0.0);

        std::printf ("  %-14s %8.1f %16s %16s\n", target.name, target.lufs, loud, quiet);
    }

    std::printf ("\n  Signed as the gain the platform applies. The zeroes are not rounding:\n");
    std::printf ("  YouTube, Tidal and Amazon Music only ever turn a master down, so a quiet\n");
    std::printf ("  one simply plays quiet. That is advice there and a correction elsewhere.\n\n");

    // ---- 5. per-band correlation -----------------------------------------------

    std::printf ("Correlation: why the full-band reading is not enough\n\n");
    std::printf ("  %-30s %10s %10s %10s %10s\n", "signal", "full", "low", "mid", "high");

    {
        transpectus::Engine engine;
        engine.prepare (sampleRate, 1024, 2);

        const auto n = static_cast<std::size_t> (sampleRate * 2.0);
        std::vector<std::vector<double>> x (2, std::vector<double> (n));

        // A wide-ish top over a sub that is exactly out of phase -- the failure
        // that survives headphones and cancels on a club system.
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            const double sub  = 0.10 * std::sin (2.0 * 3.14159265358979323846 * 50.0 * t);
            const double top  = 0.30 * std::sin (2.0 * 3.14159265358979323846 * 900.0 * t);
            const double air  = 0.20 * std::sin (2.0 * 3.14159265358979323846 * 6000.0 * t);

            x[0][i] =  sub + top + air;
            x[1][i] = -sub + top + air;
        }

        runThrough (engine, x, 512);

        std::printf ("  %-30s %10.4f %10.4f %10.4f %10.4f\n", "sub inverted, everything else not",
                     engine.getCorrelation(),
                     engine.getBandCorrelation (dsp::StereoAnalyser::low),
                     engine.getBandCorrelation (dsp::StereoAnalyser::mid),
                     engine.getBandCorrelation (dsp::StereoAnalyser::high));

        std::printf ("  low band mono safe: %s\n", engine.isLowBandMonoSafe() ? "yes" : "NO");
    }

    std::printf ("\n  The full-band number is dominated by whatever carries the energy, and\n");
    std::printf ("  reads healthy while the sub cancels completely. Only the band reading\n");
    std::printf ("  says so, which is why the panel gives the sub its own bar.\n\n");

    // ---- 6. what it costs ------------------------------------------------------

    std::printf ("CPU -- one core, 60 s of stereo audio in 128-sample blocks\n\n");

    for (const auto mode : { dsp::TruePeakMode::Off,
                             dsp::TruePeakMode::Standard,
                             dsp::TruePeakMode::Strict })
    {
        transpectus::Engine engine;
        engine.prepare (sampleRate, 128, 2);

        transpectus::Parameters parameters;
        parameters.truePeak = mode;
        engine.setParameters (parameters);

        const auto x = stereoSine (100.0, -12.0, sampleRate, 60.0);

        const auto started = std::chrono::steady_clock::now();
        runThrough (engine, x, 128);
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();

        const char* name = mode == dsp::TruePeakMode::Off      ? "true peak off"
                         : mode == dsp::TruePeakMode::Standard ? "true peak Standard"
                                                               : "true peak Strict";

        std::printf ("  %-22s %8.3f s   %7.0fx realtime\n", name, elapsed, 60.0 / elapsed);
    }

    std::printf ("\n  An analyser runs on every channel you are watching, so this is the\n");
    std::printf ("  number that decides whether you can leave it open.\n");

    return 0;
}


// ---------------------------------------------------------------------------
// anvil
// ---------------------------------------------------------------------------

namespace anvilMeasure
{
using namespace tezla;

/// Renders a sine through a configured amplifier and returns the last window.
std::vector<double> renderAnvil (const anvil::Parameters& parameters, double rate,
                                 double amplitude, double frequency, std::size_t window)
{
    anvil::Engine engine;
    engine.prepare (rate, 128, 2);
    engine.setParameters (parameters);
    engine.reset();

    std::vector<double> left (128), right (128), out;
    out.reserve (3 * window);

    double* channels[2] { left.data(), right.data() };
    std::size_t i = 0;

    while (out.size() < 3 * window)
    {
        for (int k = 0; k < 128; ++k)
            left[static_cast<std::size_t> (k)] = right[static_cast<std::size_t> (k)]
                = amplitude * std::sin (2.0 * std::numbers::pi * frequency
                                        * static_cast<double> (i + static_cast<std::size_t> (k)) / rate);

        engine.process (channels, 2, 128);

        for (int k = 0; k < 128; ++k)
            out.push_back (left[static_cast<std::size_t> (k)]);

        i += 128;
    }

    return { out.begin() + static_cast<long> (2 * window), out.begin() + static_cast<long> (3 * window) };
}

/// The loudest inharmonic component in the audible band, in absolute dBFS.
struct Alias { double hz; double dbFs; };

Alias worstAlias (const std::vector<double>& signal, double rate, double fundamentalHz)
{
    const std::size_t n = signal.size();
    const auto spectrum = measure::fftOfReal (signal);

    const double binWidth = rate / static_cast<double> (n);
    const auto fundamental = static_cast<std::size_t> (std::llround (fundamentalHz / binWidth));

    Alias worst { 0.0, -300.0 };

    for (std::size_t k = 2; k + 1 < n / 2; ++k)
    {
        const double hz = static_cast<double> (k) * binWidth;

        if (hz < 20.0 || hz > 18000.0)
            continue;

        const long harmonic = std::lround (static_cast<double> (k) / static_cast<double> (fundamental));

        if (harmonic >= 1
            && std::llabs (static_cast<long> (k) - harmonic * static_cast<long> (fundamental)) <= 1)
            continue;

        const double amplitude = 2.0 * std::abs (spectrum[k]) / static_cast<double> (n);
        const double db = 20.0 * std::log10 (std::max (amplitude, 1.0e-15));

        if (db > worst.dbFs)
            worst = { hz, db };
    }

    return worst;
}
} // namespace anvilMeasure

int runAnvil (const Args& args)
{
    using namespace tezla;
    using namespace anvilMeasure;

    const double rate = args.sampleRate;
    constexpr std::size_t window = 1 << 15;

    static const char* laneNames[3] { "clean", "vintage", "modern" };

    // ---- 1. aliasing, and the probe frequency that makes it visible -----------

    // The worst case is always the highest probe, and a single low one cannot
    // see it -- so this is swept rather than sampled. CLAUDE.md section 7.
    const double probes[] { 82.0, 330.0, 1000.0, 4400.0 };

    std::printf ("Aliasing at maximum gain with five valves, absolute dBFS, british 4x12\n");
    std::printf ("Worst of a sweep from 82 Hz to 4.4 kHz, and the sweep is the point.\n\n");

    std::printf ("  %-9s %10s %10s %10s %10s\n", "lane", "off", "x2", "x4", "x8");

    for (int lane = 0; lane < 3; ++lane)
    {
        std::printf ("  %-9s", laneNames[lane]);

        for (const auto mode : { dsp::OversamplingMode::Off, dsp::OversamplingMode::X2,
                                 dsp::OversamplingMode::X4, dsp::OversamplingMode::X8 })
        {
            double worst = -300.0;

            for (const double frequency : probes)
            {
                anvil::Parameters p;
                p.voicing = static_cast<anvil::Voicing> (lane);
                p.cabinet = anvil::CabinetChoice::british;
                p.gainDb = 48.0;
                p.masterDb = 0.0;
                p.extraStages = 2;
                p.oversampling = mode;

                const double bin = measure::binExactFrequency (frequency, rate, window);
                worst = std::max (worst,
                    worstAlias (renderAnvil (p, rate, 0.5, bin, window), rate, bin).dbFs);
            }

            std::printf ("  %9.1f ", worst);
        }

        std::printf ("\n");
    }

    std::printf ("\n  CLAUDE.md section 7 asks for nothing above -60 dBFS at maximum drive.\n");
    std::printf ("  Anvil's Auto picks x%d at %.0f Hz for exactly that reason -- a cascade\n",
                 anvil::Engine::autoFactorFor (rate), rate);
    std::printf ("  compounds, and the house table's 192 kHz target is not enough here.\n\n");

    // ---- 2. the instrument, checked before it is trusted -----------------------

    // A probe that is bin-exact *and* divides the host rate cannot see aliasing
    // at all: every alias of it lands on one of its own harmonics. 1500 Hz at
    // 48 kHz is both. CLAUDE.md section 10.
    {
        const double blind = 1500.0;
        const double honest = measure::binExactFrequency (4400.0, rate, window);

        anvil::Parameters p;
        p.voicing = anvil::Voicing::modern;
        p.cabinet = anvil::CabinetChoice::british;
        p.gainDb = 48.0;
        p.masterDb = 0.0;
        p.extraStages = 2;
        p.oversampling = dsp::OversamplingMode::X4;

        const auto seen = worstAlias (renderAnvil (p, rate, 0.5, honest, window), rate, honest);
        const auto hidden = worstAlias (renderAnvil (p, rate, 0.5, blind, window), rate, blind);

        std::printf ("  Checking the instrument. The same amplifier, the same setting, two\n");
        std::printf ("  probes -- both bin-exact, so neither leaks. Only one divides %.0f:\n\n",
                     rate);
        std::printf ("    %8.2f Hz  ->  %7.1f dBFS at %8.1f Hz\n", honest, seen.dbFs, seen.hz);
        std::printf ("    %8.2f Hz  ->  %7.1f dBFS at %8.1f Hz   <- blind: %.0f/%.0f = %.0f\n\n",
                     blind, hidden.dbFs, hidden.hz, rate, blind, rate / blind);
    }

    // ---- 3. the lanes are different amplifiers --------------------------------

    std::printf ("THD at 220 Hz, -12 dBFS in, cabinet off, master at its default\n\n");
    std::printf ("  %-9s %8s %8s %8s %8s %8s\n", "lane", "-6 dB", "0 dB", "+6 dB", "+12 dB", "+24 dB");

    for (int lane = 0; lane < 3; ++lane)
    {
        std::printf ("  %-9s", laneNames[lane]);

        for (const double gain : { -6.0, 0.0, 6.0, 12.0, 24.0 })
        {
            anvil::Parameters p;
            p.voicing = static_cast<anvil::Voicing> (lane);
            p.cabinet = anvil::CabinetChoice::none;
            p.gainDb = gain;

            const double bin = measure::binExactFrequency (220.0, rate, window);
            const auto report = measure::analyseHarmonics (
                renderAnvil (p, rate, 0.25, bin, window), rate, bin);

            std::printf (" %8.1f", report.thdDb);
        }

        std::printf ("\n");
    }

    std::printf ("\n  The clean lane is not a quieter version of the dirty one. CLAUDE.md\n");
    std::printf ("  priority 2 asks for a setting that genuinely gets out of the way.\n\n");

    // ---- 4. the mechanism the whole plugin is built on ------------------------

    std::printf ("The transformer: the same voltage, an octave apart\n\n");
    std::printf ("  %-10s %10s %10s\n", "frequency", "flux", "thd");

    for (const double frequency : { 41.0, 82.0, 164.0, 328.0, 656.0, 1312.0 })
    {
        anvil::Parameters p;
        p.voicing = anvil::Voicing::vintage;
        p.cabinet = anvil::CabinetChoice::none;
        p.gainDb = 0.0;
        p.masterDb = -3.0;
        p.coreHz = 180.0;              // a small transformer, so the effect is plain
        p.damping = 20.0;

        anvil::Engine engine;
        engine.prepare (rate, 128, 2);
        engine.setParameters (p);
        engine.reset();

        std::vector<double> left (128), right (128);
        double* channels[2] { left.data(), right.data() };
        double flux = 0.0;

        for (int i = 0; i < static_cast<int> (rate * 0.5); i += 128)
        {
            for (int k = 0; k < 128; ++k)
                left[static_cast<std::size_t> (k)] = right[static_cast<std::size_t> (k)]
                    = 0.4 * std::sin (2.0 * std::numbers::pi * frequency * (i + k) / rate);

            engine.process (channels, 2, 128);

            if (i > static_cast<int> (rate * 0.25))
                flux = std::max (flux, engine.getFlux());
        }

        const double bin = measure::binExactFrequency (frequency, rate, window);
        const auto report = measure::analyseHarmonics (
            renderAnvil (p, rate, 0.4, bin, window), rate, bin);

        std::printf ("  %8.0f Hz %10.3f %10.1f\n", frequency, flux, report.thdDb);
    }

    std::printf ("\n  Flux is the integral of voltage, so it falls 6 dB per octave and the\n");
    std::printf ("  distortion it makes falls about 18. Nothing in the code tests the\n");
    std::printf ("  frequency -- integrate the voltage and this falls out.\n\n");

    // ---- 5. what it costs ------------------------------------------------------

    // ---- the feedback loop, and what presence and resonance can reach --------
    //
    // A user reported both controls as doing nothing, and they were right. Both
    // work by shunting the feedback away at one end of the spectrum, so neither
    // can lift that end by more than the loop was holding it down -- the
    // negative feedback *is* the control's authority. The lanes shipped with
    // loop gains of 0.60, 0.15 and 0.32, which is 4.1, 1.2 and 2.4 dB, and the
    // controls measured 3.7, 1.0 and 2.0: correct, and useless.
    //
    // Measured small-signal, so the output stage is not limiting. A pinned
    // stage stays pinned however the loop is shaped, which is why the figure
    // collapses at high drive -- what moves there is the harmonic structure,
    // not the level.

    std::printf ("The feedback loop: what presence and resonance can actually reach\n\n");

    std::printf ("  %-9s %10s %12s %14s %14s\n",
                 "lane", "loop gain", "NFB", "presence 6 kHz", "resonance 40 Hz");

    for (int lane = 0; lane < 3; ++lane)
    {
        anvil::Parameters base;

        base.voicing = static_cast<anvil::Voicing> (lane);
        base.cabinet = anvil::CabinetChoice::none;
        base.gainDb = 0.0;

        anvil::Engine probe;
        probe.setParameters (base);
        probe.prepare (rate, 128, 2);

        const double loop = probe.getLoopGain();

        auto level = [&] (double presence, double resonance, double hz)
        {
            auto p = base;
            p.presence = presence;
            p.resonance = resonance;

            const auto rendered = renderAnvil (p, rate, std::pow (10.0, -48.0 / 20.0), hz, window);

            double sum = 0.0;
            for (const double s : rendered)
                sum += s * s;

            return 20.0 * std::log10 (std::max (std::sqrt (sum / static_cast<double> (rendered.size())),
                                                1.0e-30));
        };

        const double presenceLift  = level (1.0, 0.0, 6000.0) - level (0.0, 0.0, 6000.0);
        const double resonanceLift = level (0.0, 1.0, 40.0)   - level (0.0, 0.0, 40.0);

        std::printf ("  %-9s %10.2f %10.1f dB %11.1f dB %13.1f dB\n",
                     laneNames[lane], loop, 20.0 * std::log10 (1.0 + loop),
                     presenceLift, resonanceLift);
    }

    std::printf ("\n  The lift a shunt can produce is the negative feedback beside it, because\n");
    std::printf ("  that is what the shunt is removing. These lanes shipped at 0.60, 0.15\n");
    std::printf ("  and 0.32 of loop gain -- 4.1, 1.2 and 2.4 dB -- and the controls\n");
    std::printf ("  measured 3.7, 1.0 and 2.0: correct, and useless.\n\n");
    std::printf ("  5.6 dB is the ceiling and not a choice. The loop carries a one-sample\n");
    std::printf ("  delay, so its pole sits at minus the loop gain and it is stable only\n");
    std::printf ("  below 1. PowerAmp::kMaximumLoopGain holds it at 0.9. Going deeper means\n");
    std::printf ("  solving the loop implicitly through two stateful ADAA shapers, which is\n");
    std::printf ("  a project rather than a patch.\n\n");

    std::printf ("CPU, one stereo instance, milliseconds per second of audio at %.0f Hz\n\n", rate);

    for (const auto mode : { dsp::OversamplingMode::Off, dsp::OversamplingMode::X2,
                             dsp::OversamplingMode::X4, dsp::OversamplingMode::X8 })
    {
        anvil::Parameters p;
        p.voicing = anvil::Voicing::modern;
        p.gainDb = 30.0;
        p.oversampling = mode;

        anvil::Engine engine;
        engine.prepare (rate, 512, 2);
        engine.setParameters (p);
        engine.reset();

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        for (int k = 0; k < 512; ++k)
            left[static_cast<std::size_t> (k)] = right[static_cast<std::size_t> (k)]
                = 0.3 * std::sin (k * 0.05);

        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < static_cast<int> (rate); i += 512)
            engine.process (channels, 2, 512);

        const auto elapsed = std::chrono::steady_clock::now() - started;
        const double ms = std::chrono::duration<double, std::milli> (elapsed).count();

        std::printf ("  x%d: %7.1f ms/s  = %5.1f%% of one core   latency %3d samples\n",
                     engine.getOversamplingFactor(), ms, ms / 10.0, engine.getLatencySamples());
    }

    std::printf ("\n  Fidelity above CPU, CLAUDE.md section 1. Auto takes x%d.\n",
                 anvil::Engine::autoFactorFor (rate));

    return 0;
}

// ---------------------------------------------------------------------------
// sonitus
// ---------------------------------------------------------------------------

namespace sonitusMeasure
{
using namespace tezla;

/// The nonlinear chain with one oscillator, which is the only patch an aliasing
/// figure can honestly be taken from.
///
/// A reese is dense and inharmonic *by design* -- five detuned oscillators, a
/// synced partner and a ring modulator -- and any harmonic analysis counts all
/// of that as aliasing. Pointed at a real patch the number reads 0 dB and means
/// nothing. So the probe is one saw at a bin-exact fundamental, with the folder,
/// the filter's rail and the tube all the way up: the three stages that can
/// actually fold energy back over Nyquist.
sonitus::EngineParameters harmonicPatch()
{
    sonitus::EngineParameters parameters;

    parameters.voice.shapeA = dsp::OscShape::saw;
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
    parameters.keyboard = sonitus::KeyboardMode::mono;
    parameters.tubeDriveDb = 24.0;
    parameters.splitHz = 20.0;
    parameters.subMono = false;

    return parameters;
}

std::vector<double> playNote (const sonitus::EngineParameters& parameters, double rate,
                              double frequency, std::size_t window)
{
    // Parameters before prepare, so the graph is built at the factor asked for
    // rather than rebuilt on the first block -- which cuts the note.
    sonitus::Engine engine;
    engine.setParameters (parameters);
    engine.prepare (rate, 512);
    engine.tuning().setReference (60, frequency);
    engine.noteOn (60, 1.0);

    // **A second of pre-roll, counted in time rather than samples.** The tube's
    // bias shift and supply sag have 33 and 45 ms time constants, and 8192
    // samples is 171 ms at 48 kHz against 43 ms at 192 -- a settling time
    // counted in samples is a different settling time at every rate. The short
    // one read -52.99 dB where the settled figure is -69.89.
    const auto preRoll = static_cast<std::size_t> (rate);

    std::vector<double> left (512), right (512), out;
    out.reserve (window + preRoll);

    double* channels[2] { left.data(), right.data() };

    while (out.size() < window + preRoll)
    {
        engine.process (channels, 512);

        for (int i = 0; i < 512; ++i)
            out.push_back (left[static_cast<std::size_t> (i)]);
    }

    return { out.end() - static_cast<long> (window), out.end() };
}
} // namespace sonitusMeasure

int runSonitus (const Args& args)
{
    using namespace tezla;
    using namespace sonitusMeasure;

    const double rate = args.sampleRate;
    constexpr std::size_t window = 1 << 15;

    // ---- 1. aliasing across the range a bass actually plays -------------------

    std::printf ("Aliasing, one saw at full fold, filter drive 0.7 and 24 dB of tube.\n");
    std::printf ("Absolute dBFS of inharmonic energy in the audible band, at %.0f Hz.\n\n",
                 rate);

    std::printf ("  %-8s %9s %9s %9s %9s\n", "note", "off", "x2", "x4", "x8");

    const double notes[] { 41.2, 55.0, 82.4, 110.0, 164.8, 220.0, 440.0 };

    for (const double note : notes)
    {
        std::printf ("  %5.1f Hz", note);

        for (const auto mode : { dsp::OversamplingMode::Off, dsp::OversamplingMode::X2,
                                 dsp::OversamplingMode::X4, dsp::OversamplingMode::X8 })
        {
            auto parameters = harmonicPatch();
            parameters.oversampling = mode;

            const double hz = measure::binExactFrequency (note, rate, window);

            std::printf ("  %8.2f", measure::analyseHarmonics (
                playNote (parameters, rate, hz, window), rate, hz).audibleAliasingDb);
        }

        std::printf ("\n");
    }

    std::printf ("\n  CLAUDE.md section 7 asks for nothing above -60 dBFS at maximum drive,\n");
    std::printf ("  and Auto picks x%d here. That clears it from E1 to A3 -- which is what a\n",
                 dsp::autoOversamplingFactor (rate));
    std::printf ("  bass instrument plays -- and does not clear it at 440 Hz, where the\n");
    std::printf ("  control offers x8. The limit is stated rather than hidden.\n\n");

    // ---- 2. the comb, which is what the instrument is for ---------------------

    std::printf ("The comb's first notch, in Hz. The delay is 1/(2 x notch), so the row is\n");
    std::printf ("a check that the control means what the tooltip says it means.\n\n");

    std::printf ("  %-10s %12s %12s %12s\n", "time", "predicted", "measured", "inverted");

    for (const double milliseconds : { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 })
    {
        auto parameters = harmonicPatch();
        parameters.combMode = sonitus::CombMode::flange;
        parameters.combTimeMs = milliseconds;
        parameters.combMix = 1.0;

        sonitus::Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        engine.process (channels, 512);

        const double straight = engine.getCombNotchHz();

        parameters.combInverted = true;
        engine.setParameters (parameters);
        engine.process (channels, 512);

        std::printf ("  %6.2f ms %12.1f %12.1f %12.1f\n", milliseconds,
                     500.0 / milliseconds, straight, engine.getCombNotchHz());
    }

    // Key tracking is the claim that the growl comes out *tuned*: with the
    // amount at 1 the delay is the note's own period, so the notches land on
    // its harmonics whatever note is played.
    std::printf ("\n  Key tracking at 100%%: the delay becomes the note's period, so the\n");
    std::printf ("  first notch sits at half the played frequency whatever is played.\n\n");

    std::printf ("  %-10s %14s %14s\n", "note", "note / 2", "measured");

    for (const int note : { 28, 40, 52, 64 })
    {
        auto parameters = harmonicPatch();
        parameters.combMode = sonitus::CombMode::flange;
        parameters.combTimeMs = 3.0;
        parameters.combKeyTrack = 1.0;
        parameters.combMix = 1.0;

        sonitus::Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);
        engine.noteOn (note, 1.0);

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        engine.process (channels, 512);

        const double played = 440.0 * std::pow (2.0, (note - 69) / 12.0);

        std::printf ("  MIDI %-5d %14.2f %14.2f\n", note, played * 0.5,
                     engine.getCombNotchHz());
    }

    // ---- 3. what it costs -----------------------------------------------------

    std::printf ("\nCPU, one second of audio rendered in 512-sample blocks, after two\n");
    std::printf ("seconds of pre-roll -- the idle skip needs a second of silence before\n");
    std::printf ("it engages, and timing it while it engages measures the settling.\n\n");

    // Where the time actually goes, which is not where the plan assumed. The
    // voices dominate and the mangle is nearly free, so the unison table below
    // is the *shallow* axis and the polyphony is the steep one.
    {
        std::printf ("  Idle -- nothing playing, the skip engaged:\n");

        auto quiet = harmonicPatch();
        quiet.tubeDriveDb = 0.0;

        sonitus::Engine engine;
        engine.setParameters (quiet);
        engine.prepare (rate, 512);

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        for (int i = 0; i < 2 * static_cast<int> (rate); i += 512)
            engine.process (channels, 512);

        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < static_cast<int> (rate); i += 512)
            engine.process (channels, 512);

        const auto elapsed = std::chrono::steady_clock::now() - started;
        const double ms = std::chrono::duration<double, std::milli> (elapsed).count();

        std::printf ("    %7.1f ms/s = %.2f%% of one core. Without the skip this was 17.9\n",
                     ms, ms / 10.0);
        std::printf ("    ms/s -- the mangle and the decimation FIRs running on silence.\n\n");
    }

    std::printf ("  Eight voices held, saw plus saw, everything in the mangle running:\n\n");

    std::printf ("  %-22s %10s %8s %10s\n", "unison (A + B)", "ms/s", "core", "latency");

    for (const int unison : { 1, 3, 5, 7 })
    {
        auto parameters = harmonicPatch();

        parameters.keyboard = sonitus::KeyboardMode::poly;
        parameters.polyphony = 8;
        parameters.voice.levelB = 1.0;
        parameters.voice.unisonA = unison;
        parameters.voice.unisonB = unison;
        parameters.voice.detuneA = 18.0;
        parameters.voice.detuneB = 22.0;
        parameters.combMode = sonitus::CombMode::flange;
        parameters.combMix = 0.8;
        parameters.combFeedback = 0.7;
        parameters.formantMix = 0.6;

        sonitus::Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);

        for (int note = 0; note < 8; ++note)
            engine.noteOn (36 + 3 * note, 0.9);

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        // A block first, so the measurement is not timing the graph being built
        // and the voices being allocated.
        engine.process (channels, 512);

        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < static_cast<int> (rate); i += 512)
            engine.process (channels, 512);

        const auto elapsed = std::chrono::steady_clock::now() - started;
        const double ms = std::chrono::duration<double, std::milli> (elapsed).count();

        std::printf ("  %2d each = %3d oscs %10.1f %7.1f%% %8d sm\n",
                     unison, 16 * unison, ms, ms / 10.0, engine.getLatencySamples());
    }

    std::printf ("\n  **The voices dominate, not the oscillators**, and that is the opposite\n");
    std::printf ("  of what the plan assumed. Seven times the oscillators costs about a\n");
    std::printf ("  third more; an eighth of the *voices* costs an eighth. The filter, the\n");
    std::printf ("  envelopes and the folder's antialiasing are per voice and the unison\n");
    std::printf ("  bank is not. So mono is the setting that saves CPU -- and a reese is one\n");
    std::printf ("  voice anyway. Turning unison down is the wrong lever.\n\n");

    // How much does the *ceiling* cost, as opposed to the notes? The manager
    // iterates every slot every sample, so raising kMaxVoices to 32 adds 24
    // loop iterations per sample whether or not anybody is playing them. The
    // sweep below holds a rising number of notes with the ceiling fixed at 32,
    // so the difference between two rows is the marginal cost of a *sounding*
    // voice and the first row plus the idle figure above is what the ceiling
    // itself costs.
    std::printf ("  Held notes, ceiling fixed at %d slots, unison 1, mangle running:\n\n",
                 sonitus::VoiceManager::kMaxVoices);

    std::printf ("  %-14s %10s %8s %14s\n", "notes held", "ms/s", "core", "per voice");

    double previousMs = 0.0;

    for (const int held : { 1, 2, 4, 8, 16, 32 })
    {
        auto parameters = harmonicPatch();

        parameters.keyboard = sonitus::KeyboardMode::poly;
        parameters.polyphony = sonitus::VoiceManager::kMaxVoices;
        parameters.voice.levelB = 1.0;
        parameters.voice.unisonA = 1;
        parameters.voice.unisonB = 1;
        parameters.combMode = sonitus::CombMode::flange;
        parameters.combMix = 0.8;
        parameters.combFeedback = 0.7;
        parameters.formantMix = 0.6;

        sonitus::Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);

        // Spread over three octaves so no two voices share a frequency and the
        // filter and envelope of each is doing genuinely different work.
        for (int note = 0; note < held; ++note)
            engine.noteOn (28 + note, 0.9);

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        engine.process (channels, 512);

        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < static_cast<int> (rate); i += 512)
            engine.process (channels, 512);

        const auto elapsed = std::chrono::steady_clock::now() - started;
        const double ms = std::chrono::duration<double, std::milli> (elapsed).count();

        std::printf ("  %2d %-11s %10.1f %7.1f%% %12.1f ms\n", held,
                     held == 1 ? "note" : "notes", ms, ms / 10.0, ms / held);

        if (held == 1)
            previousMs = ms;
    }

    std::printf ("\n  **The ceiling is free; the notes are not.** Thirty-two idle slots and\n");
    std::printf ("  the whole mangle cost 0.5 ms/s -- five hundredths of one core -- because\n");
    std::printf ("  Voice::process returns on its first line when the amp envelope is idle\n");
    std::printf ("  and applyControls skips inactive voices entirely. One held note costs\n");
    std::printf ("  %.0f ms/s, which is %.0f times the whole idle engine. So the Voices\n",
                 previousMs, previousMs / 0.5);
    std::printf ("  control decides the bill and kMaxVoices only decides whether the player\n");
    std::printf ("  is allowed to run one up. Sixteen is the default because it is the most\n");
    std::printf ("  a sane arrangement holds at once; the ceiling is there for pads whose\n");
    std::printf ("  releases overlap, where most of the sounding voices are tails.\n\n");

    // ---- the phase-3 shapes ---------------------------------------------------

    std::printf ("The phase-3 oscillator shapes: worst inharmonic component, oscillator\n");
    std::printf ("alone at 48 kHz, bin-exact probes, morph at its default and its top.\n\n");

    std::printf ("  %-11s %10s %10s     note\n", "shape", "morph 0", "morph 1");

    {
        struct ShapeRow { dsp::OscShape shape; const char* name; const char* note; };

        const ShapeRow rows[] {
            { dsp::OscShape::vintage,   "vintage",    "the saw's class: the reset step dominates" },
            { dsp::OscShape::dome,      "dome",       "band-limited by construction" },
            { dsp::OscShape::doubleSaw, "double saw", "two BLEP ramps" },
            { dsp::OscShape::harmonic,  "harmonic",   "finite series, Nyquist-faded" },
        };

        constexpr std::size_t kWindow = 1 << 14;

        for (const auto& row : rows)
        {
            std::printf ("  %-11s", row.name);

            for (const double morph : { 0.0, 1.0 })
            {
                const double probe = measure::binExactFrequency (987.0, 48000.0, kWindow);

                dsp::Oscillator osc;
                osc.setShape (row.shape);
                osc.setMorph (morph);
                osc.setIncrement (probe / 48000.0);
                osc.reset();

                std::vector<double> rendered (kWindow);
                for (auto& sample : rendered)
                    sample = osc.advance();

                // The double saw at full offset *is* the octave-up saw -- its
                // odd harmonics, fundamental included, cancel exactly -- so an
                // analyser referenced to the played fundamental divides by a
                // vanished tone and reports nonsense. Reference that one cell
                // to the octave, which is the waveform's real fundamental; the
                // identity itself is nulled to 1e-9 in the tests.
                const bool octaveCase = row.shape == dsp::OscShape::doubleSaw && morph == 1.0;

                std::printf (" %8.1f dB",
                             measure::analyseHarmonics (rendered, 48000.0,
                                                        octaveCase ? 2.0 * probe : probe)
                                 .audibleAliasingDb);
            }

            std::printf ("     %s\n", row.note);
        }

        std::printf ("\n  These are naked-oscillator figures; the voice oversamples on top.\n");
        std::printf ("  Dome and harmonic are the by-construction pair -- the tests hold them\n");
        std::printf ("  under -120 and -100 rather than the house -60. Noise is exempt: no\n");
        std::printf ("  pitch, nothing to alias against.\n\n");
    }

    // ---- ADV envelope cost ----------------------------------------------------

    std::printf ("ADV envelopes: eight held notes, all three enabled and looping,\n");
    std::printf ("against the same patch with all three off.\n\n");

    for (const bool enabled : { false, true })
    {
        auto parameters = harmonicPatch();

        parameters.keyboard = sonitus::KeyboardMode::poly;
        parameters.polyphony = 8;

        for (auto& adv : parameters.voice.adv)
        {
            adv.enable = enabled;
            adv.loop = true;
            adv.points = 4;
            adv.sustain = 2;
        }

        sonitus::Engine engine;
        engine.setParameters (parameters);
        engine.prepare (rate, 512);

        for (int note = 0; note < 8; ++note)
            engine.noteOn (36 + 3 * note, 0.9);

        std::vector<double> left (512), right (512);
        double* channels[2] { left.data(), right.data() };

        engine.process (channels, 512);

        const auto started = std::chrono::steady_clock::now();

        for (int i = 0; i < static_cast<int> (rate); i += 512)
            engine.process (channels, 512);

        const auto elapsed = std::chrono::steady_clock::now() - started;
        const double ms = std::chrono::duration<double, std::milli> (elapsed).count();

        std::printf ("  ADV x3 %s %8.1f ms/s = %.1f%% of one core\n",
                     enabled ? "on: " : "off:", ms, ms / 10.0);
    }

    std::printf ("\n  Three breakpoint envelopes per sounding voice cost a rounding error;\n");
    std::printf ("  a disabled slot costs exactly nothing, byte-proven at the wiring commit.\n\n");

    // ---- 4. tuning ------------------------------------------------------------

    std::printf ("Tuning: what the built-in scales actually produce, against theory.\n\n");

    {
        dsp::Tuning tuning;

        tuning.setScale (dsp::Tuning::twelveToneEqual());

        double worst = 0.0;

        for (int note = 0; note < 128; ++note)
        {
            const double theory = 440.0 * std::pow (2.0, (note - 69) / 12.0);

            worst = std::max (worst, std::abs (tuning.frequencyFor (note) - theory));
        }

        std::printf ("  12-TET against 440 * 2^((n-69)/12), worst of 128 notes: %.3e Hz\n", worst);

        tuning.setScale (dsp::scales::pythagorean());

        // The Pythagorean fifth is a pure 3/2 and is 701.955 cents, not the
        // 700 that twelve-tone equal rounds it to. That two-cent difference is
        // the whole reason the scale exists.
        const double fifth = 1200.0 * std::log2 (tuning.frequencyFor (67) / tuning.frequencyFor (60));

        std::printf ("  Pythagorean fifth: %.3f cents (a pure 3/2 is 701.955, 12-TET is 700)\n",
                     fifth);

        tuning.setScale (dsp::scales::bohlenPierce());

        const double repeat = 1200.0 * std::log2 (tuning.frequencyFor (73) / tuning.frequencyFor (60));

        std::printf ("  Bohlen-Pierce repeat: %.3f cents (3/1 is 1901.955 -- a tritave, not an\n",
                     repeat);
        std::printf ("    octave, which is the point of having it)\n");
    }

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
    std::printf ("  capstone        [--fs]  limiter true peak, clipper aliasing, look-ahead, CPU\n");
    std::printf ("  loudness        [--fs]  LUFS at four rates, gating, PLR/PSR, correlation\n");
    std::printf ("  anvil           [--fs]  amplifier aliasing, lane THD, transformer flux, CPU\n");
    std::printf ("  sonitus         [--fs]  instrument aliasing, comb notches, tuning, CPU\n");
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
    if (command == "capstone")        return runCapstone (args);
    if (command == "loudness")        return runLoudness (args);
    if (command == "anvil")           return runAnvil (args);
    if (command == "sonitus")         return runSonitus (args);

    printUsage();
    return 1;
}
