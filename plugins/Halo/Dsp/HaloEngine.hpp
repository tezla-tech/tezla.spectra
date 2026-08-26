#pragma once

// Halo -- a harmonic exciter and enhancer.
//
// Framework-free: no JUCE, no VST3 headers, nothing platform specific. It takes
// double buffers and a sample rate, which is what lets the whole thing be
// measured offline before it ever reaches a DAW.
//
// Signal flow, per block:
//
//   in --+----------------------------------------- dry (delayed to match) --+
//        |                                                                   |
//        +-- [ oversampled x1/x2/x4/x8 ]                                     |
//              LR4 split at Focus -> band                                    |
//                Above: the high side, an exciter                            |
//                Below: the low side, a bass enhancer                        |
//              band limit lowpass          (Chebyshev generator only)        |
//              band envelope                                                 |
//              normalise by envelope^Track                                   |
//              harmonic generator                                            v
//                Curve:     odd/even blend, ADAA'd                         sum --> output
//                Chebyshev: chosen harmonics, direct                          ^
//              restore envelope^Track                                         |
//              DC block                                                       |
//              Floor highpass / Ceiling lowpass on the harmonics              |
//              Punch: transient-dependent gain                                |
//              Amount -------------------------------------------------------+
//            [ /oversampled ]
//
// The dry path is never filtered and the wet path carries almost no
// fundamental, so unlike a conventional exciter there is nothing for the two to
// comb against. See HarmonicGenerator.hpp for exactly how much "almost" is, and
// ChebyshevGenerator.hpp for the other generator's very different answer.

#include <array>
#include <vector>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/ChebyshevGenerator.hpp>
#include <tezla/dsp/Crossover.hpp>
#include <tezla/dsp/DcBlocker.hpp>
#include <tezla/dsp/HalfbandFir.hpp>
#include <tezla/dsp/HarmonicGenerator.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/SmoothedValue.hpp>

namespace tezla::halo
{

/// Which side of Focus gets excited.
enum class BandMode
{
    /// Everything above Focus. The classic exciter: harmonics of the presence
    /// region land in the air band.
    Above = 0,

    /// Everything below Focus. Harmonics of a 40 Hz sub land at 80 and 120 Hz,
    /// where a laptop speaker can actually reproduce them and the ear supplies
    /// the missing fundamental. This is the bass-enhancer half of the job.
    Below
};

/// Which engine makes the harmonics.
enum class Generator
{
    /// Two smooth curves blended by Colour. You choose a shape and accept the
    /// series that falls out of it -- which is how every exciter works, and
    /// which is why you cannot ask one for "the fifth, nothing else".
    Curve = 0,

    /// Chebyshev harmonic synthesis (Le Brun, JAES 27(4), 1979). You choose the
    /// series and the curve is derived from it. Drive, Colour and Track have no
    /// meaning here: Index replaces Drive, the harmonic levels replace Colour,
    /// and Track is pinned at 1 because unit amplitude is the precondition the
    /// whole method rests on.
    Chebyshev
};

struct Parameters
{
    BandMode  bandMode  { BandMode::Above };
    Generator generator { Generator::Curve };
    double    focusHz   { 3000.0 };   ///< 40 .. 12000
    double    drive     { 0.4 };      ///< 0 .. 1, Curve only
    double    colour    { 0.5 };      ///< 0 = odd harmonics, 1 = even, Curve only
    double    track     { 0.35 };     ///< 0 = level dependent, 1 = constant ratio, Curve only
    double    punch     { 0.0 };      ///< 0 .. 1, transient dependence

    bool      floorOn   { false };
    double    floorHz   { 200.0 };    ///< 20 .. 2000, harmonics below this removed
    bool      ceilingOn { true };
    double    ceilingHz { 16000.0 };  ///< 2000 .. 20000, harmonics above this removed

    /// Stereo width of the generated harmonics only. 1 is unchanged, exactly;
    /// 0 folds them to mono; 2 doubles their side content. The dry signal is
    /// never touched by this, so however wide the air gets the sub underneath it
    /// stays where it was.
    double    width     { 1.0 };      ///< 0 .. 2

    double    amountDb  { 0.0 };      ///< -60 .. +12; -60 is silence, exactly
    bool      listen    { false };    ///< solo the generated harmonics
    bool      autoTrim  { true };

    double    inputDb   { 0.0 };      ///< -24 .. +24
    double    outputDb  { 0.0 };      ///< -24 .. +24

    // ---- Chebyshev generator only -------------------------------------------

    /// Linear level for harmonics 2 through 8, in that order. All zero makes the
    /// generator exactly the zero function.
    std::array<double, dsp::ChebyshevGenerator::kNumHarmonics> harmonics
        { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

    /// Le Brun's waveshaping index. 1 is the exact point; below it the
    /// harmonics blend into one another, above it the clamp engages and what
    /// comes out is the wreckage of a chosen series rather than one.
    double chebIndex { 1.0 };       ///< 0 .. 2

    /// Slope across the seven levels. 0 leaves a hand-set recipe untouched, to
    /// the bit.
    double chebTilt  { 0.0 };       ///< -1 .. +1

    dsp::OversamplingMode oversampling { dsp::OversamplingMode::Auto };
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    /// Below this the Amount control is silence rather than a very quiet
    /// harmonic path, so the plugin can be genuinely bypassed from the front
    /// panel and not merely made inaudible.
    static constexpr double kAmountSilenceDb = -60.0;

    /// Allocates, for the worst case rather than for the current settings.
    /// Never call from the audio thread.
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /// Everything the oversampling factor changes, and nothing that allocates.
    /// Safe from the audio thread -- which is why prepare() sizes for x8
    /// whatever factor is actually running.
    void setOversamplingFactor (int factor);

    /// Clears every filter, envelope and delay line. No state may survive this.
    void reset();

    /// Cheap; safe to call once per block from the audio thread. Returns true
    /// if the oversampling factor changed, in which case the host must be told
    /// about the new latency and prepare() has already been re-run internally.
    bool setParameters (const Parameters& parameters);

    /// In-place. `channels` holds `numChannels` pointers to `numSamples` doubles.
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    [[nodiscard]] int    getLatencySamples()     const noexcept { return oversampler_.getLatencySamples(); }
    [[nodiscard]] int    getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }
    [[nodiscard]] double getOversampledRate()    const noexcept { return sampleRate_ * oversampler_.getFactor(); }
    [[nodiscard]] double getSampleRate()         const noexcept { return sampleRate_; }

    /// Metering, read from the message thread. Written once per block.
    ///
    /// How much harmonic energy is being added, relative to the input -- the
    /// number a user actually wants from an exciter, and one no level meter on
    /// the output can show them.
    [[nodiscard]] double getHarmonicsDb() const noexcept { return harmonicsDb_; }


private:
    void updateDerivedParameters();
    void updateFilters();

    /// Maps the 0..1 Drive control onto the generator's gain.
    [[nodiscard]] static double driveGainFor (double drive) noexcept;

    /// Highest harmonic the Chebyshev recipe actually asks for, from the
    /// parameters rather than from the smoothed levels -- a corner derived from
    /// a value still in motion would chatter every time a level was touched.
    [[nodiscard]] int highestRequestedHarmonic() const noexcept;

    double sampleRate_   { 44100.0 };
    int    maxBlockSize_ { 512 };
    int    numChannels_  { 2 };

    Parameters parameters_;

    // What prepare() actually configured, as opposed to what the parameters now
    // ask for. Comparing against these rather than against a "have parameters
    // been set yet" flag is what makes the very first setParameters() call take
    // effect -- prepare() necessarily runs before any parameters are known, so
    // a flag-based check silently ignores the first one.
    int preparedFactor_ { 0 };

    dsp::Oversampler oversampler_;

    struct ChannelState
    {
        dsp::LinkwitzRiley4<double>        focus;
        dsp::Adaa1<dsp::HarmonicGenerator> generator;
        dsp::DcBlocker<double>             dcBlockerA, dcBlockerB;
        dsp::Biquad<double>                floorA, floorB;      ///< 24 dB/oct, on the harmonics
        dsp::Biquad<double>                ceilingA, ceilingB;

        /// Band limit for the Chebyshev generator, ahead of everything else.
        ///
        /// T_n of a band topping out at B reaches n*B, so content above
        /// internalNyquist / n folds back. Limiting the band is what turns "this
        /// aliases less" into "this cannot alias", and it is why the generator
        /// needs no antialiasing of its own. Not used in Curve mode, where the
        /// shaper is ADAA'd instead and the band is left alone.
        dsp::Biquad<double>                limitA, limitB;

        /// The dry path is delayed rather than mixed inside the oversampled
        /// block, which is where this differs from Emberdrive deliberately.
        /// The oversampler's round-trip latency is a whole number of base-rate
        /// samples by design, so a plain integer delay aligns the two exactly
        /// and the dry signal comes through bit-for-bit untouched -- no
        /// passband ripple, no filter, nothing. For a plugin whose whole claim
        /// is that it does not disturb the source, that is worth more than the
        /// alignment convenience of mixing inside.
        dsp::DelayLine dryDelay;
    };

    std::vector<ChannelState> channels_;
    std::vector<std::vector<double>> dryInput_;   ///< base-rate copy of the input

    /// One generator for both channels, and one envelope driving it.
    ///
    /// Stereo-linked is not a preference here, it is a requirement: the
    /// generator is a nonlinearity, and letting the two channels normalise
    /// themselves independently would pull the centre image apart the moment
    /// the two sides differ. CLAUDE.md 7 says so; this is what obeying it
    /// looks like.
    dsp::HarmonicGenerator generator_;

    /// Stereo-linked for the same reason, and stateless besides -- there is no
    /// ADAA history here, because a polynomial on a band-limited signal is
    /// already band-limited and ADAA only costs fundamental rejection. See
    /// ChebyshevGenerator.hpp for the measurement that settled it.
    dsp::ChebyshevGenerator chebyshev_;

    double meanSquareA_ { 0.0 };   ///< first of two cascaded averaging poles
    double meanSquare_  { 0.0 };
    double envelope_   { 0.0 };

    /// A third averaging pole, for the Chebyshev generator only.
    ///
    /// Two poles leave the mean square of a tone rippling at twice its
    /// frequency, about 87 dB down at 400 Hz. The Curve generator does not care;
    /// Chebyshev divides by this number, so the ripple amplitude-modulates the
    /// normalised band and puts energy straight back at the fundamental -- the
    /// one thing the mode claims is absent. Measured, the leak tracked the
    /// ripple at exactly 12 dB per octave of tone frequency, which is two poles
    /// and confirms where it comes from: -64 dB at 100 Hz, and 100 Hz is
    /// precisely the material this mode is for.
    ///
    /// A third pole costs one multiply-add and buys another 43 dB. It is not
    /// shared with the Curve path, which would change a sound that is already
    /// measured and shipped.
    double chebMeanSquare_ { 0.0 };
    double chebEnvelope_   { 0.0 };
    double punchFast_  { 0.0 };
    double punchSlow_  { 0.0 };

    // Smoothed, per-sample at the oversampled rate.
    dsp::SmoothedValue<double> inputGain_;
    dsp::SmoothedValue<double> amountGain_;
    dsp::SmoothedValue<double> outputGain_;
    dsp::SmoothedValue<double> widthAmount_;

    /// Derived from the envelope, so they are recomputed at control rate and
    /// smoothed rather than stepped. `scale` is envelope^Track, which costs a
    /// pow -- affordable a few thousand times a second, not 384,000.
    dsp::SmoothedValue<double> scale_;
    dsp::SmoothedValue<double> punchGain_;

    // Smoothed, advanced once per block: these rebuild filter coefficients or
    // the generator's curve, which costs transcendentals.
    dsp::SmoothedValue<double> focus_;
    dsp::SmoothedValue<double> driveGain_;
    dsp::SmoothedValue<double> colour_;
    dsp::SmoothedValue<double> track_;
    dsp::SmoothedValue<double> chebIndex_;
    dsp::SmoothedValue<double> chebTilt_;
    dsp::SmoothedValue<double> bandLimit_;
    std::array<dsp::SmoothedValue<double>, dsp::ChebyshevGenerator::kNumHarmonics> chebGains_;

    /// 0 is all Curve, 1 is all Chebyshev. Switching generator is a discrete
    /// change between two quite different signals, so it crossfades rather than
    /// steps -- CLAUDE.md 7. In steady state one branch is skipped entirely, so
    /// the second generator costs nothing while it is not selected.
    dsp::SmoothedValue<double> generatorFade_;

    /// How often the envelope-derived values are recomputed, in oversampled
    /// samples. Chosen from the actual rate so the control rate lands near
    /// 24 kHz whatever the session and oversampling factor -- otherwise the
    /// plugin's transient response would change between 48 and 192 kHz, which
    /// is exactly what CLAUDE.md 6 forbids.
    int controlInterval_  { 1 };
    int controlCountdown_ { 0 };

    double envelopeCoefficient_ { 0.0 };
    double punchFastAttack_ { 0.0 };
    double punchFastRelease_ { 0.0 };
    double punchSlowAttack_ { 0.0 };
    double punchSlowRelease_ { 0.0 };
    double envelopeFloor_   { 0.0 };

    /// What updateFilters() last actually built, as opposed to what the
    /// parameters now ask for.
    double builtFocusHz_   { 0.0 };
    double builtFloorHz_   { 0.0 };
    double builtCeilingHz_ { 0.0 };
    double builtLimitHz_   { 0.0 };

    double autoTrimGain_ { 1.0 };
    double harmonicsDb_  { kAmountSilenceDb };

    bool floorActive_   { false };
    bool ceilingActive_ { true };
};

} // namespace tezla::halo
