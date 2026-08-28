# Tezla Sonitus

A growl and reese instrument. The suite's first sound source, and the one
plugin here that makes audio rather than changing it.

**Code `Tzso` · vendor `Tzla` · bundle `tech.tezla.Sonitus` · schema v1**

---

## The thesis

**Every reese and every growl is one dense source and a moving comb.**

Two detuned saws beat, and the beating *is* a comb whose notches sweep at the
difference frequency — you just have no handle on it. A flanger is that same
comb with a knob on the front. A vowel filter is the same comb shaped like a
mouth. They are one idea at three time constants, and the instrument is
arranged to say so:

> make dense harmonics → comb them under total control → drive them → **keep
> the sub out of it**

That last clause is what makes it usable on a real track, and it is why the
split is *inside* the instrument rather than three plugins later.

### Where it came from

A workflow assembled by hand in FL Studio: a raw source with the filter on an
LFO, into EQ, a tube, and — the part where the magic was — **a Fruity Flanger
with its rate pinned at 0 Hz so the depth knob became a direct, automatable
control of the comb**. Feedback at 72%, invert feedback on, invert wet on. The
LFO rate itself automated to step along a pattern.

Every one of those is a thing a plugin can do better than an automation lane
can, and this is what it looks like when it is built in rather than assembled:

| the old trick | here |
|---|---|
| Flanger at rate 0, depth on an automation lane | **Comb time as a modulation destination**, driven by an LFO, an envelope or the sequencer |
| Invert feedback checkbox | **Negative feedback**, continuous — the notch pattern moves by half a spacing |
| LFO rate drawn on a lane | **Sequencer → LFO 1 rate**, in octaves, locked to the transport |
| Comb wherever the plugin happened to sit | **An order switch** — tube before comb or comb before tube |

---

## Signal flow

```
 MIDI ──► VOICE ×8  (or mono / legato with glide)
          ┌──────────────────────────────────────────────────────┐
          │  OSC A ── unison ×1-7 ──┐                            │
          │    │  (sync master)     ├── ring mod ──┐             │
          │  OSC B ── unison ×1-7 ──┘              ├─ mix ─┐     │
          │    ▲  (sync slave, PM target)          │       │     │
          │    └── PM ◄─ OSC A                     │       ▼     │
          │  SUB  (sine / square, -1 or -2 oct) ───┘   FOLDER    │
          │                                               │      │
          │        FILTER (ZDF SVF, drive in the loop) ◄──┘      │
          │           ▲ audio-rate FM                            │
          │           └── OSC A                                  │
          │                    │                                 │
          │                   VCA ── amp env                     │
          └────────────────────┬─────────────────────────────────┘
                               ▼  sum of voices
 ┌─── GLOBAL MANGLE ─────────────────────────────────────────────┐
 │                                                               │
 │   SPLIT at X Hz ─┬── SUB   : mono, DC-blocked, bypasses all ──┤
 │                  │                                            │
 │                  └── BODY  : [ ORDER SWITCH ]                 │
 │                        TUBE ⇄ COMB (flange | phase)           │
 │                        FORMANT morph                          │
 │                        tilt                                   │
 │                                                          sum ─┤
 └───────────────────────────────────────────────────────────────┘
                               ▼
                        output trim ──► out
```

Voices are per-note; the mangle is global. That is the cheap arrangement and
also the right one — it is what a hardware chain does, and it leaves the CPU
for unison.

---

## The order switch is not a convenience

Tube before comb and comb before tube are **different instruments**, for the
same reason a tone stack in front of a distortion is a different amplifier from
one behind it.

- **Comb first.** The tube generates harmonics of a signal that already has
  holes in it, and the holes stay holes. Tuned, hollow, and the comb's pattern
  survives into the output.
- **Tube first.** The tube fills the comb's notches with harmonics it made
  itself, and the comb then cuts those too. Denser, less tuned, and the comb
  reads as texture rather than as pitch.

---

## Controls

### OSC

Two oscillators, identical except that A is the sync master and B is the sync
slave and PM target.

| control | note |
|---|---|
| Shape | Saw, pulse, triangle, sine. Saw is the dense one: a comb can only cut harmonics that are there |
| Octave / Semis / Fine | −3..+3 octaves, ±24 semitones, ±100 cents |
| Width | Pulse width, triangle skew. At 50% a pulse is a square and has only odd harmonics |
| Level | Into the mix |
| Unison | 1–7 copies |
| Detune | How far they spread, in cents. **The comb that costs nothing** |
| Spread | Across the stereo field. The centre copy stays centred, so the mono sum keeps its fundamental |
| Drift | Slow random pitch wander, in cents. What an analogue oscillator bank does |

**Sync B** — hard sync. B's phase resets on the played note's period, so B's own
pitch stops being a pitch and becomes a formant. This is the Pro-53 sound, and
it is worth nothing standing still: put a mod envelope on **Pitch B**.

**PM** — phase modulation of B by A. The same sidebands as FM with no DC drift,
which is why every "FM" synth since the DX7 has actually been a PM synth.

**Sub** — sine or square, one or two octaves down. Generated in the voice and
then taken *out* of the mangle by the split.

**Ring** — A × B. The sum and difference of every pair of their harmonics and
almost nothing at either original pitch.

**Fold** — a sine wave folder. Past full scale the transfer curve turns round
and comes back, so the harder you push the more harmonics appear — the opposite
of a clipper, which runs out. Antialiased, and the widest-band thing here.

### FILTER

Zero-delay-feedback state variable, with drive inside the loop.

- **Resonance** is Q on a geometric law: 0.5 at nothing, 500 at full, which is
  15 dB of peak per quarter turn all the way up. A k-linear law puts Q at 1.0
  halfway and crams 21 dB into the last 1% of the travel.
- **Drive** overdrives the integrators against a **fixed rail** (linear to 1.0,
  ceiling 2.0). A rail that falls with drive makes the control a volume knob:
  measured 5 dB of passband loss at a quarter turn, and every resonance setting
  reading identically past 0.25.
- **Key track** at 100% gives constant timbre across the keyboard; at 0 the
  filter is a fixed formant and the low notes are darker, which is usually what
  a bass wants.
- **FM** is oscillator A on the cutoff at audio rate — not a wobble.

Keyboard: **Poly / Mono / Legato**, up to eight voices, glide, bend range.
Legato does not retrigger the envelopes, so a phrase played without gaps runs
through one envelope and glides between its notes.

### ENV

Three ADSRs — amp, and two spare — each with a **Shape** control. At zero the
segments are nearly straight, which sounds mechanical because nothing physical
decays linearly; turned up they are the sharp exponential of a capacitor
discharging.

### MOD

Two LFOs, a sixteen-step sequencer, and two matrices.

**LFO rate 0 is a legitimate setting** and is the brief's original trick: the
rate pinned at nothing so the depth comes from somewhere else. The panel shows
it as "held" rather than "0.00 Hz".

**Seq to rate** points the sequencer at LFO 1's *rate*, in octaves — a wobble
that changes tempo on the step.

| matrix | slots | sources | destinations |
|---|---|---|---|
| **Voice** | 6 | amp env, mod env 1–2, velocity, key track, note random, LFO 1–2, sequencer | cutoff, resonance, filter drive, PM index, width A/B, detune A/B, osc mix, sub level, ring, fold, pitch, pitch B, level |
| **Global** | 3 | LFO 1–2, sequencer | **comb time**, comb feedback, comb mix, phase centre, vowel, tube, output |

They are separate because the per-note sources have one value *per sounding
note* and the mangle is one chain: "amp envelope drives the comb" has no answer
with eight notes down.

Depths are stored as percentages and scaled into each destination's own units —
cutoff is 6 octaves at full, pitch is 2, comb time is 3, phase centre is 4,
tube and output are 24 dB.

The sequencer is drawn as sixteen faders rather than sixteen knobs, because the
*shape* of the pattern is the thing being edited. The playing step is lit.

### MANGLE

**Split** — where the sub is taken out. Below it the signal gets a DC blocker
and nothing else. **Sub mono** is on by default: a wide sub is the single most
common way to lose a bass on a club system.

**Tube** — a triode stage, straight from Anvil. Its grid conducts on the
positive half and blocks, so the operating point drifts under load. At 0 dB it
is bit-exactly out of the path.

**Comb** — one section, two topologies:

| | Flange | Phase |
|---|---|---|
| what it is | fractional-delay comb | allpass cascade |
| shifts every frequency by | the same **time** | a different **phase** |
| notches | evenly spaced | bunched around the centre |
| character | metallic, rings | vocal, smooth |
| its own controls | time, key track, damp | centre, stages (2–16) |

Shared: feedback (**negative is the invert-feedback switch**, continuous),
spread, mix, invert wet.

**Key track** pulls the delay onto the played note's period, so the notches land
on that note's own harmonics. The growl comes out *tuned*.

**Vowel** morphs across ee–eh–ah–oh–oo: three resonant peaks at the frequencies
a human tract puts them, with the per-vowel amplitudes from the same table —
they span thirty decibels, and that balance is most of what tells one vowel from
another. The gain is divided by Q, so Sharpness sharpens the vowel rather than
turning it up.

**Harmonic lock** pulls the three resonances off the vowel and onto **harmonics
of the played note**. This is what overtone singing actually is: not a second
voice, but one source with a resonance sharp enough to select a single partial
out of the drone and make it a melody. Because it can only land on a harmonic,
it is always in tune with the bass underneath.

> **It is the comb's key tracking, applied to the formant.** The comb locks its
> notches to the note's *period*; this locks the resonances to its *harmonics*.
> Both read the same tracked note, so they agree by construction rather than
> beating against each other. Same thesis, third time constant.

The lock sharpens as it engages — selecting one partial takes a bandwidth of
about **1.6 Hz** where a spoken vowel has eighty, and Q goes from 13.5 to 275.
That extra sharpness belongs to the lock rather than to the Sharpness control on
purpose: widening the sharpness range instead would have silently re-mapped
every stored sharpness value.

**Harmonic** is which partial, continuously, so it is a modulation destination.
Point the sequencer at it and the overtone line walks the series in time. Sygyt
sings around partials 6–12.

**Notch** is an anti-formant. A nasal is not a vowel with different peaks — it is
a vowel with a **zero**: the nasal cavity is a side branch, and a side branch
cancels rather than resonates. That is what a filter with only poles cannot
make, and why no synth vowel filter can say "m", or the ending of a chanted
"AUM". 26.6 dB at the centre when full, and within 3 dB of untouched two octaves
away. Set aside from the vocal reading, it is a hole you can put anywhere.

**Tilt** is one knob of tone — two shelves in opposite directions about 700 Hz.

### TUNING

Scala `.scl` scale files and `.kbm` keyboard maps, plus 22 built-in scales.

**This belongs in this instrument rather than being a bolt-on**, because the
comb key-tracks onto harmonics of the played note. In twelve-tone equal
temperament a major third is 14 cents sharp of the real 5/4 and beats against
its own comb; in just intonation it does not, and a sustained chord locks
instead of churning. The difference is large on a bass.

| group | scales |
|---|---|
| pure | just major, just minor, Pythagorean, harmonic series 8–16 |
| historical | quarter-comma meantone, Werckmeister III, Kirnberger III, Vallotti |
| ancient | Archytas' enharmonic, diatonic and chromatic tetrachords |
| non-octave | **Bohlen–Pierce** (repeats at 3/1), **Carlos Alpha / Beta / Gamma** |
| equal | 5, 7, 12, 19, 24, 31, 53 |

Every one is **generated from its definition** rather than shipped from an
archive — a Pythagorean scale is a chain of 3/2s, Bohlen–Pierce is thirteen
equal steps of 3/1. That is arithmetic, and it is also how CLAUDE.md §2.1 and
§9 want it done.

The parser refuses a file it cannot fully read and says which line stopped it.
That is not caution for its own sake: Transpectus's `.tzref` loader took
`strtod`'s result on trust and a corrupt file loaded as 96 silent zeros. **A
tuning that half-loads is worse than one that will not load, because it plays.**

The `.scl` text is saved into the plugin's state, so a project opened on
another machine is in tune without the file.

---

## Measured

`tezla-measure sonitus`. All figures at 48 kHz unless stated.

### Aliasing

One saw at full fold, filter drive 0.7 and 24 dB of tube. Absolute dBFS of
inharmonic energy in the audible band.

| note | off | ×2 | ×4 | ×8 |
|---|---|---|---|---|
| 41.2 Hz | −69.20 | −76.86 | −82.18 | −87.96 |
| 55.0 Hz | −67.83 | −72.83 | −82.66 | −85.20 |
| 82.4 Hz | −66.76 | −69.96 | −74.12 | −86.24 |
| 110.0 Hz | −65.32 | −67.62 | −71.72 | −78.34 |
| 164.8 Hz | −55.71 | −68.69 | −76.61 | −76.21 |
| 220.0 Hz | −47.06 | −55.94 | −68.07 | −75.03 |
| **440.0 Hz** | −37.08 | −42.31 | **−56.40** | −68.84 |

Auto picks ×4 at 48 kHz. That clears CLAUDE.md §7's −60 dBFS **from E1 to A3**,
which is what a bass instrument plays, and **does not** clear it at 440 Hz,
where the control offers ×8 and gets −68.8. The limit is stated rather than
hidden.

**The measurement has to be a harmonic patch, and that is not a detail.** A
reese is dense and inharmonic *on purpose* — five detuned oscillators, a synced
partner, a ring modulator — and any harmonic analysis counts all of that as
aliasing. Pointed at a real patch the number reads 0 dB and means nothing.

### The comb

The first notch, against `1/(2T)`:

| time | predicted | measured | inverted |
|---|---|---|---|
| 0.50 ms | 1000.0 | 1000.0 | 2000.0 |
| 1.00 ms | 500.0 | 500.0 | 1000.0 |
| 2.00 ms | 250.0 | 250.0 | 500.0 |
| 4.00 ms | 125.0 | 125.0 | 250.0 |
| 8.00 ms | 62.5 | 62.5 | 125.0 |
| 16.00 ms | 31.2 | 31.2 | 62.5 |

Key tracking at 100% puts the first notch at half the played frequency, at
every note: MIDI 28/40/52/64 read 20.60 / 41.20 / 82.41 / 164.81 Hz against a
theory of 20.60 / 41.20 / 82.41 / 164.81.

A global slot on comb time with the base delay at 2 ms sweeps **31.250 Hz to
1977.9 Hz** over an LFO cycle — a ratio of 63.3 against the 64 that ±3 octaves
predicts, short only because a triangle's corners are single samples.

### CPU

One second of audio in 512-sample blocks, after two seconds of pre-roll.

| | ms/s | core |
|---|---|---|
| idle, nothing playing | **0.4** | 0.04% |
| 8 voices, 1 unison each (16 oscillators) | 268 | 27% |
| 8 voices, 3 unison each (48 oscillators) | 307 | 31% |
| 8 voices, 7 unison each (112 oscillators) | 372 | 37% |

**The voices dominate, not the oscillators.** Seven times the oscillators costs
about a third more; an eighth of the *voices* costs an eighth, because the
filter, the envelopes and the folder's antialiasing are per voice and the
unison bank is not. **Mono is the lever, not unison** — and a reese is one voice
anyway.

Idle was 17.9 ms/s until the engine learned to stop: once the chain has been
below −240 dBFS for a second with no voice sounding, the render and the
decimation filters are skipped. The clocks keep running, so a slow LFO is where
it would have been when the next note arrives.

### Tuning

- 12-TET against `440·2^((n−69)/12)`, worst of 128 notes: **3.6e-12 Hz**
- Pythagorean fifth: **701.955 cents** — a pure 3/2, not 12-TET's 700
- Bohlen–Pierce repeat: **1901.955 cents** — a tritave, not an octave

---

## Presets

| | |
|---|---|
| **Init — one clean saw** | The defaults. One saw, filter open, no mangle. The genuinely clean setting CLAUDE.md §7 asks every plugin for |
| **Reese — the classic** | Two saws, unison 3 each, mono, flanger at 72% inverted feedback, LFO 2 drawing the comb |
| **Growl — tuned comb** | Comb key-tracked to 100% with the sequencer stepping the notch |
| **Sync scream** | Hard sync with a mod envelope on Pitch B |
| **Sub — clean weight** | Sine an octave down plus the sub oscillator, split at 200 Hz |
| **Talkbox** | Vowel filter at full, sequencer morphing it |
| **Phase wash** | Allpass cascade, 8 stages, LFO on the centre, polyphonic |
| **Metal fold** | Ring and folder, ×8 oversampling |
| **Just growl** | For a pure scale — load one on the TUNING page |

---

## What is not proved

Steinberg's validator passes 47/47 on Linux and 579 DSP tests pass on x86-64
and under `qemu-aarch64`. None of that says it sounds good, and **nobody has
loaded this into a DAW from here** — the whole project is developed in a Linux
container. Your ears are the acceptance test, and the first useful thing after
it loads is telling me which presets are wrong.

---

## Roadmap

Things considered and deliberately not built yet, with the reasoning, so the
next pass does not start from scratch.

### Kargyraa — phase-locked period doubling

**The one worth doing next**, and the only one on this list that is a *source*
change rather than a filter change.

Kargyraa is the third throat-singing mechanism and it is not a resonance trick
at all: the singer's **ventricular folds vibrate at exactly half the frequency
of the true vocal folds**. Real period doubling, phase-locked — an octave down
that cannot beat against the fundamental, because it is the same waveform with
alternate cycles modified rather than a second oscillator.

For a bass instrument that is exactly the interesting property. Sonitus's sub
oscillator today is generated *independently*, so it is a clean sine or square
that has to be tuned and can drift against the note. A kargyraa sub would be the
oscillator's own output with every other cycle attenuated, locked to its own
phase by construction.

Lives in `Voice`, needs its own aliasing thought (period doubling makes the
harmonics *denser*, not higher, so it is benign — but that should be measured
rather than assumed). Held until the instrument has been heard, because it is a
sound decision as much as a DSP one.

### More vowels, including non-Western

Peterson & Barney's Table II has ten columns and only five are used: /ɪ æ ʊ ʌ ɜ˞/
are sitting in the paper already and cost nothing. Front rounded /y/ (French
*tu*, German *ü*), /ø/ (French *peu*) and back unrounded /ɯ/ (Turkish *ı*) are
the genuinely non-English colours — /y/ has F1 low like /i/ but F2 near 1800
rather than 2290, which is a sound English has no word for.

**Blocked on two things, not on effort.** A source: these would be invented
otherwise, and this project has now twice proved how that goes. And a decision:

> `Vowel` is **append-only and indexed by the morph position**. A saved
> `formantMorph` of 0.5 means "ah" today; with ten vowels in the list it would
> mean something else, and every project using the vowel filter would shift.
> CLAUDE.md §8. Adding vowels needs either a separate "vowel set" choice
> parameter, or the morph reinterpreted with the current layout preserved.

### Self-oscillation, from the filter book

Zavalishin's route to an SVF that can be driven past self-oscillation is an
**antisaturator** — `sinh`, faster than linear — in parallel with the damping
gain, so damping grows with level. Our fixed rail bounds the state instead,
which reaches the same place he describes ("effectively makes the state of the
first integrator saturate") but does not offer the *R* < 0 region where the
filter sings on its own. §6.7's second-order saturation curves are the related
note: replacing `tanh x` with `x/(1+|x|)` makes the nonlinear zero-delay
equation analytically solvable, which is the route if the nonlinearity should
ever sit genuinely inside the loop without iterating.

### Not doing

- **Full ventricular-fold biomechanics.** A research project whose audible
  result is the period doubling above, which is cheap to get directly.
- **A "throat singing" preset that is really a vowel sweep.** That is the
  marketing version of the idea, not the idea.

---

## Changelog

### v0.1.0

First version. Oscillators with hard sync and PM, unison, ZDF filter, three
envelopes, two LFOs, a sixteen-step sequencer, two modulation matrices, the
global mangle with its order switch, the sub split, Scala microtuning, nine
presets, and a six-page editor.

Plus the overtone-singing section on the vowel filter: harmonic lock, the
harmonic selector, and an anti-formant. Both new global destinations —
`Harmonic` and `Notch` — were **appended** to the destination list, and every
new control defaults to neutral, so a project saved before they existed reopens
sounding the same. There is a test for exactly that.
