#pragma once

// The cabinet: what a microphone in front of a loudspeaker actually hears.
//
// ---------------------------------------------------------------------------
// Synthesised, not captured
// ---------------------------------------------------------------------------
//
// The usual way to do this is an impulse response taken from a real cabinet.
// We do not, and the reason is in CLAUDE.md section 2.1: a captured IR of a
// commercial cabinet is that cabinet's measured property, and shipping one
// means shipping somebody else's product. Every curve here is built from the
// mechanism that produces it, and every number in the comments was measured
// from this code rather than from anybody's speaker.
//
// It costs accuracy against any one named cabinet and buys three things an IR
// cannot give: controls that move continuously and mean something physical, a
// response that stays correct at every sample rate, and no legal question.
//
// ---------------------------------------------------------------------------
// The five mechanisms
// ---------------------------------------------------------------------------
//
// **The enclosure's alignment.** A driver in a sealed box is a second-order
// highpass. The box's air is a spring in parallel with the cone's own
// suspension, so it raises the resonance from the driver's free-air fs to
// fc = fs*sqrt(1 + Vas/Vb) and raises the Q with it. A 4x12 lands near 100 Hz
// at a Q around 0.8, which is the peak just above the corner that people call
// the chug.
//
// **The rear radiation, for an open back.** The back of the cone radiates too,
// out of phase, and below the frequency whose half wavelength matches the path
// around the baffle the two cancel. A *full* dipole falls away at 6 dB/octave
// for ever; a real open back is only partly open, so the cancellation reaches
// a floor. That is a shelf, not a highpass, and getting it wrong is why open
// backs are usually modelled with far too little bottom end. Above the first
// cancellation there is a comb of nulls, of which only the first survives the
// diffraction and absorption well enough to matter -- a partial notch, some
// 4 dB deep, not the arithmetic null the theory has.
//
// **Cone breakup.** Above about 800 Hz a paper cone stops moving as one piece,
// and the modes that follow are most of what makes a guitar speaker sound like
// a guitar speaker rather than like a speaker. Three peaking sections: a dip
// where the piston band gives out, then the two strong modes that give the
// bite. Their frequencies and heights are what separate one voicing from
// another far more than the box does.
//
// **The driver's own top.** Cone mass and voice-coil inductance together roll
// the top off from around 4 kHz, second order. This is why a guitar cabinet is
// 25 dB down at 8 kHz and why nothing above it survives.
//
// **The microphone.** Two controls, both physical. Moving across the cone from
// the dust cap to the surround changes which part of the cone the microphone is
// looking at, and the outer cone radiates almost nothing above a couple of
// kilohertz -- so the position is a lowpass whose corner sweeps by two octaves.
// Backing the microphone away removes the proximity lift, which for a
// directional microphone is a first-order shelf whose corner is c/(2*pi*d) and
// which is worth several decibels inside 10 cm.
//
// ---------------------------------------------------------------------------
// Measured, from this code
// ---------------------------------------------------------------------------
//
// Three voicings, microphone on the dust cap at 5 cm, in dB relative to each
// one's own level at 1750 Hz:
//
//         Hz     40    100    200    500    1.2k   1.75k   2.5k     5k     8k
//   combo     -20.5   -9.5   -5.9   -5.5   -2.6     0.0   +1.4   -5.9  -18.2
//   modern    -16.7   -2.2   -0.2   -6.0   -4.7     0.0   -3.5  -11.7  -23.7
//   vintage   -14.0   -0.6   +1.1   -2.7   -0.0     0.0   -1.9  -13.6  -25.9
//
// The open-back combo is 7 dB thinner at 100 Hz and carries 6 dB more at 5 kHz;
// the vintage 4x12 has its peak an octave lower and gives up sooner above it.
// Those are the differences people describe, arrived at from the box, the cone
// and the coil rather than fitted to a target.
//
// The microphone, on the modern 4x12, same reference:
//
//        across the cone      200 Hz   3.5k    8k
//        dust cap              -0.2   -4.7  -23.7
//        halfway               +0.2   -8.3  -34.2
//        surround              +4.4  -14.5  -42.0
//
//        distance             100 Hz   2k     4k
//        2 cm  (+9.0 dB)       +7.07  +5.17  -1.26
//        5 cm  (+6.1 dB)       +4.12  +4.64  -1.31
//        15 cm (+2.5 dB)       +0.55  +4.60  -1.31
//        50 cm (+0.0 dB)       -1.93  +4.60  -1.31
//
// Position sweeps the top by ten decibels and leaves the low end where it was;
// distance moves the low end by nine and leaves everything above 4 kHz
// untouched to three decimal places. Two controls, two mechanisms, no overlap.
//
// Sample rate: like SpeakerLoad, this belongs inside the oversampled section --
// CLAUDE.md section 6 is explicit that a cabinet response is exactly the case
// where a plain biquad's bilinear warping matters, because the shape above
// Fs/8 is the shape that is being modelled.

#include <algorithm>
#include <array>
#include <cmath>

#include "Biquad.hpp"

namespace tezla::dsp {

/// One cone resonance: where the cone stops moving as a piston starts to show.
struct BreakupMode
{
    double frequencyHz { 1000.0 };
    double q { 2.0 };
    double gainDb { 0.0 };

    [[nodiscard]] bool operator== (const BreakupMode&) const = default;
};

/// A cabinet's physical description. Everything a voicing is.
struct CabinetVoicing
{
    // ---- the enclosure ------------------------------------------------------

    /// The alignment's corner: fs*sqrt(1 + Vas/Vb) for a sealed box, or near
    /// the driver's free-air fs for an open one.
    double boxCornerHz { 105.0 };

    /// Qtc. Above 0.707 there is a peak just above the corner.
    double boxQ { 0.85 };

    // ---- the open back, if there is one -------------------------------------

    /// Where the rear radiation stops cancelling the front. Zero means a sealed
    /// box and switches the whole rear section off.
    double rearCornerHz { 0.0 };

    double rearQ { 0.6 };

    /// How much low end the cancellation costs, in dB. Negative. A full dipole
    /// would fall away for ever; a real back is partly closed, so this is the
    /// floor it reaches.
    double rearGainDb { 0.0 };

    /// The first cancellation null, which diffraction and absorption fill in to
    /// a few decibels rather than leaving as the arithmetic zero.
    double rearNotchHz { 0.0 };
    double rearNotchQ { 2.0 };
    double rearNotchDb { 0.0 };

    // ---- the cone -----------------------------------------------------------

    std::array<BreakupMode, 3> breakup {
        BreakupMode { 480.0, 1.6, -3.5 },
        BreakupMode { 1750.0, 2.4, 6.0 },
        BreakupMode { 3100.0, 3.0, 4.5 }
    };

    /// Cone mass and voice-coil inductance, together.
    double topCornerHz { 4200.0 };
    double topQ { 0.75 };

    [[nodiscard]] bool operator== (const CabinetVoicing&) const = default;
};

namespace cabinets {

/// Open-backed combo with a single 12 inch driver. Thin at the bottom because
/// the rear radiation cancels most of it, and bright because there is no box
/// resonance holding the low mids up.
[[nodiscard]] inline CabinetVoicing combo() noexcept
{
    CabinetVoicing v;
    v.boxCornerHz = 82.0;      // near free air: an open box loads the cone barely at all
    v.boxQ = 0.62;
    v.rearCornerHz = 330.0;    // c/(2d) for a path of about half a metre round the baffle
    v.rearQ = 0.6;
    v.rearGainDb = -11.0;
    v.rearNotchHz = 640.0;
    v.rearNotchQ = 2.0;
    v.rearNotchDb = -4.0;
    v.breakup = { BreakupMode { 600.0, 1.5, -2.5 },
                  BreakupMode { 2200.0, 2.0, 5.0 },
                  BreakupMode { 4200.0, 3.0, 2.5 } };
    v.topCornerHz = 4600.0;
    v.topQ = 0.75;
    return v;
}

/// Closed 4x12 with later, stiffer cones. The tightest low end of the three and
/// the highest breakup, which is the bite people reach for on a modern rhythm
/// sound.
[[nodiscard]] inline CabinetVoicing modernFourByTwelve() noexcept
{
    return CabinetVoicing {};   // the defaults above are this cabinet
}

/// Closed 4x12 with lighter, earlier cones. The breakup sits an octave lower
/// and the top gives out sooner: more midrange honk, less air.
[[nodiscard]] inline CabinetVoicing vintageFourByTwelve() noexcept
{
    CabinetVoicing v;
    v.boxCornerHz = 98.0;
    v.boxQ = 0.78;
    v.breakup = { BreakupMode { 420.0, 1.4, -2.5 },
                  BreakupMode { 1450.0, 2.0, 7.0 },
                  BreakupMode { 2600.0, 2.6, 3.0 } };
    v.topCornerHz = 3400.0;
    v.topQ = 0.70;
    return v;
}

} // namespace cabinets

struct CabinetParameters
{
    CabinetVoicing voicing {};

    /// Where the microphone sits across the cone: 0 is on the dust cap, 1 is at
    /// the surround. This is a lowpass whose corner sweeps two octaves, because
    /// the outer cone radiates almost nothing above a couple of kilohertz.
    double micPosition { 0.0 };

    /// How far back, in metres. Under about 10 cm the proximity lift is worth
    /// several decibels; past 30 cm it has gone.
    double micDistanceMetres { 0.05 };

    /// Bit-exact passthrough. CLAUDE.md section 7.
    bool bypassed { false };

    [[nodiscard]] bool operator== (const CabinetParameters&) const = default;
};

class Cabinet
{
public:
    /// The beaming lowpass with the microphone on the dust cap, and at the
    /// surround. Two octaves apart, which is what a sweep across a 12 inch cone
    /// actually costs.
    static constexpr double kMicCapHz = 6000.0;
    static constexpr double kMicEdgeHz = 1500.0;

    /// The proximity lift never exceeds this, however close the microphone is
    /// put. The theoretical first-order rise has no limit; a real capsule's
    /// does, and an unbounded one would make the control unusable.
    static constexpr double kMaximumProximityDb = 9.0;

    /// Closer than this and the model stops meaning anything -- the microphone
    /// is inside the near field of a moving cone.
    static constexpr double kMinimumDistanceMetres = 0.01;

    /// Speed of sound, for the proximity corner.
    static constexpr double kSpeedOfSound = 343.0;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        design();
        reset();
    }

    void reset() noexcept
    {
        for (auto& section : sections_)
            section.reset();
    }

    void setParameters (const CabinetParameters& parameters) noexcept
    {
        if (parameters == parameters_)
            return;

        parameters_ = parameters;
        design();
    }

    [[nodiscard]] const CabinetParameters& getParameters() const noexcept { return parameters_; }

    [[nodiscard]] double process (double x) noexcept
    {
        if (parameters_.bypassed)
            return x;

        for (std::size_t i = 0; i < used_; ++i)
            x = sections_[i].process (x);

        return x;
    }

    /// The whole cabinet's magnitude at one frequency, as the product of its
    /// sections. For tests and for drawing the curve on a panel.
    [[nodiscard]] double magnitudeAt (double frequencyHz) const noexcept
    {
        if (parameters_.bypassed)
            return 1.0;

        double magnitude = 1.0;

        for (std::size_t i = 0; i < used_; ++i)
            magnitude *= sections_[i].getCoefficients().magnitudeAt (frequencyHz, sampleRate_);

        return magnitude;
    }

    /// How many biquads the current voicing costs.
    [[nodiscard]] std::size_t getSectionCount() const noexcept { return used_; }

    /// Where the beaming lowpass sits for a given position across the cone.
    ///
    /// Geometric rather than linear, because a filter corner is heard in
    /// octaves: half travel lands halfway between the two in pitch, not in
    /// hertz.
    [[nodiscard]] static double beamingCornerHz (double position) noexcept
    {
        const double p = std::clamp (position, 0.0, 1.0);
        return kMicCapHz * std::pow (kMicEdgeHz / kMicCapHz, p);
    }

    /// Where a directional microphone's proximity rise begins: c/(2*pi*d).
    ///
    /// This is the start of a 6 dB/octave ramp with no end to it. An ideal
    /// pressure-gradient transducer at 2 cm really is 29 dB up at 100 Hz, which
    /// is why nobody mics a cabinet that close without a highpass somewhere --
    /// and why every microphone built for the job has a compensating rolloff
    /// that the ideal transducer does not.
    [[nodiscard]] static double proximityRiseStartsHz (double metres) noexcept
    {
        const double d = std::max (metres, kMinimumDistanceMetres);
        return kSpeedOfSound / (6.283185307179586 * d);
    }

    /// Where the shelf that stands in for that ramp is placed.
    ///
    /// The ramp is capped at kMaximumProximityDb, so it reaches its full gain
    /// an octave per 6 dB below where it started, and below that it is flat --
    /// which is a shelf. Putting the corner at the *start* of the ramp instead
    /// is wrong in a way that is easy to miss and audible immediately: at 2 cm
    /// the ramp starts at 2.7 kHz, so a shelf there lifts the whole midrange by
    /// 9 dB and the cabinet stops sounding like one.
    [[nodiscard]] static double proximityCornerHz (double metres) noexcept
    {
        const double start = proximityRiseStartsHz (metres);
        return start / std::pow (2.0, proximityGainDb (metres) / 6.0);
    }

    [[nodiscard]] static double proximityGainDb (double metres) noexcept
    {
        const double d = std::max (metres, kMinimumDistanceMetres);

        // Zero by a third of a metre, rising towards the cap as the microphone
        // closes in. The 0.05 m reference is where a cabinet is usually mic'd.
        const double closeness = std::clamp (std::log (0.33 / d) / std::log (0.33 / 0.02), 0.0, 1.0);
        return kMaximumProximityDb * closeness;
    }

private:
    static constexpr std::size_t kMaxSections = 9;

    void design() noexcept
    {
        const auto& v = parameters_.voicing;

        used_ = 0;

        const auto add = [this] (const BiquadCoefficients<double>& c)
        {
            if (used_ < kMaxSections)
                sections_[used_++].setCoefficients (c);
        };

        // The enclosure's alignment.
        add (design::highpass (std::max (v.boxCornerHz, 1.0), std::max (v.boxQ, 0.1), sampleRate_));

        // The rear radiation, if the back is open.
        if (v.rearGainDb < 0.0 && v.rearCornerHz > 0.0)
            add (design::lowShelf (v.rearCornerHz, std::max (v.rearQ, 0.1), v.rearGainDb, sampleRate_));

        if (v.rearNotchDb < 0.0 && v.rearNotchHz > 0.0)
            add (design::peak (v.rearNotchHz, std::max (v.rearNotchQ, 0.1), v.rearNotchDb, sampleRate_));

        // The cone.
        for (const auto& mode : v.breakup)
            if (mode.frequencyHz > 0.0 && mode.gainDb != 0.0)
                add (design::peak (mode.frequencyHz, std::max (mode.q, 0.1), mode.gainDb, sampleRate_));

        // The driver's own top, then what the microphone can see of it.
        add (design::lowpass (std::max (v.topCornerHz, 1.0), std::max (v.topQ, 0.1), sampleRate_));
        add (design::lowpass (beamingCornerHz (parameters_.micPosition), 0.7, sampleRate_));

        // And how close it is standing.
        const double proximity = proximityGainDb (parameters_.micDistanceMetres);

        if (proximity > 0.0)
            add (design::lowShelf (proximityCornerHz (parameters_.micDistanceMetres),
                                   0.7, proximity, sampleRate_));
    }

    double sampleRate_ { 48000.0 };

    CabinetParameters parameters_;

    std::array<Biquad<double>, kMaxSections> sections_ {};
    std::size_t used_ { 0 };
};

} // namespace tezla::dsp
