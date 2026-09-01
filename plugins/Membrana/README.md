<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Membrana — the microphone stage

**Code `Tzmb` · "Tezla Membrana" · effect · goes BEFORE Phonoss.**

Latin *membrana*, the membrane: the mic diaphragm — the artificial tympanum
that feeds the suite's malleus and incus. Everything else in the suite that
makes a voice "more present" *invents* signal: Emberdrive distorts, Halo
synthesises harmonics, Ferrite saturates. Membrana adds nothing. It works on
what the microphone already captured — where the singer stood, what the
capsule body did to the top octaves, and whether the quiet detail sits up
against the vowels — and every mechanism in it is physics, not curves.

No named-microphone emulation, ever. No impulse responses, no measured
curves of anybody's product. The physics continuum is the instrument:
pattern, body, distance, angle are the controls a real mic session has, and
they couple here the way they couple in the air.

---

## The three mechanisms

### 1. The mic and its position — exact, first order

A pressure (omni) capsule and a gradient capsule, mixed by **Pattern**. The
gradient half carries the proximity effect — the 1/r term of a spherical
wave refusing to vanish close up — so everything couples by construction:

- An **omni has no proximity at any distance**. Exactly none: the test
  asserts 0.000 dB by predicate, not by rounding.
- A cardioid at 90° is −6.02 dB with exactly zero proximity.
- A figure-8 doubles a cardioid's proximity corner at the same distance.
- Doubling the distance moves the corner down exactly one octave.

The whole stage is computed **relative to one reference condition: the same
mic, 1 m away, on axis**. At 1 m the plugin is a bit-exact identity — not
transparent, *identical* — and moving anything re-images the take as if the
reference recording had been made at the new position instead. The LF LIMIT
highpass (the simulated diaphragm's own corner) engages with the position
EQ and bounds the sub: a cardioid at 2 cm honestly implies +34 dB below
30 Hz, and this is what makes that survivable under a dubstep sub.

### 2. The body — the exact rigid-sphere series

What a capsule actually receives is the pressure on its body's surface, and
the body rewrites the top octaves before any electronics exist. Membrana
computes that exactly: the rigid-sphere scattering series at finite source
range (Duda & Martens, JASA 1998 — their own overflow-proof algorithm,
attributed in the source and in `docs/DSP-REFERENCES.md`), realised as a
minimum-phase FIR — faithful, because the sphere response itself is
minimum-phase at every range and angle.

The audible facts the series just *has*, where a drawn curve would have to
fake them: on-axis presence rise toward +6 dB; the off-axis shadow (−12 dB
at 150° by µ = 30); and the close-range trade — at point-blank range the
high rise collapses to about +2 dB while the low end swells, which is the
physical reason "closer" reads as "warmer" twice over. **Character** scales
how much of the raw diffraction survives; at the 1 m reference it is 0 dB
everywhere regardless, so the default costs nothing until the mic moves.
**Grille** adds the basket resonance (up to 6 dB, Q 2.5) at **G Freq**.

### 3. Presence and detail — dynamics, not tone

- **PRESENCE** is a high shelf that *leans in when the singer backs off*:
  full lift on the quiet line, none on the shout. **Track** blends a
  standing shelf (0%) against a fully ridden one (100%); **Thresh** anchors
  the ride, with a 12 dB knee below it. The riding is deliberately slow —
  120 ms to back off, 400 ms to lean in — an engineer's hand, not a
  compressor. Bounded by **Presence** by construction: no transient can
  overshoot it.
- **DETAIL** is a bounded upward expander on the high band, with an
  absolute **Floor**: consonants and breath (the 20 dB above the floor)
  come up; anything at or below the floor — hiss — is lifted **exactly
  0.000 dB**, measured. A vowel alone reads exactly zero lift (the
  detector is 4th-order steep), so the body of the voice never brightens
  itself. This is the thing a shelf cannot do.

Both stages make **one linked decision for both channels**, so the ride
cannot pull the centre image sideways, and both smooth their gain in the
log domain (Giannoulis/Massberg/Reiss 2012's recommended topology) — the
measured modulation residue at maximum settings is −150 dBFS.

**Auto Level** divides the loudness change out of the mic model so Distance
reads as *tone*; off, the physical level applies (+24 dB max).

---

## The panel

The composed curve at top left is drawn **from the same coefficients the
audio runs through** — the position section, the LF limit, and a DFT of the
live FIR taps. It is built to be unable to disagree with the sound. Hover
for the exact dB at any frequency. THE RIDES at top right shows what the
two dynamic stages are doing on one clock, growing upward because these
stages only ever add; the POSITION title carries Auto Level's current trim,
and the PRESENCE and DETAIL titles carry their live lifts.

## Presets

*Neutral* (the bit-exact reference) · *Close & Warm* · *Radio Chest* ·
*De-Boom* (taking proximity OUT of a take recorded too close) ·
*Backed-Off Detail* · *Quiet-Verse Lift* · *Crisp Small Capsule* ·
*Podcast Presence*.

## Where it goes

**First, before Phonoss** — mic character, then the strip. See
[`docs/VOCAL-CHAIN.md`](../../docs/VOCAL-CHAIN.md). It is the wrong tool
for: adding harmonics (Halo), grit (Emberdrive), or a ceiling (Capstone).

## Honesty section

- Directivity × diffraction are composed, not solved as one coupled
  scattering problem — the same superposition Anvil states for its
  driver × enclosure × mic chain.
- The detail band's audio split is first-order complementary (no crossover
  allpass touches the voice). Its −20 dB leak means a loud vowel can ride
  up ≤ 0.06 dB during a lifted consonant (measured at maximum detail) —
  masked by the consonant that caused it, and stated here rather than
  hidden.
- Everything above is measured in `tests/` and printed by
  `tezla-measure membrana`; the numbers in this README are that tool's
  output, not aspirations.

## Measured at close-out (`tezla-measure membrana`, 48 kHz)

| claim | figure |
|---|---|
| neutral identity | 40001 of 40001 samples bit-identical |
| capsule fit, rendered vs analytic target | worst 0.023 dB (700 Hz–20 kHz) |
| rate independence 44.1/48/96/192 | worst spread 0.012 dB |
| autoLevel holding 1 kHz across the distance sweep | worst 0.0004 dB |
| gain-ride modulation residue at maximum settings | −149.8 dBFS RMS |
| hiss below the floor, full detail | lifted 0.000000000 dB |
| a vowel alone, any level, full detail | exactly zero lift |
| CPU, everything engaged, stereo 48 kHz / 480 | 1.36% of one core |

Version 0.1.0. Latency 0 (minimum-phase throughout). The acceptance test,
as always, is the user's ears on the Windows rig.
