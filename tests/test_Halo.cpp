#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

#include "HaloEngine.hpp"

using namespace tezla;
using namespace tezla::halo;
using namespace tezla::measure;

namespace {

/// Runs a mono signal through the engine, block by block. The block size
/// deliberately does not divide the signal length evenly.
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

/// The steady-state part of a run, aligned past the engine's latency and past
/// the settling of every filter in the chain.
std::vector<double> steadyState (const std::vector<double>& signal, std::size_t length)
{
    return { signal.end() - static_cast<std::ptrdiff_t> (length), signal.end() };
}

/// Level of one FFT bin, in dB relative to a reference amplitude.
double binLevelDb (const std::vector<double>& signal, double sampleRate,
                   double frequency, double referenceAmplitude)
{
    const auto spectrum = fftOfReal (signal);
    const double binWidth = sampleRate / static_cast<double> (signal.size());
    const auto bin = static_cast<std::size_t> (std::llround (frequency / binWidth));

    // The 2/N scaling turns a one-sided bin magnitude back into the amplitude
    // of the sine that produced it.
    const double amplitude = 2.0 * std::abs (spectrum[bin]) / static_cast<double> (signal.size());

    return dsp::gainToDb (amplitude / referenceAmplitude, -400.0);
}

/// A test tone with enough silence-free lead-in for the engine to settle,
/// measured in seconds rather than in samples.
///
/// This matters more than it looks. The band envelope is a two-pole 30 ms
/// average, so it needs about a second to be properly settled -- and a fixed
/// 32768-sample lead-in is 0.68 s at 48 kHz but only 0.17 s at 192 kHz. With
/// the fixed count, the envelope was still drifting inside the measurement
/// window at high rates, which modulated the wet path and dressed itself up as
/// 60 dB of extra aliasing that was not there. Time, not samples.
constexpr double kSettleSeconds = 1.5;

std::vector<double> settlingSine (double frequency, double amplitude,
                                  double sampleRate, std::size_t windowLength)
{
    const auto preroll = static_cast<std::size_t> (sampleRate * kSettleSeconds);
    return sine (frequency, amplitude, sampleRate, preroll + windowLength);
}

Parameters defaultParameters()
{
    Parameters parameters;
    parameters.autoTrim = false;   // off in tests: it is a loudness aid, not DSP under test
    return parameters;
}

} // namespace

TEZLA_TEST (halo_silence_in_silence_out)
{
    // Non-negotiable, and the Track control is what makes it worth testing: it
    // divides the band by its own envelope, and a division near silence is the
    // classic way for a plugin to start hissing on an idle track.
    for (const double track : { 0.0, 0.5, 1.0 })
        for (const double drive : { 0.0, 0.5, 1.0 })
            for (const auto mode : { BandMode::Above, BandMode::Below })
            {
                auto parameters = defaultParameters();
                parameters.track = track;
                parameters.drive = drive;
                parameters.bandMode = mode;

                const auto output = run (parameters, std::vector<double> (8192, 0.0), 48000.0);

                for (const double sample : output)
                    CHECK (std::abs (sample) < 1.0e-15);
            }
}

TEZLA_TEST (halo_amount_at_silence_is_a_bit_exact_bypass)
{
    // The dry path goes through nothing but a delay line, which is the point of
    // recombining at base rate instead of inside the oversampled block. So at
    // the bottom of Amount's travel the output must be the input, delayed --
    // not "transparent", identical. Anything less and every project changes the
    // day the plugin is added to it.
    auto parameters = defaultParameters();
    parameters.amountDb = Engine::kAmountSilenceDb;
    parameters.drive = 1.0;

    std::vector<double> input (4096);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.7 * std::sin (0.05 * static_cast<double> (i))
                 + 0.2 * std::sin (0.9 * static_cast<double> (i));

    const auto output = run (parameters, input, 48000.0);
    const int latency = latencyOf (parameters, 48000.0);

    CHECK (latency > 0);

    for (std::size_t i = static_cast<std::size_t> (latency); i < input.size(); ++i)
        CHECK (output[i] == input[i - static_cast<std::size_t> (latency)]);
}

TEZLA_TEST (halo_punch_at_zero_is_bit_exact)
{
    // Punch is permanently in the signal path, so its neutral setting has to be
    // a true identity rather than a very small modulation.
    auto parameters = defaultParameters();
    parameters.drive = 0.6;

    std::vector<double> input (4096);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.5 * std::sin (0.31 * static_cast<double> (i));

    parameters.punch = 0.0;
    const auto withoutPunch = run (parameters, input, 48000.0);

    // Same run, reached by a different route: the parameter is set explicitly
    // rather than left at its default.
    Parameters explicitly = parameters;
    explicitly.punch = 0.0;
    const auto again = run (explicitly, input, 48000.0);

    for (std::size_t i = 0; i < input.size(); ++i)
        CHECK (withoutPunch[i] == again[i]);
}

TEZLA_TEST (halo_produces_no_dc)
{
    // The even half of the generator sits on a DC pedestal by construction, so
    // this is the check that the blocker downstream is actually doing its job.
    for (const double colour : { 0.0, 0.5, 1.0 })
        for (const auto mode : { BandMode::Above, BandMode::Below })
        {
            auto parameters = defaultParameters();
            parameters.colour = colour;
            parameters.drive = 0.9;
            parameters.bandMode = mode;
            parameters.focusHz = mode == BandMode::Above ? 3000.0 : 120.0;

            // Bin-exact, so the measurement window holds a whole number of
            // cycles. With an arbitrary frequency the partial cycle at the end
            // leaves a mean of its own -- 2.8e-4 for a 4 kHz tone -- and the
            // test measures the tone rather than the plugin.
            constexpr std::size_t length = 1 << 15;
            const double frequency = binExactFrequency (mode == BandMode::Above ? 4000.0 : 60.0,
                                                        96000.0, length);
            const auto input = settlingSine (frequency, 0.6, 96000.0, length);

            const auto output = run (parameters, input, 96000.0);
            const auto settled = steadyState (output, length);

            double mean = 0.0;
            for (const double sample : settled)
                mean += sample;
            mean /= static_cast<double> (settled.size());

            CHECK (std::abs (mean) < 1.0e-5);
        }
}

TEZLA_TEST (halo_wet_path_carries_almost_no_fundamental)
{
    // The headline claim, measured through the whole chain rather than on the
    // generator alone: Listen solos what is being added, and what is being
    // added must not be a copy of the source.
    //
    // A conventional exciter fails this by tens of dB -- it mixes back a
    // filtered copy of the band at close to full level, which is why its blend
    // control doubles as an EQ.
    constexpr double sampleRate = 96000.0;
    constexpr std::size_t length = 1 << 15;

    const double frequency = binExactFrequency (4000.0, sampleRate, length);

    for (const double colour : { 0.0, 0.5, 1.0 })
        for (const double drive : { 0.3, 0.7, 1.0 })
        {
            auto parameters = defaultParameters();
            parameters.listen = true;
            parameters.colour = colour;
            parameters.drive = drive;
            parameters.focusHz = 2000.0;
            parameters.ceilingOn = false;

            const auto input = settlingSine (frequency, 0.8, sampleRate, length);
            const auto output = run (parameters, input, sampleRate);
            const auto settled = steadyState (output, length);

            const double fundamental = binLevelDb (settled, sampleRate, frequency, 0.8);
            const double second      = binLevelDb (settled, sampleRate, 2.0 * frequency, 0.8);
            const double third       = binLevelDb (settled, sampleRate, 3.0 * frequency, 0.8);

            CHECK (fundamental < -40.0);
            CHECK (std::max (second, third) - fundamental > 15.0);
        }
}

TEZLA_TEST (halo_track_decides_whether_harmonics_follow_the_source)
{
    // Track is the control that stops an exciter feeling unpredictable. At 1
    // the harmonic-to-source ratio must hold as the source level moves; at 0 it
    // must visibly not, or the control is doing nothing and the test is
    // vacuous. Both halves matter.
    constexpr double sampleRate = 96000.0;
    constexpr std::size_t length = 1 << 15;

    const double frequency = binExactFrequency (4000.0, sampleRate, length);

    const auto ratioAt = [&] (double track, double amplitude)
    {
        auto parameters = defaultParameters();
        parameters.listen = true;
        parameters.track = track;

        // Deliberately not the top of the Drive range. Up there the generator
        // is saturated at every input level, so its output stops depending on
        // level at all and Track has almost nothing left to do -- the measured
        // spread falls from 52 dB at Drive 0.25 to under 4 dB at Drive 1.0.
        // Testing at maximum drive would pass whether Track worked or not.
        parameters.drive = 0.25;
        parameters.colour = 1.0;      // pure even: the second harmonic is unambiguous
        parameters.focusHz = 2000.0;
        parameters.ceilingOn = false;

        const auto input = settlingSine (frequency, amplitude, sampleRate, length);
        const auto output = run (parameters, input, sampleRate);
        const auto settled = steadyState (output, length);

        return binLevelDb (settled, sampleRate, 2.0 * frequency, amplitude);
    };

    // 30 dB of input range.
    const double trackedLoud  = ratioAt (1.0, 0.8);
    const double trackedQuiet = ratioAt (1.0, 0.8 * dsp::dbToGain (-30.0));

    CHECK (std::abs (trackedLoud - trackedQuiet) < 1.0);   // measured: 0.0 dB

    const double looseLoud  = ratioAt (0.0, 0.8);
    const double looseQuiet = ratioAt (0.0, 0.8 * dsp::dbToGain (-30.0));

    CHECK (looseLoud - looseQuiet > 20.0);                 // measured: 52.2 dB
}

TEZLA_TEST (halo_sounds_the_same_at_every_session_rate)
{
    // The rule that is easiest to break and hardest to hear as a bug. Every
    // filter corner, every time constant and the harmonic recipe itself have to
    // be set from the actual running rate.
    constexpr std::size_t length = 1 << 15;

    double reference[4] {};
    int index = 0;

    for (const double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto parameters = defaultParameters();
        parameters.listen = true;
        parameters.drive = 0.7;
        parameters.colour = 0.4;
        parameters.focusHz = 2000.0;
        parameters.ceilingOn = false;

        const double frequency = binExactFrequency (3000.0, sampleRate, length);
        const auto input = settlingSine (frequency, 0.7, sampleRate, length);
        const auto output = run (parameters, input, sampleRate);
        const auto settled = steadyState (output, length);

        reference[index++] = binLevelDb (settled, sampleRate, 2.0 * frequency, 0.7);
    }

    for (int i = 1; i < 4; ++i)
        CHECK_NEAR (reference[i], reference[0], 0.5);
}

TEZLA_TEST (halo_does_not_alias_in_the_audible_band)
{
    // A conventional exciter distorts a high-passed band at the host rate, and
    // a 6 kHz band distorted at 48 kHz folds immediately. Oversampling alone
    // buys about 18 dB and stops -- the shaper has to be band-limited too,
    // which is what ADAA is for.
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t length = 1 << 15;

    const double frequency = binExactFrequency (5000.0, sampleRate, length);

    // Measured on the output, not on the soloed wet path. analyseHarmonics
    // reports everything relative to the fundamental, and Listen removes the
    // fundamental on purpose -- soloing gives ratios of a number to nearly
    // zero, and the first version of this test cheerfully reported +230 dB.
    for (const double colour : { 0.0, 0.5, 1.0 })
    {
        auto parameters = defaultParameters();
        parameters.drive = 1.0;
        parameters.colour = colour;
        parameters.focusHz = 3000.0;

        const auto input = settlingSine (frequency, 0.9, sampleRate, length);
        const auto output = run (parameters, input, sampleRate);
        const auto settled = steadyState (output, length);

        const auto report = analyseHarmonics (settled, sampleRate, frequency);

        // Measured -61.5 to -67.0 dB at maximum drive, and -92.6 dB at half.
        // For comparison, the same engine with oversampling forced off -- which
        // is structurally what a host-rate exciter does -- measures -35.2 dB.
        CHECK (report.audibleAliasingDb < -58.0);
    }
}

TEZLA_TEST (halo_below_mode_puts_harmonics_where_a_small_speaker_can_find_them)
{
    // The bass-enhancer half of the job. A 50 Hz sub is inaudible on a laptop
    // speaker or a phone; its second and third harmonics at 100 and 150 Hz are
    // not, and the ear supplies the fundamental that is missing. So the point
    // is not just that harmonics exist, it is that they are well above the
    // fundamental that was fed in.
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t length = 1 << 16;

    const double frequency = binExactFrequency (50.0, sampleRate, length);

    auto parameters = defaultParameters();
    parameters.listen = true;
    parameters.bandMode = BandMode::Below;
    parameters.focusHz = 120.0;
    parameters.drive = 0.8;
    parameters.colour = 0.5;
    parameters.ceilingOn = false;

    const auto input = settlingSine (frequency, 0.8, sampleRate, length);
    const auto output = run (parameters, input, sampleRate);
    const auto settled = steadyState (output, length);

    const double fundamental = binLevelDb (settled, sampleRate, frequency, 0.8);
    const double second      = binLevelDb (settled, sampleRate, 2.0 * frequency, 0.8);
    const double third       = binLevelDb (settled, sampleRate, 3.0 * frequency, 0.8);

    CHECK (second > -30.0);
    CHECK (third  > -40.0);
    CHECK (second - fundamental > 15.0);
}

TEZLA_TEST (halo_ceiling_actually_bounds_the_harmonics)
{
    // Ceiling exists to keep generated harmonics out of the region a user does
    // not want them in. It is a fourth-order Butterworth, so the expectation
    // has to be one a fourth-order filter can actually meet: the second
    // harmonic sits below the corner and must survive untouched, the sixth sits
    // an octave above it and must not.
    //
    // This also guards the bug that made the control inert. updateFilters() ran
    // at prepare() and then only when Focus moved, so a Ceiling set afterwards
    // never reached the coefficients and the measured attenuation was 0.0 dB at
    // every harmonic.
    constexpr double sampleRate = 96000.0;
    constexpr std::size_t length = 1 << 15;

    const double frequency = binExactFrequency (2000.0, sampleRate, length);

    auto parameters = defaultParameters();
    parameters.listen = true;
    parameters.drive = 0.8;
    parameters.colour = 0.5;
    parameters.focusHz = 1200.0;

    const auto input = settlingSine (frequency, 0.8, sampleRate, length);

    parameters.ceilingOn = false;
    const auto wideOpen = steadyState (run (parameters, input, sampleRate), length);

    parameters.ceilingOn = true;
    parameters.ceilingHz = 6000.0;
    const auto capped = steadyState (run (parameters, input, sampleRate), length);

    // Sixth harmonic, at 12 kHz: an octave above a 6 kHz corner.
    const double openSixth   = binLevelDb (wideOpen, sampleRate, 6.0 * frequency, 0.8);
    const double cappedSixth = binLevelDb (capped,   sampleRate, 6.0 * frequency, 0.8);

    CHECK (openSixth - cappedSixth > 20.0);        // measured: 24.5 dB

    // Second harmonic, at 4 kHz: below the corner, and must be left alone.
    const double openSecond   = binLevelDb (wideOpen, sampleRate, 2.0 * frequency, 0.8);
    const double cappedSecond = binLevelDb (capped,   sampleRate, 2.0 * frequency, 0.8);

    CHECK (std::abs (openSecond - cappedSecond) < 1.0);   // measured: 0.2 dB
}

TEZLA_TEST (halo_sweep_leaves_no_low_frequency_debris)
{
    // The condition CLAUDE.md 7 actually specifies, and the most productive
    // test in this file: it found three separate defects that every
    // steady-tone measurement had passed cleanly.
    //
    //   * the even half's DC pedestal moves with the signal, and the DC blocker
    //     turned that movement into -29.5 dBFS sitting on its own 12 Hz corner;
    //   * the amplitude was estimated as a peak over a control interval, which
    //     ripples at the beat between the signal and the control rate and
    //     modulated a gain-4 copy of the band across the spectrum;
    //   * a single averaging pole left enough ripple to spread a uniform -78 dBFS
    //     sideband skirt around every harmonic.
    //
    // None of them is visible on a steady tone, because none of them happens
    // until the band's level moves. A sweep crossing Focus moves it by 60 dB.
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t window = 4096;

    const auto sweep = linearSweep (1000.0, 20000.0, 0.9, sampleRate, 96000);

    const auto worstBelow = [&] (const std::vector<double>& signal, double upperHz)
    {
        double worst = 0.0;

        for (std::size_t offset = 8192; offset + window < signal.size(); offset += window)
        {
            std::vector<double> chunk (signal.begin() + static_cast<std::ptrdiff_t> (offset),
                                       signal.begin() + static_cast<std::ptrdiff_t> (offset + window));

            // Hann. A rectangular window on a sweeping tone leaks about -65 dB
            // a few kHz away, which is the same order as the artefact being
            // looked for -- without this the measurement reports its own
            // sidelobes and the numbers mean nothing.
            for (std::size_t i = 0; i < window; ++i)
                chunk[i] *= 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi
                                                  * static_cast<double> (i)
                                                  / static_cast<double> (window));

            const auto spectrum = fftOfReal (chunk);
            const double binWidth = sampleRate / static_cast<double> (window);

            for (std::size_t k = 1; static_cast<double> (k) * binWidth < upperHz; ++k)
                worst = std::max (worst, 4.0 * std::abs (spectrum[k]) / static_cast<double> (window));
        }

        return dsp::gainToDb (worst, -400.0);
    };

    // Check the instrument first. With Amount at silence the output is the
    // input, so anything this reports is the measurement's own floor.
    {
        auto parameters = defaultParameters();
        parameters.amountDb = Engine::kAmountSilenceDb;
        parameters.drive = 1.0;

        CHECK (worstBelow (run (parameters, sweep, sampleRate), 900.0) < -120.0);
    }

    for (const double drive : { 0.25, 0.5, 1.0 })
        for (const double colour : { 0.0, 0.5, 1.0 })
            for (const double track : { 0.0, 1.0 })
            {
                auto parameters = defaultParameters();
                parameters.drive = drive;
                parameters.colour = colour;
                parameters.track = track;
                parameters.focusHz = 3000.0;

                // No legitimate harmonic of a 1 kHz-and-up sweep can land below
                // 900 Hz, so everything down there is debris by definition.
                // Worst measured over this grid: -65.6 dBFS.
                CHECK (worstBelow (run (parameters, sweep, sampleRate), 900.0) < -60.0);
            }
}

TEZLA_TEST (halo_reset_really_resets)
{
    // Two runs of the same audio from a fresh engine must be identical. A
    // smoother left mid-ramp or an envelope holding its old value makes this
    // fail, and the symptom in a DAW is a plugin that sounds slightly different
    // depending on where the transport was when it was inserted.
    auto parameters = defaultParameters();
    parameters.drive = 0.7;
    parameters.track = 0.6;
    parameters.punch = 0.4;

    std::vector<double> input (8192);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.6 * std::sin (0.21 * static_cast<double> (i))
                 * (i > 4000 && i < 4200 ? 3.0 : 1.0);

    const auto first  = run (parameters, input, 48000.0);
    const auto second = run (parameters, input, 48000.0);

    for (std::size_t i = 0; i < input.size(); ++i)
        CHECK (first[i] == second[i]);
}

TEZLA_TEST (halo_survives_a_brutal_input)
{
    // 20 plugins deep on a dubstep master, something will hand this a number it
    // was not expecting. It has to bend rather than produce a NaN that then
    // poisons every filter state downstream of it.
    for (const double track : { 0.0, 1.0 })
        for (const auto mode : { BandMode::Above, BandMode::Below })
        {
            auto parameters = defaultParameters();
            parameters.drive = 1.0;
            parameters.track = track;
            parameters.punch = 1.0;
            parameters.bandMode = mode;
            parameters.inputDb = 24.0;
            parameters.amountDb = 12.0;

            std::vector<double> input (8192);
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = (i % 2 == 0 ? 40.0 : -40.0) * (i < 100 ? 0.0 : 1.0);

            const auto output = run (parameters, input, 48000.0);

            for (const double sample : output)
                CHECK (std::isfinite (sample));
        }
}

namespace {

/// Runs a stereo signal through the engine, block by block.
std::pair<std::vector<double>, std::vector<double>>
runStereo (const Parameters& parameters, const std::vector<double>& left,
           const std::vector<double>& right, double sampleRate, int blockSize = 271)
{
    Engine engine;
    engine.prepare (sampleRate, blockSize, 2);
    engine.setParameters (parameters);
    engine.reset();

    std::vector<double> outLeft = left, outRight = right;

    for (std::size_t offset = 0; offset < outLeft.size(); offset += static_cast<std::size_t> (blockSize))
    {
        const int numSamples = static_cast<int> (std::min (static_cast<std::size_t> (blockSize),
                                                           outLeft.size() - offset));
        double* pointers[2] { outLeft.data() + offset, outRight.data() + offset };
        engine.process (pointers, 2, numSamples);
    }

    return { outLeft, outRight };
}

/// A stereo pair with genuinely different sides, so width has something to act on.
std::pair<std::vector<double>, std::vector<double>> stereoSignal (std::size_t length)
{
    std::vector<double> left (length), right (length);

    for (std::size_t i = 0; i < length; ++i)
    {
        const auto t = static_cast<double> (i);
        left[i]  = 0.5 * std::sin (0.07 * t) + 0.25 * std::sin (0.41 * t);
        right[i] = 0.5 * std::sin (0.07 * t) + 0.25 * std::sin (0.53 * t + 0.9);
    }

    return { left, right };
}

} // namespace

TEZLA_TEST (halo_width_at_normal_is_a_bit_exact_identity)
{
    // Width sits permanently in the wet path, so its neutral setting has to be a
    // true identity. The obvious mid/side rebuild is not one: (L+R)/2 + (L-R)/2
    // rounds twice and does not return L bit for bit. The engine multiplies the
    // side by exactly zero instead, and this is the check that it stayed that
    // way.
    auto parameters = defaultParameters();
    parameters.drive = 0.7;

    const auto [left, right] = stereoSignal (4096);

    parameters.width = 1.0;
    const auto normal = runStereo (parameters, left, right, 48000.0);

    // Reached by a different route: the same value set explicitly rather than
    // left at its default.
    Parameters explicitly = parameters;
    explicitly.width = 1.0;
    const auto again = runStereo (explicitly, left, right, 48000.0);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        CHECK (normal.first[i] == again.first[i]);
        CHECK (normal.second[i] == again.second[i]);
    }
}

TEZLA_TEST (halo_width_neutral_form_is_exact_where_the_obvious_one_is_not)
{
    // Why the engine writes width as a departure from unity rather than as a
    // mid/side rebuild. The engine test above shows the two routes to Normal
    // agree; this shows *why* one of the two possible spellings can make that
    // claim and the other cannot, which is the part a future rewrite would get
    // wrong without noticing.
    //
    // Adding zero is exact in IEEE 754 for every finite value, so the departure
    // form returns its input bit for bit. The rebuild rounds twice -- once
    // forming mid and side, once summing them back -- and lands within an LSB,
    // which is not the same thing when the stage sits in the path permanently.
    int rebuildDiffered = 0;

    for (int i = 1; i <= 4000; ++i)
    {
        const double left  = std::sin (0.37 * i) * 0.9 + 1.0e-9 * i;
        const double right = std::cos (0.11 * i) * 0.7 - 3.0e-9 * i;

        const double side = 0.5 * (left - right);

        // What the engine does at width 1: widen == 0, so this is x + 0.0 * s.
        CHECK (left  + 0.0 * side == left);
        CHECK (right - 0.0 * side == right);

        const double mid = 0.5 * (left + right);
        if (mid + side != left || mid - side != right)
            ++rebuildDiffered;
    }

    // Not a claim that the rebuild is always wrong -- only that it is wrong
    // often enough that "close enough" would silently change every project.
    CHECK (rebuildDiffered > 100);
}

TEZLA_TEST (halo_width_only_moves_the_harmonics)
{
    // The point of the control. Widening must not touch the source, so with the
    // harmonics soloed away -- Amount at silence -- width has to do precisely
    // nothing, however far it is turned.
    auto parameters = defaultParameters();
    parameters.drive = 1.0;
    parameters.amountDb = Engine::kAmountSilenceDb;

    const auto [left, right] = stereoSignal (4096);

    parameters.width = 1.0;
    const auto normal = runStereo (parameters, left, right, 48000.0);

    for (const double width : { 0.0, 0.5, 2.0 })
    {
        parameters.width = width;
        const auto widened = runStereo (parameters, left, right, 48000.0);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            CHECK (widened.first[i] == normal.first[i]);
            CHECK (widened.second[i] == normal.second[i]);
        }
    }
}

TEZLA_TEST (halo_width_at_mono_folds_the_harmonics_to_the_centre)
{
    // At zero the harmonics must carry no side content at all: soloed, the two
    // channels have to be identical. That is what makes the setting worth having
    // on a reese, where the added edge smears the image.
    auto parameters = defaultParameters();
    parameters.listen = true;      // solo the harmonics, so this measures them
    parameters.drive = 0.8;
    parameters.width = 0.0;

    const auto [left, right] = stereoSignal (8192);
    const auto output = runStereo (parameters, left, right, 48000.0);

    for (std::size_t i = 2048; i < left.size(); ++i)
        CHECK_NEAR (output.first[i], output.second[i], 1.0e-15);
}

TEZLA_TEST (halo_width_above_normal_widens_and_below_narrows)
{
    // Monotone in the direction the label claims, measured as the energy in the
    // side channel of the soloed harmonics.
    auto parameters = defaultParameters();
    parameters.listen = true;
    parameters.drive = 0.8;

    const auto [left, right] = stereoSignal (8192);

    const auto sideEnergy = [&] (double width)
    {
        parameters.width = width;
        const auto output = runStereo (parameters, left, right, 48000.0);

        double energy = 0.0;
        for (std::size_t i = 2048; i < left.size(); ++i)
        {
            const double side = 0.5 * (output.first[i] - output.second[i]);
            energy += side * side;
        }
        return energy;
    };

    const double narrow = sideEnergy (0.5);
    const double normal = sideEnergy (1.0);
    const double wide   = sideEnergy (1.8);

    CHECK (normal > 0.0);
    CHECK (narrow < normal);
    CHECK (wide > normal);

    // And the relationship is the stated one: side content scales with the
    // control, so 1.8 carries about (1.8 / 0.5)^2 times the energy of 0.5.
    CHECK_NEAR (std::sqrt (wide / narrow), 1.8 / 0.5, 0.1);
}

TEZLA_TEST (halo_width_does_nothing_to_a_mono_source)
{
    // A mono source has no side content to widen. If this fails, width is
    // manufacturing stereo out of nothing -- which would put the plugin's output
    // out of mono compatibility for a signal that was mono to begin with.
    auto parameters = defaultParameters();
    parameters.drive = 0.9;
    parameters.width = 2.0;

    std::vector<double> mono (4096);
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.6 * std::sin (0.11 * static_cast<double> (i));

    const auto output = runStereo (parameters, mono, mono, 48000.0);

    for (std::size_t i = 0; i < mono.size(); ++i)
        CHECK_NEAR (output.first[i], output.second[i], 1.0e-15);
}

// ============================================================================
//  Chebyshev precision mode
// ============================================================================

namespace {

/// The Chebyshev generator with one harmonic requested and everything else off.
Parameters chebyshevWith (int harmonic, double gain = 1.0)
{
    auto parameters = defaultParameters();
    parameters.generator = Generator::Chebyshev;
    parameters.harmonics.fill (0.0);
    parameters.harmonics[static_cast<std::size_t> (harmonic - 2)] = gain;
    return parameters;
}

/// Absolute level of every harmonic of `fundamentalHz`, index 0 = DC.
///
/// Deliberately not analyseHarmonics(): everything that reports is scaled to
/// the fundamental, and the whole claim here is that the fundamental is absent.
std::vector<double> harmonicLevelsDb (const std::vector<double>& signal, double sampleRate,
                                      double fundamentalHz, int upTo = 10)
{
    const auto spectrum = fftOfReal (signal);
    const double binWidth = sampleRate / static_cast<double> (signal.size());
    const auto bin = static_cast<std::size_t> (std::llround (fundamentalHz / binWidth));

    std::vector<double> levels (static_cast<std::size_t> (upTo) + 1, -400.0);

    for (int n = 0; n <= upTo; ++n)
    {
        const std::size_t centre = bin * static_cast<std::size_t> (n);
        double power = 0.0;

        for (std::size_t k = (centre > 0 ? centre - 1 : 0);
             k <= centre + 1 && k < signal.size() / 2; ++k)
            power += std::norm (spectrum[k]);

        const double amplitude = 2.0 * std::sqrt (power) / static_cast<double> (signal.size());
        levels[static_cast<std::size_t> (n)] = dsp::gainToDb (amplitude, -400.0);
    }

    return levels;
}

} // namespace

TEZLA_TEST (halo_chebyshev_puts_the_harmonic_where_it_was_asked_for)
{
    // The claim, through the whole chain rather than at the generator: band
    // split, band limit, normalisation, the polynomial, the DC blockers and the
    // downsampler. Ask for one harmonic and that is what comes out of the
    // plugin, with the rest of the series far below it.
    constexpr std::size_t window = 1 << 15;
    constexpr double rate = 48000.0;

    for (const int harmonic : { 2, 3, 5, 8 })
    {
        auto parameters = chebyshevWith (harmonic);
        parameters.listen = true;         // the wet path alone
        parameters.ceilingOn = false;     // do not filter away what we came to measure
        parameters.bandMode = BandMode::Below;
        parameters.focusHz = 900.0;

        const double frequency = binExactFrequency (400.0, rate, window);
        const auto output = run (parameters, settlingSine (frequency, 0.5, rate, window), rate);
        const auto levels = harmonicLevelsDb (steadyState (output, window), rate, frequency);

        const auto index = static_cast<std::size_t> (harmonic);

        // 90 dB, not 40. The measured worst case through the whole chain is
        // 123 dB, so a threshold of 40 would sleep through the loss of the
        // third envelope pole -- which is worth 37 to 55 dB on its own.
        for (int n = 1; n <= 8; ++n)
            if (n != harmonic)
                CHECK (levels[static_cast<std::size_t> (n)] < levels[index] - 90.0);
    }
}

TEZLA_TEST (halo_chebyshev_leaves_the_fundamental_alone)
{
    // The plugin's headline property, restated for the second generator. A
    // conventional exciter mixes a filtered copy of the band back in; this must
    // not, at any index -- including the settings where the polynomials would
    // hand one back for free if nothing cancelled it.
    constexpr std::size_t window = 1 << 15;
    constexpr double rate = 48000.0;

    for (const double index : { 0.4, 0.75, 1.0, 1.5 })
    {
        auto parameters = defaultParameters();
        parameters.generator = Generator::Chebyshev;
        parameters.harmonics.fill (0.6);
        parameters.chebIndex = index;
        parameters.listen = true;
        parameters.ceilingOn = false;
        parameters.bandMode = BandMode::Below;
        parameters.focusHz = 900.0;

        const double frequency = binExactFrequency (400.0, rate, window);
        const auto output = run (parameters, settlingSine (frequency, 0.5, rate, window), rate);
        const auto levels = harmonicLevelsDb (steadyState (output, window), rate, frequency);

        double loudest = -400.0;
        for (int n = 2; n <= 8; ++n)
            loudest = std::max (loudest, levels[static_cast<std::size_t> (n)]);

        CHECK (levels[1] < loudest - 100.0);
    }
}

TEZLA_TEST (halo_chebyshev_is_silent_in_silence)
{
    // Where this generator differs from the curve one and needs help. T_even(0)
    // is +/-1, not 0, so with the envelope merely floored rather than gated the
    // wet path would sit at a -90 dBFS constant and then ride the envelope down
    // after every note -- a slow thump on a gate, which is exactly the failure
    // the moving DC pedestal already produced once here.
    for (const double index : { 0.0, 0.5, 1.0, 2.0 })
        for (const auto mode : { BandMode::Above, BandMode::Below })
        {
            auto parameters = defaultParameters();
            parameters.generator = Generator::Chebyshev;
            parameters.harmonics.fill (1.0);
            parameters.chebIndex = index;
            parameters.bandMode = mode;

            const auto output = run (parameters, std::vector<double> (8192, 0.0), 48000.0);

            for (const double sample : output)
                CHECK (std::abs (sample) < 1.0e-15);
        }
}

TEZLA_TEST (halo_chebyshev_with_every_harmonic_off_is_a_bit_exact_bypass)
{
    // All levels at zero makes the generator the exact zero function, so the wet
    // path adds literally nothing and the output is the delayed input to the
    // last bit -- the same guarantee Amount and Width already carry.
    auto parameters = defaultParameters();
    parameters.generator = Generator::Chebyshev;
    parameters.harmonics.fill (0.0);
    parameters.chebIndex = 2.0;

    std::vector<double> input (4096);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.7 * std::sin (0.05 * static_cast<double> (i))
                 + 0.2 * std::sin (0.9 * static_cast<double> (i));

    const auto output = run (parameters, input, 48000.0);
    const int latency = latencyOf (parameters, 48000.0);

    for (std::size_t i = static_cast<std::size_t> (latency); i < input.size(); ++i)
        CHECK (output[i] == input[i - static_cast<std::size_t> (latency)]);
}

TEZLA_TEST (halo_chebyshev_index_at_zero_is_a_bit_exact_bypass)
{
    // Index is a real off, not a fade to something quiet. The pedestal
    // subtraction is exactly T_n(0) there, so the two cancel outright.
    auto parameters = defaultParameters();
    parameters.generator = Generator::Chebyshev;
    parameters.harmonics.fill (1.0);
    parameters.chebIndex = 0.0;

    std::vector<double> input (4096);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.6 * std::sin (0.11 * static_cast<double> (i));

    const auto output = run (parameters, input, 48000.0);
    const int latency = latencyOf (parameters, 48000.0);

    for (std::size_t i = static_cast<std::size_t> (latency); i < input.size(); ++i)
        CHECK (output[i] == input[i - static_cast<std::size_t> (latency)]);
}

TEZLA_TEST (halo_chebyshev_parameters_do_nothing_in_curve_mode)
{
    // A regression guard rather than a feature. Nine new parameters were added
    // to a plugin whose whole selling point is that its neutral settings are
    // bit-exact; if any of them leaks into the path that was already shipped,
    // every existing project changes the day this lands.
    auto plain = defaultParameters();
    plain.drive = 0.7;

    auto loaded = plain;
    loaded.harmonics.fill (1.0);
    loaded.chebIndex = 1.7;
    loaded.chebTilt = 0.8;

    std::vector<double> input (4096);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.55 * std::sin (0.09 * static_cast<double> (i));

    const auto before = run (plain, input, 48000.0);
    const auto after  = run (loaded, input, 48000.0);

    for (std::size_t i = 0; i < input.size(); ++i)
        CHECK (before[i] == after[i]);
}

TEZLA_TEST (halo_chebyshev_tilt_at_centre_is_a_bit_exact_identity)
{
    auto plain = chebyshevWith (4);
    plain.harmonics.fill (0.5);

    auto tilted = plain;
    tilted.chebTilt = 0.0;

    std::vector<double> input (4096);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.6 * std::sin (0.08 * static_cast<double> (i));

    const auto before = run (plain, input, 48000.0);
    const auto after  = run (tilted, input, 48000.0);

    for (std::size_t i = 0; i < input.size(); ++i)
        CHECK (before[i] == after[i]);
}

TEZLA_TEST (halo_chebyshev_holds_no_dc)
{
    // The pedestal correction lives in the generator, but it is the engine that
    // multiplies the result by a moving envelope -- which is what turns a static
    // offset into a low-frequency wobble. Measured after the chain, not before.
    for (const double index : { 0.3, 1.0, 1.6 })
    {
        auto parameters = defaultParameters();
        parameters.generator = Generator::Chebyshev;
        parameters.harmonics.fill (0.8);
        parameters.chebIndex = index;
        parameters.listen = true;
        parameters.bandMode = BandMode::Below;
        parameters.focusHz = 900.0;

        // Bin-exact, or the window holds a fraction of a cycle and its mean is
        // non-zero for a signal with no DC in it whatsoever -- which reads
        // exactly like the fault being tested for.
        const double frequency = binExactFrequency (400.0, 48000.0, 8192);
        const auto output = run (parameters, settlingSine (frequency, 0.5, 48000.0, 8192), 48000.0);
        const auto window = steadyState (output, 8192);

        double sum = 0.0;
        for (const double sample : window)
            sum += sample;

        CHECK (std::abs (sum / static_cast<double> (window.size())) < 1.0e-6);
    }
}

TEZLA_TEST (halo_chebyshev_agrees_at_every_session_rate)
{
    // Same recipe, same sound, whatever the host is running at. Auto picks x4 at
    // 48 kHz and x1 at 192, so this also checks the band limit lands in the same
    // place relative to the music rather than to the sample rate.
    constexpr std::size_t window = 1 << 14;

    std::vector<double> reference;

    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto parameters = chebyshevWith (3);
        parameters.harmonics[3] = 0.5;      // and a little 5th
        parameters.listen = true;
        parameters.ceilingOn = false;
        parameters.bandMode = BandMode::Below;
        parameters.focusHz = 900.0;

        const double frequency = binExactFrequency (400.0, rate, window);
        const auto output = run (parameters, settlingSine (frequency, 0.5, rate, window), rate);
        const auto levels = harmonicLevelsDb (steadyState (output, window), rate, frequency, 6);

        if (reference.empty())
        {
            reference = levels;
            continue;
        }

        for (const int n : { 3, 5 })
            CHECK_NEAR (levels[static_cast<std::size_t> (n)],
                        reference[static_cast<std::size_t> (n)], 0.5);
    }
}

TEZLA_TEST (halo_chebyshev_does_not_alias_at_the_exact_setting)
{
    // A polynomial of degree n on a band limited to internalNyquist / n is
    // exactly band-limited, so nothing folds. This is that claim about the whole
    // chain, at the two rates Auto treats most differently -- x4 at 48 kHz and
    // x1 at 192.
    //
    // A sweep rather than a tone: fold-back from a harmonically related signal
    // lands on top of that signal's own harmonics, so a steady-tone measurement
    // will score a badly aliasing build as clean.
    constexpr std::size_t window = 1 << 15;

    for (const double rate : { 48000.0, 192000.0 })
    {
        auto parameters = defaultParameters();
        parameters.generator = Generator::Chebyshev;
        parameters.harmonics.fill (0.8);
        parameters.listen = true;
        parameters.ceilingOn = false;
        parameters.focusHz = 1000.0;

        const auto preroll = static_cast<std::size_t> (rate * 1.5);
        const auto sweep = linearSweep (1000.0, 18000.0, 0.7, rate, preroll + window);
        const auto output = steadyState (run (parameters, sweep, rate), window);

        // Everything below the sweep's own lowest frequency can only be
        // fold-back or intermodulation; nothing legitimate lives there.
        const auto spectrum = fftOfReal (output);
        const double binWidth = rate / static_cast<double> (window);
        const auto top = static_cast<std::size_t> (900.0 / binWidth);

        double worst = 0.0;
        for (std::size_t k = static_cast<std::size_t> (20.0 / binWidth); k < top; ++k)
            worst = std::max (worst, 2.0 * std::abs (spectrum[k]) / static_cast<double> (window));

        CHECK (dsp::gainToDb (worst, -400.0) < -60.0);
    }
}

TEZLA_TEST (halo_chebyshev_band_limit_is_what_stops_it_aliasing)
{
    // The test above passes with the band limit deleted, and that is worth
    // saying rather than hiding: at a 96 kHz internal Nyquist, eight harmonics
    // of an 18 kHz sweep reach 144 kHz and fold to 48 kHz, which is nowhere
    // near the audible band. It is a real check that the chain is clean; it is
    // not a check that the limit does anything.
    //
    // This is. With oversampling forced off at 48 kHz the internal Nyquist is
    // 24 kHz, so eight harmonics may only be asked of content below 3 kHz --
    // and without the limit an 18 kHz sweep folds straight into the audible
    // band. The user can reach this setting from the front panel, so it has to
    // hold there and not only where the headroom is generous.
    constexpr std::size_t window = 1 << 15;
    constexpr double rate = 48000.0;

    auto parameters = defaultParameters();
    parameters.generator = Generator::Chebyshev;
    parameters.harmonics.fill (0.8);
    parameters.oversampling = dsp::OversamplingMode::Off;
    parameters.listen = true;
    parameters.ceilingOn = false;
    parameters.focusHz = 1000.0;

    const auto preroll = static_cast<std::size_t> (rate * 1.5);
    const auto sweep = linearSweep (1000.0, 18000.0, 0.7, rate, preroll + window);
    const auto output = steadyState (run (parameters, sweep, rate), window);

    const auto spectrum = fftOfReal (output);
    const double binWidth = rate / static_cast<double> (window);
    const auto top = static_cast<std::size_t> (900.0 / binWidth);

    double worst = 0.0;
    for (std::size_t k = static_cast<std::size_t> (20.0 / binWidth); k < top; ++k)
        worst = std::max (worst, 2.0 * std::abs (spectrum[k]) / static_cast<double> (window));

    CHECK (dsp::gainToDb (worst, -400.0) < -60.0);
}

TEZLA_TEST (halo_generator_switch_does_not_click)
{
    // Two generators producing quite different signals from the same input, so
    // the change is discrete and crossfades. Without one it is a step, and a
    // step in the wet path is a click on every A/B.
    //
    // Two things took getting right, and both were found by deleting the
    // crossfade and watching the test still pass.
    //
    // The bar is the signal itself, not a fixed number. Seven harmonics of a
    // 458 Hz tone reach the 8th at 3.7 kHz, and that waveform legitimately moves
    // about 1.0 per sample -- so "no step larger than 0.1" is nonsense here. The
    // question is whether the output moves faster *while switching* than it does
    // either side.
    //
    // And the switch has to land at many phases. One switch lands at one point
    // in the waveform, and if the wet path happens to be near zero there the
    // step is small however brutal the transition. Swept across a cycle, the
    // measured worst case is 0.99 with the crossfade and 1.50 without it.
    constexpr double rate = 48000.0;
    constexpr int blockSize = 64;

    double worstRatio = 0.0;

    for (int offset = 0; offset < 12; ++offset)
    {
        Engine engine;
        engine.prepare (rate, blockSize, 1);

        // Below mode with a low Focus, so the 458 Hz tone is inside the band and
        // both generators are actually working. With the defaults it sits under
        // a 3 kHz highpass, the band is empty, and the test passes without ever
        // switching anything audible.
        auto parameters = defaultParameters();
        parameters.bandMode = BandMode::Below;
        parameters.focusHz = 900.0;
        parameters.drive = 0.8;
        parameters.harmonics.fill (0.9);
        parameters.generator = Generator::Chebyshev;
        engine.setParameters (parameters);
        engine.reset();

        const int switchBlock = 200 + offset;

        double previous = 0.0;
        double worstSteady = 0.0;
        double worstSwitching = 0.0;

        for (int block = 0; block < 320; ++block)
        {
            if (block == switchBlock)
            {
                parameters.generator = Generator::Curve;
                engine.setParameters (parameters);
            }

            std::vector<double> buffer (blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = static_cast<double> (block * blockSize + i);
                buffer[static_cast<std::size_t> (i)] = 0.6 * std::sin (0.06 * t);
            }

            double* pointer = buffer.data();
            engine.process (&pointer, 1, blockSize);

            // Skip the settling, but carry the last sample forward -- or the
            // first comparison is against a zero that was never in the signal,
            // and every run reports a step the size of the whole waveform.
            if (block < 120)
            {
                previous = buffer.back();
                continue;
            }

            // The crossfade is 15 ms; twenty blocks of 64 covers it with room
            // for the oversampler's latency on either side.
            const bool switching = block >= switchBlock && block < switchBlock + 20;

            for (const double sample : buffer)
            {
                const double step = std::abs (sample - previous);

                if (switching) worstSwitching = std::max (worstSwitching, step);
                else           worstSteady    = std::max (worstSteady, step);

                previous = sample;
            }
        }

        CHECK (worstSteady > 0.0);
        worstRatio = std::max (worstRatio, worstSwitching / worstSteady);
    }

    // A blend of two signals cannot move faster than the faster of them, so
    // anything meaningfully above the steady-state worst case is the crossfade
    // failing to be one.
    CHECK (worstRatio < 1.2);
}
