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

**Sub** — sine or square, anywhere from **two octaves below the note to two
above**. Zero doubles the note rather than underpinning it, which is a thickener;
above the note it stops being a sub at all and becomes a fixed-interval second
voice. Generated in the voice and then taken *out* of the mangle by the split,
whichever octave it is in.

**Ring** — A × B. The sum and difference of every pair of their harmonics and
almost nothing at either original pitch.

**Kargyraa** — period doubling, the way a Tuvan throat singer gets one. See its
own section below.

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

Keyboard: **Poly / Mono / Legato**, 1–32 voices (16 by default), glide, bend range.
Legato does not retrigger the envelopes, so a phrase played without gaps runs
through one envelope and glides between its notes.

### ENV

Three **AHDSR** envelopes — amp, and two spare — each drawn as the shape it is,
with four draggable corners: attack across, hold across, decay across and
sustain up and down on the third, release across. The knobs beside each graph do
the same job to the sample; the graph is for finding a shape, the knobs for
pinning it down. Double-click a corner for its default.

**Hold** is the H, and it sits at *full level* for a set time between the attack
and the decay. It is what makes a plucked or gated sound possible without setting
the sustain to 1 and shortening the note — which is not the same thing, because
the release would then start from wherever the key was let go rather than from
the top. At 0 the stage is skipped entirely rather than entered for no samples.

#### Tension is bipolar, and there is one per segment

The old single **Shape** control ran from "sharp analogue" to "nearly straight"
and no further, because the trick it was built on — aim past the destination and
stop early — only bends a curve **one way**. No value of the overshoot gives the
opposite.

The other half is the mirror, and it falls out of the same recursion with one
sign change. For a segment from A to B:

| tension | aims at | and then | shape |
|---|---|---|---|
| positive | `B + d(T−1)` | **approaches** it | fast then slow — a capacitor charging |
| negative | `A − d(T−1)` | **recedes** from it | slow then fast — the same curve reflected |

Both are `level = target + (level − target)·c`, one multiply-add. The only
difference is that the second uses `1/c`: receding from a target is approaching
it with time running backwards, and the escape factor that lands exactly on B is
precisely the reciprocal of the approach factor. So the mirror costs a division
at design time and **nothing per sample**, and the segment still lasts exactly
the time it was asked for.

Zero is straight — or as straight as the arithmetic allows. `|tension| = 0` maps
to an overshoot of 32, where the curve is 0.004 off a line at its midpoint;
exactly linear is the limit T → ∞ and degenerates the time expression, so it is
approached rather than reached.

Each of the three timed segments has its own, because they want different ones: a
percussive envelope is usually a sharp positive decay under a straight attack,
and a swell is the reverse.

Measured at the half-way point of a 100 ms attack, which is where a curve and a
line are furthest apart:

| tension | −1.00 | −0.50 | 0.00 | +0.50 | +1.00 |
|---|---|---|---|---|---|
| level at 50 ms | 0.179 | 0.455 | **0.504** | 0.545 | 0.821 |

The old control could only produce the right-hand half of that. And the mirror is
checked point by point rather than by eye: `level(u, −t) == 1 − level(1−u, +t)`
to within one sample of grid offset, at three tensions.

**The curve drawn on the panel is the curve that plays** — same arithmetic, same
constants, per segment — so a graph that looks wrong is a graph telling the
truth about something wrong.

The bar down the right of each graph is that envelope's live output on the most
recently played note.

### MOD

Two LFOs, a sixteen-step sequencer, and two matrices.

**LFO rate 0 is a legitimate setting** and is the brief's original trick: the
rate pinned at nothing so the depth comes from somewhere else. The panel shows
it as "held" rather than "0.00 Hz".

**The other end goes to 160 Hz**, which is well past a wobble and into audio
rate: above roughly 20 Hz an LFO on the cutoff stops being movement and starts
being modulation, making sidebands of its own — the same mechanism as the FILTER
page's FM control, reached from the matrix instead. The centre of the travel is
still 2 Hz, so everything below it feels as it did and the old 40 Hz maximum now
sits at 80% of the knob.

Two clamps used to stop it well short: `Lfo::setRateHz` capped at 100 Hz, and
the engine capped the *effective* rate at 100 again after key tracking and the
sequencer had multiplied it. Neither was reachable from a 40 Hz knob without key
tracking, which is exactly why nothing caught it — and key tracking is what the
instrument is for. The engine's ceiling is now derived from its own control rate
(half of it, so the output means what the knob says), which moves with the
oversampling factor: 3000 Hz at 48 kHz ×4, 689 Hz at 44.1 kHz with oversampling
off. A fixed constant would be wrong at one end or the other.

**Retrig** restarts an LFO from the top of its cycle on every note. Free-running
is right for a wobble that should keep its place across a phrase; retriggered is
right for anything that has to line up with the note. On a reese the difference
is the whole sound — with it on, every note gets the same phase relationship and
the growl is repeatable.

**Attack** fades an LFO's *depth* in from nothing over a set time, restarting on
every note. Delayed vibrato is the classic use — the note arrives steady and the
movement creeps in after it — and on a filter it is a sweep that opens up rather
than one already going. It restarts whether or not Retrig is on: the two are
separate ideas, one about the waveform's phase and the other about its depth, and
tying the fade to the retrigger switch would give a knob that silently did
nothing half the time. Eased rather than ramped, so it arrives without the corner
a straight fade leaves at the top.

**Key track** makes an LFO's rate follow the played note, referenced to middle C.
At 100% an octave up doubles the rate, so the modulation keeps its relationship
to the pitch all the way up the keyboard and reads as part of the tone rather
than as an effect laid over it.

**Seq to rate** points the sequencer at LFO 1's *rate*, in octaves — a wobble
that changes tempo on the step.

| matrix | slots | sources | destinations |
|---|---|---|---|
| **Voice** | 6 | amp env, mod env 1–2, velocity, key track, note random, LFO 1–2, sequencer | cutoff, resonance, filter drive, PM index, width A/B, detune A/B, osc mix, sub level, ring, fold, pitch, pitch B, level |
| **Global** | 3 | LFO 1–2, sequencer, **amp env, mod env 1–2, velocity** | **comb time**, comb feedback, comb mix, phase centre, vowel, tube, output, harmonic, notch |

They are separate because the per-note sources have one value *per sounding note*
and the mangle is one chain. The global matrix takes them anyway, by the same
route the comb and the vowel filter already use: it **follows the tracked note**,
the most recently started voice still sounding. That makes the whole mangle
follow one note rather than three stages disagreeing about which. With nothing
sounding those sources read zero, which is the right answer for an envelope.

#### Depth is square, and the ends are extreme

The depth law is `sign(d)·d²`, bipolar, in both matrices. That resolves precision
against reach in one knob rather than adding a range switch: a tenth of the
travel is a hundredth of the depth, so fine control survives at the bottom while
the end of the knob is enormous.

| destination | full depth | a tenth of the knob |
|---|---|---|
| Pitch, Pitch B | ±7200 cents — **six octaves** | 72 cents |
| Cutoff | ±10 octaves | 0.1 octave |
| Detune A/B | ±1200 cents | 12 cents |
| PM index | ×16 | ×0.16 |
| Comb time, Phase centre | ±6 octaves | 0.06 octave |
| Tube | ±36 dB | 0.36 dB |
| Harmonic | ±23 partials | 0.23 |
| Notch | ±6 octaves | 0.06 octave |
| Output | ±24 dB | 0.24 dB |

Comb time and Phase centre deliberately overshoot their own controls' ranges, so
a full-depth envelope drives into the ends and stays there for part of the sweep
— which is the point for a sync-style effect. **Output is the one held back**: it
is a level rather than a character, and thirty-six decibels of it swinging under
an envelope is a hazard, not a sound.

The sequencer is drawn as sixteen faders rather than sixteen knobs, because the
*shape* of the pattern is the thing being edited. The playing step is lit.

### KARGYRAA

The third throat-singing mechanism, and the only one that is a **source** change
rather than a filter change.

In the Tuvan and Tibetan style the **ventricular folds** — the false vocal folds
sitting above the true ones — are drawn into vibration by the airflow and close
at exactly *half* the true folds' rate. Every second glottal pulse is damped by
them. The voice gains a real subharmonic while the pitch being sung, and the
formants shaping it, stay where they were.

**It is not an octave divider and it is not the Sub knob.** A sub adds a
separate tone an octave down, generated independently, which has to be tuned and
can beat against the note. This damps alternate cycles of the waveform that is
already there, so what appears is the **half-integer series** — f/2, 3f/2, 5f/2 —
around every harmonic, at levels set by how different the two cycles are. The
same voice with a doubled period, which is why it growls rather than sounding
like two notes.

3f/2 is the measurement that separates the two, and there is a test for exactly
that: with the control down a saw has energy at 110, 220 and 330 Hz and nothing
between; with it up, 55 Hz **and** 165 Hz and 275 Hz all arrive.

| control | note |
|---|---|
| **Kargyraa** | Depth. 0 is bit-exactly out of the path. It gets quieter as it goes up, because the effect is a periodic absence — that is the sound, not a fault, and there is no hidden make-up gain hiding it |
| **Rasp** | How sharp the damped part of the cycle is. Low is a smooth subharmonic with little more than f/2; high is a narrow rasp with much more of the series |
| **Divisor** | **/2 is kargyraa** — it is what the throat does. /3 and /4 are not anything anatomical; the machinery is the same and a third-order subdivision is a sound this instrument should be able to make |

It is also a voice modulation destination, appended to the end of that list.

#### The lock is by construction

The modulator's phase is *derived* from oscillator A's own cycle counter —
`(cycle + phase) / N` — rather than accumulated from a clock of its own. So it
cannot drift against the note however long it is held, a glide takes it along,
and there is no tuning to get wrong. Measured at A1, A2 and A3: the loudest
component below the fundamental lands at half of it every time, within an FFT
bin. A free-running modulator passes at one note and fails at the others, which
is why the test measures three.

Legato deliberately does **not** restart the clock, so a phrase played without
gaps keeps the growl running through it rather than re-articulating it on every
note. A cold note does restart, from the top of the group, so it always begins
on the undamped cycle.

#### Why the shape is a power of a raised cosine

A gain that steps between cycles is a square wave at f/N multiplying the signal,
and a square has infinite bandwidth. CLAUDE.md §7 calls aliasing a defect
everywhere except where it is the instrument, and here it is not — and
oversampling would not save it, for the same reason it does not save a hard
clipper.

`(0.5 − 0.5·cos t)^k` is `sin²ᵏ(t/2)`, and `sin²ᵏ` expands into a **finite**
cosine series: exactly *k* harmonics of *t* and nothing above them. The
modulator is therefore band-limited by construction, at a bandwidth the Rasp
control names, and the product widens the carrier by exactly `k·f/N` — 220 Hz at
the bottom of the keyboard with the default. No antiderivative, no oversampling
argument, and nothing to measure in order to *know* the bound.

Rasp interpolates between two adjacent integer powers rather than varying *k*
continuously, because a fractional power is an infinite series and would throw
the guarantee away. A linear blend of two band-limited signals is band-limited to
the wider of them.

Measured anyway, because an argument about the modulator is not a statement
about the instrument. At full depth, sharpest rasp, A5, everything above where
the maths says the modulation stops:

| | worst inharmonic, 5–18 kHz |
|---|---|
| raised-cosine power | **−200.5 dB** — the numerical floor, nothing there at all |
| hard gate on the alternate cycle | **−24.4 dB at 5.7 kHz** |

176 dB, between a modulator with a finite Fourier series and one without. The
second row is the break-check: the obvious implementation, seen red.

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
| idle, nothing playing, all 32 slots | **0.5** | 0.05% |
| 8 voices, 1 unison each (16 oscillators) | 387 | 39% |
| 8 voices, 3 unison each (48 oscillators) | 422 | 42% |
| 8 voices, 7 unison each (112 oscillators) | 504 | 50% |

**The voices dominate, not the oscillators.** Seven times the oscillators costs
about a third more; an eighth of the *voices* costs an eighth, because the
filter, the envelopes and the folder's antialiasing are per voice and the
unison bank is not. **Mono is the lever, not unison** — and a reese is one voice
anyway.

### Polyphony: 32 slots, 16 by default

**The ceiling is free and the notes are not**, and the two are worth separating
because they look like the same number:

| notes held | ms/s | core | per voice |
|---|---|---|---|
| 1 | 94 | 9% | 94 |
| 2 | 135 | 14% | 68 |
| 4 | 217 | 22% | 54 |
| 8 | 386 | 39% | 48 |
| 16 | 724 | 72% | 45 |
| 32 | 1431 | 143% | 45 |

Thirty-two *idle* slots plus the whole mangle cost 0.5 ms/s — five hundredths
of a core, and the same figure the engine read when the ceiling was eight —
because `Voice::process` returns on its first line when the amp envelope is
idle and `applyControls` skips inactive voices entirely. A *sounding* voice
costs about 45 ms/s; the first note carries the mangle's fixed 50 on top.

So the Voices control decides the bill and `kMaxVoices` only decides whether
you are allowed to run one up. It defaults to **16** — about 72% of one core
with all sixteen ringing, and more than any sane arrangement holds at once. The
ceiling above that is for a pad whose releases overlap, where most of the
sounding voices are tails on their way out. Thirty-two genuinely sounding at
once is more than a core, which is why it is the ceiling and not the default.

Idle was 17.9 ms/s until the engine learned to stop: once the chain has been
below −240 dBFS for a second with no voice sounding, the render and the
decimation filters are skipped. The clocks keep running, so a slow LFO is where
it would have been when the next note arrives.

### Tails, and what oversampling multiplies

Two things decide the bill when chords are played over each other, and they
multiply:

- **A releasing voice costs the same as a held one** until its envelope goes
  idle, so the real polyphony of a pad passage is *notes per chord × chords
  per release time*. Overlapping four three-note chords through a two-second
  release is twelve full-price voices, legitimately. Releases now end exactly
  when the knob says — an envelope defect that stretched every release to
  roughly eleven times its stated time, piling up inaudible full-price voices
  until the meter pinned, is fixed and regression-tested — so the release
  knob is also the CPU knob for pad work.
- **Oversampling multiplies the whole voice.** Auto at 48 kHz is ×4: near
  enough four times the per-voice price, bought back as aliasing that stays
  below −60 dB at full drive (the table above). Turning it off on a clean
  patch — low fold, moderate filter drive, no tube push — is an honest trade:
  the aliasing table's "off" column says exactly what it costs at each note,
  and on a patch that barely distorts the answer is "very little". At 96 kHz
  sessions Auto already halves the factor, and at 192 kHz it turns off.

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
| **Gate stab** | The hold doing its job: full level for a moment, then gone — the release starts from the top every time, which sustain-at-1 and short notes cannot give |
| **Slow bloom** | A pad, twenty seconds up and six down, with a *negative* attack tension so it starts slowly and arrives — the half of the tension control the old Shape knob could not reach — and an LFO fading in behind it |
| **Sideband growl** | The LFO at 140 Hz on the cutoff, where it stops being a wobble and becomes modulation, key-tracked so the relationship holds up the keyboard |
| **Kargyraa** | The doubled voice: a legato drone, the period halved, and a mod envelope walking the vowel over the top — the folds and the tongue doing two independent things at once, which is what the style actually is |
| **Clockwork wobble** | The ADV envelope as a wobble engine: a looping two-leg envelope, snapped to the grid, closing the filter in exact 1/8s with a snapping down and a swelling up that no symmetric LFO gives. Change the tempo and the wobble retunes live |
| **Twin ramp** | The flanger inside the wave: a double saw whose second ramp a bar-locked LFO sweeps via Morph — no comb, no delay line, and the same bar gets the same sweep on every pass |
| **Dome bloom** | Harmonics grown from a pure tone with zero aliasing at any drive; an ADV loop breathes the pressing, velocity adds bite |
| **Steam pipe** | Noise into a key-tracked comb at 88% feedback: broadband in, pitch out — the playable metallic growl, five decorrelated noise streams wide |
| **FM punch** | Two-operator FM the DX way: a silent 3:1 modulator, a sine carrier, the PM index riding a fast-decay envelope. The brightness *is* the envelope |
| **Bell foundry** | The same two operators pushed inharmonic (4.76:1), with an ADV envelope giving the index a strike, a duck, a shimmer-back and a slow fade no ADSR draws. Chords ring like a gamelan |
| **Vintage swell** | The analogue curve doing pad work: RC-softened saws, drift, both envelopes snapped so the swell lands on the grid, a two-bar LFO on the vowel |

---

## What is not proved

Steinberg's validator passes 47/47 on Linux and **655 DSP tests pass on x86-64**.
The last four-platform run was at 579 tests; ARM64 and macOS are paused on
purpose while the Windows build is finished, so those figures are older than the
count — CLAUDE.md §2.3.

None of that says it sounds good. Your ears are the acceptance test, and the
instrument has now been played on the rig once — the panel rework, the LFO range
and the envelope range all came from that. Kargyraa has not: it is measured and
it is not yet heard.

---

## Roadmap

Kargyraa has been built — see its section under Controls. What is left of the
throat-singing thread is the vowel work below.


Things considered and deliberately not built yet, with the reasoning, so the
next pass does not start from scratch.

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

### Unreleased

**Nine oscillator shapes, a Morph slider, and a live waveform preview.** Five
new shapes join the classic four, each read through a per-oscillator **Morph**
whose meaning is the shape's own and whose zero is always the classic form —
the four originals ignore it entirely, held to the bit by a test. **Vintage**
is an analogue saw core: an RC charging curve with the saw's reset, Morph
deepening the sag. **Dome** is a pressed sine with *zero aliasing by
construction* — integer exponents of the kargyraa identity, blended — measured
at −300 dB where the house line is −60. **Double saw** is two BLEP ramps with
Morph as the offset: a one-oscillator flanger, and at full offset provably the
octave-up saw (nulled to 1e-9). **Harmonic** is sixteen partials with Morph as
the roll-off, each partial faded out approaching Nyquist so pitch sweeps cannot
pop one. **Noise** ignores pitch, sync and PM (the tooltip says so), spreads
wide under unison because every voice gets its own stream, and darkens under
Morph. A small waveform picture beside each shape combo draws the *same
function the DSP reads*, live, so the picture cannot lie; Morph rides a compact
slider beneath it, greyed when the shape ignores it. Morph A/B are modulation
destinations.

**ADV envelopes 1–3: multi-stage breakpoint envelopes.** Off by default and
free when off — byte-proven — each unfolds on the ENV page into up to eight
draggable points (x a segment's time, y its level, a segment's *middle* dragged
vertically for its curve), a ringed sustain point, a loop region that cycles
between the loop point and the sustain while the key is held, and Snap. The
curve arithmetic is the AHDSRs' own, shared rather than copied. They appear as
sources in both matrices as ADV 1–3; loop + snap is a tempo-locked rhythmic
modulator.

**Tempo sync, in two places.** Each LFO gains a Sync toggle and a note-division
choice — synced with Retrig off and the transport running, the *phase* is
assigned from the song position, so the same bar is the same wobble on every
pass, exactly. And every envelope (AHDSRs and ADVs) gains **Snap**, quantising
its times to note lengths at the host tempo — nearest in musical (log)
distance, times under half a 1/32 passing through so plucks stay plucks, the
grid retuning live when the tempo moves.

**The sub split has a bypass.** SPLIT off on the MANGLE page is the pure path:
no crossover, no sub mono, the whole signal through the mangle and one 5 Hz DC
blocker on the way out — for band-splitting on a DAW mixer bus instead. On (the
default) is bit-identical to the engine that shipped; the toggle crossfades
over 30 ms.

**Voices, and the pages.** The global matrix now sits above the voice matrix —
it is the one you reach for — and the page scrollbars grew from a 4 px sliver
to a rail and thumb that can be seen and hit.

**A brushed silver chassis with dark control plates.** The pink-tinted panel had
pink knobs sitting on it and both stopped reading; a saturated accent needs
somewhere uncoloured to sit against. The chassis is now painted metal — a
gradient, a specular band and hashed striations, cached in an image so it costs
one blit — and it shows *between* the plates rather than being covered by a page
fill. The plates stay dark because that is what the accents need: measured
across the six, the lightest a plate can be and still clear 4.5:1 is L 0.34.

**Groups can share a row.** SUB/RING/FOLD, SYNC/PM and KARGYRAA are one band
now, and so are FILTER/KEYBOARD and VOWEL/OVERTONE. Widths are proportional to
each group's column count, so the cells come out the same size across a band.

**Output and Oversampling moved to the header**, beside A/B and COPY, on every
plugin that has them — and **Mix** joins them on the ones that have a dry/wet
(Emberdrive and Anvil). They are the same controls on every plugin in the suite
and the last thing anybody touches; hunting for the output trim on a different
tab in each plugin is the sort of thing that is only annoying twenty times a day.

**Sonitus's output now defaults to 0 dB**, unity, like every other plugin in the
suite already did. The -6 dB it had was a safety margin nobody asked for, paid
for in dynamic range on the way into the mixer.

**A TIPS switch in the header.** The tooltips here are whole paragraphs — they
are how the plugins document themselves — which is worth a great deal on the
first day and is in the way on the fiftieth. The setting is saved with the
project.

**Seven more built-in tunings**, each derived from its definition: the harmonic
series an octave higher, the **undertone series** (the harmonic series
reflected, and the scale-shaped twin of what kargyraa does in the time domain),
just 7-limit, a seventeen-note Pythagorean chain, and 17-, 22- and 41-TET.
Slendro and pelog are deliberately **not** here: they vary per gamelan and there
is no definition to derive, so shipping one would be shipping somebody's
measurements.

**Three more presets** — see the table above.

**AHDSR, and tension that goes both ways.** A hold stage at full level between
the attack and the decay, and the single one-way Shape control replaced by three
**bipolar** tensions — one per timed segment. Zero is straight, positive is the
analogue curve, negative is that curve reflected. See ENV above for the
arithmetic and the numbers. The `<x>Shape` parameter ids are gone rather than
kept as dead aliases; nothing has shipped and an id that silently does nothing is
worse than one that is absent.

**An attack on each LFO** — a fade-in on its depth, restarting per note.

**A new palette: hot pink, and five siblings a golden angle apart.** Every page
wears its own accent, all at one OKLCH lightness and each at its own hue's chroma
limit in sRGB. The tab row is a colour key. See PluginEditor.cpp for why the
golden angle is the right spacing and why there is no such thing as a colour that
makes anyone more creative.

**Kargyraa.** The third throat-singing mechanism, and the only one that changes
the source rather than the filter: alternate cycles of the waveform damped,
locked to oscillator A's own cycle counter, producing the half-integer series
around every harmonic. Three controls, a modulation destination, a preset, and
four tests — the subharmonic series, the phase lock at three pitches, bit-exact
bypass at zero, and aliasing at **−200.5 dB** against a hard gate's −24.4 dB.
The `kargyraa` destination is **appended** to the voice list and every new
control defaults to neutral, so nothing that existed before it changes.

**The LFOs reach 160 Hz**, up from 40, and two clamps that would have eaten it
are gone — including one that key tracking could already hit. See MOD above.

**The envelopes reach 20 seconds** on attack, decay and release, up from 5 and
10: long enough for a pad that takes a phrase to arrive. The centres are
unchanged, so the short end feels exactly as it did.

**The panel, reworked.** Smaller controls, denser layout, and the envelopes
drawn rather than tabulated.

- A shared **arc-and-pointer look and feel** (`shared/tezla-ui/KnobLookAndFeel`).
  A stroke of constant thickness reads at any diameter where a filled wedge
  turns into a blob, which is what makes a 40-pixel knob legible — and 40 pixels
  is what sixty controls on six pages need. The arc grows from the parameter's
  **anchor**, found from the range itself, so a bipolar control draws as a
  departure from centre rather than as a bar growing from the far left.
- Pages are built from **groups**, each with its own heading, its own column
  count and its own panel. A short group centres on what it has rather than on
  what it was allowed.
- The **ENV page is bespoke**: three draggable graphs with their knobs beside
  them. See the ENV section above.
- The guidance note moved from the bottom of the page to a **fixed strip under
  the viewport**. On the page it was below the fold on exactly the two pages long
  enough to scroll — and the MANGLE note is the one that says what oversampling
  is doing right now.
- **LFO Retrig and Key track reached the panel.** Both parameters existed and
  were wired to the engine; neither had a control, so neither could be used.
- Grouping fixes that were invisible while headings were only row separators:
  Sync and PM are their own group, and Output and Oversampling are no longer
  filed under Overtone.

**Modulation depth is square, and the ceilings are raised.** See the table in
the MOD section. **The sub oscillator moved to −2..+2 octaves.**

**The editor is now checkable from here.** `tezla-render editor` drives it with
no display: `hit:<id>` asks whether a control is the thing a click at its own
centre would reach, and `shot:` photographs a page. Every tab, every page, the
step strip, the three envelope graphs and every parameter cell carry a component
id, so the check covers them. Seen red by replacing the step strip's
`addAndMakeVisible` with a bare `setVisible` — which is the bug that shipped in
v0.1.0 — and the run exits 1 with "no component with id steps".

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
