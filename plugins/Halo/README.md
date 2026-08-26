# Tezla Halo

A harmonic exciter and bass enhancer. Code `Tzha`, version 0.1.0.

It adds harmonics that were not in the source, and — unlike the structure
every exciter since 1979 has used — it does not also add a copy of the source
while doing it.

---

## Why it is built this way

The classic exciter is three blocks: highpass a copy of the signal, distort it,
mix it back. That is what the Aphex patent describes, what Calf's Exciter does
with four cascaded biquads into a tube curve, and what almost everything else
does too.

The problem is in the second block. A soft shaper passes most of its input
straight through, so what gets mixed back is mostly **a filtered, phase-shifted
copy of the band**. That is why an exciter's blend control also acts as an EQ,
and why it combs against the dry path around the filter corner.

Halo's generator has no linear term to pass through. Two curves, both with zero
slope at the origin, from the same family so one square root serves both:

```
u = drive * x        r = sqrt(1 + u^2)        s = u^2 / (r * (r + 1))

even    s                       an even function: DC and even harmonics, nothing else
odd     -x * s   + trim * x     a saturator minus a straight wire, minus its own fundamental
```

The even half is an even function of its input, so its Fourier series contains
DC and even harmonics and **nothing at the fundamental** — for any input, at any
level, at any drive. Measured, the fundamental in the wet path sits at −271 dB
at Colour = Even, and what little is there appears at exactly the same level on
the odd harmonics an even function cannot produce at all: it is the ADAA
approximation floor, not a leaked copy.

The odd half needed more than removing the linear term, and measuring is what
showed why. A cubic residual carries three times as much fundamental as third
harmonic, so at maximum drive the odd half measured **−0.4 dB** at the
fundamental: it was cancelling the band, not exciting it. It now also subtracts
the saturator's own describing function — a closed-form fit to the elliptic
integral, exact at both asymptotes and within 0.14% everywhere.

---

## Controls

### MAIN

| Control | What it does |
|---|---|
| **Mode** | Which side of Focus gets excited. **Above** is the classic exciter. **Below** is a bass enhancer: harmonics of a 40 Hz sub land at 80 and 120 Hz, where a phone or a laptop can actually reproduce them, and the ear supplies the fundamental it cannot hear. |
| **Focus** | Where the band starts or ends. A 24 dB/octave Linkwitz-Riley split, so it is decisive rather than a tilt. |
| **Drive** | How hard the band is pushed into the generator. Sets the *recipe* — how far up the harmonic series the energy goes — not the level. At 0 the generator is exactly the zero function. |
| **Colour** | Odd (third, fifth: edge and bite) through even (second, fourth: octave shimmer, and the surgically clean half). Level-matched to 1.4 dB across the whole Drive range. |
| **Amount** | How much of the generated harmonics get added. At the bottom of its travel it reads Off and the output is the input, **bit for bit**. |
| **Track** | How much the harmonics follow the source level. At 0 they behave like a real nonlinearity; at 100% the harmonic-to-source ratio holds constant at every level. |
| **Punch** | Transient discrimination — harmonics arrive on the hits and leave the sustain alone. What stops an exciter turning a jungle break into a wash of cymbals. At 0 it is bit-exact. |
| **Output** | Plain level trim. |

### SHAPE

| Control | What it does |
|---|---|
| **Floor** / **Floor Hz** | Removes generated harmonics below a frequency. Mostly for Below mode: the second harmonic of a 40 Hz sub is 80 Hz, which is still sub, and adding energy there makes the low end muddier rather than more audible. |
| **Ceiling** / **Ceiling Hz** | Removes generated harmonics above a frequency. On by default; worth leaving on. |
| **Listen** | Solos the harmonics. Because the wet path carries almost no fundamental by design, this really is the added material rather than a filtered copy of the source. |
| **Auto Trim** | Holds output level steady as harmonics are added, so you judge brightness rather than loudness. Exactly 1.0 when nothing is being added. |
| **Input** | Level trim before everything. Both paths see it, so it is not a drive control. |
| **Oversampling** | Auto / Off / ×2 / ×4 / ×8. Auto lands near 192 kHz internally whatever the session rate. |

Bypass is not on this page — it lives in the header, where it is reachable from
any tab. See **Global controls** below.

Both Floor and Ceiling shape the **harmonics only**. The dry signal passes
through a delay line and nothing else, so nothing here can thin the sub.

---

## Measured

`tezla-measure selftest` passes before any of these are trusted.

### Audible-band aliasing, dB relative to the fundamental

5 kHz tone at −0.9 dBFS, Focus 3 kHz, Ceiling on at 16 kHz, Auto oversampling.

| Drive | Colour 0 | Colour 0.5 | Colour 1 |
|---|---|---|---|
| 0.25 | −112.4 | −113.7 | −115.6 |
| 0.50 | −92.6 | −92.7 | −92.7 |
| 0.75 | −72.3 | −70.1 | −68.7 |
| 1.00 | −67.0 | −63.4 | −61.5 |

The same measurement at a **192 kHz session**, where Auto correctly runs ×1,
agrees to within 0.1 dB at every setting: −114.2 / −115.6 / −117.5 at Drive
0.25 and −67.0 / −63.4 / −61.4 at Drive 1.0.

The baseline: the same 5 kHz tone through a highpass, a `tanh` and a blend at
the host rate — structurally what a conventional exciter does — measures
**−27.7 dB**. `tezla-measure naive-exciter` builds and measures it, so the
comparison is our own code both times.

### Harmonic content of the wet path alone

Listen on, Drive 0.7, Focus 2 kHz, Ceiling off, dB relative to the input tone.

| Colour | H1 | H2 | H3 | H4 |
|---|---|---|---|---|
| Odd | −55.3 | −264.5 | −13.5 | −277.7 |
| Mid | −58.4 | −17.6 | −16.6 | −21.1 |
| Even | **−271.2** | −14.6 | −272.1 | −18.1 |

H1 is the fundamental leaking into the wet path — the thing a conventional
exciter mixes back at close to full level.

### Everything else

| Check | Result |
|---|---|
| 1 k → 20 k sweep, worst inharmonic below 900 Hz | **−65.6 dBFS** over a 36-point drive/colour/track grid |
| Harmonic profile across 44.1 / 48 / 96 / 192 kHz | within 0.5 dB |
| Track = 100%, source swept 30 dB | harmonic ratio constant to **0.0 dB** |
| Track = 0, same sweep | 52.2 dB of level dependence — the control does something |
| Colour swept at fixed drive | output within 0.2 dB |
| Ceiling at 6 kHz | 6th harmonic down 24.5 dB, 2nd unchanged within 0.2 dB |
| DC at any setting | 1e-7 |
| Amount at Off | bit-exact against the input |
| Silence in | silence out, at every Track setting |
| Steinberg `validator` | 47 passed, 0 failed |

### Two-tone intermodulation

3 kHz + 3.3 kHz, difference product at 300 Hz, relative to one input tone.
The odd half produces essentially none (−107 dB or below); the even half
produces it by construction, at −10 to −17 dB. That is inherent to
even-harmonic generation and is what **Floor** is there to remove.

---

## What measuring changed

Every one of these was invisible to reasoning and to steady-tone tests, and
each is now pinned by a test:

1. **Ceiling was inert.** Filters were rebuilt at `prepare()` and then only when
   Focus moved, so a Ceiling set afterwards never reached the coefficients:
   0.0 dB of attenuation at every harmonic. Filters are now checked against what
   was actually built, not against what changed.
2. **The odd half was a band canceller.** At maximum drive it removed the band
   to within 0.4 dB. Fixed by subtracting the describing function.
3. **The even half made full-scale harmonics from a quiet band**, because it
   saturates towards 1 in absolute terms. A source 30 dB down measured its
   second harmonic 8 dB *above* the input. It is now scaled by the amplitude it
   is fed.
4. **A moving DC pedestal.** The even half's DC grows and shrinks with the
   signal, and the blocker turned that movement into −29.5 dBFS sitting exactly
   on its own 12 Hz corner. Subtracted at the source now, with a second blocking
   pole behind it.
5. **A bad amplitude estimator, twice.** A peak over a control interval ripples
   at the beat between signal and control rate; a per-sample peak follower cannot
   reach the peak of a 4 kHz tone. It is a mean square now — exactly the quantity
   the trims are derived for. An asymmetric follower on a squared signal, tried
   in between, drifts towards the peak of the square and over-read by 40%.
6. **One averaging pole was not enough**, leaving a uniform −78 dBFS sideband
   skirt around every harmonic that summed to a −50 dB "aliasing" figure which
   was not aliasing.
7. **The measurement lead-in was a sample count, not a duration**, so at 192 kHz
   the envelope was still settling inside the window and reported 60 dB of
   aliasing that did not exist.

---

## Roadmap

Halo stays what it is: a focused exciter and bass enhancer with one band and a
side. The multiband enhancer is **not** a phase 2 of this plugin -- it is a
bigger plugin of its own, with four bands, per-band width and considerably more
signal complexity and CPU than belongs in a tool you reach for on a single
channel. It is reserved in the registry as `Prism`.

What might still land here:

**Precision mode.** Chebyshev harmonic synthesis (Le Brun, JAES 1979) driving
harmonics 2 to 5 independently, so the recipe is chosen rather than inherited
from the shape of a curve. `T_n(cos x) = cos(n x)`, so feeding a unit-amplitude
sine through the nth Chebyshev polynomial produces exactly the nth harmonic and
nothing else. It needs the input normalised to unit amplitude to be exact, which
Track already does at its top setting.

**Known duplication, now resolved.** The editor's meter, note and page
components were Halo's own copies of Emberdrive's. The header, palette, A/B and
spectrum display now live in `shared/tezla-ui` and compile into every plugin
target; the remaining page and meter classes should follow when a third plugin
needs them.

---

## Global controls

The header carries the two controls reached for while listening, so they work
from any tab:

- **BYPASS** lights orange when engaged. Latency-matched and crossfaded over
  10 ms -- see `shared/tezla-dsp/include/tezla/dsp/BypassMixer.hpp` for why that
  is not a detail.
- **A / B** holds two complete settings and swaps between them; **COPY** puts
  the current one into the other slot. Every parameter moves except bypass, and
  both slots are saved with the project.

## Spectrum

Input and output drawn over each other, so the excitation is visible rather than
inferred: the output curve lifting away from the input above the Focus line *is*
the effect. The Focus frequency is marked and the side being worked on is shaded.

The analysis is framework-free and lives in `shared/tezla-dsp`, so the same code
can drive a standalone analyser later without a GUI framework attached.
