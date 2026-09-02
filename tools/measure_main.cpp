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
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>
#include <vector>

#include <tezla/dsp/Ratio.hpp>
#include <tezla/dsp/SvfFilter.hpp>
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
#include "FerriteEngine.hpp"
#include "CapstoneEngine.hpp"
#include "CrossbarEngine.hpp"
#include "PhonossEngine.hpp"
#include "MembranaEngine.hpp"
#include "EmberdriveEngine.hpp"
#include "HaloEngine.hpp"
#include "MalleusEngine.hpp"
#include "SonitusEngine.hpp"
#include "Sf2TestBuilder.hpp"
#include "SvaraEngine.hpp"
#include "TranspectusEngine.hpp"
#include "IctusEngine.hpp"

#include <tezla/measure/WavWriter.hpp>

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

    /// sonitus-stress only: how hard to push the instrument, and at which
    /// oversampling. `osMode` is off | x2 | x4 | x8 | all (the default).
    int voices          { 16 };
    int unison          { 7 };
    double seconds      { 4.0 };
    std::string osMode  { "all" };

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
            else if (key == "--voices")  args.voices  = std::max (1, std::atoi (value.c_str()));
            else if (key == "--unison")  args.unison  = std::clamp (std::atoi (value.c_str()), 1, 7);
            else if (key == "--seconds") args.seconds = std::max (1.0, std::atof (value.c_str()));
            else if (key == "--os")      args.osMode  = value;
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

// -----------------------------------------------------------------------------
// sonitus-stress: the CPU worst case, measured the way the rig meets it.
//
// Many voices in wide unison with a long release, so every voice is sounding
// for the whole timed window, at each oversampling factor. This exists because
// the answer to "why does x8 smash the meter" is arithmetic, not mystery -- the
// whole voice runs at the internal rate, so the bill is linear in the factor --
// and because a number measured here on GCC is not a number measured on the
// rig with MSVC. Run it there; it prints ms of CPU per second of audio, which
// divided by ten is the percentage of one core.
//
// Options: --voices N (16)  --unison N (7)  --seconds S (4)  --os off|x2|x4|x8|all
// -----------------------------------------------------------------------------
int runSonitusStress (const Args& args)
{
    using namespace tezla;

    const double rate = args.sampleRate;
    constexpr int block = 512;

    struct Mode { const char* name; dsp::OversamplingMode mode; };
    const Mode all[] { { "off", dsp::OversamplingMode::Off }, { "x2", dsp::OversamplingMode::X2 },
                       { "x4", dsp::OversamplingMode::X4 },   { "x8", dsp::OversamplingMode::X8 } };

    std::printf ("Sonitus stress: %d voices, unison %d + %d (%d oscillators), 6 s release,\n"
                 "%.0f Hz in %d-sample blocks, %.0f s of audio timed per factor, best of 3.\n"
                 "Every voice is sounding for the whole window: held for 1 s, then released.\n\n",
                 args.voices, args.unison, args.unison, args.voices * 2 * args.unison,
                 rate, block, args.seconds);
    std::printf ("  factor   internal rate     ms per second of audio    %% of one core\n");

    bool any = false;
    for (const auto& m : all)
    {
        if (args.osMode != "all" && args.osMode != m.name)
            continue;
        any = true;

        sonitus::EngineParameters p;
        p.voice.shapeA = dsp::OscShape::saw;
        p.voice.shapeB = dsp::OscShape::saw;
        p.voice.unisonA = args.unison;
        p.voice.unisonB = args.unison;
        p.voice.detuneA = 25.0;
        p.voice.detuneB = 25.0;
        p.voice.spreadA = 1.0;
        p.voice.spreadB = 1.0;
        p.voice.levelA = 1.0;
        p.voice.levelB = 1.0;
        p.voice.centsB = 7.0;
        p.voice.subLevel = 0.5;
        p.voice.foldAmount = 0.3;
        p.voice.cutoffHz = 4000.0;
        p.voice.resonance = 0.4;
        p.voice.filterDrive = 0.5;
        p.voice.amp.attack = 0.005;
        p.voice.amp.sustain = 1.0;
        p.voice.amp.release = 6.0;
        p.voice.ampVelocity = 0.0;
        p.voice.level = 0.5;
        p.keyboard = sonitus::KeyboardMode::poly;
        p.polyphony = args.voices;
        p.tubeDriveDb = 12.0;
        p.oversampling = m.mode;

        // 414 kB: on the heap, never the stack (tests/test_Sonitus.cpp says why).
        auto engine = std::make_unique<sonitus::Engine>();
        engine->setParameters (p);
        engine->prepare (rate, block);
        engine->setTransport (0.0, 120.0, true);

        std::vector<double> left (static_cast<std::size_t> (block)), right (static_cast<std::size_t> (block));
        double* channels[2] { left.data(), right.data() };
        const auto render = [&] (double secs)
        {
            const long blocks = static_cast<long> (secs * rate / block);
            for (long i = 0; i < blocks; ++i)
                engine->process (channels, block);
        };
        const auto noteFor = [] (int n) { return 36 + (n * 3) % 40; };   // a chord across four octaves

        double best = 1.0e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            engine->allNotesOff();
            engine->reset();
            for (int n = 0; n < args.voices; ++n)
                engine->noteOn (noteFor (n), 1.0);
            render (0.25);                                   // warm-up, untimed

            const auto t0 = std::chrono::steady_clock::now();
            render (1.0);
            for (int n = 0; n < args.voices; ++n)
                engine->noteOff (noteFor (n));
            render (args.seconds - 1.0);                     // into the long release
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
        }

        const double msPerSecond = 1000.0 * best / args.seconds;
        std::printf ("  %-6s   %8.0f Hz      %10.1f                %6.1f%%\n",
                     m.name, rate * engine->getOversamplingFactor(), msPerSecond, msPerSecond / 10.0);
    }

    if (! any)
    {
        std::printf ("  --os must be off, x2, x4, x8 or all\n");
        return 1;
    }

    std::printf ("\n  Linear in the factor: the whole voice -- oscillators, filters, fold,\n"
                 "  envelopes -- runs at the internal rate, so x8 is eight times x1. Auto at\n"
                 "  %.0f Hz picks the factor that lands near 192 kHz internally; see the\n"
                 "  README's CPU section for what the factors buy in aliasing.\n\n", rate);
    return 0;
}

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

    // ---- the phase-4 additions ------------------------------------------------

    std::printf ("\n--- phase 4 -------------------------------------------------------\n\n");

    // ---- the filter morph -----------------------------------------------------
    //
    // The whole point of the control is that it is *between* two shapes rather
    // than crossfading two static ones, so the table below reads the response
    // three octaves either side of the corner at each stop along the axis. A
    // crossfade would show the low end and the high end moving together; a
    // morph shows them trading.
    {
        std::printf ("Filter morph: response 3 octaves below and above an 800 Hz corner,\n");
        std::printf ("no resonance, measured through the running filter at 48 kHz.\n\n");

        std::printf ("  %-8s %12s %12s     %s\n", "morph", "100 Hz", "6400 Hz", "shape");

        const struct { double morph; const char* name; } stops[] {
            { 0.00, "lowpass" },
            { 0.25, "" },
            { 0.50, "bandpass" },
            { 0.75, "" },
            { 1.00, "highpass" },
        };

        for (const auto& stop : stops)
        {
            const auto measure = [&stop] (double hz)
            {
                dsp::SvfFilter filter;

                filter.prepare (48000.0);
                filter.setMode (dsp::SvfMode::lowpass);
                filter.setCutoffHz (800.0);
                filter.setResonance (0.0);
                filter.setMorph (stop.morph);

                double phase = 0.0;
                double sum = 0.0;

                const int settle = 12000;
                const int measured = 12000;

                for (int i = 0; i < settle + measured; ++i)
                {
                    phase += 2.0 * std::numbers::pi * hz / 48000.0;

                    const double out = filter.process (std::sin (phase));

                    if (i >= settle)
                        sum += out * out;
                }

                return dsp::gainToDb (std::sqrt (2.0 * sum / static_cast<double> (measured)));
            };

            std::printf ("  %+6.2f   %10.2f dB %10.2f dB     %s\n",
                         stop.morph, measure (100.0), measure (6400.0), stop.name);
        }
    }

    // ---- the FM ratio readout -------------------------------------------------
    {
        std::printf ("\nFM ratio readout: what the OSC page says for a set of B pitches,\n");
        std::printf ("with A at unity. The panel prints the third column.\n\n");

        std::printf ("  %-24s %10s   %s\n", "B offset", "ratio", "reads");

        const struct { double octaves; double semis; double cents; const char* label; } rows[] {
            { 0.0,  0.0,   0.0,   "unison" },
            { 1.0,  0.0,   0.0,   "an octave" },
            { 0.0,  7.0,   0.0,   "a tempered fifth" },
            { 0.0,  7.0,   1.955, "a pure fifth" },
            { 1.0,  0.0,   26.0,  "an octave, 26 cents sharp" },
            { 0.0,  19.0,  0.0,   "an octave and a fifth" },
            { 3.0,  0.0,   0.0,   "four octaves" },
            { 3.0,  24.0,  0.0,   "five octaves" },
        };

        for (const auto& row : rows)
        {
            const double ratio = dsp::ratioFromOffset (row.octaves, row.semis, row.cents);
            const auto match = dsp::nearestRatio (ratio);

            char reads[64] {};

            if (match.numerator == 0)
                std::snprintf (reads, sizeof (reads), "%.3f  far apart", ratio);
            else if (match.simple)
                std::snprintf (reads, sizeof (reads), "%d:%d  harmonic %+d c",
                               match.numerator, match.denominator,
                               static_cast<int> (std::lround (match.centsError)));
            else
                std::snprintf (reads, sizeof (reads), "%.3f  %d c off %d:%d", ratio,
                               static_cast<int> (std::lround (std::abs (match.centsError))),
                               match.numerator, match.denominator);

            std::printf ("  %-24s %10.4f   %s\n", row.label, ratio, reads);
        }
    }

    // ---- the scale-locked comb ------------------------------------------------
    //
    // The figure that matters is **how far the lock has to move the comb**,
    // because that is what says whether it is a correction or a retune. On a
    // fine scale it should be a few cents; on a coarse one, tens.
    {
        std::printf ("\nComb scale lock: how far it moves a comb swept across four decades,\n");
        std::printf ("as cents, per tuning. Half the scale's widest step is the ceiling.\n\n");

        std::printf ("  %-26s %8s %10s %10s\n", "tuning", "degrees", "worst c", "mean c");

        const dsp::Scale tunings[] {
            dsp::scales::twelveToneEqual(), dsp::scales::justMajor(),
            dsp::scales::pythagorean(),     dsp::scales::bohlenPierce(),
            dsp::scales::partch43(),
        };

        for (const auto& scale : tunings)
        {
            dsp::Tuning tuning;

            if (! tuning.setScale (scale))
                continue;

            double worst = 0.0;
            double total = 0.0;
            int counted = 0;

            for (int i = 0; i < 2000; ++i)
            {
                const double hz = 20.0 * std::pow (10.0, 3.0 * static_cast<double> (i) / 2000.0);
                const double moved = std::abs (1200.0 * std::log2 (tuning.nearestScaleHz (hz) / hz));

                worst = std::max (worst, moved);
                total += moved;
                ++counted;
            }

            std::printf ("  %-26s %8d %10.2f %10.2f\n", scale.name.c_str(), scale.size(),
                         worst, total / static_cast<double> (counted));
        }
    }

    return 0;
}


// ---------------------------------------------------------------------------
// svarayantra
// ---------------------------------------------------------------------------

namespace svarayantraMeasure
{

/// A playable rig around the test-built sine font: the whole instrument, no
/// sample data shipped. Period 100 at 48 kHz is a 480 Hz source.
struct SvaraRig
{
    tezla::svarayantra::Sf2File file;
    tezla::svarayantra::Sf2Model model;
    tezla::svarayantra::SvaraEngine engine;

    explicit SvaraRig (double rate, sf2test::SimpleFont font)
    {
        const auto bytes = font.build();

        if (! file.parse (bytes.data(), bytes.size()).ok)
            std::abort();   // the builder and parser are both ours

        model.build (file);
        engine.prepare (rate);
        engine.setFont (&file, &model);
    }
};

} // namespace svarayantraMeasure

int runSvarayantra (const Args& args)
{
    using namespace tezla;
    using namespace svarayantraMeasure;

    const double rate = args.sampleRate;
    constexpr std::size_t window = 1 << 14;
    constexpr int kRootKey = 57;
    constexpr int period = 100;   // source tone = rate / period

    // ---- 1. resampling aliasing through the whole instrument ---------------

    std::printf ("Hermite resampling through the full engine: a test-built %.0f Hz sine\n",
                 rate / period);
    std::printf ("font played at intervals from its root. Audible-band inharmonic energy\n");
    std::printf ("relative to the tone, %.0f Hz host rate.\n\n", rate);
    std::printf ("  %-18s %9s\n", "interval", "aliasing");

    struct Case { const char* name; int key; };
    const Case cases[] = {
        { "unison", kRootKey },
        { "up a fourth", kRootKey + 5 },
        { "up an octave", kRootKey + 12 },
        { "down a fifth", kRootKey - 7 },
        { "down an octave", kRootKey - 12 },
    };

    for (const auto& testCase : cases)
    {
        SvaraRig rig (rate, sf2test::sineFont (period, 4,
                                               static_cast<std::uint32_t> (rate), kRootKey));

        // The OUTPUT tone is the 480 Hz source shifted by the interval, not
        // the key's own MIDI pitch -- and nudging the concert pitch is what
        // lands it exactly on an FFT bin so the analysis is honest.
        const double nominal = (rate / period)
                                 * std::exp2 ((testCase.key - kRootKey) / 12.0);
        const double exact = measure::binExactFrequency (nominal, rate, window);
        rig.engine.tuning().setConcertPitch (440.0 * exact / nominal);

        rig.engine.noteOn (testCase.key, 127);

        std::vector<double> left (window), right (window);
        rig.engine.process (left.data(), right.data(), static_cast<int> (window));
        rig.engine.process (left.data(), right.data(), static_cast<int> (window));

        const auto analysis = measure::analyseHarmonics (left, rate, exact);
        std::printf ("  %-18s %8.1f dB\n", testCase.name, analysis.audibleAliasingDb);
    }

    // ---- 2. CPU: cost per held voice ---------------------------------------

    std::printf ("\nCPU: one second of audio at %.0f Hz, 512-sample blocks, looped sine\n", rate);
    std::printf ("voices held. Percent of one core, plain path and filtered+vibrato.\n\n");
    std::printf ("  %-8s %10s %14s\n", "voices", "plain", "filter+vib");

    for (const int voices : { 1, 8, 16, 32, 64 })
    {
        double results[2] {};

        for (int lane = 0; lane < 2; ++lane)
        {
            auto font = sf2test::sineFont (period, 4,
                                           static_cast<std::uint32_t> (rate), kRootKey);

            if (lane == 1)
            {
                font.instrumentGens.push_back ({ 8, 8000 });    // initialFilterFc
                font.instrumentGens.push_back ({ 9, 200 });     // 20 dB of resonance
            }

            SvaraRig rig (rate, std::move (font));

            if (lane == 1)
                rig.engine.setModWheel (1.0);

            for (int voice = 0; voice < voices; ++voice)
                rig.engine.noteOn (30 + voice, 100);

            std::vector<double> left (512), right (512);
            const int blocks = static_cast<int> (rate) / 512;

            // Warm up, then time.
            for (int i = 0; i < 8; ++i)
                rig.engine.process (left.data(), right.data(), 512);

            const auto started = std::chrono::steady_clock::now();

            for (int block = 0; block < blocks; ++block)
                rig.engine.process (left.data(), right.data(), 512);

            const double seconds = std::chrono::duration<double> (
                std::chrono::steady_clock::now() - started).count();
            const double audioSeconds = blocks * 512.0 / rate;

            results[lane] = 100.0 * seconds / audioSeconds;
        }

        std::printf ("  %-8d %9.2f%% %13.2f%%\n", voices, results[0], results[1]);
    }

    std::printf ("\nEvery figure is the whole engine: player, envelopes, pan, control\n");
    std::printf ("timer. The font is built in memory by the same builder the tests use.\n");
    return 0;
}


// ---------------------------------------------------------------------------
// ferrite
// ---------------------------------------------------------------------------

namespace ferriteMeasure
{

std::vector<double> renderFerrite (const tezla::ferrite::Parameters& parameters,
                                   const std::vector<double>& input, double rate)
{
    tezla::ferrite::Engine engine;
    engine.prepare (rate, 512, 2);
    engine.setParameters (parameters);
    engine.reset();

    std::vector<double> left = input, right = input;

    for (std::size_t at = 0; at < left.size(); at += 271)
    {
        const int n = static_cast<int> (std::min<std::size_t> (271, left.size() - at));
        double* pointers[2] { left.data() + at, right.data() + at };
        engine.process (pointers, 2, n);
    }

    return left;
}

/// A tape at rest: wobble and hiss off, so the analysis sees the loop alone.
tezla::ferrite::Parameters still (double drive, double inputDb = 0.0)
{
    tezla::ferrite::Parameters p;
    p.drive = drive;
    p.inputDb = inputDb;
    p.wowDepth = 0.0;
    p.flutterDepth = 0.0;
    p.hissDb = -200.0;
    return p;
}

} // namespace ferriteMeasure

int runFerrite (const Args& args)
{
    using namespace tezla::measure;
    using namespace ferriteMeasure;

    const double rate = args.sampleRate;
    constexpr std::size_t fftSize = 1 << 14;

    std::printf ("Ferrite: the tape machine, measured at %.0f Hz.\n\n", rate);

    // ---- 1. the loop itself ---------------------------------------------------

    // The plugin's identity as a picture: input field against magnetisation
    // for one settled cycle, at three bias settings. Plot column 1 against
    // 2, 3 and 4 -- high bias hugs the clean curve, low bias opens the loop.
    {
        const std::string path = args.outPath.empty() ? "ferrite-loop.csv" : args.outPath;

        if (auto* csv = openForWriting (path))
        {
            constexpr int period = 512;
            constexpr int cycles = 6;   // settle 5, keep the 6th

            std::array<std::vector<double>, 3> loops;
            const double biases[3] { 0.9, 0.5, 0.1 };

            for (int b = 0; b < 3; ++b)
            {
                tezla::ferrite::Hysteresis stage;
                stage.prepare (rate * 4.0);
                stage.setParameters (0.7, 0.5, biases[b]);

                loops[static_cast<std::size_t> (b)].resize (period);

                for (int i = 0; i < cycles * period; ++i)
                {
                    const double x = 2.0 * std::sin (2.0 * std::numbers::pi * i / period);
                    const double y = stage.process (x);

                    if (i >= (cycles - 1) * period)
                        loops[static_cast<std::size_t> (b)][static_cast<std::size_t> (i % period)] = y;
                }
            }

            std::fprintf (csv, "input,bias_high_0.9,bias_mid_0.5,bias_low_0.1\n");

            for (int i = 0; i < period; ++i)
                std::fprintf (csv, "%.6f,%.6f,%.6f,%.6f\n",
                              2.0 * std::sin (2.0 * std::numbers::pi * i / period),
                              loops[0][static_cast<std::size_t> (i)],
                              loops[1][static_cast<std::size_t> (i)],
                              loops[2][static_cast<std::size_t> (i)]);

            std::fclose (csv);
            std::printf ("Hysteresis loops (drive 0.7, three bias settings) -> %s\n\n", path.c_str());
        }
    }

    // ---- 2. drive against tone, with the trim holding loudness ----------------

    std::printf ("Drive sweep at 315 Hz, -20 dBFS in, auto-trim on: the THD moves and\n");
    std::printf ("the level does not, which is the whole point of measuring the trim.\n\n");
    std::printf ("  drive     THD        2nd        3rd     out RMS\n");

    double trimReference = 0.0;

    for (const double drive : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        const double hz = binExactFrequency (315.0, rate, fftSize);
        const auto out = renderFerrite (still (drive), sine (hz, 0.1, rate, 3 * fftSize), rate);
        const std::vector<double> settled (out.begin() + 2 * static_cast<long> (fftSize), out.end());

        const auto report = analyseHarmonics (settled, rate, hz);

        double sum = 0.0;
        for (const double v : settled)
            sum += v * v;
        const double rmsDb = 10.0 * std::log10 (sum / static_cast<double> (settled.size())) + 3.01;

        if (trimReference == 0.0)
            trimReference = rmsDb;

        std::printf ("  %5.2f %8.1f dB %8.1f dB %8.1f dB %8.2f dB (%+.2f)\n",
                     drive, report.thdDb,
                     report.harmonicsDb.empty() ? -300.0 : report.harmonicsDb[0],
                     report.harmonicsDb.size() < 2 ? -300.0 : report.harmonicsDb[1],
                     rmsDb, rmsDb - trimReference);
    }

    // ---- 2b. level against tone: the input IS the drive ------------------------

    // At -20 dBFS the loop sits in its Rayleigh region -- hysteresis
    // distortion, third-first, nearly independent of the Drive control. The
    // level-driven saturation is what pushing tape actually is, so this is
    // the table that shows the machine.
    std::printf ("\nLevel sweep at drive 0.7 (315 Hz, -20 dBFS source, Input raised):\n\n");
    std::printf ("  onto tape     THD        3rd     out RMS\n");

    trimReference = 0.0;

    for (const double inputDb : { 0.0, 8.0, 14.0, 20.0 })
    {
        const double hz = binExactFrequency (315.0, rate, fftSize);
        const auto out = renderFerrite (still (0.7, inputDb), sine (hz, 0.1, rate, 3 * fftSize), rate);
        const std::vector<double> settled (out.begin() + 2 * static_cast<long> (fftSize), out.end());

        const auto report = analyseHarmonics (settled, rate, hz);

        double sum = 0.0;
        for (const double v : settled)
            sum += v * v;
        const double rmsDb = 10.0 * std::log10 (sum / static_cast<double> (settled.size())) + 3.01;

        if (trimReference == 0.0)
            trimReference = rmsDb;

        std::printf ("  %5.0f dBFS %8.1f dB %8.1f dB %8.2f dB (%+.2f)\n",
                     -20.0 + inputDb, report.thdDb,
                     report.harmonicsDb.size() < 2 ? -300.0 : report.harmonicsDb[1],
                     rmsDb, rmsDb - trimReference);
    }

    // ---- 3. the loss curves ----------------------------------------------------

    std::printf ("\nWavelength losses, designed filter against the analytic curve\n");
    std::printf ("(spacing 5 um, thickness 35 um, gap 2.5 um):\n\n");
    std::printf ("  speed        2 kHz     5 kHz    10 kHz    15 kHz   worst err\n");

    for (const double ips : { 3.75, 7.5, 15.0, 30.0 })
    {
        tezla::ferrite::TapeLoss loss;
        loss.prepare (rate);
        loss.setSpeedIps (ips);
        for (int i = 0; i < 8192; ++i)
            (void) loss.process (0.0);

        double worstErr = 0.0;

        for (double hz = 100.0; hz < 0.42 * rate && hz <= 20000.0; hz *= 1.3)
        {
            const double analytic = tezla::ferrite::TapeLoss::analyticMagnitude (
                hz, ips, 5.0, 35.0, 2.5);

            if (analytic < 0.01)
                continue;

            worstErr = std::max (worstErr, std::abs (
                20.0 * std::log10 (loss.designedMagnitudeAt (hz) / analytic)));
        }

        const auto at = [&] (double hz)
        {
            return 20.0 * std::log10 (std::max (loss.designedMagnitudeAt (hz), 1.0e-9));
        };

        std::printf ("  %5.2f ips %7.2f dB %7.2f dB %7.2f dB %7.2f dB %8.3f dB\n",
                     ips, at (2000.0), at (5000.0), at (10000.0), at (15000.0), worstErr);
    }

    // ---- 4. aliasing against the oversampling factor ---------------------------

    std::printf ("\nAliasing at maximum drive (drive 1, saturation 1, bias 0, +24 dB in),\n");
    std::printf ("worst of bin-exact non-divisor probes at 1 and 4.4 kHz, absolute dBFS\n");
    std::printf ("of the audible-band inharmonic energy (gate: -60):\n\n");

    for (const auto mode : { tezla::dsp::OversamplingMode::Off,
                             tezla::dsp::OversamplingMode::X2,
                             tezla::dsp::OversamplingMode::X4 })
    {
        auto p = still (1.0, 24.0);
        p.saturation = 1.0;
        p.bias = 0.0;
        p.oversampling = mode;

        double worst = -300.0;

        for (const double roughHz : { 1000.0, 4400.0 })
        {
            const double hz = binExactFrequency (roughHz, rate, fftSize);
            const auto out = renderFerrite (p, sine (hz, 0.5, rate, 3 * fftSize), rate);
            const std::vector<double> settled (out.begin() + 2 * static_cast<long> (fftSize), out.end());

            const auto report = analyseHarmonics (settled, rate, hz);
            worst = std::max (worst, report.fundamentalDbFs + report.audibleAliasingDb);
        }

        std::printf ("  x%d: %7.1f dBFS\n",
                     tezla::dsp::oversamplingFactor (mode, rate), worst);
    }

    // ---- 5. the wobble ---------------------------------------------------------

    std::printf ("\nWow and flutter: short-window pitch spread of a 1 kHz tone.\n\n");

    for (const double depth : { 0.15, 1.0 })
    {
        auto p = still (0.1);
        p.wowDepth = depth;
        p.flutterDepth = depth;
        p.autoTrim = false;

        std::vector<double> tone (1 << 17);
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 1000.0
                                      * static_cast<double> (i) / rate);

        const auto out = renderFerrite (p, tone, rate);

        const int window = static_cast<int> (rate / 10.0);
        double lowest = 1.0e9, highest = 0.0;

        for (int w = 4; w < 12; ++w)
        {
            double first = -1.0, last = -1.0;
            int cycles = 0;

            for (int i = w * window + 1; i < (w + 1) * window; ++i)
            {
                const double a = out[static_cast<std::size_t> (i - 1)];
                const double b = out[static_cast<std::size_t> (i)];

                if (! (a < 0.0 && b >= 0.0))
                    continue;

                const double at = static_cast<double> (i - 1) - a / (b - a);

                if (first < 0.0)
                    first = at;
                else { last = at; ++cycles; }
            }

            const double hz = cycles < 1 ? 0.0 : cycles * rate / (last - first);
            lowest = std::min (lowest, hz);
            highest = std::max (highest, hz);
        }

        std::printf ("  depth %.2f: %.2f%% spread\n", depth, 100.0 * (highest - lowest) / 1000.0);
    }

    // ---- 6. CPU ----------------------------------------------------------------

    std::printf ("\nCPU, one stereo instance, 110 Hz + 1.3 kHz programme:\n\n");

    for (const auto mode : { tezla::dsp::OversamplingMode::X4,
                             tezla::dsp::OversamplingMode::X8 })
    {
        auto p = still (0.6);
        p.oversampling = mode;

        tezla::ferrite::Engine engine;
        engine.prepare (rate, 512, 2);
        engine.setParameters (p);
        engine.reset();

        std::vector<double> left (512), right (512);
        const auto start = std::chrono::steady_clock::now();

        const int blocks = static_cast<int> (rate) / 512;

        for (int n = 0; n < blocks; ++n)
        {
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                const double t = static_cast<double> (n * 512 + static_cast<int> (i)) / rate;
                left[i] = right[i] = 0.4 * std::sin (2.0 * std::numbers::pi * 110.0 * t)
                                   + 0.2 * std::sin (2.0 * std::numbers::pi * 1300.0 * t);
            }

            double* pointers[2] { left.data(), right.data() };
            engine.process (pointers, 2, 512);
        }

        const double seconds = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - start).count();
        const double audioSeconds = blocks * 512.0 / rate;

        std::printf ("  x%d: %5.1f%% of one core\n",
                     tezla::dsp::oversamplingFactor (mode, rate),
                     100.0 * seconds / audioSeconds);
    }

    std::printf ("\nEvery figure is the whole engine: oversampled hysteresis, losses,\n");
    std::printf ("bump, wobble, trims and the latency-matched dry paths.\n");

    return 0;
}

// ---------------------------------------------------------------------------
// malleus
// ---------------------------------------------------------------------------

namespace malleusMeasure
{
using namespace tezla::malleus;

/// Plays one note and returns the rendered audio.
std::vector<double> playNote (const VoiceSettings& settings, double rate,
                              int note, double heldSeconds, double tailSeconds,
                              int sympathetic = 0)
{
    MalleusEngine engine;
    engine.prepare (rate);
    engine.settings() = settings;

    if (sympathetic > 0)
        engine.setSympathetic (sympathetic, 0.5, 0.7, 0.0, 8.0, 0.6);

    const auto held = static_cast<int> (heldSeconds * rate);
    const auto total = held + static_cast<int> (tailSeconds * rate);

    std::vector<double> out (static_cast<std::size_t> (total), 0.0);

    engine.noteOn (note, 1.0);
    engine.process (out.data(), held);
    engine.noteOff (note);
    engine.process (out.data() + held, total - held);

    return out;
}

/// Decay time to -60 dB, measured from the rendered envelope rather than
/// from the coefficient that was asked for.
double measuredT60 (const std::vector<double>& audio, double rate)
{
    // Peak-normalised envelope, sampled in 10 ms blocks.
    const auto block = static_cast<std::size_t> (0.01 * rate);
    double peak = 0.0;
    std::vector<double> envelope;

    for (std::size_t n = 0; n + block <= audio.size(); n += block)
    {
        double sum = 0.0;

        for (std::size_t i = 0; i < block; ++i)
            sum += audio[n + i] * audio[n + i];

        const double rms = std::sqrt (sum / static_cast<double> (block));
        envelope.push_back (rms);
        peak = std::max (peak, rms);
    }

    if (peak <= 0.0)
        return 0.0;

    for (std::size_t n = 0; n < envelope.size(); ++n)
        if (envelope[n] < peak * 0.001)
            return static_cast<double> (n) * 0.01;

    return static_cast<double> (envelope.size()) * 0.01;
}
/// Hann-windowed power at one frequency. A modal object's partials are
/// deliberately NOT a harmonic series, so the harmonic analyser used
/// everywhere else in this tool is the wrong instrument here: it counts
/// every real partial of a bar as "inharmonic" and reports -4 dB of
/// aliasing for a bank that cannot alias at all. (It did, the first time
/// this ran. Section 10: check the instrument before trusting it.)
double powerAt (const std::vector<double>& x, std::size_t from, std::size_t to,
                double hz, double rate)
{
    double re = 0.0;
    double im = 0.0;
    const auto count = static_cast<double> (to - from);

    for (std::size_t n = from; n < to && n < x.size(); ++n)
    {
        const double along = static_cast<double> (n - from) / count;
        const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * along);
        const double phase = 2.0 * std::numbers::pi * hz
                           * static_cast<double> (n - from) / rate;

        re += window * x[n] * std::cos (phase);
        im += window * x[n] * std::sin (phase);
    }

    return (re * re + im * im) / (count * count);
}
} // namespace malleusMeasure

int runMalleus (const Args& args)
{
    using namespace tezla;
    using namespace malleusMeasure;

    const double rate = args.sampleRate;

    // ---- 1. the mode tables, which are the instrument ------------------------

    std::printf ("The five mode-ratio tables, first eight partials. Bar and membrane are\n");
    std::printf ("root-found here at design time; string and plate are closed form; the\n");
    std::printf ("bell's canonical seven are the founders' empirical profile.\n\n");

    std::printf ("  %-10s", "mode");

    const char* materialNames[] { "String", "Bar", "Membrane", "Plate", "Bell" };

    for (const char* name : materialNames)
        std::printf (" %10s", name);

    std::printf ("\n");

    for (int mode = 0; mode < 8; ++mode)
    {
        std::printf ("  %-10d", mode + 1);

        for (int material = 0; material < 5; ++material)
            std::printf (" %10.4f",
                         dsp::ModeShapes::ratioAt (static_cast<double> (material), mode, 0.0));

        std::printf ("\n");
    }

    if (! args.outPath.empty())
    {
        std::FILE* file = std::fopen (args.outPath.c_str(), "w");

        if (file != nullptr)
        {
            std::fprintf (file, "mode,string,bar,membrane,plate,bell\n");

            for (int mode = 0; mode < dsp::ModeShapes::kMaxModes; ++mode)
            {
                std::fprintf (file, "%d", mode + 1);

                for (int material = 0; material < 5; ++material)
                    std::fprintf (file, ",%.10f",
                                  dsp::ModeShapes::ratioAt (static_cast<double> (material),
                                                            mode, 0.0));

                std::fprintf (file, "\n");
            }

            std::fclose (file);
            std::printf ("\n  full 64-mode tables written to %s\n", args.outPath.c_str());
        }
    }

    // ---- 2. Overtone Lock, the flagship --------------------------------------

    std::printf ("\nOvertone Lock: worst distance from a scale degree, in cents, over the\n");
    std::printf ("first 32 partials of a stretched bell rooted at A2.\n\n");

    std::printf ("  %-22s %10s %10s\n", "scale", "lock 0", "lock 1");

    const auto worstCents = [] (const dsp::Scale& scale, double amount)
    {
        double worst = 0.0;
        const double fundamental = 110.0;

        for (int mode = 0; mode < 32; ++mode)
        {
            const double free = fundamental * dsp::ModeShapes::ratioAt (4.0, mode, 0.3);
            const double locked = dsp::ModeShapes::lockToScale (free, fundamental,
                                                               scale, amount);

            const double x = locked / fundamental;
            const double k = std::floor (std::log (x) / std::log (scale.repeat));
            const double base = x / std::pow (scale.repeat, k);

            double nearest = 1.0e9;

            for (const double ratio : scale.ratios)
                nearest = std::min (nearest, std::abs (1200.0 * std::log2 (base / ratio)));

            nearest = std::min (nearest, std::abs (1200.0 * std::log2 (base / scale.repeat)));
            worst = std::max (worst, nearest);
        }

        return worst;
    };

    for (const auto& scale : { dsp::Tuning::twelveToneEqual(), dsp::scales::bohlenPierce(),
                               dsp::scales::fiveToneEqual() })
        std::printf ("  %-22s %10.2f %10.4f\n", scale.name.c_str(),
                     worstCents (scale, 0.0), worstCents (scale, 1.0));

    std::printf ("\n  At full lock every partial sits on a degree -- including on\n");
    std::printf ("  Bohlen-Pierce, which has no octave at all.\n");

    // ---- 3. decay accuracy ---------------------------------------------------

    std::printf ("\nDecay: the asked T60 against the rendered one, measured from the\n");
    std::printf ("envelope of a struck bar (tilt 0, so every partial shares the decay).\n\n");

    std::printf ("  %10s %12s\n", "asked", "measured");

    for (const double decay : { 0.25, 0.5, 1.0, 2.0, 4.0 })
    {
        VoiceSettings settings;
        settings.decaySeconds = decay;
        settings.tilt = 0.0;
        settings.partials = 16;

        const auto audio = playNote (settings, rate, 45, 0.01, decay * 2.0 + 0.5);

        std::printf ("  %8.2f s %10.2f s\n", decay, measuredT60 (audio, rate));
    }

    // ---- 4. strike spectra vs hardness ---------------------------------------

    std::printf ("\nHardness: spectral centroid of the strike, on a 32-partial harmonic\n");
    std::printf ("object at 100 Hz. Contact time runs 8 ms of felt to 0.15 ms of brass.\n\n");

    std::printf ("  %10s %12s %14s\n", "hardness", "contact", "centroid");

    for (const double hardness : { 0.0, 0.2, 0.5, 0.9, 1.0 })
    {
        double frequencies[32];
        double weights[32];

        for (int mode = 0; mode < 32; ++mode)
            frequencies[mode] = 100.0 * (mode + 1);

        malletWeights (weights, frequencies, 32, 0.29, hardness, 1.0);

        double power = 0.0;
        double weighted = 0.0;

        for (int mode = 0; mode < 32; ++mode)
        {
            const double p = weights[mode] * weights[mode];
            power += p;
            weighted += frequencies[mode] * p;
        }

        std::printf ("  %10.2f %9.3f ms %11.1f Hz\n", hardness,
                     1000.0 * contactSeconds (hardness),
                     power > 0.0 ? weighted / power : 0.0);
    }

    // ---- 5. the bow's onset map ----------------------------------------------

    std::printf ("\nBow onset: sustained RMS after 1.5 s, over the pressure x speed plane.\n");
    std::printf ("Below the onset the object will not speak; above it, it sings.\n\n");

    std::printf ("  %-10s", "P \\ S");

    const double speeds[] { 0.1, 0.3, 0.5, 0.7, 1.0 };

    for (const double speed : speeds)
        std::printf (" %9.1f", speed);

    std::printf ("\n");

    for (const double pressure : { 0.01, 0.05, 0.2, 0.5, 1.0 })
    {
        std::printf ("  %-10.2f", pressure);

        for (const double speed : speeds)
        {
            VoiceSettings settings;
            settings.exciter = Exciter::Bow;
            settings.bowPressure = pressure;
            settings.bowSpeed = speed;
            settings.partials = 16;
            settings.decaySeconds = 2.0;

            const auto audio = playNote (settings, rate, 45, 1.5, 0.0);

            // The tail's RMS about its own mean: a bow statically deflects
            // the object, and a standing offset is not oscillation.
            const auto from = audio.size() - static_cast<std::size_t> (0.3 * rate);

            double mean = 0.0;

            for (std::size_t n = from; n < audio.size(); ++n)
                mean += audio[n];

            mean /= static_cast<double> (audio.size() - from);

            double sum = 0.0;

            for (std::size_t n = from; n < audio.size(); ++n)
                sum += (audio[n] - mean) * (audio[n] - mean);

            std::printf (" %9.4f",
                         std::sqrt (sum / static_cast<double> (audio.size() - from)));
        }

        std::printf ("\n");
    }

    // ---- 6. what is between the modes -----------------------------------

    std::printf ("\nWhat sits BETWEEN the modes, at maximum hardness with 64 partials of\n");
    std::printf ("an inharmonic bar and no oversampling anywhere. Probes on each mode\n");
    std::printf ("against probes at the geometric midpoints between them, in dB.\n\n");

    std::printf ("  %-10s %10s %14s %12s\n", "note", "modes", "between", "gap");

    for (const int note : { 33, 45, 57, 69, 81 })
    {
        VoiceSettings settings;
        settings.material = 1.0;
        settings.partials = 64;
        settings.hardness = 1.0;
        settings.decaySeconds = 3.0;

        const auto audio = playNote (settings, rate, note, 1.6, 0.0);

        // From 0.7 s on: before that the vactrol's fast early fall puts real
        // sidebands around every mode -- that is the ping, not aliasing.
        const auto from = static_cast<std::size_t> (0.7 * rate);
        const auto to = static_cast<std::size_t> (1.5 * rate);

        MalleusEngine probe;
        probe.prepare (rate);

        const double fundamental = probe.tuning().frequencyFor (note);

        double modeFrequencies[dsp::ModalResonator::kMaxModes] {};
        VoiceSettings shape = settings;

        (void) buildModeFrequencies (modeFrequencies, shape, fundamental,
                                     probe.tuning().getScale(), rate);

        double onMode = 0.0;
        double between = 0.0;

        for (int mode = 0; mode + 1 < settings.partials; ++mode)
        {
            if (modeFrequencies[mode] >= 0.45 * rate)
                break;

            onMode = std::max (onMode, powerAt (audio, from, to,
                                                modeFrequencies[mode], rate));

            if (modeFrequencies[mode + 1] / modeFrequencies[mode] > 1.1
                && modeFrequencies[mode + 1] < 0.45 * rate)
                between = std::max (between,
                    powerAt (audio, from, to,
                             std::sqrt (modeFrequencies[mode] * modeFrequencies[mode + 1]),
                             rate));
        }

        const double gap = 10.0 * std::log10 (between / onMode);

        std::printf ("  %6.1f Hz %10.2f %14.2f %11.1f\n", fundamental,
                     10.0 * std::log10 (onMode), 10.0 * std::log10 (between), gap);
    }

    std::printf ("\n  A modal bank cannot alias: every partial is computed, and one that\n");
    std::printf ("  would land past 0.45 fs is dropped rather than folded. The strike is\n");
    std::printf ("  injected in closed form, so it carries nothing that was never\n");
    std::printf ("  computed -- which is why this instrument oversamples nowhere.\n");

    // ---- 7. Phase 2: bloom, damping and the listening pair ---------------------

    std::printf ("\n----- Phase 2 -----------------------------------------------\n");

    // Bloom's operating point, measured on a **voice** rather than on a bare
    // bank -- the first draft of this table built a harmonic bank where
    // tests/test_ModalResonator.cpp builds an inharmonic bar, and the two
    // disagreed about where the control engages. Velocity is what a player
    // has a handle on, so velocity is what this sweeps.
    std::printf ("\nBloom: the late high-band share of a struck plate, against the\n");
    std::printf ("velocity it was struck at. A linear bank reads the same figure at\n");
    std::printf ("every velocity, which is the property Bloom removes.\n\n");
    std::printf ("  %-10s %-12s %-12s %-12s\n", "velocity", "voice peak", "bloom 1", "bloom 0");

    for (const double velocity : { 0.1, 0.25, 0.4, 0.55, 0.7, 0.85, 1.0 })
    {
        double reading[2] {};
        double peak = 0.0;

        for (int on = 0; on < 2; ++on)
        {
            MalleusVoice voice;
            VoiceSettings settings;

            settings.material = 3.6;
            settings.partials = 48;
            settings.decaySeconds = 8.0;
            settings.tilt = 0.3;
            settings.position = 0.17;
            settings.hardness = 0.34;

            voice.prepare (rate);
            voice.noteOn (45, 110.0, velocity, 1, settings,
                          dsp::scales::twelveToneEqual(), 0);
            voice.setBloom (on == 0 ? dsp::ModalResonator::kMaxBloom : 0.0);

            const int total = static_cast<int> (rate);
            std::vector<double> left (static_cast<std::size_t> (total), 0.0);
            std::vector<double> right (static_cast<std::size_t> (total), 0.0);

            for (int done = 0; done < total; done += 128)
            {
                const int take = std::min (128, total - done);

                voice.controlTick (take);
                voice.render (left.data() + done, right.data() + done, take);
            }

            if (on == 1)
                for (const double sample : left)
                    peak = std::max (peak, std::abs (sample));

            // The share above four times the fundamental, late in the ring.
            double high = 0.0;
            double whole = 0.0;

            for (int partial = 1; partial <= 24; ++partial)
            {
                const double hz = 110.0 * partial;

                if (hz > 0.45 * rate)
                    break;

                double re = 0.0;
                double im = 0.0;
                const std::size_t from = 14400;
                const std::size_t span = 9600;

                for (std::size_t n = 0; n < span && from + n < left.size(); ++n)
                {
                    const double along = static_cast<double> (n) / static_cast<double> (span);
                    const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * along);
                    const double phase = 2.0 * std::numbers::pi * hz
                                       * static_cast<double> (n) / rate;

                    re += window * left[from + n] * std::cos (phase);
                    im += window * left[from + n] * std::sin (phase);
                }

                const double power = re * re + im * im;

                whole += power;

                if (hz >= 440.0)
                    high += power;
            }

            reading[on] = whole > 0.0 ? high / whole : 0.0;
        }

        std::printf ("  %-10.2f %-12.4f %-12.4f %-12.4f\n",
                     velocity, peak, reading[0], reading[1]);
    }

    std::printf ("\n  Bloom is a hit-it-hard effect, which is what the physics says a\n");
    std::printf ("  large-displacement nonlinearity is. The useful window is about\n");
    std::printf ("  9 dB wide: above it the injection swamps the state rather than\n");
    std::printf ("  perturbing it, and the control reverses.\n");

    // The damping law, as T60 against frequency.
    std::printf ("\nDamp: measured T60 in seconds, on modes with a 4 s natural decay.\n");
    std::printf ("The loss is proportional to frequency, so each doubling of pitch\n");
    std::printf ("roughly halves the time -- dull before quiet.\n\n");
    std::printf ("  %-8s %-9s %-9s %-9s %-9s\n", "Hz", "0.00", "0.25", "0.50", "1.00");

    for (const double hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0 })
    {
        std::printf ("  %-8.0f", hz);

        for (const double damp : { 0.0, 0.25, 0.5, 1.0 })
        {
            dsp::ModalResonator bank;
            bank.prepare (rate);
            bank.setModeCount (1);
            bank.setMode (0, hz, 4.0, 1.0);
            bank.setDamp (damp);
            bank.excite (0, 1.0);

            const int limit = static_cast<int> (20.0 * rate);
            const int period = std::max (2, static_cast<int> (rate / hz));

            double first = 0.0;
            int sample = 0;

            for (; sample < period * 2 && sample < limit; ++sample)
                first = std::max (first, std::abs (bank.process()));

            const double target = first * 0.001;

            while (sample < limit)
            {
                double loudest = 0.0;

                for (int n = 0; n < period && sample < limit; ++n, ++sample)
                    loudest = std::max (loudest, std::abs (bank.process()));

                if (loudest <= target)
                    break;
            }

            std::printf (" %-9.3f", static_cast<double> (sample) / rate);
        }

        std::printf ("\n");
    }

    // The listening pair: width against what survives a mono fold.
    std::printf ("\nListen: two points on the object, mirrored at (q, 1-q).\n");
    std::printf ("Width and mono compatibility trade off directly.\n\n");
    std::printf ("  %-6s %-6s %-14s %-8s\n", "L", "R", "correlation", "mono keeps");

    for (const double q : { 0.05, 0.10, 0.20, 0.29, 0.35, 0.45 })
    {
        MalleusVoice voice;
        VoiceSettings settings;

        settings.material = 1.2;
        settings.partials = 32;
        settings.decaySeconds = 2.0;
        settings.position = 0.29;
        settings.hardness = 0.6;
        settings.listenLeft = q;
        settings.listenRight = 1.0 - q;
        settings.listenAmount = 1.0;

        voice.prepare (rate);
        voice.noteOn (57, 220.0, 0.85, 0x9E3779B97F4A7C15ULL, settings,
                      dsp::scales::twelveToneEqual(), 0);

        const int total = static_cast<int> (0.5 * rate);
        std::vector<double> left (static_cast<std::size_t> (total), 0.0);
        std::vector<double> right (static_cast<std::size_t> (total), 0.0);

        for (int done = 0; done < total; done += 128)
        {
            const int take = std::min (128, total - done);

            voice.controlTick (take);
            voice.render (left.data() + done, right.data() + done, take);
        }

        double dot = 0.0;
        double energyLeft = 0.0;
        double energyRight = 0.0;
        double energyMono = 0.0;

        for (std::size_t n = 0; n < left.size(); ++n)
        {
            dot += left[n] * right[n];
            energyLeft += left[n] * left[n];
            energyRight += right[n] * right[n];

            const double fold = 0.5 * (left[n] + right[n]);
            energyMono += fold * fold;
        }

        const auto count = static_cast<double> (left.size());
        const double correlation = dot / std::sqrt (energyLeft * energyRight);
        const double stereo = 0.5 * (std::sqrt (energyLeft / count)
                                       + std::sqrt (energyRight / count));

        std::printf ("  %-6.2f %-6.2f %+-14.4f %-8.3f\n",
                     q, 1.0 - q, correlation, std::sqrt (energyMono / count) / stereo);
    }

    std::printf ("\n  At MATCHED width an asymmetric pair survives better: 0.10/0.75\n");
    std::printf ("  and 0.20/0.80 are equally wide and keep 0.641 against 0.600.\n");

    // ---- 8. CPU --------------------------------------------------------------

    std::printf ("\nCPU, as a percentage of one core, at %.0f Hz.\n\n", rate);

    const auto costOf = [rate] (const char* label, const VoiceSettings& settings,
                                int voices, int sympathetic)
    {
        MalleusEngine engine;
        engine.prepare (rate);
        engine.settings() = settings;

        if (sympathetic > 0)
            engine.setSympathetic (sympathetic, 0.5, 0.8, 0.3, 8.0, 0.6);

        for (int voice = 0; voice < voices; ++voice)
            engine.noteOn (36 + 3 * voice, 0.8);

        std::vector<double> buffer (480, 0.0);

        for (int block = 0; block < 10; ++block)
            engine.process (buffer.data(), 480);   // let everything speak

        const auto start = std::chrono::steady_clock::now();

        for (int block = 0; block < 100; ++block)
            engine.process (buffer.data(), 480);

        const double seconds = std::chrono::duration<double> (
            std::chrono::steady_clock::now() - start).count();

        std::printf ("  %-38s %7.2f%%\n", label, 100.0 * seconds);
    };

    VoiceSettings struck;
    struck.partials = 64;
    struck.decaySeconds = 2.0;

    VoiceSettings bowed = struck;
    bowed.exciter = Exciter::Bow;
    bowed.bowPressure = 0.5;

    costOf ("one struck voice, 64 partials", struck, 1, 0);
    costOf ("16 struck voices, 64 partials", struck, 16, 0);
    costOf ("16 bowed voices, 64 partials", bowed, 16, 0);
    costOf ("16 bowed + 12 sympathetic strings", bowed, 16, 12);

    std::printf ("\n  The plan budgeted 10-15%% of a core for the full instrument. A voice\n");
    std::printf ("  whose key is up and whose vactrol has gone dark is retired and costs\n");
    std::printf ("  nothing -- measured separately in tests/test_MalleusEngine.cpp.\n");

    return 0;
}

// ---------------------------------------------------------------------------
// Crossbar -- the telephone tone instrument
// ---------------------------------------------------------------------------

namespace crossbarMeasure {

using namespace tezla::crossbar;

/// Hann-windowed amplitude at a frequency, normalised so a pure sine of
/// amplitude A reads A. RMS rather than peak, per CLAUDE.md section 10.
double amplitudeAt (const std::vector<double>& x, std::size_t from, std::size_t to,
                    double hz, double rate)
{
    double re = 0.0;
    double im = 0.0;
    double windowSum = 0.0;

    const double span = static_cast<double> (to - from);

    for (std::size_t n = from; n < to && n < x.size(); ++n)
    {
        const double along = static_cast<double> (n - from) / span;
        const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * along);
        const double phase = 2.0 * std::numbers::pi * hz
                               * static_cast<double> (n - from) / rate;

        re += window * x[n] * std::cos (phase);
        im -= window * x[n] * std::sin (phase);
        windowSum += window;
    }

    return windowSum <= 0.0 ? 0.0 : 2.0 * std::hypot (re, im) / windowSum;
}

CrossbarEngine::Parameters plain()
{
    CrossbarEngine::Parameters p;
    p.attackSeconds = 0.002;
    p.decaySeconds = 0.1;
    p.sustain = 1.0;
    p.releaseSeconds = 0.02;
    p.band = BandMode::off;
    p.rateIndex = 0;
    p.codec = tezla::dsp::CompandingLaw::off;
    return p;
}

std::vector<double> render (CrossbarEngine& engine, int note, double seconds, double rate)
{
    const auto total = static_cast<std::size_t> (seconds * rate);
    std::vector<double> out (total, 0.0);

    engine.noteOn (note, 1.0);

    for (std::size_t n = 0; n < total; n += 128)
        engine.process (out.data() + n,
                        nullptr,
                        static_cast<int> (std::min<std::size_t> (128, total - n)));

    return out;
}

/// A boxcar RMS envelope with a threshold at 1/sqrt(2) of the peak: the only
/// shape whose crossings are unbiased on both edges, so a burst measures the
/// length it actually is. See tests/test_Crossbar.cpp for what the two wrong
/// shapes did.
std::vector<double> crossings (const std::vector<double>& x, double rate)
{
    const auto window = static_cast<std::size_t> (0.040 * rate);
    std::vector<double> envelope (x.size(), 0.0);
    double sum = 0.0;

    for (std::size_t n = 0; n < x.size(); ++n)
    {
        sum += x[n] * x[n];

        if (n >= window)
            sum -= x[n - window] * x[n - window];

        envelope[n] = std::sqrt (sum / static_cast<double> (window));
    }

    const double threshold = *std::max_element (envelope.begin(), envelope.end())
                               / std::numbers::sqrt2;

    std::vector<double> found;
    bool above = false;

    for (std::size_t n = 0; n < envelope.size(); ++n)
    {
        const bool nowAbove = envelope[n] > threshold;

        if (n > 0 && nowAbove != above)
            found.push_back (static_cast<double> (n) / rate);

        above = nowAbove;
    }

    return found;
}

double snrForSine (const tezla::dsp::Compander& compander, double levelDb, double rate)
{
    const auto length = static_cast<std::size_t> (10.0 * rate);
    const double amplitude = tezla::dsp::dbToGain (levelDb);

    double signalPower = 0.0;
    double errorPower = 0.0;

    for (std::size_t n = 0; n < length; ++n)
    {
        const double x = amplitude
                           * std::sin (2.0 * std::numbers::pi * 997.0
                                         * static_cast<double> (n) / rate);
        const double y = compander.process (x);

        signalPower += x * x;
        errorPower += (y - x) * (y - x);
    }

    return errorPower <= 0.0 ? 999.0 : 10.0 * std::log10 (signalPower / errorPower);
}

} // namespace crossbarMeasure

int runCrossbar (const Args& args)
{
    using namespace tezla;
    using namespace crossbarMeasure;

    const double rate = args.sampleRate;

    // ---- 1. the DTMF matrix, against ITU-T Q.23 ------------------------------

    std::printf ("The sixteen DTMF pairs, measured at the engine's output against the\n");
    std::printf ("Q.23 figures. A real receiver accepts 1.5%%; the error here is the\n");
    std::printf ("analysis window's, not the generator's.\n\n");

    std::printf ("  %-4s %10s %10s %10s %10s %10s\n",
                 "key", "low asked", "measured", "high asked", "measured", "twist dB");

    double worstError = 0.0;

    for (int i = 0; i < 16; ++i)
    {
        const auto tone = static_cast<Tone> (i);

        int row = 0, column = 0;
        dtmfIndices (tone, row, column);

        CrossbarEngine engine;
        engine.prepare (rate);
        engine.setParameters (plain());

        const auto out = render (engine, noteForTone (tone), 0.45, rate);

        const auto from = static_cast<std::size_t> (0.10 * rate);
        const auto to = static_cast<std::size_t> (0.40 * rate);

        // The peak of a windowed DFT swept either side of the asked frequency
        // is where the tone actually is.
        const auto peakNear = [&] (double asked)
        {
            double best = asked;
            double bestAmplitude = 0.0;

            for (double hz = asked - 6.0; hz <= asked + 6.0; hz += 0.05)
            {
                const double amplitude = amplitudeAt (out, from, to, hz, rate);

                if (amplitude > bestAmplitude)
                {
                    bestAmplitude = amplitude;
                    best = hz;
                }
            }

            return std::pair { best, bestAmplitude };
        };

        const auto [lowHz, lowAmplitude] = peakNear (kDtmfRowHz[row]);
        const auto [highHz, highAmplitude] = peakNear (kDtmfColHz[column]);

        worstError = std::max ({ worstError,
                                 std::abs (lowHz - kDtmfRowHz[row]),
                                 std::abs (highHz - kDtmfColHz[column]) });

        std::printf ("  %-4s %10.0f %10.2f %10.0f %10.2f %10.3f\n",
                     nameFor (tone), kDtmfRowHz[row], lowHz,
                     kDtmfColHz[column], highHz,
                     20.0 * std::log10 (highAmplitude / lowAmplitude));
    }

    std::printf ("\n  Worst frequency error over all 32 tones: %.3f Hz, which is %.4f%% of\n",
                 worstError, 100.0 * worstError / 697.0);
    std::printf ("  the lowest one -- against the 1.5%% a receiver tolerates.\n");

    // ---- 2. what else is in there -------------------------------------------

    {
        CrossbarEngine engine;
        engine.prepare (rate);
        engine.setParameters (plain());

        const auto out = render (engine, noteForTone (Tone::digit5), 0.5, rate);
        const auto from = static_cast<std::size_t> (0.10 * rate);
        const auto to = static_cast<std::size_t> (0.40 * rate);

        double worst = 0.0;
        double worstHz = 0.0;

        for (double hz = 100.0; hz <= 20000.0; hz += 25.0)
        {
            if (std::abs (hz - 770.0) < 150.0 || std::abs (hz - 1336.0) < 150.0)
                continue;

            const double amplitude = amplitudeAt (out, from, to, hz, rate);

            if (amplitude > worst)
            {
                worst = amplitude;
                worstHz = hz;
            }
        }

        std::printf ("\n  Loudest component anywhere else, 100 Hz to 20 kHz: %.3e at %.0f Hz,\n",
                     worst, worstHz);
        std::printf ("  which is %.1f dB down. A sine has no harmonics to fold, which is why\n",
                     20.0 * std::log10 (worst / 0.4427));
        std::printf ("  this instrument oversamples nowhere.\n");
    }

    // ---- 3. cadences, at four sample rates ----------------------------------

    std::printf ("\nCadence timing across sample rates. The tables state seconds and the\n");
    std::printf ("coefficients come from the actual rate, so these must not move.\n\n");

    std::printf ("  %-10s %12s %12s %12s\n", "rate", "burst (s)", "period (s)", "UK gap (s)");

    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CrossbarEngine bell;
        bell.prepare (fs);
        bell.setParameters (plain());

        const auto busy = crossings (render (bell, noteForTone (Tone::busy), 2.6, fs), fs);

        auto uk = plain();
        uk.region = Region::unitedKingdom;

        CrossbarEngine bt;
        bt.prepare (fs);
        bt.setParameters (uk);

        const auto ring = crossings (render (bt, noteForTone (Tone::ringback), 3.5, fs), fs);

        std::printf ("  %-10.0f %12.4f %12.4f %12.4f\n", fs,
                     busy.size() >= 2 ? busy[1] - busy[0] : -1.0,
                     busy.size() >= 3 ? busy[2] - busy[0] : -1.0,
                     ring.size() >= 3 ? ring[2] - ring[1] : -1.0);
    }

    std::printf ("\n  Asked: 0.5 burst, 1.0 period, 0.2 UK gap.\n");

    // ---- 4. G.711, which is where the sound comes from -----------------------

    std::printf ("\nThe line's codec: signal-to-noise against level, on a 997 Hz sine.\n");
    std::printf ("A companding law's noise follows the signal down; a linear quantiser's\n");
    std::printf ("does not, and that difference IS the difference between a telephone and\n");
    std::printf ("a sampler.\n\n");

    std::printf ("  %-10s %10s %10s %10s\n", "level dB", "mu-law", "A-law", "linear 8");

    dsp::Compander mu;
    mu.setLaw (dsp::CompandingLaw::muLaw);

    dsp::Compander a;
    a.setLaw (dsp::CompandingLaw::aLaw);

    dsp::Compander linear;
    linear.setLaw (dsp::CompandingLaw::linear);
    linear.setBits (8);

    double muTop = -999.0, muBottom = 999.0;
    double linearTop = -999.0, linearBottom = 999.0;

    for (double levelDb = 0.0; levelDb >= -40.001; levelDb -= 10.0)
    {
        const double muSnr = snrForSine (mu, levelDb, rate);
        const double aSnr = snrForSine (a, levelDb, rate);
        const double linearSnr = snrForSine (linear, levelDb, rate);

        muTop = std::max (muTop, muSnr);
        muBottom = std::min (muBottom, muSnr);
        linearTop = std::max (linearTop, linearSnr);
        linearBottom = std::min (linearBottom, linearSnr);

        std::printf ("  %-10.0f %10.2f %10.2f %10.2f\n", levelDb, muSnr, aSnr, linearSnr);
    }

    std::printf ("\n  Spread over the 40 dB: mu-law %.2f dB, linear %.2f dB.\n",
                 muTop - muBottom, linearTop - linearBottom);
    std::printf ("  Ceilings the structure implies: mu-law %.6f, A-law %.6f.\n",
                 dsp::Compander::kMuPeak, dsp::Compander::kAPeak);

    // ---- 5. the band, and the aliasing that is on purpose -------------------

    std::printf ("\nThe band edges, measured on the DTMF constituent tones, and the image\n");
    std::printf ("the rate reduction is SUPPOSED to make.\n\n");

    {
        const auto through = [&] (BandMode band, Tone tone, double hz)
        {
            CrossbarEngine engine;
            engine.prepare (rate);

            auto p = plain();
            p.band = band;
            engine.setParameters (p);

            const auto out = render (engine, noteForTone (tone), 0.5, rate);

            return amplitudeAt (out, static_cast<std::size_t> (0.15 * rate),
                                static_cast<std::size_t> (0.45 * rate), hz, rate);
        };

        std::printf ("  %-10s %10s %10s %10s\n", "probe", "off", "toll dB", "wide dB");

        const Tone probes[] { Tone::row697, Tone::row941, Tone::col1633,
                              Tone::singleFrequency };
        const double hz[] { 697.0, 941.0, 1633.0, 2600.0 };

        for (int i = 0; i < 4; ++i)
        {
            const double reference = through (BandMode::off, probes[i], hz[i]);

            std::printf ("  %-10.0f %10.4f %10.3f %10.3f\n", hz[i], reference,
                         20.0 * std::log10 (through (BandMode::toll, probes[i], hz[i])
                                              / reference),
                         20.0 * std::log10 (through (BandMode::wideband, probes[i], hz[i])
                                              / reference));
        }

        const auto image = [&] (int rateIndex)
        {
            CrossbarEngine engine;
            engine.prepare (rate);

            auto p = plain();
            p.rateIndex = rateIndex;
            engine.setParameters (p);

            const auto out = render (engine, noteForTone (Tone::col1477), 0.5, rate);

            return amplitudeAt (out, static_cast<std::size_t> (0.10 * rate),
                                static_cast<std::size_t> (0.40 * rate), 2523.0, rate);
        };

        std::printf ("\n  1477 Hz held and sampled at 4 kHz images at 2523 Hz:\n");
        std::printf ("    rate off  %.3e (%.1f dB)\n", image (0),
                     20.0 * std::log10 (std::max (image (0), 1.0e-12)));
        std::printf ("    4 kHz     %.3e (%.1f dB)\n", image (7),
                     20.0 * std::log10 (std::max (image (7), 1.0e-12)));
        std::printf ("  That is CLAUDE.md section 7's documented exception, on purpose.\n");
    }

    // ---- 6. what it costs ---------------------------------------------------

    std::printf ("\nCPU, as a percentage of one core at %.0f Hz.\n\n", rate);

    {
        const auto costOf = [&] (const char* label, int voices, bool line)
        {
            CrossbarEngine engine;
            engine.prepare (rate);

            auto p = plain();

            if (line)
            {
                p.band = BandMode::toll;
                p.rateIndex = kDefaultRateIndex;
                p.codec = dsp::CompandingLaw::muLaw;
                p.noise = 0.3;
            }

            engine.setParameters (p);

            for (int i = 0; i < voices; ++i)
                engine.noteOn (noteForTone (Tone::dialTone) + i, 1.0);

            const auto total = static_cast<std::size_t> (4.0 * rate);
            std::vector<double> buffer (512, 0.0);

            const auto start = std::chrono::steady_clock::now();

            for (std::size_t n = 0; n < total; n += 512)
                engine.process (buffer.data(), nullptr, 512);

            const auto elapsed = std::chrono::duration<double> (
                std::chrono::steady_clock::now() - start).count();

            std::printf ("  %-40s %6.2f%%\n", label,
                         100.0 * elapsed / (static_cast<double> (total) / rate));
        };

        costOf ("one tone, line off", 1, false);
        costOf ("16 tones, line off", 16, false);
        costOf ("16 tones, full line (toll/8k/mu/hiss)", 16, true);
        costOf ("idle, full line", 0, true);
    }

    if (! args.outPath.empty())
        std::printf ("\n  (--out is accepted for consistency; this command prints tables "
                     "rather than writing a CSV.)\n");

    return 0;
}

// ---------------------------------------------------------------------------
// phonoss
// ---------------------------------------------------------------------------

namespace phonossMeasure {

using namespace tezla::phonoss;

/// A voice-shaped probe. `sibilant` swaps the body of the voice for a
/// high-band burst of the same total energy, which is what makes the de-esser's
/// level-independence claim testable: the two differ in *spectrum*, not level.
double voiceAt (std::size_t index, double rate, double levelDb, bool sibilant)
{
    const double t = static_cast<double> (index) / rate;
    const double twoPi = 2.0 * std::numbers::pi;

    const double body = 1.00 * std::sin (twoPi * 180.0 * t)
                      + 0.50 * std::sin (twoPi * 360.0 * t)
                      + 0.30 * std::sin (twoPi * 720.0 * t)
                      + 0.12 * std::sin (twoPi * 2400.0 * t);

    // Two high partials beating, which is as close to broadband /s/ noise as a
    // deterministic probe gets -- and deterministic matters, because the whole
    // measurement is a comparison across levels.
    const double hiss = 0.9 * std::sin (twoPi * 7300.0 * t)
                            * std::sin (twoPi * 5100.0 * t + 1.0)
                      + 0.5 * std::sin (twoPi * 9700.0 * t);

    return tezla::dsp::dbToGain (levelDb) * 0.4 * (sibilant ? body * 0.25 + hiss : body);
}

/// Runs the probe through a strip and returns the worst reduction each stage
/// reached.
///
/// **The first 100 ms are discarded**, and that is not tidiness. Every filter
/// in the chain starts from zero state, and a sine switched on at phase zero
/// is a step: the settling transient is broadband, so it looks to the de-esser
/// exactly like sibilance. Measuring from sample zero reported 2.96 dB of
/// reduction on a pure vowel -- a defect that existed only in the harness.
PhonossEngine::Meters worstOf (const PhonossEngine::Settings& settings, double rate,
                              double seconds, double levelDb, bool sibilant)
{
    PhonossEngine engine;
    engine.prepare (rate);
    engine.setSettings (settings);

    const auto settle = static_cast<std::size_t> (0.1 * rate);
    PhonossEngine::Meters worst;

    for (std::size_t n = 0; n < static_cast<std::size_t> (seconds * rate); ++n)
    {
        double left = voiceAt (n, rate, levelDb, sibilant);
        double right = left;

        engine.process (&left, &right, 1);

        if (n < settle)
            continue;

        const auto m = engine.getMeters();
        worst.gateDb = std::min (worst.gateDb, m.gateDb);
        worst.deEssDb = std::min (worst.deEssDb, m.deEssDb);
        worst.levellerDb = std::min (worst.levellerDb, m.levellerDb);
        worst.peakDb = std::min (worst.peakDb, m.peakDb);
    }

    return worst;
}

} // namespace phonossMeasure

int runPhonoss (const Args& args)
{
    using namespace phonossMeasure;

    const double rate = args.sampleRate > 0.0 ? args.sampleRate : 48000.0;

    std::printf ("tezla-measure phonoss -- %.0f Hz\n\n", rate);

    // -----------------------------------------------------------------------
    // The claim the plugin is built around
    // -----------------------------------------------------------------------

    std::printf ("De-esser: reduction against input level\n");
    std::printf ("  The point of measuring sibilance as a RATIO rather than a level:\n");
    std::printf ("  the same /s/ at different levels must be reduced by the same amount,\n");
    std::printf ("  and a vowel must not be reduced at any level.\n\n");
    std::printf ("  %-12s %14s %14s\n", "level", "on an /s/", "on a vowel");

    {
        PhonossEngine::Settings s;

        // Range wide open, deliberately. CLAUDE.md section 10: a guard at the
        // end of a chain makes every measurement of the guarded quantity true.
        // Range **is** such a guard -- it clamps the reduction -- and with it
        // at 12 dB every level read exactly -12.000, which looks like perfect
        // level independence and is really just the clamp. What has to be
        // measured is what the detector asked for, so the cap is put out of
        // reach and the reduction is detector-limited.
        s.deEss.rangeDb = 36.0;
        s.deEss.thresholdDb = -12.0;

        double worstSpread = 0.0;
        double firstSibilant = 0.0;
        bool haveFirst = false;
        double worstVowel = 0.0;

        for (const double levelDb : { -36.0, -30.0, -24.0, -18.0, -12.0, -6.0 })
        {
            const double onS = worstOf (s, rate, 0.6, levelDb, true).deEssDb;
            const double onVowel = worstOf (s, rate, 0.6, levelDb, false).deEssDb;

            std::printf ("  %8.0f dB %11.3f dB %11.3f dB\n", levelDb, onS, onVowel);

            if (! haveFirst)
            {
                firstSibilant = onS;
                haveFirst = true;
            }

            worstSpread = std::max (worstSpread, std::abs (onS - firstSibilant));
            worstVowel = std::min (worstVowel, onVowel);
        }

        std::printf ("\n  spread across 30 dB of input   %.3f dB\n", worstSpread);
        std::printf ("  worst reduction on a vowel     %.3f dB\n", worstVowel);
        std::printf ("  A level-thresholded de-esser would track the level one for one:\n");
        std::printf ("  30 dB in would be about 30 dB of spread.\n");
    }

    // -----------------------------------------------------------------------
    // Compressors: asked against measured
    // -----------------------------------------------------------------------

    std::printf ("\nCompressor: measured ratio against asked\n");
    std::printf ("  %-10s %14s %12s\n", "asked", "measured", "error");

    for (const double ratio : { 2.0, 4.0, 8.0, 16.0 })
    {
        // Well above the knee, so the curve's straight section is what is
        // being measured rather than its corner.
        tezla::dsp::CompressorCore compressor;
        compressor.prepare (rate);
        compressor.setThresholdDb (-40.0);
        compressor.setRatio (ratio);
        compressor.setKneeDb (0.0);

        // Slow, and that is the measurement rather than a setting. "Ratio"
        // names the **static** curve, so the gain has to be steady across a
        // cycle of the probe. With a 1 ms release the follower lets go between
        // the peaks of a 220 Hz tone -- the gain then modulates within each
        // cycle, which raises the output and read 16:1 as 14.5:1. That is the
        // compressor behaving correctly and the harness asking the wrong
        // question. Switching peak-picking to RMS did not fix it, which is
        // what said the fault was in the time constants and not the amplitude.
        compressor.setAttackMs (1.0);
        compressor.setReleaseMs (200.0);

        // **RMS, never peak, for the amplitude of a sine** -- CLAUDE.md
        // section 10, and this harness proved it again. Peak-picking read
        // 16:1 as 14.484:1, because at a high ratio the envelope follower
        // ripples and the peak lands on the ripple rather than on the level.
        const auto steadyOutput = [&] (double inputDb)
        {
            compressor.reset();

            const double amplitude = tezla::dsp::dbToGain (inputDb);
            double sumOfSquares = 0.0;
            std::size_t counted = 0;

            for (std::size_t n = 0; n < static_cast<std::size_t> (0.5 * rate); ++n)
            {
                const double phase = 2.0 * std::numbers::pi * 220.0
                                       * static_cast<double> (n) / rate;
                const double out = compressor.process (amplitude * std::sin (phase));

                if (n > static_cast<std::size_t> (0.4 * rate))
                {
                    sumOfSquares += out * out;
                    ++counted;
                }
            }

            const double rms = std::sqrt (sumOfSquares / static_cast<double> (counted));

            return 20.0 * std::log10 (rms * std::numbers::sqrt2);
        };

        const double lowOut = steadyOutput (-20.0);
        const double highOut = steadyOutput (-8.0);
        const double measured = 12.0 / (highOut - lowOut);

        std::printf ("  %6.1f:1 %12.3f:1 %10.3f\n", ratio, measured, measured - ratio);
    }

    // -----------------------------------------------------------------------
    // Gate: the hysteresis window
    // -----------------------------------------------------------------------

    std::printf ("\nGate: transitions on a tone sitting ON the threshold\n");
    std::printf ("  A vocal tail sits at whatever threshold you set, because that is how\n");
    std::printf ("  you set it. Counted over two seconds of a 400 Hz tone wobbling\n");
    std::printf ("  +/-0.5 dB at 5 Hz, centred exactly on -30 dB.\n\n");
    std::printf ("  %-14s %-12s %12s\n", "hysteresis", "hold", "transitions");

    for (const auto& [hysteresisDb, holdMs] : { std::pair { 0.0, 0.0 },
                                               std::pair { 0.0, 40.0 },
                                               std::pair { 3.0, 0.0 },
                                               std::pair { 3.0, 40.0 } })
    {
        Gate gate;
        gate.prepare (rate);
        gate.setThresholdDb (-30.0);
        gate.setHysteresisDb (hysteresisDb);
        gate.setRangeDb (24.0);
        gate.setHoldMs (holdMs);
        gate.setAttackMs (1.0);
        gate.setReleaseMs (50.0);

        int transitions = 0;
        bool wasOpen = gate.isOpen();

        for (std::size_t n = 0; n < static_cast<std::size_t> (2.0 * rate); ++n)
        {
            const double t = static_cast<double> (n) / rate;
            const double wobbleDb = 0.5 * std::sin (2.0 * std::numbers::pi * 5.0 * t);
            const double amplitude = tezla::dsp::dbToGain (-30.0 + wobbleDb);

            (void) gate.process (amplitude
                                   * std::sin (2.0 * std::numbers::pi * 400.0 * t));

            if (gate.isOpen() != wasOpen)
            {
                ++transitions;
                wasOpen = gate.isOpen();
            }
        }

        std::printf ("  %9.1f dB %9.0f ms %12d\n", hysteresisDb, holdMs, transitions);
    }

    // -----------------------------------------------------------------------
    // Section 7: the strip is permanently in the path
    // -----------------------------------------------------------------------

    std::printf ("\nNeutral is the identity, bit for bit\n");

    {
        PhonossEngine engine;
        engine.prepare (rate);
        engine.setSettings (PhonossEngine::Settings {});

        std::size_t exact = 0;
        std::size_t total = 0;
        double worst = 0.0;

        for (int i = -20000; i <= 20000; ++i)
        {
            double left = static_cast<double> (i) / 19997.0;
            double right = -left * 0.37;
            const double original = left;

            engine.process (&left, &right, 1);
            ++total;

            if (tezla::dsp::isExactly (left, original))
                ++exact;

            worst = std::max (worst, std::abs (left - original));
        }

        std::printf ("  %zu of %zu samples bit-identical, worst difference %.3e\n",
                     exact, total, worst);
        std::printf ("  isIdentity() reports %s\n",
                     PhonossEngine::isIdentity (PhonossEngine::Settings {}) ? "true" : "false");
    }

    // -----------------------------------------------------------------------
    // CPU
    // -----------------------------------------------------------------------

    std::printf ("\nCPU, stereo, 480-sample blocks\n");

    {
        const auto costOf = [&] (const char* label, const PhonossEngine::Settings& settings)
        {
            PhonossEngine engine;
            engine.prepare (rate);

            constexpr int kBlock = 480;
            const int blocks = static_cast<int> (rate) / kBlock;

            std::vector<double> source (static_cast<std::size_t> (blocks * kBlock));

            for (std::size_t n = 0; n < source.size(); ++n)
                source[n] = voiceAt (n, rate, -12.0, n % 19200 < 3840);

            std::vector<double> left (kBlock, 0.0);
            std::vector<double> right (kBlock, 0.0);
            double sink = 0.0;

            const auto start = std::chrono::steady_clock::now();

            for (int block = 0; block < blocks; ++block)
            {
                const auto* from = source.data() + static_cast<std::size_t> (block) * kBlock;
                std::copy (from, from + kBlock, left.begin());
                std::copy (from, from + kBlock, right.begin());

                engine.setSettings (settings);
                engine.process (left.data(), right.data(), kBlock);
                sink += left[0];
            }

            const auto elapsed = std::chrono::duration<double> (
                std::chrono::steady_clock::now() - start).count();

            std::printf ("  %-40s %6.2f%%   (sink %g)\n", label,
                         100.0 * elapsed * rate / static_cast<double> (blocks * kBlock),
                         sink);
        };

        costOf ("everything neutral", PhonossEngine::Settings {});

        PhonossEngine::Settings working;
        working.highpassHz = 90.0;
        working.gate.thresholdDb = -40.0;
        working.gate.rangeDb = 18.0;
        working.deEss.rangeDb = 9.0;
        working.deEss.thresholdDb = -12.0;
        working.leveller.thresholdDb = -24.0;
        working.leveller.ratio = 2.5;
        working.leveller.makeupDb = 3.0;
        working.leveller.programDependent = true;
        working.peak.thresholdDb = -12.0;
        working.peak.ratio = 6.0;
        working.peak.attackMs = 2.0;
        working.peak.releaseMs = 80.0;
        working.eq.lowShelfDb = -2.0;
        working.eq.midDb = 1.5;
        working.eq.highShelfDb = 2.5;
        working.outputTrimDb = -1.0;

        costOf ("every stage working", working);
    }

    if (! args.outPath.empty())
        std::printf ("\n  (--out is accepted for consistency; this command prints tables "
                     "rather than writing a CSV.)\n");

    return 0;
}

// ---------------------------------------------------------------------------
// membrana
// ---------------------------------------------------------------------------

namespace membranaMeasure {

using tezla::membrana::MembranaEngine;

/// Steady-state RMS gain of a sine through a freshly prepared engine.
double engineSineGainDb (const MembranaEngine::Settings& settings, double rate,
                         double hz, double peak, double settleSeconds)
{
    MembranaEngine engine;
    engine.prepare (rate);
    engine.setSettings (settings);

    const double dPhase = 2.0 * std::numbers::pi * hz / rate;
    double phase = 0.0, energyIn = 0.0, energyOut = 0.0;
    std::vector<double> left (512), right (512), input (512);
    const auto settle = static_cast<int> (settleSeconds * rate);
    const auto measure = static_cast<int> (0.5 * rate);
    int done = 0;

    while (done < settle + measure)
    {
        for (int i = 0; i < 512; ++i)
        {
            input[static_cast<std::size_t> (i)] = peak * std::sin (phase);
            left[static_cast<std::size_t> (i)] = right[static_cast<std::size_t> (i)]
                = input[static_cast<std::size_t> (i)];
            phase += dPhase;
        }

        engine.process (left.data(), right.data(), 512);

        for (int i = 0; i < 512; ++i)
            if (done + i >= settle)
            {
                energyIn += input[static_cast<std::size_t> (i)] * input[static_cast<std::size_t> (i)];
                energyOut += left[static_cast<std::size_t> (i)] * left[static_cast<std::size_t> (i)];
            }

        done += 512;
    }

    return 10.0 * std::log10 (energyOut / energyIn);
}

} // namespace membranaMeasure

int runMembrana (const Args& args)
{
    using namespace membranaMeasure;
    using tezla::membrana::CapsuleEq;
    using tezla::membrana::DetailLift;
    using tezla::membrana::MembranaEngine;
    using tezla::membrana::MicPattern;
    using tezla::membrana::SphereDiffraction;

    const double rate = args.sampleRate > 0.0 ? args.sampleRate : 48000.0;

    std::printf ("tezla-measure membrana -- %.0f Hz\n\n", rate);

    // -----------------------------------------------------------------------
    std::printf ("Proximity against the closed form (cardioid, on axis)\n");
    std::printf ("  boost_dB(f) = 10 log10(1 + (G c / (2 pi f r D))^2), corner = G c / (2 pi r D)\n\n");

    for (double r : { 0.05, 0.10 })
    {
        std::printf ("  r = %2.0f cm, corner %6.1f Hz:", r * 100.0,
                     MicPattern::cornerHz (0.5, 1.0, r));

        for (double f : { 100.0, 546.0, 2000.0 })
            std::printf ("   %5.0f Hz %+7.3f dB", f, MicPattern::boostDb (0.5, 1.0, r, f));

        std::printf ("\n");
    }

    std::printf ("  omni, any distance:                %5.0f Hz %+7.3f dB (exact zero by predicate)\n\n",
                 100.0, MicPattern::boostDb (1.0, 1.0, 0.05, 100.0));

    // -----------------------------------------------------------------------
    std::printf ("Sphere limits against Duda & Martens (rho = 100 unless said)\n\n");
    std::printf ("  on axis, mu = 1:            %+7.3f dB   (paper: about +3)\n",
                 SphereDiffraction::magnitudeDb (100.0, 1.0, 1.0));
    std::printf ("  on axis, mu = 30:           %+7.3f dB   (paper: +6.02 limit)\n",
                 SphereDiffraction::magnitudeDb (100.0, 30.0, 1.0));
    std::printf ("  150 degrees, mu = 30:       %+7.3f dB   (paper: about -13)\n",
                 SphereDiffraction::magnitudeDb (100.0, 30.0, std::cos (150.0 * std::numbers::pi / 180.0)));

    const auto rise = [] (double rho)
    {
        return SphereDiffraction::magnitudeDb (rho, 60.0, 1.0)
               - SphereDiffraction::magnitudeDb (rho, 0.1, 1.0);
    };

    std::printf ("  LF-to-HF rise, rho = 1.25:  %+7.3f dB   (paper: about +2)\n", rise (1.25));
    std::printf ("  LF-to-HF rise, rho = 100:   %+7.3f dB   (paper: about +6)\n\n", rise (100.0));

    // -----------------------------------------------------------------------
    std::printf ("Capsule EQ fit (rendered coefficients against the analytic target)\n\n");

    {
        struct Config { const char* name; double pattern, body, character, grille, grilleHz, distance, axis; };
        constexpr Config configs[] = {
            { "cardioid 5 cm",       0.5, 50.0, 0.35, 0.0, 7000.0, 0.05, 0.0 },
            { "char 100, 60 mm 2 cm", 0.5, 60.0, 1.00, 0.0, 7000.0, 0.02, 0.0 },
            { "grille full at 9 kHz", 0.5, 50.0, 0.00, 1.0, 9000.0, 1.00, 0.0 },
        };

        for (const auto& config : configs)
        {
            tezla::membrana::CapsuleEq eq;
            eq.setPattern (config.pattern);
            eq.setBodyMm (config.body);
            eq.setCharacter (config.character);
            eq.setGrille (config.grille, config.grilleHz);
            eq.setPosition (config.distance, config.axis);
            eq.setLowLimitHz (40.0);
            eq.setAutoLevel (true);
            eq.prepare (rate);

            double worst = 0.0, worstHz = 0.0;

            for (double hz = 700.0; hz <= 20000.0 && hz < rate * 0.45; hz *= 1.06)
            {
                const double error = std::abs (eq.renderedDbAt (hz) - eq.targetDbAt (hz));

                if (error > worst)
                {
                    worst = error;
                    worstHz = hz;
                }
            }

            std::printf ("  %-22s worst %6.4f dB at %5.0f Hz, 1 kHz hold %+9.6f dB\n",
                         config.name, worst, worstHz, eq.renderedDbAt (1000.0));
        }

        std::printf ("\n");
    }

    // -----------------------------------------------------------------------
    std::printf ("Presence curve (amount 6 dB, threshold -28, knee 12, track 1)\n");
    std::printf ("  lift = amount * ((1 - track) + track * s(clamp01((T - L) / 12)))\n\n");

    for (double level : { -16.0, -28.0, -34.0, -40.0, -52.0 })
    {
        double xc = (-28.0 - level) / 12.0;
        xc = xc < 0.0 ? 0.0 : xc > 1.0 ? 1.0 : xc;
        const double eased = xc * xc * (3.0 - 2.0 * xc);
        std::printf ("  level %4.0f dB -> lift %6.3f dB\n", level, 6.0 * eased);
    }

    std::printf ("\n");

    // -----------------------------------------------------------------------
    std::printf ("Detail window (amount 6 dB, floor -55: T_d = -35, knee 15, ease 6)\n\n");

    for (double level : { -30.0, -40.0, -45.0, -50.0, -55.0, -70.0 })
        std::printf ("  level %4.0f dB -> lift %6.3f dB%s\n", level,
                     tezla::membrana::DetailLift::curveLiftDb (6.0, -55.0, level),
                     level <= -70.0 ? "   (hiss: the floor holds)" : "");

    std::printf ("\n");

    // -----------------------------------------------------------------------
    std::printf ("Whole engine\n\n");

    {
        MembranaEngine::Settings neutral;
        std::printf ("  all-neutral identity:      %s\n",
                     MembranaEngine::isIdentity (neutral) ? "bit-exact (predicate)" : "NOT IDENTITY -- BROKEN");

        MembranaEngine::Settings close;
        close.mic.distanceCm = 8.0;
        close.mic.character01 = 1.0;
        std::printf ("  autoLevel 1 kHz at 8 cm:   %+8.4f dB (should be 0)\n",
                     engineSineGainDb (close, rate, 1000.0, 0.1, 0.5));

        MembranaEngine::Settings shelf;
        shelf.presence.amountDb = 6.0;
        shelf.presence.track01 = 0.0;
        shelf.presence.frequencyHz = 1000.0;
        std::printf ("  static presence 6 dB, 16k: %+8.4f dB (asymptote 6)\n",
                     engineSineGainDb (shelf, rate, 16000.0, 0.1, 3.0));
    }

    // -----------------------------------------------------------------------
    std::printf ("\nCPU, everything engaged, stereo, 480-sample blocks\n\n");

    {
        MembranaEngine engine;
        MembranaEngine::Settings settings;
        settings.mic.distanceCm = 8.0;
        settings.mic.character01 = 1.0;
        settings.mic.grille01 = 0.5;
        settings.presence.amountDb = 6.0;
        settings.detail.amountDb = 8.0;
        settings.outputDb = 1.0;
        engine.prepare (rate);
        engine.setSettings (settings);

        std::vector<double> left (480), right (480);
        double phase = 0.0;
        const double dPhase = 2.0 * std::numbers::pi * 800.0 / rate;
        const int blocks = 2000;

        const auto start = std::chrono::steady_clock::now();

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < 480; ++i)
            {
                left[static_cast<std::size_t> (i)] = right[static_cast<std::size_t> (i)]
                    = 0.05 * std::sin (phase);
                phase += dPhase;
            }

            engine.process (left.data(), right.data(), 480);
        }

        const auto stop = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double> (stop - start).count();
        const double audioSeconds = blocks * 480.0 / rate;

        std::printf ("  %.2f%% of one core (%.3f s processing for %.1f s of audio)\n",
                     100.0 * seconds / audioSeconds, seconds, audioSeconds);
    }

    if (! args.outPath.empty())
        std::printf ("\n  (--out is accepted for consistency; this command prints tables "
                     "rather than writing a CSV.)\n");

    return 0;
}

// ---------------------------------------------------------------------------
// ictus
// ---------------------------------------------------------------------------

namespace ictusMeasure
{
using namespace tezla::ictus;

/// One hit of `settings` through the whole engine at a host rate; the left
/// channel comes back.
std::vector<double> renderHit (const KickSettings& settings, double rate, double seconds,
                               tezla::dsp::OversamplingMode mode = tezla::dsp::OversamplingMode::Auto,
                               int* factorOut = nullptr, int* latencyOut = nullptr)
{
    auto engine = std::make_unique<Engine>();
    engine->prepare (rate, 512);

    EngineParameters parameters;
    parameters.kick1 = settings;
    parameters.oversampling = mode;
    engine->setParameters (parameters);

    const int total = static_cast<int> (seconds * rate);
    std::vector<double> left (static_cast<std::size_t> (total), 0.0);
    std::vector<double> right (static_cast<std::size_t> (total), 0.0);

    engine->noteOn (36, 1.0);

    int done = 0;

    while (done < total)
    {
        const int take = std::min (512, total - done);
        double* block[2] = { left.data() + done, right.data() + done };
        engine->process (block, take);
        done += take;
    }

    if (factorOut != nullptr)
        *factorOut = engine->getOversamplingFactor();

    if (latencyOut != nullptr)
        *latencyOut = engine->getLatencySamples();

    return left;
}

std::vector<double> crossings (const std::vector<double>& x, double rate)
{
    std::vector<double> out;

    // Only from inside the first lobe: a linear-phase decimator pre-rings
    // before the hit -- at -100 dB far out, at -40 dB right at the onset --
    // and that ripple crosses zero. Counting it misaligned every cycle that
    // followed (10,000 cents on the first run, one whole cycle on the
    // second, while the undecimated 192 kHz case read 0.015). Detection
    // starts where the signal first reaches a tenth of its peak, which is
    // inside the body's first half-cycle, so the first crossing counted is
    // the end of the first cycle at every rate.
    double peak = 0.0;
    for (const double v : x)
        peak = std::max (peak, std::abs (v));

    std::size_t onset = x.size();
    for (std::size_t n = 0; n < x.size(); ++n)
        if (std::abs (x[n]) > 0.1 * peak)
        {
            onset = n;
            break;
        }

    for (std::size_t n = onset + 1; n < x.size(); ++n)
        if (x[n - 1] < 0.0 && x[n] >= 0.0)
        {
            const double frac = -x[n - 1] / (x[n] - x[n - 1]);
            out.push_back ((static_cast<double> (n - 1) + frac) / rate);
        }

    return out;
}

double closedFormHz (const KickSettings& s, double t)
{
    const double dropTau = s.dropSeconds / tezla::dsp::TensionDrop::kLandFactor;
    const double sighTau = s.sighSeconds / tezla::dsp::TensionDrop::kLandFactor;
    const double cents = 100.0 * s.startSemitones * std::exp (-t / dropTau)
                       + 100.0 * s.sighSemitones * std::exp (-t / sighTau);
    return s.tuneHz * std::exp2 (cents / 1200.0);
}

double peakOf (const std::vector<double>& x)
{
    double peak = 0.0;
    for (const double v : x)
        peak = std::max (peak, std::abs (v));
    return peak;
}
} // namespace ictusMeasure

int runIctus (const Args& args)
{
    using namespace tezla;
    using namespace ictusMeasure;

    const double rate = args.sampleRate;

    std::printf ("tezla-measure ictus -- table 1, the kick\n\n");

    // ---- 1. the pitch trajectory at four host rates ----------------------

    KickSettings kick;
    kick.tuneHz = 50.0;
    kick.startSemitones = 30.0;
    kick.dropSeconds = 0.03;
    kick.sighSemitones = 1.5;
    kick.sighSeconds = 0.5;
    kick.decaySeconds = 1.0;
    kick.level = 1.0;
    kick.velocityDrop = 0.0;

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    std::printf ("Pitch of the default kick body (50 Hz landed, +30 st drop over 30 ms,\n");
    std::printf ("+1.5 st sigh over 0.5 s), cycle by cycle from zero crossings, against the\n");
    std::printf ("closed form's period over the same cycle. Auto oversampling: x4, x4, x2, x1.\n\n");

    std::vector<std::vector<double>> allTimes;

    for (const double r : rates)
    {
        int factor = 0;
        int latency = 0;
        const auto out = renderHit (kick, r, 0.6, dsp::OversamplingMode::Auto, &factor, &latency);
        auto times = crossings (out, r);
        for (auto& t : times)
            t -= static_cast<double> (latency) / r;
        allTimes.push_back (std::move (times));
    }

    // The closed form's crossings: its phase integral reaching whole cycles.
    std::vector<double> predicted;
    {
        constexpr double dt = 1.0e-6;
        double phase = 0.0;
        double next = 1.0;
        double previous = closedFormHz (kick, 0.0);

        for (double t = 0.0; t < 0.6; t += dt)
        {
            const double hz = closedFormHz (kick, t + dt);
            phase += 0.5 * (previous + hz) * dt;
            previous = hz;

            if (phase >= next)
            {
                predicted.push_back (t + dt - (phase - next) / (hz * dt) * dt);
                next += 1.0;
            }
        }
    }

    std::printf ("  %5s %9s %10s |", "cycle", "starts ms", "closed Hz");
    for (const double r : rates)
        std::printf (" %8.0fk %7s", r / 1000.0, "cents");
    std::printf (" | spread c\n");

    std::size_t cycles = predicted.size();
    for (const auto& times : allTimes)
        cycles = std::min (cycles, times.size());

    double worstCents = 0.0;
    double worstSpread = 0.0;

    for (std::size_t k = 0; k + 1 < cycles; ++k)
    {
        const double closedPeriod = predicted[k + 1] - predicted[k];
        const bool show = k < 8 || k % 10 == 9 || k + 2 == cycles;

        if (show)
            std::printf ("  %5zu %9.2f %10.3f |", k + 1, 1000.0 * predicted[k], 1.0 / closedPeriod);

        double lo = 1.0e9;
        double hi = -1.0e9;

        for (const auto& times : allTimes)
        {
            const double period = times[k + 1] - times[k];
            const double cents = 1200.0 * std::log2 (closedPeriod / period);
            lo = std::min (lo, cents);
            hi = std::max (hi, cents);
            worstCents = std::max (worstCents, std::abs (cents));

            if (show)
                std::printf (" %9.3f %7.3f", 1.0 / period, cents);
        }

        worstSpread = std::max (worstSpread, hi - lo);

        if (show)
            std::printf (" | %7.3f\n", hi - lo);
    }

    std::printf ("\n  worst over %zu cycles: %.3f cents against the closed form, %.3f cents between rates\n",
                 cycles - 1, worstCents, worstSpread);

    // ---- 2. rise time and the sub-band share -----------------------------

    {
        KickSettings s;   // the defaults
        const auto out = renderHit (s, rate, 1.0);
        const double peak = peakOf (out);

        int rise = 0;
        for (std::size_t n = 0; n < out.size(); ++n)
            if (std::abs (out[n]) >= 0.707 * peak)
            {
                rise = static_cast<int> (n);
                break;
            }

        // Energy below 80 Hz over the whole hit, from a zero-padded FFT.
        std::vector<double> padded (1u << 16, 0.0);
        std::copy (out.begin(), out.begin() + std::min (out.size(), padded.size()), padded.begin());
        const auto spectrum = measure::fftOfReal (padded);
        const double binWidth = rate / static_cast<double> (padded.size());

        double sub = 0.0;
        double total = 0.0;

        for (std::size_t bin = 1; bin < spectrum.size(); ++bin)
        {
            const double power = std::norm (spectrum[bin]);
            total += power;
            if (static_cast<double> (bin) * binWidth < 80.0)
                sub += power;
        }

        std::printf ("\nDefault kick at %.0f Hz: peak %.3f, rise to -3 dB in %.2f ms, %.1f%% of the energy below 80 Hz\n",
                     rate, peak, 1000.0 * rise / rate, 100.0 * sub / total);
    }

    // ---- 3. the even curve's DC, after the blocker, per corner -------------

    {
        std::printf ("\nEven harmonics at full (a DC pedestal by construction) through the per-hit blocker:\n");
        std::printf ("the largest 50 ms mean of the output, relative to its peak.\n");

        for (const double corner : { 5.0, 10.0, 20.0, 40.0 })
        {
            KickSettings s;
            s.harmonics = 1.0;
            s.even = 1.0;
            s.dcBlockerHz = corner;

            const auto out = renderHit (s, rate, 1.0);
            const double peak = peakOf (out);
            const auto window = static_cast<std::size_t> (0.05 * rate);

            double worst = 0.0;
            for (std::size_t n = 0; n + window <= out.size(); n += window / 4)
            {
                double mean = 0.0;
                for (std::size_t i = 0; i < window; ++i)
                    mean += out[n + i];
                worst = std::max (worst, std::abs (mean / static_cast<double> (window)));
            }

            std::printf ("  blocker %4.0f Hz: DC bump %7.2f dB re peak\n", corner,
                         20.0 * std::log10 (std::max (worst, 1.0e-12) / peak));
        }
    }

    // ---- 4. the harmonics stage's inharmonic floor at the internal rate ---

    {
        constexpr double internalRate = 192000.0;
        constexpr std::size_t fftSize = 1u << 16;
        const double hz = measure::binExactFrequency (55.0, internalRate, fftSize);
        const auto input = measure::sine (hz, 1.0, internalRate, fftSize);

        dsp::SoftEven even { KickEngine::kShaperGainAtFull };
        dsp::SoftOdd odd { KickEngine::kShaperGainAtFull };
        dsp::Adaa1<dsp::SoftEven> adaaEven;
        dsp::Adaa1<dsp::SoftOdd> adaaOdd;
        dsp::DcBlocker<double> blocker;
        blocker.prepare (internalRate, 10.0);

        // Two windows in, the second analysed: the blocker's settling from the
        // onset is a non-harmonic component the analyser would otherwise count.
        std::vector<double> output (fftSize);
        for (std::size_t pass = 0; pass < 2; ++pass)
            for (std::size_t i = 0; i < fftSize; ++i)
            {
                const double x = input[i];
                const double shaped = 0.5 * adaaEven.process (x, even) + 0.5 * adaaOdd.process (x, odd);
                output[i] = blocker.process (x + shaped);
            }

        const auto report = measure::analyseHarmonics (output, internalRate, hz, 12);
        std::printf ("\nHarmonics stage at full, 55 Hz full scale at 192 kHz: THD %.1f dB, inharmonic %.1f dB (%.1f dB audible)\n",
                     report.thdDb, report.aliasingDb, report.audibleAliasingDb);
    }

    // ---- 5. CPU ---------------------------------------------------------

    {
        KickEngine engine;
        engine.prepare (192000.0);

        KickSettings s;
        s.harmonics = 0.7;
        s.toneEnabled = true;
        s.click = 0.5;
        s.clickNoise = 0.4;
        s.tailMix = 0.5;
        s.decaySeconds = 2.0;

        constexpr int samples = 192000;
        double sink = 0.0;

        const auto time = [&] (const KickSettings& settings)
        {
            engine.start (settings, 50.0, 1.0, 1234, 0);
            const auto start = std::chrono::steady_clock::now();

            for (int n = 0; n < samples; ++n)
            {
                if (n % Engine::kControlIntervalSamples == 0)
                    engine.advanceControl (Engine::kControlIntervalSamples);
                sink += engine.process();
            }

            return 1.0e9 * std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count()
                 / samples;
        };

        KickSettings neutral;
        neutral.decaySeconds = 2.0;

        const double nsFull = time (s);
        const double nsNeutral = time (neutral);

        std::printf ("\nKick engine, one second at 192 kHz: %.1f ns/sample everything on, %.1f ns/sample neutral"
                     " (%.2f%% / %.2f%% of a core at 192 kHz; sink %g)\n",
                     nsFull, nsNeutral, nsFull * 192000.0 / 1.0e7, nsNeutral * 192000.0 / 1.0e7, sink);
    }

    // ---- 6. a WAV to listen to ------------------------------------------

    if (! args.outPath.empty())
    {
        KickSettings s;
        s.harmonics = 0.4;
        s.click = 0.4;
        s.clickNoise = 0.3;
        s.toneEnabled = true;

        const auto out = renderHit (s, rate, 1.5);

        if (measure::writeWav (args.outPath, { out }, rate))
            std::printf ("\nWrote %s (one hit, %.0f Hz, harmonics 0.4, click 0.4, tone x8)\n", args.outPath.c_str(), rate);
        else
            std::printf ("\nCould not write %s\n", args.outPath.c_str());
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
    std::printf ("  sonitus-stress  [--fs --voices N --unison N --seconds S --os off|x2|x4|x8|all]\n");
    std::printf ("                          the CPU worst case per oversampling factor, ms per second of audio\n");
    std::printf ("  svarayantra     [--fs]  soundfont engine aliasing and CPU per voice\n");
    std::printf ("  ferrite         [--fs --out FILE]  tape loops, losses, aliasing, wobble, CPU\n");
    std::printf ("  malleus         [--fs --out FILE]  mode tables, lock, decay, bow onset, CPU\n");
    std::printf ("  crossbar        [--fs --out FILE]  DTMF accuracy, cadences, G.711 SNR, CPU\n");
    std::printf ("  phonoss          [--fs --out FILE]  de-ess level independence, ratios, gate, CPU\n");
    std::printf ("  membrana        [--fs --out FILE]  proximity, sphere limits, fit, curves, CPU\n");
    std::printf ("  ictus           [--fs --out FILE]  kick pitch trajectory at four rates, DC, aliasing, CPU\n");
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
    if (command == "sonitus-stress")  return runSonitusStress (args);
    if (command == "svarayantra")     return runSvarayantra (args);
    if (command == "ferrite")         return runFerrite (args);
    if (command == "malleus")         return runMalleus (args);
    if (command == "crossbar")        return runCrossbar (args);
    if (command == "phonoss")          return runPhonoss (args);
    if (command == "membrana")        return runMembrana (args);
    if (command == "ictus")           return runIctus (args);

    printUsage();
    return 1;
}
