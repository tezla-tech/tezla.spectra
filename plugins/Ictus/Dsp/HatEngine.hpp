// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The hat engine -- one hit of a metal instrument: closed hat, open hat, and
// with the ratios spread and the bands opened, a ride, a china, or scrap.
//
// A cymbal has no harmonic series. Its partials are set by the shape of a
// stiff, irregular plate and land where they land, so the way to synthesise
// one is to sum oscillators whose frequencies are deliberately incommensurate
// and then to FILTER the result rather than to tune it (Reid, "Synthesizing
// Percussion", Sound On Sound: why cymbals and hats are unpitched; Clark, the
// Nord Modular percussion chapter: metal as many incommensurate oscillators
// through a band-pass AND AN OVERDRIVE; both read first-hand, see
// docs/DSP-REFERENCES.md).
//
// The generator is six pulses through a pair of band-passes, which is the
// topology analysed in Werner, Abel and Smith, "The TR-808 Cymbal: a
// Physically-Informed, Circuit-Bendable Digital Model" (ICMC/SMC 2014,
// CC BY 3.0, read first-hand). Three numbers are TAKEN from that paper rather
// than derived, under CLAUDE.md section 9's rule that a published measurement
// no measurement of ours could check is better copied than approximated:
// `kMetalHz` (the six oscillator frequencies), `kDutyCycle` (0.4798, their
// measured duty) and `kUpperBandRatio` (7100 / 3440, the spacing of the two
// band-passes they sum into). Nothing else comes from it, and no control,
// preset or set is named after any product (CLAUDE.md section 2.1).
//
// ---------------------------------------------------------------------------
// What the first version got wrong, and what fixes it
// ---------------------------------------------------------------------------
//
// Six pulses through a band-pass is a SPARSE COMB. It reads as a metallic
// chord rather than as a cymbal, it has no transient, and it decays at one
// uniform rate from the moment it starts -- which is the one thing no real
// cymbal does. Four things put that right, and each is a control:
//
//   RING     The low three oscillators multiplied by the high three. A ring
//            modulator's output holds the SUM AND DIFFERENCE of every pair of
//            harmonics in its two inputs, so one multiply turns two sparse
//            combs into a dense inharmonic wash -- which is what a plate of
//            metal actually sounds like. Both operands are low-passed at an
//            eighth of the internal rate FIRST, so every product lands below
//            a quarter of it and the multiplication cannot alias at all. That
//            is the whole reason the low-pass is there.
//   DRIVE    The overdrive the Nord chapter's cymbal patch ends with, as an
//            antialiased soft clip. It fills the gaps between partials with
//            intermodulation and glues the layers into one instrument.
//   DAMP     A low-pass that CLOSES as the hit decays. On a real cymbal the
//            high modes die first -- that fall from bright to dark over the
//            ring is most of what "lush" means, and a fixed filter cannot do
//            it. Retuned once per control chunk, so it costs one tangent
//            every 32 samples and nothing per sample.
//   STRIKE   A short, loud transient on top of the body envelope: the stick.
//            With Damp up it is automatically the brightest part of the hit,
//            because Damp is still wide open when it lands.
//
// The noise is a layer in its own right now rather than a garnish, and --
// this is the part that matters -- it is not a SEPARATE layer. On a real
// cymbal the hiss IS the plate: the sizzle is its own modes being excited
// chaotically rather than struck cleanly, which is why the noise and the
// harmonics in a sampled hat sound like one object, and why noise simply
// added beside an oscillator bank sounds like two things glued together.
//
//   SIZZLE   runs the noise through six band-passes at the frequencies the
//            metal is HEARD at: for each partial, its harmonic nearest one of
//            the two bands (the low three partials aim at Colour, the high
//            three at the upper band). At 0 it is raw filtered hiss beside
//            the metal; at 1 every bit of it is the plate speaking. The
//            crossfade is exact at both ends and the bank is not run at 0.
//
//            The first version rang the hiss at the six FUNDAMENTALS, 205 to
//            800 Hz -- and the chain then high-passes at 1.2 kHz and listens
//            through bands at 3.4 and 7.1 kHz, so those resonances were 15 to
//            30 dB under the floor. Measured at the default chain (I4.3):
//            Sizzle 100 cut the hiss by 16.5 dB and put 0.0 % of its energy
//            within a semitone of a partial; what it did was dull the hiss
//            (86 % above 6 kHz to 57 %). Nothing you hear of the metal is a
//            fundamental -- that is the picture's own caption -- so the hiss
//            has to ring at the harmonics, where the metal is.
//   AIR      its level, with its own tone and its own decay -- so a long
//            hiss can shimmer over a short metal body, or a fast chiff can
//            sit on a long one, which is how a sampled hat is put together.
//
// The rig's fourth round (I4.4) asked for more say over the hiss, and for an
// open pad that can hold longer than the closed one. Four controls on the
// noise, one on the envelope, every one exact at its neutral setting:
//
//   AIR TILT  a slope about 6 kHz, dark to bright. Air tone is a high-pass
//             and could only ever thin the hiss; this can dull it. A low
//             shelf and a high shelf of opposite sign, +-12 dB at the ends,
//             designed per hit and not run at all at 0.
//   AIR ATTACK the hiss's own rise, up to half a second. The metal is struck
//             and the wash comes up behind it -- what an open hat does in its
//             first 50 ms (F&R 20.3), and the cheap standing-in for the
//             build-up parked in docs/ROADMAP.md section 9.
//   GRAIN     the hiss's density, from an event every sample to a sparse
//             crackle of 300 a second. A cymbal's sizzle is chaotic rather
//             than Gaussian (Chaigne, Touze & Thomas), and the sandy, gritty
//             hat lives at the sparse end; through Sizzle's band-passes each
//             impulse rings the partials, which is a metallic crackle rather
//             than a click. Per second, not per sample, so it is the same
//             texture at every rate. See kGrainMakeupExponent for the level.
//   VEL > AIR velocity to the hiss, off by default: a soft tap with less
//             spray, or a pedal chick with more.
//   OPEN HOLD the open pad's own plateau, up to a second, behind a LINK lamp
//             that is lit by default -- lit, both pads share Hold as they
//             always did; dark, the open pad holds for its own time. On a
//             plate hat a long hold plateaus the envelope while the plate
//             darkens underneath it, its high modes dying first, which is how
//             a real open hat behaves; the six pulses hold flat.
//
// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
//
//   six pulses --> (+ the ring product) --> x metal envelope --,
//        |                                                      |
//        | the same six frequencies                             +--> Drive
//        v                                                      |    -> the
//   noise -> Air tone -> Air tilt -> (Sizzle) -> x air env ---------'     two
//   (Grain thins the noise before any of it)
//                                                                    bands
//                                                                    -> the
//                                                                    high-pass
//                                                                    -> Damp
//
// The envelopes are BEFORE the filters, where a real instrument's are, so the
// filters ring on past them. The hit is retired when both envelopes are dead
// and the filters have fallen below -180 dB, which takes well under a
// millisecond and is measured; the cut is then exact and the pad's activity
// count is honest (CLAUDE.md section 7).
//
// ---------------------------------------------------------------------------
// The plate, and why the six pulses could never be fat
// ---------------------------------------------------------------------------
//
// The rig's verdict on all of the above was "thin and tinny", and the ask was
// the chunky hat of a sampled real pair of cymbals. Six pulses cannot get
// there whatever the filters do: open the bands and you hear a pulse chord,
// not a cymbal, because the source has no body that survives them. A real
// 14-inch plate has hundreds of modes -- a mode every 20-30 Hz by thin-plate
// theory, some 900 below 20 kHz (Fletcher & Rossing section 3.6; Perrin et
// al. 2008 counted "over 100 modes, plus many split degenerate partners" on
// one crash cymbal below 3 kHz) -- so above 2 kHz it is structured noise and
// below that it has real body. That density is what "chunky" is.
//
//   PLATE    a bank of up to 64 modes (`dsp::ModalResonator`, the bank
//            Malleus and the snare shell already use), placed by the modified
//            Chladni law the cymbal literature fits real cymbals to:
//
//                f(m, n) = c * (m + beta * n)^p
//
//            m nodal diameters, n nodal circles. The exponent p = 1.47 is
//            TAKEN from Fletcher & Rossing's Table 20.1 (Rossing 1982): it is
//            the fit for the 14-inch thick cymbal, the one closest to a hat
//            top and the only cymbal in the table a single straight line fits.
//            Chladni's flat plate has beta = 2; on a domed cymbal the
//            nodal-circle families sit far higher, and beta = 7.4 is a design
//            choice read off Perrin et al.'s two cymbals, where (2,1) lands on
//            (9,0) for the 18-inch and (1,1) on (8,0) for the 12-inch. Tune
//            sets c so the lowest mode (2,0) sits AT Tune, as the lowest pulse
//            does. A deterministic +/-1.2 % jitter per mode stands in for the
//            doublet splitting and near-neighbour mixing Perrin measured, so no
//            two modes are commensurate and every hit is still the same hit.
//            Spread widens the jitter.
//
//            Each mode's own decay follows the damping law Ducceschi & Touze
//            (JSV 2015) used for their cymbal, c_p proportional to omega^0.7,
//            so T60 falls as f^-0.7: the high modes die first on their own,
//            before Damp does anything. The exponent is TAKEN; the anchor (a
//            mode at 1.5 kHz rings for twice the pad's decay) is design.
//
//            The strike excites every mode at once, in phase, with an
//            amplitude falling as f^-0.5 -- a hard tip, since Rossing's
//            spectra show the region above 10 kHz present from the strike and
//            unchanged afterwards.
//
//            What is NOT here, and was tried: the cascade. A real cymbal
//            moves energy upward after the strike through the von Karman
//            quadratic coupling (Chaigne, Touze & Thomas 2005); Rossing
//            measured the 2-10 kHz band building by 10 dB or more in the
//            first 50 ms (F&R 20.3), and that build is most of an open hat's
//            sizzle. The bank's Bloom is that coupling, built and bounded for
//            Malleus -- and on THIS bank it does not work: 64 modes a few tens
//            of hertz apart put strong difference tones into the coupling
//            term, and a resonator driven at a frequency far below its own
//            answers with a forced response about 1 / (2 sin(omega/2)) times
//            the drive, which for a 205 Hz mode at 192 kHz is 150x. Measured
//            at full velocity, struck at the calibrated level: the first 80 ms
//            had a spectral centroid of 128 Hz on a plate whose lowest mode is
//            205 Hz. At a tenth of the amount nothing collapsed and nothing
//            measurably built either. So the plate is linear, the cymbal's
//            fall from bright to dark is Damp's and the damping law's, and the
//            build-up is parked in docs/ROADMAP.md with the mechanism and the
//            route that would work (a coherent per-mode drive, not an impulse
//            spread in time -- a slow unipolar push cannot excite a fast mode).
//
//            Plate is a crossfade: 0 is the six pulses exactly -- the bank is
//            not built and the old path is bit for bit what it was -- and 1 is
//            the plate alone. Equal-power in between, with both ends branched.
//
//   GRIT     the crunch of a low-resolution sample path: the summed layers
//            quantised to between 16 and 4 bits before Drive. This is
//            quantisation used as a nonlinearity, NOT a bit-crusher effect --
//            it runs at the internal rate, so the images the decimator removes
//            are gone and what stays is the in-band quantisation error, which
//            is signal-correlated and fills the gaps between partials the way
//            the cascade does. CLAUDE.md section 7 puts a crusher at the host
//            rate for the folded images; a per-pad host-rate stage needs the
//            per-pad buses of I7, and this is honest about being the other
//            thing. Exact at 0 (the crusher's own bypass at 16 bits).
//
// EVERYTHING IS SNAPSHOTTED AT `start()`, as in the kick and the snare.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/Adaa.hpp>
#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Biquad.hpp>
#include <tezla/dsp/Bitcrusher.hpp>
#include <tezla/dsp/Exact.hpp>
#include <tezla/dsp/ModalResonator.hpp>
#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/SvfFilter.hpp>
#include <tezla/dsp/UnisonBank.hpp>
#include <tezla/dsp/Waveshapers.hpp>

namespace tezla::ictus {

/// Every hat control. One set is shared by the closed and open pads -- they
/// are the same pair of cymbals -- with a decay each.
struct HatSettings
{
    // ---- the metal -------------------------------------------------------
    double tuneHz { 205.3 };             ///< where the lowest partial sits, 60..1200 Hz
    double harmonics { 0.0 };            ///< position along the ratio-set list, 0..7
    double spread { 0.0 };               ///< pulls the six apart, 0..1
    double ring { 0.35 };                ///< the low three times the high three, 0..1
    double drive { 0.25 };               ///< soft clip after the layers meet, 0..1

    // ---- the plate ------------------------------------------------------
    double plate { 0.0 };                ///< the six pulses (0) crossfaded to a modal cymbal (1); 0 is exact
    double grit { 0.0 };                 ///< bit depth before Drive: 0 is 16 bits and exact, 1 is 4 bits

    // ---- the noise layer -------------------------------------------------
    double air { 0.45 };                 ///< its level, 0..1
    double airToneHz { 5000.0 };         ///< its own high-pass, 200..12000 Hz
    double airDecay { 1.0 };             ///< its decay as a multiple of the pad's, 0.1..3
    double sizzle { 0.6 };               ///< how much of the hiss rings through the six partials, 0..1
    double airTilt { 0.0 };              ///< the hiss's slope about 6 kHz, -1 (dark) .. +1 (bright); 0 is exact
    double airAttackSeconds { 0.0 };     ///< the hiss's rise, 0..0.5; 0 is instant and exact
    double grain { 0.0 };                ///< the hiss's density: 0 every sample, 1 a sparse crackle; 0 is exact

    // ---- the band the whole thing is heard through -----------------------
    double colourHz { 3440.0 };          ///< the lower band-pass's centre, 800..12000 Hz
    double width { 0.5 };                ///< how wide the two bands are, 0 (narrow) .. 1 (open)
    double highpassHz { 1200.0 };        ///< under the bands, 200..8000 Hz
    double damp { 0.4 };                 ///< how far the top closes as the hit decays, 0..1

    // ---- the envelope ----------------------------------------------------
    double strike { 0.4 };               ///< the stick: a short transient over the body, 0..1
    double holdSeconds { 0.0 };          ///< a plateau before the decay, 0..0.2
    double holdOpenSeconds { 0.0 };      ///< the OPEN pad's own plateau when `holdLink` is off, 0..1
    bool   holdLink { true };            ///< lit: the open pad's hold IS `holdSeconds`; dark: `holdOpenSeconds`
    double shape { 0.0 };                ///< 0 exponential, 1 linear
    double decayClosedSeconds { 0.06 };  ///< the closed pad's fall, 0.01..0.4
    double decayOpenSeconds { 0.5 };     ///< the open pad's, 0.1..3

    double level { 0.7 };                ///< 0..1
    bool   choke { true };               ///< a closed hit silences the open pad
    bool   gate { false };               ///< lit: a note-off fades the hit over `releaseSeconds`
    double releaseSeconds { 0.0 };       ///< 0..2; 0 is a 1 ms cut

    // ---- velocity amounts, all of the form x * ((1 - a) + a * v) --------
    double velocityLevel { 1.0 };
    double velocityDecay { 0.3 };
    double velocityColour { 0.4 };
    double velocityStrike { 0.5 };
    double velocityAir { 0.0 };          ///< velocity to the hiss's level; 0 is exact
};

class HatEngine
{
public:
    static constexpr int kOscillators = 6;

    /// The six oscillator frequencies of the circuit analysed in the TR-808
    /// cymbal paper (Werner, Abel and Smith, ICMC/SMC 2014, CC BY 3.0, read
    /// first-hand; the two tunable oscillators at their nominal settings).
    /// TAKEN, not derived -- see the header comment and
    /// docs/DSP-REFERENCES.md.
    static constexpr double kMetalHz[kOscillators] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

    /// The measured duty of those rectangular waves, from the same paper.
    static constexpr double kDutyCycle = 0.4798;

    /// The paper's two band-pass centres, 3440 and 7100 Hz, as a ratio: the
    /// upper band follows Colour at this spacing.
    static constexpr double kUpperBandRatio = 7100.0 / 3440.0;

    /// Width's two ends, as Q. Narrow is a whistle you can pick a partial out
    /// of; open is a band wide enough to hear the whole plate at once.
    static constexpr double kNarrowQ = 4.0;
    static constexpr double kOpenQ = 0.6;
    static constexpr double kHighpassQ = 0.707;

    /// Where the ring modulator's operands are band-limited.
    ///
    /// This is the one number that makes ring modulation safe here. A product
    /// of two signals holds the sum of every pair of their frequencies, so
    /// multiplying two full-band signals puts energy up to twice Nyquist and
    /// folds it back. Low-passing both operands first keeps the products in
    /// range, and 20 kHz is chosen because it is above everything the
    /// decimator will keep -- so the limit costs the audible band nothing.
    ///
    /// **An ABSOLUTE frequency, and that is the whole lesson.** The first
    /// version made it a fraction of the internal rate, which meant the ring
    /// products were built from a 24 kHz band at 192 kHz and a 6 kHz band at
    /// 48 kHz: the same patch was a different instrument at a different host
    /// rate, which is exactly what CLAUDE.md section 6 forbids. Measured
    /// before it was fixed -- the spectral centroid moved from 6951 Hz to
    /// 4849 Hz, 30 % -- and the fraction below is only a floor for a rate too
    /// low to give 20 kHz a quarter of itself.
    static constexpr double kRingOperandHz = 20000.0;
    static constexpr double kRingOperandCeilingFraction = 0.25;

    /// The ring product's weight against the direct sum at Ring 1.
    static constexpr double kRingGain = 2.4;

    /// Drive's pre-gain at 1, and the trim exponent that keeps the level from
    /// running away with it (a full trim by the gain makes the drive quieter
    /// than no drive at all, which is not what a drive control is for).
    static constexpr double kDriveRange = 12.0;

    /// How much of the pre-gain is trimmed back off again.
    ///
    /// A clipper's output level depends on where the signal already sat
    /// against the threshold, so no single exponent holds both engines
    /// exactly level: a full 1/g is right for the clap, whose sum peaks at
    /// 0.37 and is barely clipped, and 8 dB too much for the hat, whose
    /// layers already reach 1.5 before the stage. 0.75 is the compromise,
    /// measured: over the whole control the clap moves +2.1 dB and the hat
    /// -2.7 dB, so Drive buys harmonics rather than loudness either way
    /// (CLAUDE.md section 7) without a level detector in the path.
    static constexpr double kDriveTrimExponent = 0.75;

    /// Damp's widest corner, and the floor it can close to.
    static constexpr double kDampTopHz = 18000.0;
    static constexpr double kDampFloorHz = 700.0;

    /// How steeply Damp follows the envelope at 1: the corner is the top
    /// times the envelope raised to this power.
    static constexpr double kDampExponent = 3.0;

    /// The stick's own fall, 60 dB.
    static constexpr double kStrikeSeconds = 0.006;
    static constexpr double kStrikeFloor = 1.0e-5;

    /// The shortest release -- a note-off with Release at 0 ramps out over
    /// this rather than stepping to zero (the kick's and snare's constant).
    static constexpr double kMinimumReleaseSeconds = 0.001;

    /// Retirement: once both envelopes are dead, the filters are allowed to
    /// ring out until the output has been this small for this many samples in
    /// a row. -180 dB, and a Q of 4 at 800 Hz reaches it in under a
    /// millisecond -- measured in tests/test_Ictus.cpp.
    static constexpr double kQuietThreshold = 1.0e-9;
    static constexpr int kQuietSamples = 64;

    /// How many ratio sets exist today. Positions past the last one clamp to
    /// it, so appending a set gives those positions a meaning without moving
    /// any saved value. **Append-only** (CLAUDE.md section 8).
    static constexpr int kSetCount = 4;

    /// The most Harmonics can be asked for: room for four more sets.
    static constexpr double kMaxHarmonicsPosition = 7.0;

    /// The ratio sets, each ASCENDING in frequency so that the morph pairs
    /// rank with rank: partial n of one set glides to partial n of the next.
    ///
    ///   0  Metal   the paper's six, over the lowest of them. Tight, dense,
    ///              the classic closed hat.
    ///   1  Bell    near-harmonic -- a ride's bell has a pitch, and these
    ///              ratios are close enough to a series to give it one.
    ///   2  Trash   deliberately incommensurate and wider: a china, a lid.
    ///   3  Wide    spread over three octaves; thin, bright, glassy.
    static constexpr double kSets[kSetCount][kOscillators] {
        { 1.0, 1.482709, 1.800292, 2.546030, 2.630298, 3.896737 },
        { 1.0, 1.500000, 2.000000, 2.666667, 3.000000, 4.000000 },
        { 1.0, 1.414214, 2.090000, 2.828427, 3.710000, 5.130000 },
        { 1.0, 1.930000, 3.830000, 5.710000, 7.660000, 9.550000 },
    };

    /// The sets' names, for a value readout and a caption. Framework-free:
    /// plain C strings, in the same order as `kSets`.
    static constexpr const char* kSetNames[kSetCount] { "Metal", "Bell", "Trash", "Wide" };

    /// How far Spread pulls the six apart, in semitones at Spread 1, and the
    /// fixed pattern it pulls them along. The pattern sums to zero so the
    /// set's centre of gravity does not move with the control.
    static constexpr double kSpreadSemitones = 1.5;
    static constexpr double kSpreadPattern[kOscillators] { -1.0, 0.55, -0.35, 0.8, -0.65, 0.65 };

    /// Air's level against the six pulses.
    static constexpr double kAirGain = 1.4;

    /// The Q of the six band-passes Sizzle runs the noise through, and the
    /// make-up that keeps the resonated hiss level with the raw hiss.
    ///
    /// Narrow enough that each one rings and the noise takes the partial's
    /// pitch; wide enough that six of them are a sizzle rather than six
    /// whistles. A band-pass at Q 12 passes a small fraction of the noise
    /// power a flat path does, hence the make-up -- re-measured at I4.3 for
    /// the bank sitting inside the bands rather than under the high-pass,
    /// so that Sizzle 0 and 100 are within a decibel through the default
    /// chain (the old placement lost 16.5 dB).
    static constexpr double kSizzleQ = 12.0;
    static constexpr double kSizzleMakeup = 0.65;

    /// Where Sizzle's six band-passes sit: for each partial, the harmonic of
    /// it nearest the band it is assigned to -- the low three partials to
    /// `colourHz`, the high three to the upper band at `kUpperBandRatio`
    /// times it -- clamped under the rate. Static so the panel's picture
    /// draws the very frequencies the engine rings.
    static void sizzleCentres (const double (&partials)[kOscillators], double colourHz, double rate,
                               double (&centres)[kOscillators]) noexcept
    {
        const double ceiling = rate * 0.45;

        for (int i = 0; i < kOscillators; ++i)
        {
            const double target = i < kOscillators / 2 ? colourHz
                                                       : std::min (colourHz * kUpperBandRatio, ceiling);
            const double partial = std::max (partials[i], 1.0);
            const double harmonic = std::max (1.0, std::round (target / partial));

            centres[i] = std::clamp (harmonic * partial, 20.0, ceiling);
        }
    }

    /// The output trim, chosen from measurement so a default hit lands near
    /// full scale (`tezla-measure ictus` table 3).
    static constexpr double kOutputGain = 1.6;

    /// The noise stream is salted so a hat and a snare with the same hit seed
    /// do not draw the same numbers.
    static constexpr std::uint64_t kAirSalt = 0x2545F4914F6CDD1Dull;

    /// Air Tilt's pivot and range: a low shelf and a high shelf of opposite
    /// sign about 6 kHz, each +-12 dB at the ends of the knob. Sonitus's
    /// tilt, moved up three octaves. The pivot is placed where the hiss's
    /// weight sits once Air tone and the two bands have shaped it (measured
    /// centroid about 10 kHz on a wide-open rig, lower through the default
    /// bands), so the knob reads as a slope and not as a volume: at 4 kHz
    /// nearly all of the hiss was above the pivot and Bright was +10 dB. The
    /// pivot sits under Fs/8 at every internal rate the Auto policy runs
    /// (CLAUDE.md section 6), so the slope is the same at 44.1 k and at
    /// 192 k; and a 0 dB shelf is bit-exactly the identity by `Biquad`'s
    /// a0/a0 normalise, on top of which the stage is branched out at 0.
    static constexpr double kAirTiltPivotHz = 6000.0;
    static constexpr double kAirTiltRangeDb = 12.0;
    static constexpr double kAirTiltQ = 0.7071067811865476;

    /// Grain's far end, in events per second. The hiss is white noise at the
    /// internal rate -- an event every sample -- and Grain thins it to a
    /// sparse stream of single-sample impulses, geometrically, down to this
    /// many a second at 1: a distinct crackle, the vinyl end. Per second
    /// rather than per sample, so the same knob is the same texture at every
    /// rate (CLAUDE.md section 6); half way is about 7 600 a second at the
    /// rates Auto runs, which is sand.
    static constexpr double kGrainMinEventsPerSecond = 300.0;

    /// How the kept impulses are made up for the ones dropped. Constant RMS
    /// would be p^-0.5 -- 25x at the sparse end, and a 25x impulse into a
    /// band-pass is a click at +28 dBFS whatever Drive does. Constant peak
    /// (no make-up) loses 25 dB of level over the knob. Half way between the
    /// two in dB, p^-0.25, keeps a sparse crackle within about 13 dB of the
    /// dense hiss, which is roughly how much louder impulsive sounds read
    /// per unit of RMS anyway. Measured in tests/test_Ictus.cpp.
    static constexpr double kGrainMakeupExponent = 0.25;

    // ---- the plate ------------------------------------------------------

    /// How many modes the plate may hold: the bank's whole capacity. Fewer
    /// are placed when Tune is high, because nothing is placed above
    /// `kPlateTopHz` -- a small plate simply has fewer modes below it.
    static constexpr int kPlateModes = dsp::ModalResonator::kMaxModes;

    /// The exponent of the modified Chladni law, f = c (m + beta n)^p.
    /// TAKEN from Fletcher & Rossing, *The Physics of Musical Instruments*,
    /// Table 20.1 (Rossing 1982): the 14-inch thick cymbal, p = 1.47 -- the
    /// closest cymbal in the table to a hat top, and the only one a single
    /// line fits. Attributed again in docs/DSP-REFERENCES.md.
    static constexpr double kPlateExponent = 1.47;

    /// How much a nodal circle costs in "diameters". Chladni's flat plate
    /// says 2; on a domed cymbal the circle families sit much higher. Read
    /// off Perrin, Swallowe, Zietlow & Moore (Proc. IoA 2008): (2,1) lands on
    /// (9,0) for their 18-inch crash and (1,1) on (8,0) for their 12-inch, so
    /// beta is 7 to 7.6; 7.4 is inside that and not an integer, so the
    /// families interleave rather than land on each other. Design, informed.
    static constexpr double kPlateCircleOffset = 7.4;

    /// The deterministic per-mode jitter at Spread 0, as a fraction of the
    /// frequency. Stands in for the doublet splitting and the near-neighbour
    /// mixing Perrin measured (four peaks within 15 Hz at 340 Hz), and keeps
    /// every pair of modes incommensurate. The lowest mode is never jittered:
    /// it IS Tune. Spread doubles it at 1.
    static constexpr double kPlateJitter = 0.012;

    /// No mode is placed above this, whatever Tune and the rate allow: there
    /// is nothing to hear there and a mode there is arithmetic for nothing.
    static constexpr double kPlateTopHz = 18000.0;

    /// The strike's spectrum: mode amplitude falls as (f / f0)^-tilt. A hard
    /// stick tip -- Rossing's spectra (F&R Fig. 20.6) have the band above
    /// 10 kHz present at the strike and unchanged after it.
    static constexpr double kPlateTilt = 0.5;

    /// Each mode's own decay, T60 = anchorRatio * decay * (anchorHz / f)^exp.
    /// The exponent is TAKEN from Ducceschi & Touze (JSV 344, 2015), whose
    /// cymbal simulation uses the modal damping c_p = 0.007 omega^0.7 -- so
    /// T60 falls as omega^-0.7 and the high modes die first. The anchor is
    /// design: a mode at 1.5 kHz rings for twice the pad's decay, so the
    /// envelope is what you hear shaping the middle of the plate, the low
    /// modes outlast it (the envelope cuts them), and the top falls away on
    /// its own before Damp is asked to do anything.
    static constexpr double kPlateDampExponent = 0.7;
    static constexpr double kPlateDampAnchorHz = 1500.0;
    static constexpr double kPlateDampAnchorRatio = 2.0;
    static constexpr double kPlateMaxT60Seconds = 8.0;

    /// The amplitude the plate is struck at: the root of the modes' summed
    /// squared amplitudes. Kept at the level a Malleus voice rings at (it
    /// peaks near 0.05) so that the bank is used in the regime its own tests
    /// measure, with the level made up at the output by `kPlateGain`; the
    /// bank is linear, so the split is exact and only the readout changes.
    static constexpr double kPlateExcitation = 0.07;

    /// The plate's level against the six pulses at the crossfade's far end,
    /// the excitation above included. From measurement, so the crossfade
    /// moves timbre rather than loudness (CLAUDE.md section 7): at 1 / 0.07
    /// the plate's RMS through the default chain read 2.2 to 3.2 dB above the
    /// metal's (closed and open, default and everything-on) and 0.4 to 0.7 dB
    /// under it through a fat chain (Colour 1.5 kHz, Highpass 300 Hz). At 12
    /// it still read +2.1 dB closed and +1.5 dB open through the default
    /// chain (`tezla-measure ictus` table 3); at 10 it reads +1.5 dB closed
    /// and +1.1 dB open there -- the default Drive of 25 % compresses the
    /// plate's hotter strike, so the level does not follow the gain linearly
    /// -- and level with the metal through a fat chain. As level as two
    /// different spectra through a soft clip get.
    static constexpr double kPlateGain = 10.0;

    /// Grit's far end, in bits. Sixteen is the crusher's exact bypass; four
    /// is coarse enough that the steps are the sound. About six -- the depth
    /// the classic sampled drum machines stored their cymbals at -- sits at
    /// two thirds of the control.
    static constexpr double kGritMinBits = 4.0;

    /// Places the plate's modes for a Tune, in ascending order, and returns
    /// how many were placed. `hz` and `amplitude` are the mode table; the
    /// amplitudes are normalised so their squares sum to `kPlateExcitation`
    /// squared -- the strike, at the level the bank's coupling expects.
    ///
    /// The (m, n) field of the modified Chladni law is walked as a k-way
    /// merge of its nodal-circle families -- each family is monotonic in m,
    /// so the next mode overall is always one of the family heads -- until
    /// `kPlateModes` are placed or every head is above the ceiling. Bounded
    /// work, no sort, no allocation, so it can run at `start()`.
    ///
    /// Static, so the panel's picture draws exactly the modes the engine
    /// rings, the way `ratiosAt` serves both for the six pulses.
    static int plateModesAt (double tuneHz, double spread, double ceilingHz,
                             double (&hz)[kPlateModes], double (&amplitude)[kPlateModes]) noexcept
    {
        constexpr int kFamilies = 12;

        const double tune = std::clamp (tuneHz, 60.0, 1200.0);
        const double ceiling = std::min (ceilingHz, kPlateTopHz);

        // c so that (2,0) sits at Tune.
        const double c = tune / std::pow (2.0, kPlateExponent);
        const double jitter = kPlateJitter * (1.0 + std::clamp (spread, 0.0, 1.0));

        // The family heads: the n = 0 (rim) family starts at m = 2, since
        // m = 0 and 1 with no nodal circle are rigid-body motions of a free
        // plate; the others start at m = 0, the axisymmetric singlet.
        int head[kFamilies] {};
        for (int n = 0; n < kFamilies; ++n)
            head[n] = n == 0 ? 2 : 0;

        const auto frequencyOf = [c] (int m, int n) noexcept
        {
            return c * std::pow (static_cast<double> (m) + kPlateCircleOffset * static_cast<double> (n),
                                 kPlateExponent);
        };

        int count = 0;
        double sumSquares = 0.0;

        // The golden-ratio sequence, keyed on the emission index: a
        // low-discrepancy jitter, the same for every hit.
        constexpr double kGolden = 0.6180339887498949;

        while (count < kPlateModes)
        {
            int best = -1;
            double bestHz = ceiling;

            for (int n = 0; n < kFamilies; ++n)
            {
                const double f = frequencyOf (head[n], n);

                if (f < bestHz)
                {
                    bestHz = f;
                    best = n;
                }
            }

            if (best < 0)
                break;

            ++head[best];

            double f = bestHz;

            // The lowest mode is never jittered -- it IS Tune. The branch is
            // what says so; the sequence's first term happens to be exactly
            // 0.5 as well (a factor of exactly 1.0), so a break-check that
            // removed the branch changed nothing, and one that moved the
            // mode by 1 % went red.
            if (count > 0)
            {
                const double sequence = std::fmod (0.5 + static_cast<double> (count) * kGolden, 1.0);
                f *= 1.0 + jitter * (2.0 * sequence - 1.0);
            }

            if (f >= ceiling)
                continue;

            hz[count] = f;
            amplitude[count] = std::pow (f / tune, -kPlateTilt);
            sumSquares += amplitude[count] * amplitude[count];
            ++count;
        }

        if (sumSquares > 0.0)
        {
            const double normalise = kPlateExcitation / std::sqrt (sumSquares);

            for (int i = 0; i < count; ++i)
                amplitude[i] *= normalise;
        }

        return count;
    }

    /// Grit to bits: geometric from 16 down to `kGritMinBits`, so each step
    /// of the control halves the resolution by the same ratio. 16 bits is
    /// the crusher's exact bypass.
    [[nodiscard]] static double bitsForGrit (double grit) noexcept
    {
        const double g = std::clamp (grit, 0.0, 1.0);
        return dsp::Bitcrusher::kMaxBits * std::pow (kGritMinBits / dsp::Bitcrusher::kMaxBits, g);
    }

    /// Grain's kept fraction at `rate`: 1 (every sample) at 0, geometrically
    /// down to `kGrainMinEventsPerSecond / rate` at 1. Exactly 1.0 at 0.
    [[nodiscard]] static double grainDensityFor (double grain, double rate) noexcept
    {
        const double g = std::clamp (grain, 0.0, 1.0);

        if (dsp::isExactlyZero (g))
            return 1.0;

        const double floor = std::clamp (kGrainMinEventsPerSecond / std::max (rate, 1000.0), 1.0e-6, 1.0);
        return std::pow (floor, g);
    }

    /// The make-up applied to each kept impulse at density `p`.
    [[nodiscard]] static double grainMakeupFor (double density) noexcept
    {
        return std::pow (std::clamp (density, 1.0e-6, 1.0), -kGrainMakeupExponent);
    }

    void prepare (double internalRate) noexcept
    {
        rate_ = internalRate > 0.0 ? internalRate : 48000.0;

        plate_.prepare (rate_);

        for (auto& oscillator : oscillators_)
        {
            oscillator.setShape (dsp::OscShape::pulse);
            oscillator.setWidth (kDutyCycle);
        }

        // The ring operands' band limit: fixed, and the reason it exists is
        // in kRingOperandFraction.
        for (auto* filter : { &ringLow_, &ringHigh_ })
        {
            filter->prepare (rate_);
            filter->setMode (dsp::SvfMode::lowpass);
            filter->setResonance (dsp::SvfFilter::resonanceForQ (0.707));
            filter->setCutoffHz (std::min (kRingOperandHz, rate_ * kRingOperandCeilingFraction));
        }

        for (auto* band : { &lowBand_, &highBand_ })
        {
            band->prepare (rate_);
            band->setMode (dsp::SvfMode::bandpass);
        }

        highpass_.prepare (rate_);
        highpass_.setMode (dsp::SvfMode::highpass);
        highpass_.setResonance (dsp::SvfFilter::resonanceForQ (kHighpassQ));

        airFilter_.prepare (rate_);
        airFilter_.setMode (dsp::SvfMode::highpass);
        airFilter_.setResonance (dsp::SvfFilter::resonanceForQ (0.707));

        for (auto& band : sizzleBank_)
        {
            band.prepare (rate_);
            band.setMode (dsp::SvfMode::bandpass);
            band.setResonance (dsp::SvfFilter::resonanceForQ (kSizzleQ));
        }

        damp_.prepare (rate_);
        damp_.setMode (dsp::SvfMode::lowpass);
        damp_.setResonance (dsp::SvfFilter::resonanceForQ (0.707));

        body_.prepare (rate_);
        airEnvelope_.prepare (rate_);
        gateEnv_.prepare (rate_);

        strikeCoefficient_ = std::exp (-6.907755278982137 / (kStrikeSeconds * rate_));

        reset();
    }

    void reset() noexcept
    {
        active_ = false;
        ringOn_ = false;
        driveOn_ = false;
        dampOn_ = false;
        airOn_ = false;
        sizzleOn_ = false;
        quiet_ = 0;

        for (auto& oscillator : oscillators_)
            oscillator.reset (0.0);

        ringLow_.reset();
        ringHigh_.reset();
        lowBand_.reset();
        highBand_.reset();
        highpass_.reset();
        airFilter_.reset();
        airTiltLow_.reset();
        airTiltHigh_.reset();
        airTiltOn_ = false;
        grainOn_ = false;

        for (auto& band : sizzleBank_)
            band.reset();

        damp_.reset();
        shaper_.reset();

        plate_.reset();
        plateOn_ = false;
        gritOn_ = false;
        plateCount_ = 0;

        body_.kill();
        airEnvelope_.kill();
        gateEnv_.kill();
        releasing_ = false;
        strikeLevel_ = 0.0;
    }

    /// The ratios of the set list at `position`, morphed geometrically
    /// between the two sets it lies between and written into `ratios`.
    ///
    /// Geometric rather than linear because a ratio is a pitch: halfway
    /// between 1.5 and 3.0 should be an octave's midpoint, 2.12, not 2.25.
    /// Exact at an integer position by branch -- a set is its own numbers.
    static void ratiosAt (double position, double (&ratios)[kOscillators]) noexcept
    {
        const double clamped = std::clamp (position, 0.0, kMaxHarmonicsPosition);
        const int lower = std::min (static_cast<int> (std::floor (clamped)), kSetCount - 1);
        const double fraction = lower >= kSetCount - 1 ? 0.0 : clamped - static_cast<double> (lower);

        if (dsp::isExactlyZero (fraction))
        {
            for (int i = 0; i < kOscillators; ++i)
                ratios[i] = kSets[lower][i];

            return;
        }

        const int upper = lower + 1;

        for (int i = 0; i < kOscillators; ++i)
            ratios[i] = std::exp2 ((1.0 - fraction) * std::log2 (kSets[lower][i])
                                   + fraction * std::log2 (kSets[upper][i]));
    }

    /// Width's Q: geometric between the two ends, so the control is even.
    [[nodiscard]] static double qForWidth (double width) noexcept
    {
        const double w = std::clamp (width, 0.0, 1.0);
        return kNarrowQ * std::pow (kOpenQ / kNarrowQ, w);
    }

    /// Strikes. `open` picks which of the two decays this pad uses;
    /// `velocity` 0..1; `seed` feeds Air; `samplesToBoundary` as in the kick.
    void start (const HatSettings& s, bool open, double velocity, std::uint64_t seed,
                int samplesToBoundary) noexcept
    {
        reset();

        const double v = std::clamp (velocity, 0.0, 1.0);

        const auto scaled = [v] (double x, double amount) noexcept
        {
            const double a = std::clamp (amount, 0.0, 1.0);
            return x * ((1.0 - a) + a * v);
        };

        // ---- gate, first: it is the one thing a note-off can reach ----
        gate_ = s.gate;
        release_ = std::max (kMinimumReleaseSeconds, std::clamp (s.releaseSeconds, 0.0, 2.0));

        // ---- the six partials ----
        const double tune = std::clamp (s.tuneHz, 60.0, 1200.0);

        double ratios[kOscillators] {};
        ratiosAt (s.harmonics, ratios);

        const double spread = std::clamp (s.spread, 0.0, 1.0);
        const bool spreadOn = ! dsp::isExactlyZero (spread);

        for (int i = 0; i < kOscillators; ++i)
        {
            double hz = tune * ratios[i];

            if (spreadOn)
                hz *= std::exp2 (spread * kSpreadSemitones * kSpreadPattern[i] / 12.0);

            partials_[i] = hz;

            // Every oscillator starts at phase 0, so a hit is the same hit
            // every time it is struck -- the sync the Nord Modular chapter
            // makes the point about, and what keeps two hits identical
            // until Humanise (I6) says otherwise.
            oscillators_[i].reset (0.0);
            oscillators_[i].setIncrement (hz / rate_);
        }

        ring_ = std::clamp (s.ring, 0.0, 1.0);
        ringOn_ = ! dsp::isExactlyZero (ring_);

        // ---- the envelope's decay, needed by the plate's own damping ----
        const double decay = open ? std::clamp (s.decayOpenSeconds, 0.1, 3.0)
                                  : std::clamp (s.decayClosedSeconds, 0.01, 0.4);
        const double bodySeconds = scaled (decay, s.velocityDecay);

        // ---- the plate ----
        //
        // Both ends of the crossfade are BRANCHES, not arithmetic: at 0 the
        // bank is never built and the metal is multiplied by nothing, so the
        // path is bit for bit the engine before the plate existed; at 1 the
        // metal's weight is exactly 0.0 rather than cos(pi/2)'s 6e-17.
        const double plate = std::clamp (s.plate, 0.0, 1.0);
        plateOn_ = ! dsp::isExactlyZero (plate);

        if (plateOn_)
        {
            const bool plateOnly = plate >= 1.0;
            metalWeight_ = plateOnly ? 0.0 : std::cos (plate * 1.5707963267948966);
            plateWeight_ = (plateOnly ? 1.0 : std::sin (plate * 1.5707963267948966)) * kPlateGain;

            double amplitude[kPlateModes] {};
            plateCount_ = plateModesAt (tune, spread, rate_ * 0.45, plateHz_, amplitude);

            plate_.setModeCount (std::max (1, plateCount_));

            for (int k = 0; k < plateCount_; ++k)
            {
                // Ducceschi & Touze's law for the mode's own decay, anchored
                // to the pad's decay -- see kPlateDampExponent.
                const double t60 = std::clamp (kPlateDampAnchorRatio * bodySeconds
                                                   * std::pow (kPlateDampAnchorHz / plateHz_[k],
                                                               kPlateDampExponent),
                                               dsp::ModalResonator::kMinT60Seconds, kPlateMaxT60Seconds);

                plate_.setMode (k, plateHz_[k], t60, 1.0);
                plate_.excite (k, amplitude[k]);
            }
        }
        else
        {
            plateCount_ = 0;
            metalWeight_ = 1.0;
            plateWeight_ = 0.0;
        }

        // ---- grit ----
        const double grit = std::clamp (s.grit, 0.0, 1.0);
        gritOn_ = ! dsp::isExactlyZero (grit);
        crusher_.setBits (bitsForGrit (grit));

        // ---- drive ----
        const double drive = std::clamp (s.drive, 0.0, 1.0);
        driveOn_ = ! dsp::isExactlyZero (drive);
        driveGain_ = 1.0 + (kDriveRange - 1.0) * drive;
        driveTrim_ = std::pow (driveGain_, -kDriveTrimExponent);

        // ---- the band the whole thing is heard through ----
        const double colour = scaled (std::clamp (s.colourHz, 800.0, std::min (12000.0, rate_ * 0.4)),
                                      s.velocityColour);

        const double q = dsp::SvfFilter::resonanceForQ (qForWidth (s.width));

        lowBand_.setResonance (q);
        highBand_.setResonance (q);
        lowBand_.setCutoffHz (colour);
        highBand_.setCutoffHz (std::min (colour * kUpperBandRatio, rate_ * 0.45));

        highpass_.setCutoffHz (std::clamp (s.highpassHz, 200.0, 8000.0));

        dampAmount_ = std::clamp (s.damp, 0.0, 1.0);
        dampOn_ = ! dsp::isExactlyZero (dampAmount_);
        damp_.setCutoffHz (kDampTopHz);

        // ---- the envelopes ----
        const double tension = 1.0 - std::clamp (s.shape, 0.0, 1.0);

        // The plateau: Hold for the closed pad, and for the open pad too
        // while LINK is lit; Open hold -- its own, longer range -- when it is
        // dark. An old project has LINK lit, so it reopens as it was.
        const double hold = (open && ! s.holdLink) ? std::clamp (s.holdOpenSeconds, 0.0, 1.0)
                                                   : std::clamp (s.holdSeconds, 0.0, 0.2);

        body_.setAttackSeconds (0.0);
        body_.setAttackTension (0.0);
        body_.setHoldSeconds (hold);
        body_.setDecaySeconds (bodySeconds);
        body_.setDecayTension (tension);
        body_.setSustain (0.0);
        body_.noteOn();

        strike_ = scaled (std::clamp (s.strike, 0.0, 1.0), s.velocityStrike);
        strikeLevel_ = dsp::isExactlyZero (strike_) ? 0.0 : 1.0;

        // ---- air ----
        air_ = scaled (std::clamp (s.air, 0.0, 1.0), s.velocityAir);
        airOn_ = ! dsp::isExactlyZero (air_);

        if (airOn_)
        {
            random_.seed (seed ^ kAirSalt);
            airFilter_.setCutoffHz (std::clamp (s.airToneHz, 200.0, std::min (12000.0, rate_ * 0.4)));

            // Air tilt: a low shelf and a high shelf of opposite sign about
            // the pivot. Designed per hit -- two shelf designs, nothing per
            // sample beyond two biquads -- and not run at all at 0.
            const double tilt = std::clamp (s.airTilt, -1.0, 1.0);
            airTiltOn_ = ! dsp::isExactlyZero (tilt);

            if (airTiltOn_)
            {
                const double gainDb = tilt * kAirTiltRangeDb;
                airTiltLow_.setCoefficients (dsp::design::lowShelf (kAirTiltPivotHz, kAirTiltQ, -gainDb, rate_));
                airTiltHigh_.setCoefficients (dsp::design::highShelf (kAirTiltPivotHz, kAirTiltQ, gainDb, rate_));
            }

            // Grain: the fraction of samples that carry an impulse, and what
            // each is made up by. At 0 the old draw runs, bit for bit.
            const double grain = std::clamp (s.grain, 0.0, 1.0);
            grainOn_ = ! dsp::isExactlyZero (grain);

            if (grainOn_)
            {
                grainDensity_ = grainDensityFor (grain, rate_);
                grainMakeup_ = grainMakeupFor (grainDensity_);
            }

            // Sizzle: the hiss through the metal's own partials as HEARD --
            // their harmonics nearest the two bands, at whatever Tune,
            // Harmonics, Spread and Colour have just put them.
            sizzle_ = std::clamp (s.sizzle, 0.0, 1.0);
            sizzleOn_ = ! dsp::isExactlyZero (sizzle_);

            if (sizzleOn_)
            {
                sizzleCentres (partials_, colour, rate_, sizzleHz_);

                for (int i = 0; i < kOscillators; ++i)
                    sizzleBank_[static_cast<std::size_t> (i)].setCutoffHz (sizzleHz_[i]);
            }

            // Air attack: the hiss can rise after the metal -- the wash of an
            // open hat, the cheap cousin of the cascade parked in
            // docs/ROADMAP.md section 9. 0 is instant, as it always was.
            airEnvelope_.setAttackSeconds (std::clamp (s.airAttackSeconds, 0.0, 0.5));
            airEnvelope_.setAttackTension (0.0);
            airEnvelope_.setHoldSeconds (hold);
            airEnvelope_.setDecaySeconds (bodySeconds * std::clamp (s.airDecay, 0.1, 3.0));
            airEnvelope_.setDecayTension (tension);
            airEnvelope_.setSustain (0.0);
            airEnvelope_.noteOn();
        }

        gain_ = scaled (std::clamp (s.level, 0.0, 1.0), s.velocityLevel) * kOutputGain;

        active_ = true;
        bodyLevel_ = 1.0;

        if (samplesToBoundary > 0)
            advanceControl (samplesToBoundary);
    }

    /// The control tick: Damp's corner follows the body envelope, retuned
    /// here rather than per sample -- one tangent every 32 samples.
    void advanceControl (int numSamples) noexcept
    {
        if (! active_ || numSamples <= 0 || ! dampOn_)
            return;

        const double corner = kDampFloorHz
                            + (kDampTopHz - kDampFloorHz)
                              * std::pow (std::clamp (bodyLevel_, 0.0, 1.0),
                                          dampAmount_ * kDampExponent);

        damp_.setCutoffHz (std::min (corner, rate_ * 0.45));
    }

    /// Note-off. With Gate lit the WHOLE hit ramps out over the release from
    /// wherever it is -- the metal, the hiss and the filters' ring alike --
    /// so a long open hat can be stopped by lifting the key rather than only
    /// by a closed hit's choke. A one-shot ignores this entirely.
    ///
    /// It is a ramp on the output rather than a release on the two envelopes
    /// because they are BEFORE the filters here: releasing them would leave
    /// the bands ringing for a few milliseconds after the key came up, which
    /// is exactly the tail the control exists to remove.
    void release() noexcept
    {
        if (! active_ || ! gate_ || releasing_)
            return;

        // A release envelope parked at 1.0: attack, hold and decay instant
        // with the sustain at 1, so three samples land it in its sustain
        // stage at exactly 1.0 and the note-off releases from there.
        gateEnv_.setAttackSeconds (0.0);
        gateEnv_.setHoldSeconds (0.0);
        gateEnv_.setDecaySeconds (0.0);
        gateEnv_.setSustain (1.0);
        gateEnv_.setReleaseSeconds (release_);
        gateEnv_.setReleaseTension (1.0);
        gateEnv_.noteOn();
        (void) gateEnv_.skip (3);
        gateEnv_.noteOff();

        releasing_ = true;
    }

    [[nodiscard]] bool isGated() const noexcept { return gate_; }

    /// One internal sample. Exactly 0.0 once the hit has been retired.
    [[nodiscard]] double process() noexcept
    {
        if (! active_)
            return 0.0;

        // ---- the envelopes, and the stick on top of them ----
        double body = 0.0;

        if (body_.isActive())
        {
            body = body_.process();

            // Sustain is 0: arriving there IS the end (the zombie lesson,
            // CLAUDE.md section 7).
            if (body_.getStage() == dsp::AdsrStage::sustain)
                body_.kill();
        }

        bodyLevel_ = body;

        double airEnv = 0.0;

        if (airOn_ && airEnvelope_.isActive())
        {
            airEnv = airEnvelope_.process();

            if (airEnvelope_.getStage() == dsp::AdsrStage::sustain)
                airEnvelope_.kill();
        }

        if (! dsp::isExactlyZero (strikeLevel_))
        {
            const double stick = strike_ * strikeLevel_;
            body += stick;
            airEnv += stick;

            strikeLevel_ *= strikeCoefficient_;

            if (strikeLevel_ < kStrikeFloor)
                strikeLevel_ = 0.0;
        }

        // ---- the metal ----
        double low = 0.0;
        double high = 0.0;

        for (int i = 0; i < kOscillators; ++i)
        {
            const double value = oscillators_[i].advance();

            if (i < kOscillators / 2)
                low += value;
            else
                high += value;
        }

        double metal = (low + high) * (1.0 / kOscillators);

        // Ring modulation, with both operands band-limited first so that no
        // product can land above Nyquist -- see kRingOperandFraction.
        if (ringOn_)
        {
            const double a = ringLow_.process (low * (2.0 / kOscillators));
            const double b = ringHigh_.process (high * (2.0 / kOscillators));

            metal += ring_ * kRingGain * a * b;
        }

        // ---- the plate, and the two sources meet ----
        //
        // `plateOn_` false is the engine as it was: metal times the envelope
        // and not a multiplication more (the golden render in the I4.3 notes
        // is what says so).
        double x = plateOn_
                 ? (metal * metalWeight_ + plate_.process() * plateWeight_) * body
                 : metal * body;

        if (airOn_)
        {
            // The noise: every sample at Grain 0 -- the draw the engine has
            // always made -- or, with Grain up, a single-sample impulse with
            // probability `grainDensity_`, made up by `grainMakeup_`, and
            // exact zero otherwise. Two draws per kept event, one per dropped
            // one, from the same seeded stream: a hit is still a pure
            // function of its seed.
            double noise;

            if (grainOn_)
                noise = random_.next() < grainDensity_ ? random_.bipolar() * grainMakeup_ : 0.0;
            else
                noise = random_.bipolar();

            double hiss = airFilter_.process (noise);

            if (airTiltOn_)
                hiss = airTiltHigh_.process (airTiltLow_.process (hiss));

            // The hiss rung through the six partials: the plate speaking
            // rather than a second instrument playing alongside it. Exact at
            // both ends of the crossfade, and the bank is not run at 0.
            if (sizzleOn_)
            {
                double resonated = 0.0;

                for (auto& band : sizzleBank_)
                    resonated += band.process (hiss);

                resonated *= kSizzleMakeup / kOscillators;

                hiss += sizzle_ * (resonated - hiss);
            }

            x += air_ * kAirGain * airEnv * hiss;
        }

        // ---- grit: the steps of a low-resolution sample path ----
        if (gritOn_)
            x = crusher_.process (x);

        // ---- drive ----
        if (driveOn_)
        {
            // `SoftClipExcess` is what the clipper CHANGES -- clip(x) - x --
            // not the clipped signal, so it is ADDED back to the driven
            // signal to make one. Subtracting it, or worse taking it alone,
            // leaves only the clipping residue: exactly zero below the knee
            // and a harsh remnant above it. That was the first version, and
            // it read as a drive that muted the pad as it was turned down
            // and stripped the tail off as it was turned up.
            //
            // The trim is 1/g, so small signals pass at unity and the control
            // buys harmonics rather than loudness (CLAUDE.md section 7).
            // Measured over the whole range, the clap's RMS moves 0.023 ->
            // 0.023 / 0.022 / 0.021 / 0.019: 1.7 dB, while its peak falls
            // 0.320 -> 0.119, which is the clipping doing its job.
            const double driven = x * driveGain_;

            x = (driven + shaper_.process (driven, clip_)) * driveTrim_;
        }

        // ---- the band, the high-pass and the damping ----
        x = lowBand_.process (x) + highBand_.process (x);
        x = highpass_.process (x);

        if (dampOn_)
            x = damp_.process (x);

        x *= gain_;

        // ---- the gate's release ramp ----
        if (releasing_)
        {
            x *= gateEnv_.process();

            if (! gateEnv_.isActive())
            {
                // Landed at exactly 0: the hit is over, whatever was ringing.
                reset();
                return 0.0;
            }
        }

        // ---- retirement ----
        //
        // The envelopes are before the filters, so the filters ring on past
        // them; the hit is over when they have. Counted rather than assumed,
        // and the count is what makes the last sample an exact zero.
        if (! body_.isActive() && ! airEnvelope_.isActive() && dsp::isExactlyZero (strikeLevel_))
        {
            if (std::abs (x) < kQuietThreshold)
            {
                if (++quiet_ >= kQuietSamples)
                {
                    reset();
                    return 0.0;
                }
            }
            else
            {
                quiet_ = 0;
            }
        }

        return x;
    }

    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// The six partials of the hit that is sounding, in Hz -- what the
    /// panel's picture draws and the measurement checks.
    [[nodiscard]] double getPartialHz (int index) const noexcept
    {
        return index >= 0 && index < kOscillators ? partials_[index] : 0.0;
    }

    /// Where the ring modulator's operands are band-limited, in Hz: 20 kHz
    /// wherever the rate has room for it, and a quarter of the rate where it
    /// has not. A test asserts the products cannot reach Nyquist.
    [[nodiscard]] double getRingOperandCutoffHz() const noexcept
    {
        return std::min (kRingOperandHz, rate_ * kRingOperandCeilingFraction);
    }

    /// Damp's corner at the last control boundary: the thing that makes the
    /// hit darken as it rings, and what a test watches to prove it does.
    [[nodiscard]] double getDampCutoffHz() const noexcept { return damp_.getCutoffHz(); }

    /// Where Sizzle's band-pass `index` sits for the hit that is sounding.
    [[nodiscard]] double getSizzleHz (int index) const noexcept
    {
        return index >= 0 && index < kOscillators ? sizzleHz_[index] : 0.0;
    }

    /// How many modes the plate placed for the hit that is sounding -- 0 when
    /// Plate is 0, which is also the proof that the bank was never built.
    [[nodiscard]] int getPlateModeCount() const noexcept { return plateCount_; }

    /// One placed mode's frequency, in Hz.
    [[nodiscard]] double getPlateModeHz (int index) const noexcept
    {
        return index >= 0 && index < plateCount_ ? plateHz_[index] : 0.0;
    }

    /// The bit depth Grit resolved to for this hit; 16 is the exact bypass.
    [[nodiscard]] double getGritBits() const noexcept { return crusher_.getBits(); }

    /// The hiss's level for the hit that is sounding, velocity applied.
    [[nodiscard]] double getAirLevel() const noexcept { return air_; }

    /// The plateau the body envelope was given for this hit, in seconds --
    /// Hold, or Open hold on the open pad with LINK dark.
    [[nodiscard]] double getHoldSeconds() const noexcept { return body_.getHoldSeconds(); }

    /// The hiss's rise for this hit, in seconds.
    [[nodiscard]] double getAirAttackSeconds() const noexcept { return airEnvelope_.getAttackSeconds(); }

    /// Whether the tilt shelves are in the hiss's path for this hit.
    [[nodiscard]] bool isAirTiltOn() const noexcept { return airTiltOn_; }

    /// Grain's kept fraction for this hit; exactly 1.0 at Grain 0.
    [[nodiscard]] double getGrainDensity() const noexcept { return grainOn_ ? grainDensity_ : 1.0; }

    [[nodiscard]] double getSampleRate() const noexcept { return rate_; }

private:
    double rate_ { 48000.0 };
    bool active_ { false };

    dsp::Oscillator oscillators_[kOscillators];
    double partials_[kOscillators] {};

    dsp::SvfFilter ringLow_, ringHigh_;
    bool ringOn_ { false };
    double ring_ { 0.0 };

    dsp::ModalResonator plate_;
    double plateHz_[kPlateModes] {};
    int plateCount_ { 0 };
    bool plateOn_ { false };
    double metalWeight_ { 1.0 };
    double plateWeight_ { 0.0 };

    dsp::Bitcrusher crusher_;
    bool gritOn_ { false };

    dsp::Adaa1<dsp::SoftClipExcess> shaper_;
    dsp::SoftClipExcess clip_;
    bool driveOn_ { false };
    double driveGain_ { 1.0 };
    double driveTrim_ { 1.0 };

    dsp::SvfFilter lowBand_, highBand_, highpass_, damp_;
    bool dampOn_ { false };
    double dampAmount_ { 0.0 };

    dsp::Adsr body_;
    dsp::Adsr gateEnv_;
    bool gate_ { false };
    bool releasing_ { false };
    double release_ { kMinimumReleaseSeconds };
    double bodyLevel_ { 0.0 };
    double strike_ { 0.0 };
    double strikeLevel_ { 0.0 };
    double strikeCoefficient_ { 0.0 };

    dsp::SvfFilter airFilter_;
    dsp::Biquad<double> airTiltLow_, airTiltHigh_;
    bool airTiltOn_ { false };
    bool grainOn_ { false };
    double grainDensity_ { 1.0 };
    double grainMakeup_ { 1.0 };
    dsp::SvfFilter sizzleBank_[kOscillators];
    double sizzleHz_[kOscillators] {};
    bool sizzleOn_ { false };
    double sizzle_ { 0.0 };
    dsp::Adsr airEnvelope_;
    dsp::SmallRandom random_;
    bool airOn_ { false };
    double air_ { 0.0 };

    int quiet_ { 0 };
    double gain_ { 1.0 };
};

} // namespace tezla::ictus
