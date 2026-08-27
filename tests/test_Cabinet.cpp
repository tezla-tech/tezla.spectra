#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <tezla/dsp/Cabinet.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kRate = 192000.0;

Cabinet made (const CabinetParameters& parameters = {}, double sampleRate = kRate)
{
    Cabinet cabinet;
    cabinet.prepare (sampleRate);
    cabinet.setParameters (parameters);
    cabinet.reset();
    return cabinet;
}

double dbOf (double magnitude) { return 20.0 * std::log10 (std::max (magnitude, 1.0e-12)); }

double dbAt (const Cabinet& cabinet, double frequency)
{
    return dbOf (cabinet.magnitudeAt (frequency));
}

/// The measured magnitude, by running audio through it -- so the biquad chain
/// and the analytic magnitude can be checked against each other.
double measuredDbAt (Cabinet& cabinet, double frequency, double sampleRate = kRate)
{
    cabinet.reset();

    const int settle = static_cast<int> (sampleRate * 0.15);
    const int measure = static_cast<int> (std::round (80.0 * sampleRate / frequency));

    double phase = 0.0;
    const double step = 2.0 * std::numbers::pi * frequency / sampleRate;

    for (int i = 0; i < settle; ++i)
    {
        (void) cabinet.process (std::sin (phase));
        phase += step;
    }

    double inPhase = 0.0;
    double quadrature = 0.0;

    for (int i = 0; i < measure; ++i)
    {
        const double y = cabinet.process (std::sin (phase));
        inPhase += y * std::sin (phase);
        quadrature += y * std::cos (phase);
        phase += step;
    }

    return dbOf (2.0 * std::hypot (inPhase, quadrature) / measure);
}
} // namespace

// ---------------------------------------------------------------------------
// The enclosure
// ---------------------------------------------------------------------------

TEZLA_TEST (cabinet_sealed_box_rolls_off_at_twelve_decibels_an_octave)
{
    // A driver in a sealed box is a second-order highpass, because the box's
    // air is a spring in parallel with the cone's suspension. Two poles, so
    // 12 dB per octave -- and well below the corner it is the asymptote
    // exactly, not approximately.
    CabinetParameters parameters;
    parameters.voicing = cabinets::modernFourByTwelve();
    parameters.micDistanceMetres = 1.0;      // proximity out of the way

    const auto cabinet = made (parameters);

    const double at20 = dbAt (cabinet, 20.0);
    const double at40 = dbAt (cabinet, 40.0);
    const double at10 = dbAt (cabinet, 10.0);

    CHECK_NEAR (at40 - at20, 12.0, 0.6);
    CHECK_NEAR (at20 - at10, 12.0, 0.3);
}

TEZLA_TEST (cabinet_an_open_back_loses_the_bottom_and_keeps_the_top)
{
    // The rear of the cone radiates out of phase, and below the frequency whose
    // half wavelength matches the path round the baffle the two cancel. So an
    // open back is thin where a sealed box is not -- and only there.
    //
    // Measured: 7.3 dB down at 100 Hz against the closed 4x12, and within
    // 2 dB of it above 2 kHz.
    CabinetParameters open;
    open.voicing = cabinets::combo();

    CabinetParameters closed;
    closed.voicing = cabinets::modernFourByTwelve();

    const auto a = made (open);
    const auto b = made (closed);

    const auto relative = [&a, &b] (double f)
    {
        return (dbAt (a, f) - dbAt (a, 1750.0)) - (dbAt (b, f) - dbAt (b, 1750.0));
    };

    CHECK (relative (100.0) < -6.0);
    CHECK (relative (60.0) < -4.0);

    // Above the cancellation they are the same kind of thing again.
    CHECK (std::abs (relative (2500.0)) < 6.0);
}

TEZLA_TEST (cabinet_the_open_backs_cancellation_reaches_a_floor)
{
    // A *full* dipole falls away at 6 dB/octave for ever. A real open back is
    // only partly open, so the cancellation reaches a floor -- which makes it a
    // shelf, not a highpass. Modelling it as a highpass is why open backs are
    // usually simulated with far too little bottom end.
    //
    // Checked by removing the enclosure's own alignment, so what is left is the
    // rear section alone: it must flatten out rather than keep falling.
    CabinetVoicing voicing = cabinets::combo();
    voicing.boxCornerHz = 1.0;               // the alignment out of the way
    voicing.boxQ = 0.5;
    voicing.breakup = {};
    voicing.topCornerHz = 40000.0;

    CabinetParameters parameters;
    parameters.voicing = voicing;
    parameters.micDistanceMetres = 1.0;

    const auto cabinet = made (parameters);

    const double reference = dbAt (cabinet, 1000.0);

    // It falls...
    CHECK (dbAt (cabinet, 100.0) - reference < -8.0);

    // ...and then stops, rather than falling another 6 dB per octave.
    CHECK_NEAR (dbAt (cabinet, 25.0), dbAt (cabinet, 100.0), 1.5);
}

// ---------------------------------------------------------------------------
// The cone
// ---------------------------------------------------------------------------

TEZLA_TEST (cabinet_cone_breakup_is_what_makes_the_voicings_differ)
{
    // Above about 800 Hz a paper cone stops moving as one piece. The modes that
    // follow are most of what makes a guitar speaker sound like one, and their
    // frequencies are what separates one cabinet from another far more than the
    // box does.
    //
    // The vintage voicing's main mode sits at 1450 Hz against the modern one's
    // 1750, and its top gives out sooner.
    CabinetParameters modern;
    modern.voicing = cabinets::modernFourByTwelve();
    modern.micDistanceMetres = 1.0;

    CabinetParameters vintage;
    vintage.voicing = cabinets::vintageFourByTwelve();
    vintage.micDistanceMetres = 1.0;

    const auto a = made (modern);
    const auto b = made (vintage);

    const auto peakFrequency = [] (const Cabinet& cabinet)
    {
        double best = 0.0;
        double bestAt = 0.0;

        for (double f = 700.0; f < 4000.0; f *= 1.005)
        {
            const double magnitude = cabinet.magnitudeAt (f);

            if (magnitude > best)
            {
                best = magnitude;
                bestAt = f;
            }
        }

        return bestAt;
    };

    const double modernPeak = peakFrequency (a);
    const double vintagePeak = peakFrequency (b);

    CHECK (vintagePeak < modernPeak);
    CHECK (vintagePeak > 1200.0);
    CHECK (vintagePeak < 1700.0);
    CHECK (modernPeak > 1500.0);
    CHECK (modernPeak < 2100.0);

    // And the vintage cone gives up sooner above its peak.
    CHECK ((dbAt (b, 5000.0) - dbAt (b, vintagePeak))
           < (dbAt (a, 5000.0) - dbAt (a, modernPeak)));
}

TEZLA_TEST (cabinet_is_done_by_eight_kilohertz_like_a_guitar_speaker)
{
    // Cone mass and voice-coil inductance together roll the top off from around
    // 4 kHz. A guitar cabinet has nothing above 8 kHz, and a simulation that
    // does is the thing people describe as fizzy.
    for (const auto& voicing : { cabinets::combo(),
                                 cabinets::modernFourByTwelve(),
                                 cabinets::vintageFourByTwelve() })
    {
        CabinetParameters parameters;
        parameters.voicing = voicing;
        parameters.micDistanceMetres = 1.0;

        const auto cabinet = made (parameters);

        const double reference = dbAt (cabinet, 1750.0);

        CHECK (dbAt (cabinet, 8000.0) - reference < -15.0);
        CHECK (dbAt (cabinet, 12000.0) - reference < -28.0);

        // But still alive where the guitar is.
        CHECK (dbAt (cabinet, 1000.0) - reference > -8.0);
    }
}

// ---------------------------------------------------------------------------
// The microphone
// ---------------------------------------------------------------------------

TEZLA_TEST (cabinet_mic_position_sweeps_two_octaves_and_touches_nothing_low)
{
    // Moving from the dust cap to the surround changes which part of the cone
    // the microphone is looking at, and the outer cone radiates almost nothing
    // above a couple of kilohertz. So it is a lowpass whose corner sweeps -- and
    // it must leave the low end exactly where it was, because the outer cone is
    // where the low end comes from.
    CHECK_NEAR (Cabinet::beamingCornerHz (0.0) / Cabinet::beamingCornerHz (1.0), 4.0, 1.0e-9);

    // Geometric, so half travel is halfway in pitch rather than in hertz.
    CHECK_NEAR (Cabinet::beamingCornerHz (0.5),
                std::sqrt (Cabinet::beamingCornerHz (0.0) * Cabinet::beamingCornerHz (1.0)),
                1.0e-9);

    CabinetParameters cap;
    cap.micPosition = 0.0;
    cap.micDistanceMetres = 1.0;

    CabinetParameters edge = cap;
    edge.micPosition = 1.0;

    const auto a = made (cap);
    const auto b = made (edge);

    // The top falls away hard...
    CHECK (dbAt (b, 3500.0) - dbAt (a, 3500.0) < -8.0);
    CHECK (dbAt (b, 8000.0) - dbAt (a, 8000.0) < -15.0);

    // ...and the low end does not move at all.
    CHECK (std::abs (dbAt (b, 100.0) - dbAt (a, 100.0)) < 0.5);
    CHECK (std::abs (dbAt (b, 200.0) - dbAt (a, 200.0)) < 0.5);
}

TEZLA_TEST (cabinet_proximity_lifts_the_bass_and_leaves_the_midrange_alone)
{
    // A directional microphone's proximity rise is 6 dB/octave below
    // c/(2*pi*d), capped here because a real cabinet microphone has a
    // compensating rolloff that an ideal gradient transducer does not.
    //
    // The shelf goes where the capped ramp *reaches* its gain, not where the
    // ramp starts. Putting it at the start is wrong in a way that is easy to
    // miss and audible at once: at 2 cm the ramp starts at 2.7 kHz, so a shelf
    // there lifts the whole midrange 9 dB and the cabinet stops sounding like
    // one. This test is what caught that.
    CHECK (Cabinet::proximityCornerHz (0.02) < Cabinet::proximityRiseStartsHz (0.02) / 2.0);

    const auto at = [] (double metres, double frequency)
    {
        CabinetParameters parameters;
        parameters.micDistanceMetres = metres;
        return dbAt (made (parameters), frequency);
    };

    // Closer is more bass, monotonically.
    CHECK (at (0.02, 100.0) > at (0.05, 100.0));
    CHECK (at (0.05, 100.0) > at (0.15, 100.0));
    CHECK (at (0.15, 100.0) > at (0.50, 100.0));

    // Worth 8 dB across the range, which is what a close mic actually buys.
    CHECK (at (0.02, 100.0) - at (0.50, 100.0) > 6.0);

    // And above the shelf almost nothing moves. A shelf has a tail about an
    // octave wide, and the physics has one too -- the real ramp at 2 cm does
    // start at 2.7 kHz -- so the claim is not that 2 kHz is untouched but that
    // what reaches it is a few percent of the shelf rather than the whole of
    // it. Measured, with a 9 dB shelf at 965 Hz: 0.57 dB at 2 kHz, and nothing
    // at all by 4 kHz.
    CHECK (at (0.02, 2000.0) - at (0.50, 2000.0) < 1.0);

    for (const double frequency : { 4000.0, 8000.0 })
        CHECK (std::abs (at (0.02, frequency) - at (0.50, frequency)) < 0.05);
}

// ---------------------------------------------------------------------------
// The usual obligations
// ---------------------------------------------------------------------------

TEZLA_TEST (cabinet_biquad_chain_matches_its_own_magnitude_response)
{
    // magnitudeAt multiplies the sections' analytic magnitudes; process() runs
    // the audio through them. If the chain is assembled differently from the
    // way it is described, these disagree.
    CabinetParameters parameters;
    parameters.voicing = cabinets::combo();

    auto cabinet = made (parameters);

    for (const double frequency : { 60.0, 120.0, 330.0, 640.0, 1200.0, 2200.0, 5000.0 })
        CHECK_NEAR (measuredDbAt (cabinet, frequency), dbAt (cabinet, frequency), 0.05);
}

TEZLA_TEST (cabinet_bypass_is_bit_exact)
{
    CabinetParameters parameters;
    parameters.bypassed = true;

    auto cabinet = made (parameters);

    bool exact = true;

    for (int i = 0; i < 4096; ++i)
    {
        const double x = std::sin (i * 0.017) * 0.9 + std::sin (i * 0.31) * 0.1;

        if (cabinet.process (x) != x)
            exact = false;
    }

    CHECK (exact);
    CHECK_NEAR (cabinet.magnitudeAt (1000.0), 1.0, 0.0);
}

TEZLA_TEST (cabinet_is_silent_in_silence)
{
    auto cabinet = made();

    bool silent = true;

    for (int i = 0; i < 8192; ++i)
        if (cabinet.process (0.0) != 0.0)
            silent = false;

    CHECK (silent);
}

TEZLA_TEST (cabinet_is_the_same_cabinet_at_the_rates_it_runs_at)
{
    // It runs inside Anvil's oversampled section, so 96 and 192 kHz are the
    // rates that matter -- and CLAUDE.md section 6 names a cabinet response as
    // exactly the case where a plain biquad's warping does, because the shape
    // above Fs/8 *is* the shape being modelled.
    CabinetParameters parameters;
    parameters.voicing = cabinets::modernFourByTwelve();

    const auto at96 = made (parameters, 96000.0);
    const auto at192 = made (parameters, 192000.0);

    for (const double frequency : { 40.0, 100.0, 500.0, 1750.0, 4000.0 })
        CHECK_NEAR (dbAt (at96, frequency), dbAt (at192, frequency), 0.25);

    // And the warping is real up top, which is why the stage is oversampled
    // rather than run at the host rate.
    const auto at48 = made (parameters, 48000.0);

    CHECK (std::abs (dbAt (at48, 8000.0) - dbAt (at192, 8000.0)) > 1.0);
}
