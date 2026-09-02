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
| Drift | Slow random pitch wander, in cents, per copy. What an analogue oscillator bank does — and it carries on between notes: a key restarts the unison scatter exactly as before, never the drift |

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
- **Drift** is the voice card's temperature, in cents of cutoff: a fixed
  per-voice mismatch plus a slow wander that carries on between notes, moving
  cutoff and resonance together and the whole voice's tuning a little. See
  "Analogue drift" under Measured for the model and the numbers.

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

Scala `.scl` scale files and `.kbm` keyboard maps, plus **44 built-in scales**
— and a panel that says what each one *is*.

**This belongs in this instrument rather than being a bolt-on**, because the
comb key-tracks onto harmonics of the played note. In twelve-tone equal
temperament a major third is 14 cents sharp of the real 5/4 and beats against
its own comb; in just intonation it does not, and a sustained chord locks
instead of churning. The difference is large on a bass.

| group | scales |
|---|---|
| pure | just major/minor, 7-limit, Pythagorean (12 and 17), harmonic series 8–16 and 16–32, undertone series, **Partch 43** |
| historical | quarter-comma meantone, Werckmeister III, Kirnberger III, Vallotti |
| ancient Greece | Archytas' enharmonic, diatonic and chromatic; **Ptolemy's even diatonic** (the 12/11 neutral second, 2nd century AD) |
| **Old Babylonian** | the seven tunings of the tablets — **nīd qabli** (the Hurrian hymn tuning, ~1400 BC, the oldest named tuning of the oldest written music), išartum, embūbum, kitmum, pītum, nīš gabrim, qablītum — the diatonic's seven modes from a chain of pure fifths, a millennium before Pythagoras |
| ancient China | the **twelve lü** by the san fen sun yi rule — the one-way chain of fifths of the Yellow Bell |
| **Persian dastgah** | **Shur** and **Chahargah** on Farhat's theoretical intervals — the koron neutral seconds (135/165 cents) and the 270-cent plus tone |
| maqam theory | **Rast** twice: al-Farabi's just ratios with Zalzal's 27/22 neutral third, and the Turkish Arel–Ezgi–Uzdilek Rast on the 53-comma grid — 30 cents apart on the third, both on purpose |
| non-octave | **Bohlen–Pierce** (repeats at 3/1), **Carlos Alpha / Beta / Gamma** (divisions of the fifth), **Golden phi** (seven equal parts of 833.09 cents — the golden ratio as the repeat) |
| equal | 5, 7, 12, 17, 19, 22, 24, 31, 41, 53 |

**The panel shows the selected scale's theorem and its story.** Every built-in
carries its construction — the one sentence of arithmetic its degrees fall out
of — and a few sentences of where it comes from and why it matters, beside a
degree table: exact fraction where the degree is one (the detector accepts
near-exact rationals only, so a tempered degree never wears a fraction it did
not earn), cents, the step to the next degree, and **the sounding frequency in
Hz**, with the repeat interval as the last row. A scale loaded from a file gets
the same table computed from its own numbers.

**Concert pitch is a control, and the history is on the panel.** The **A4**
slider scales the whole tuning — keyboard-map reference included — by one ratio
against 440, so it means something even in a scale with no A in it; the Hz
column and the root readout follow the drag live, and the setting is saved with
the project and left alone by presets. Each scale also states, in bold, what
its tradition actually tuned to: no absolute pitch survives from Babylon or
Greece; Persian and gamelan practice tune to the singer or the forge; the
baroque settles on **A415** in modern practice (one click applies it);
Huangzhong was an absolute standard of state whose bells survive; Partch fixed
his 1/1 at **G-392**; and A440 itself is only ISO 16 of 1955 — A432 has no
historical orchestra behind it, which the panel says while making it one drag
away.

Every one is **generated from its definition** rather than shipped from an
archive — a Pythagorean scale is a chain of 3/2s, Bohlen–Pierce is thirteen
equal steps of 3/1, the Babylonian seven are rotations of six pure fifths, and
Partch's 43 (the one list with no generating rule) is reproduced with
attribution and verified for the symmetry its book claims. That is arithmetic,
and it is also how CLAUDE.md §2.1 and §9 want it done; the access honesty for
the historical numbers is in `docs/DSP-REFERENCES.md`.

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
| 8 voices, 1 unison each (16 oscillators) | 274 | 27% |
| 8 voices, 3 unison each (48 oscillators) | 312 | 31% |
| 8 voices, 7 unison each (112 oscillators) | 403 | 40% |

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
| 1 | 80 | 8% | 80 |
| 2 | 107 | 11% | 54 |
| 4 | 160 | 16% | 40 |
| 8 | 265 | 26% | 33 |
| 16 | 486 | 49% | 30 |
| 32 | 937 | 94% | 29 |

Thirty-two *idle* slots plus the whole mangle cost 0.5 ms/s — five hundredths
of a core, and the same figure the engine read when the ceiling was eight —
because `Voice::process` returns on its first line when the amp envelope is
idle and `applyControls` skips inactive voices entirely. A *sounding* voice
costs about 29 ms/s; the first note carries the mangle's fixed 50 on top.

So the Voices control decides the bill and `kMaxVoices` only decides whether
you are allowed to run one up. It defaults to **16** — about half a core with
all sixteen ringing, and more than any sane arrangement holds at once. The
ceiling above that is for a pad whose releases overlap, where most of the
sounding voices are tails on their way out. Thirty-two genuinely sounding at
once is nearly a whole core, which is why it is the ceiling and not the
default.

Idle was 17.9 ms/s until the engine learned to stop: once the chain has been
below −240 dBFS for a second with no voice sounding, the render and the
decimation filters are skipped. The clocks keep running, so a slow LFO is where
it would have been when the next note arrives.

### The ×8 stress case, measured, and what was done about it

The case that pins the meter: **16 voices, unison 7 + 7 (224 oscillators), a
6 s release, every voice sounding for the whole window**, 48 kHz, 512-sample
blocks. `tezla-measure sonitus-stress` reproduces it, on any machine. Measured
here on 2026-09-01, GCC 13 on x86-64 Linux, best of three, in milliseconds of
CPU per second of audio (÷10 = % of one core):

| oversampling | internal rate | ms/s | core |
|---|---|---|---|
| off | 48 kHz | 252 | 25 % |
| ×2 | 96 kHz | 489 | 49 % |
| ×4 (Auto at 48 k) | 192 kHz | 975 | 98 % |
| ×8 | 384 kHz | **1973** | **197 %** |

**Exactly linear in the factor**, to the percent, because the whole voice —
oscillators, filters, fold, envelopes — runs at the internal rate. ×8 at 48 kHz
is 384 kHz internally, past anything the anti-aliasing here needs (the policy
in CLAUDE.md §6 targets ~192 kHz effective); it exists as the deliberate
CPU-for-headroom trade and costs precisely what it says.

**Where the ×8 time goes**, from callgrind (share of all instructions) and
from removing one feature at a time (share of the bill):

| stage | profile | removed | saves |
|---|---|---|---|
| polyBLEP oscillators + unison summing | 60 % | unison 7+7 → 1+1 | 42 % |
| SVF filters, of which the rail `tanh` | 14 % | filter drive 0.5 → 0 | 21 % |
| fold (ADAA) | 8 % | fold 0.3 → 0 | 11 % |
| tube stage (`pow`, per mix sample) | 6 % | tube 12 dB → 0 | 7 % |
| sine sub (libm `sin`) | 9.5 % | sub off | 5 % |
| per-chunk control (`applyControls`) | 4 % | | |
| halfband down-sampler | 1.7 % | | |

The drive column is the interesting disagreement: removing drive saves three
times the profile's `tanh` share, because with drive the integrator states
spend most of their time past the rail's knee, where the `tanh` is doing real
work. Below the knee the rail is already the identity, bit for bit, with an
early return — so there is nothing free to take there.

**Link-time optimisation is not the lever.** Same harness, five builds:

| | gcc | gcc `-flto=auto` | clang | clang thin | clang full |
|---|---|---|---|---|---|
| ×8 | 1973 | 2077 (+5 %) | 2382 | 2247 (−6 %) | 2374 (±0) |

Best case −6 %; GCC's LTO is 5–9 % *slower*. 93 % of the time is inside one
function into which everything is already inlined, so there is no boundary for
LTO to cross. MSVC on the rig is unmeasured — run the command there with and
without `-lto` — and the same structural argument applies.

**An AVX2 build is a lever, and a bit-exact one.** With contraction off the
compiler vectorises without reordering sums, so the AVX2 build's 32 golden
renders are byte-identical to the SSE2 build's; interleaved on the stress
case it reads ×8 1561 → 1400 ms/s (−10 %), ×4 799 → 702 (−12 %), off 210 →
186 (−11 %). It is off by default only because an AVX2 binary needs a
2013-or-newer CPU — see `BUILD.md`, "A note on `TEZLA_ENABLE_AVX2`".

**What was done, under the rule that the output must not change by a single
bit.** Every change was checked against 32 golden renders — eight patches (the
stress case, PM with feedback, hard sync, pulse/triangle with morph,
envelope-to-cutoff with filter FM, formant with flange, ring with kargyraa,
drift) at each of the four factors — rendered before the change and compared
byte for byte after it, plus the full test suite:

- **Change-guards on four per-chunk setters** (`Oscillator::setShape` and
  `setWidth`, `UnisonBank::setMorph`, and the SVF's cutoff update no longer
  recomputing the resonance `pow`): 32/32 identical, 1002/1002; ×8 1973 →
  1928 ms/s (**−2.3 %**), off 252 → 245 (−2.7 %).
- **The right-channel filter adopts the left's cutoff coefficient** instead of
  evaluating the same `tan`: 32/32 identical, 1002/1002; and honestly **not
  measurable** — 970 → 976 ms/s at ×4 on the envelope-to-cutoff patch, inside
  the noise, because the saved `tan` is about 2 % of the work. Kept because it
  is strictly less work for the same bits.

**What was not done, because it would change the sound**, however slightly,
and the user's brief was that nothing may: replacing the sub's `sin`, the
rail's `tanh` or the tube's `pow` with cheaper evaluations (~−20 % together,
but different bits); updating filter coefficients per control chunk instead of
per sample (a different sweep); skipping the formant bank at mix 0 (its state
would be cold the instant the knob moved); and the structural fix — running
the band-limited oscillators at the base rate and oversampling only the
nonlinear stages, roughly halving ×8 — which changes the signal path and needs
the aliasing and rate-independence measurements again. All four are on the
roadmap with what would unpark them.

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

### Render quality

The factor a session *plays* at is a CPU decision, and the factor a bounce
*gets* need not be the same one: a host rendering offline is not on a
deadline, so ×8 there is paid in render time rather than dropouts. **RENDER**
in the header, beside OS, is that second setting. *Same as live* — the default,
and neutral — bounces exactly what was played; *Auto / ×2 / ×4 / ×8* apply only
while the host reports it is rendering offline, and the engine then builds the
same graph it would for that factor live. A bounce at ×8 is therefore a live
×8 bit for bit, and a test holds it to that sample for sample — armed but not
rendering it changes nothing either, checked the same way. Latency is
re-declared at the render factor.

Two honest caveats. A bounce at a higher factor than the session played at is
not what was auditioned — that is the point, and also why the default is *same
as live*. And whether a host flips the offline flag before rendering, and
when, is the host's business: the VST3 contract is that it does so before
re-activating the plugin, which builds the graph at the render factor before
the first sample. FL Studio has not been checked from here. If a bounce turns
out to begin with a cut note, that is the host flipping the flag per block
instead of re-preparing, and the clean-stop rebuild the OS control has always
done.

### Analogue drift: the voice card's temperature

Two kinds of drift, because a real voice card has two. **Per oscillator** (the
OSC page's Drift, per bank): each VCO has its own exponential converter and its
own tempco, so the copies of a unison stack wander *against each other*, and
that differential wander is the part the ear hears as alive — a linked-only
drift would freeze the stack's internal beating, which is the "one loud saw"
the drift exists to prevent. **Per voice** (the FILTER page's Drift): the whole
card warms and cools together, so its VCF, its resonance and its VCOs all move
a little *in the same direction* — audible not within one voice but between
the voices of a chord.

The per-voice process, D being the control in cents:

- A **fixed mismatch** per voice slot, drawn once at prepare (real polysynths
  trim their filters by hand, and not to zero), and a **wander**: a one-pole at
  0.15 Hz towards a new uniform target every 0.5 s — slower than the
  oscillators' 0.35 Hz because a filter board is thermally heavier than a
  transistor pair. Stepped once per control chunk for every voice, sounding or
  not, and **never restarted by a key**; only a prepare restarts it.
- **Cutoff**: × 2^(D·w / 1200), w the composite (half mismatch, half wander),
  so bounded by ±D. Lands on the 4 ms cutoff smoother's target: click-free, and
  the per-sample filter path is untouched.
- **Resonance, paired**: + 0.2 · (D / 600) · w, clamped to 0…1 — ±0.01 at the
  30 cents a warm polysynth does, ±0.2 at full.
- **Pitch, a little**: min(D / 4, 15) cents × the wander only, on both banks
  and the sub — VCOs get autotuned at power-up and VCFs do not, so the mismatch
  belongs to the filter alone; capped so the creative end moves the filter,
  not the tuning.
- **At 0 nothing runs**: every application sits behind a branch on the
  control, so the default changes no bit — the goldens are the check.

Measured, 8 voices at D = 100 cents over 4 s: every voice's cutoff drift stays
within ±100 cents (the furthest any voice went was **69.0 cents**), no two
voices agree at any instant, and the largest step between control chunks is
**0.047 cents** (the test allows 0.1). At D = 600 with resonance at full, the
resonance stays within 0…1. The tuning never moves more than the cap: at
D = 600 the furthest was **9.2 cents** against a cap of 15; at D = 40,
**6.1 cents** against 10.

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
| **Sixteen-step gate** | Sixteen 1/16 legs of one ADV envelope on Level, looped and snapped: a step sequencer with *curves* between the steps. The ENV ruler draws the bar underneath it |
| **Bar riser** | The same sixteen points as a shape rather than a gate — a stepped climb whose acceleration is drawn leg by leg, sweeping cutoff, filter type and drive together |
| **Two-hand macro** | Two knobs, eight destinations: MACRO 1 is aggression, MACRO 2 is size. Play with one hand on each |
| **Morph wah** | A wah that changes filter *type* rather than cutoff — a bar-synced LFO swinging lowpass to bandpass and back. No cutoff sweep makes that sound |

---

## What is not proved

Steinberg's validator passes 47/47 on Linux and **931 DSP tests pass on x86-64**.
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

**The drift knobs wear a pastel rainbow.** All three — Drift on each
oscillator and the FILTER page's Drift — draw their ring as a pastel hue sweep
along the travel, faintly even at zero and fully where the value is, with the
pointer taking the hue under it. The mark of a control whose job is to add
colour to the sound, and it is a house variant (`ui::spectralKnob`, the
numbers in `PanelDesign.hpp`) rather than a knob styled by hand.

**Voice drift.** A second kind of analogue drift, on the FILTER page: the
voice card's temperature. Cutoff and resonance wander together, each voice
with its own fixed mismatch and its own slow wander that carries on between
notes, and the whole voice's tuning moves a little with them (a quarter of the
amount, capped at 15 cents). About 40 cents is what a warm polysynth does; the
control goes to 600 for creative use, and at 0 it is bit-exactly off (the 32
goldens are unchanged by it). Parameter `voiceDrift`, schema V7. The model and
the measurements are under "Analogue drift" in Measured.

**The analogue drift no longer retriggers.** Every cold note used to call the
unison banks' full reset, which re-seeded the one random stream that fed both
the phase scatter and the drift and zeroed the walk — so a given voice slot
replayed the same wander from zero on every press, and note two sounded exactly
like note one. The bank now has two streams: the scatter's is re-seeded on
every cold note, so the unison and every other oscillator signal retrigger
byte-for-byte as they always have (tested: with the drift at zero a repeated
note *is* the first note again); the drift's is seeded once, at prepare, and
never by a note, so the walk carries on through a key press and two presses
are two different notes (tested: every copy's drift reads the same value
before and after the note-on, and the second note differs). A bounce from a
fresh session is still reproducible — two engines prepared alike render the
same doubles. Of the 32 golden renders, 28 are byte-identical to before; the
four of the drift patch changed, as they had to.

**Render quality.** A second oversampling setting, RENDER, in the header
beside OS: what an offline bounce runs at. *Same as live* is the default and
neutral; *Auto / ×2 / ×4 / ×8* take effect only while the host reports offline
rendering, through `dsp::RenderOversampling` and one shared resolver, so a
session can play at ×2 and bounce at ×8 without the ×8 ever costing a live
dropout. The bounce is the live graph at that factor, bit for bit (tested
against a live ×8 sample for sample, and against a live ×2 with the setting
armed but the host playing). Parameter `renderOversampling`, schema V6; the
header's OS and RENDER tooltips now read the live state. See "Render quality"
under Measured for the caveats.

**The panel gets its own design, and the wheel stops editing.** Eight variants
of this panel were built as real editors and photographed; the one chosen is
now the house design for the whole suite, and its numbers live in
`shared/tezla-ui/include/tezla/ui/PanelDesign.hpp` rather than in a variant
table — an environment switch is a thing to decide *with*, not a thing to ship
on a control surface. What landed:

- **A hue per group**, 18 degrees apart, on the heading, a spine down the
  plate, the names, the knob tracks and the dropdowns. So MANGLE's four groups
  are four colours and the eye can find COMB without reading a word.
- **Cells narrower and taller** (118 × 86–112 against 172 × 62–80), and a group
  **fills its row** rather than centring in a sea of metal — so the knobs came
  out both bigger and closer together, which sounds contradictory and is just
  what happens when a six-column count chosen for a 172 px cell is allowed to
  become eleven.
- **A size hierarchy**: CUTOFF and LEVEL at 1.32×, DRIFT and SPREAD at 0.74×.
- **Knobs in a countersunk well** with a machined skirt. The panel that shipped
  drew the knob body and the plate behind it within a few points of the same
  lightness, and the control read as a smudge; geometry separates them, not hue.
- **Never a tick box.** Every on/off control is a moulded cap in a recessed
  bezel that travels when pressed and lights red with a halo. It is red on every
  group deliberately: a power switch is red on every box in a rack precisely so
  it can be read without first being identified.
- **Dropdowns wear their group's colour** — tab, wash and outline — because a
  choice holds a word where its neighbours hold a number, which makes it the one
  control that reads as a caption.
- **Bigger numbers**: 14 pt, 16.5 pt on a lead control, bold. The number under a
  knob is what is actually read, and 11.5 pt was chosen for a much wider cell.
- **The wheel scrolls the panel and never moves a control.** Six pages of sixty
  controls do not fit a window, so the wheel is how you reach the bottom of one
  — and a wheel that also edits is a trap with no feedback: the pointer passes
  over Detune on the way down, the page does not move, and the patch has
  silently changed by three cents.

**The ADV envelope graphs zoom.** Wheel over one and the time axis zooms about
the pointer, up to 32×; shift-wheel scrolls it, and so does dragging the strip
that appears along the bottom; double-click puts the whole envelope back. That
is the one place the wheel does something other than scroll the page, and the
reason is that it changes *nothing* — no parameter moves, and the graph in front
of you answers what happened. Sixteen points across 900 pixels is 56 of them a
leg, and a point's time, level and tension have no knobs at all, so this graph
is the only way to reach them.

Dragging a point at 8× moves it an eighth as far per pixel, which is the whole
point: zooming in has to make the gesture *finer*, and taking the scale from the
plot's width instead of the axis would have made it coarser. Zooming out fully
hands the next notch to the page, so the wheel is never dead over a graph; and
zoom in, pan, double-click leaves the panel **bit-identical** to one that was
never touched — checked by rendering both and differencing them, and seen to
fail with the reset aimed at 1.02× instead of 1×.

Two bugs fell out of photographing the result, both older than the design work:

- **The lit switch's glow was being thrown away.** JUCE clips a component's
  painting to its own bounds, and the halo was drawn at `expanded(3.5)` —
  entirely outside the clip, so all that ever reached the screen was the
  one-pixel sliver between the bezel and the component edge. The button is now
  larger than the switch drawn inside it, by `LampButton::kGlowMargin`.
- **AMPLITUDE's Snap switch was drawn on top of MOD ENVELOPE 1's Attack.** The
  envelope grid is three by three, filled row by row, and AMPLITUDE carries ten
  cells — eight stage controls, Velocity and Snap. The tenth went to row three,
  one row below its own block. It has been doing that since Snap was added, and
  nothing caught it because there is no layout test; it took a screenshot. The
  grid now widens instead of overflowing, and fills column-major, so each column
  is one envelope stage with its tension and its level and the whole-envelope
  controls fall into the last one.

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
free when off — byte-proven — each unfolds on the ENV page into up to **sixteen**
draggable points (x a segment's time, y its level, a segment's *middle* dragged
vertically for its curve), a ringed sustain point, a loop region that cycles
between the loop point and the sustain while the key is held, and Snap. The
curve arithmetic is the AHDSRs' own, shared rather than copied. They appear as
sources in both matrices as ADV 1–3; loop + snap is a tempo-locked rhythmic
modulator.

Sixteen is the number that makes a *pattern*: with Loop and Snap on, sixteen
points is a bar of sixteenths, so one ADV envelope is a step sequencer with
curves between the steps rather than a gate. The ceiling was eight until it was
raised; points 9–16 are appended parameters, so a patch saved against the
eight-point build reopens bit for bit unchanged (verified by rendering it
before and after), and the handles' grab radius shrinks with density so a
sixteen-point envelope's segment curves stay draggable.

### Phase 4, measured

`tezla-measure sonitus` grew a phase-4 section. The three tables it prints:

**The filter morph trades low for high** — response three octaves either side of
an 800 Hz corner, no resonance, through the running filter at 48 kHz:

| morph | 100 Hz | 6400 Hz | |
|---|---|---|---|
| +0.00 | −0.13 dB | −37.29 dB | lowpass |
| +0.25 | −6.09 dB | −24.66 dB | |
| +0.50 | −18.20 dB | −18.70 dB | bandpass |
| +0.75 | −24.16 dB | −6.08 dB | |
| +1.00 | −36.27 dB | −0.12 dB | highpass |

A crossfade between two static filters would move both ends together; a morph
trades them, and the bandpass row sitting symmetrically at −18 dB on both sides
is what that looks like.

**The FM ratio readout**, for a set of B pitches with A at unity: `1:1`, `2:1`,
`3:2 −2 c` (a tempered fifth — this is why 12-TET fifths beat), `3:2 +0 c` for a
pure one, `2.030  26 c off 2:1` for a beating pair, `8:1` at four octaves, and
`32.000  far apart` at five.

**The scale lock's correction**, as cents, sweeping a comb across four decades:

| tuning | degrees | worst | mean |
|---|---|---|---|
| 12-TET | 12 | 49.98 | 25.00 |
| Just major (5-limit) | 7 | 101.95 | 44.94 |
| Pythagorean | 12 | 56.83 | 25.29 |
| Bohlen-Pierce | 13 | 73.12 | 36.56 |
| Partch 43 | 43 | 19.42 | 7.40 |

The finer the scale, the smaller the correction — which is the right shape, and
it says the lock is a nudge rather than a retune everywhere but the seven-note
just scale, where half a step really is 102 cents.

**Four macros, sources in both matrices.** A macro is one knob wired to as many
destinations as you point it at — the one control shape a matrix structurally
cannot give, because a row has one source and one destination, so "open the
filter *and* add drive *and* widen the unison" costs three rows whose depths
then have to be kept in step by hand. Assign the same macro in three rows, in
either matrix or both, and one control moves all three, each by its own depth
and in its own direction.

They are plain values rather than generators — nothing to tick — and at their
default of 0 they contribute exactly nothing wherever they are pointed, so an
unassigned macro is free and a patch saved before they existed is untouched
(byte-proven through the plugin with all four at full).

**The comb locks to the scale.** It already key-tracks, but its delay is a
*continuous* frequency, so on a microtuned patch — which is half of why Sonitus
exists — it resonates between the scale's notes and fights the tuning it is
meant to serve. **Scale lock** on the MANGLE page snaps the comb's resonance
onto the loaded tuning. On 12-TET it is a small convenience; on Partch's
43-tone or a Persian dastgah it is the difference between a comb that belongs
and one that does not.

The snap is applied to where the comb *actually* ended up — key tracking,
modulation and all — rather than to the knob, because the knob is wrong
whenever anything is sweeping it. `Tuning::nearestScaleHz` does the arithmetic
(nearest in cents, checked by brute force against every degree within six
repeats); `Comb::setTuningRatio` applies it as one multiplication, so the comb
stays framework-free and knows nothing about scales. Off is exactly 1.0 and
therefore bit-exact, byte-proven through the plugin.

**The filter morphs.** Mode is a *choice*, and a choice cannot be a modulation
destination (a switch reconfigures rather than adjusts), so the filter's
character was the one thing in a voice no envelope could sweep. **Morph** is a
continuous, bipolar control along lowpass → bandpass → highpass, and it is a
destination in both matrices.

Bipolar and **centred on whatever Mode says**, which is the part that matters
for compatibility: an absolute 0–1 "position" control would default to lowpass
and silently convert every bandpass patch ever saved. Zero is the chosen mode
bit for bit — verified against a filter that never had `setMorph` called at
all, for all four modes, over a 20 Hz–20 kHz sweep. From a lowpass, +50% is
exactly the bandpass and +100% exactly the highpass; from a highpass, −100% is
exactly the lowpass.

It genuinely sweeps rather than crossfading between two static filters: the
three outputs are summed *before* the measurement, so the bandpass's 90° lead
at the corner is part of the result. `magnitudeAt` does that arithmetic in
complex form and agrees with the running filter to **0.0102 dB** across 90
combinations of mode, morph and frequency.

**Notch is not on the axis and ignores Morph.** It is the sum of the two ends
rather than a point between them; putting it on a slider between lowpass and
highpass would be inventing a shape nothing makes.

**The OSC page states the FM ratio.** Two oscillators tuned in octaves,
semitones and cents is the right interface for detuning and the wrong one for
FM, where the only question is the ratio and whether it is simple: 2:1 and 3:2
fuse into one instrument, 2.03:1 beats, 4.76:1 is a bell. The SYNC AND PM
heading now says which you have — `B:A 3:2  harmonic`, or
`B:A 2.030  26c sharp of 2:1` when it is not, or the plain decimal past four
octaves where no small pair describes it. No new parameter; it is computed from
the six pitch knobs that were already there.

Nearness is measured in **cents**, not as a difference of ratios — a fixed
ratio tolerance would be eight times as forgiving at 8:1 as at 1:1 and an ear
is not. `shared/tezla-dsp/include/tezla/dsp/Ratio.hpp`, which is deliberately
*not* `Tuning.hpp`'s `nearestFraction`: that one recovers p/q only when the
double **is** p/q to within a few ulps and refuses tempered intervals, because
a tuning table printing "442/295" for an equal-tempered degree would be a lie.
Both are right; merging them would break one.

**Four presets for the four new things, and a headroom pass that came out of
measuring them.** *Sixteen-step gate* and *Bar riser* are the sixteen-point ADV
envelopes used the two opposite ways — as a rhythm and as a shape; *Two-hand
macro* is the macros; *Morph wah* is the filter morph on a bar-synced LFO. The
numbers are in the phase-4 section below.

Rendering them beside the existing presets found **four presets above full scale
on a single note** — *Scale drone* at 1.949, nearly +6 dB over, from the phase-4
close-out an hour earlier. On a sixteen-voice instrument that leaves no headroom
at all for a chord. Fixed by trimming Output and nothing else. No test could have
caught it: a preset's level is not a claim any test makes, so the rule now lives
beside the preset table in the source, with the measurement that produced it.

**The envelopes have a ruler.** With Snap on, an ADV graph draws the grid it is
snapping to: bar lines in the accent colour, beats and subdivisions behind them,
numbered bars along a strip at the bottom, and the note each leg landed on
written over the leg — so a 1/8 triplet reads as `1/8 T` rather than as 167 ms.
A point arriving exactly on a beat drops a stem to the ruler and one that does
not, does not; that is the useful part, because Snap quantises each leg's
*length*, so a chain of legal note values can still land between beats. With
Loop on, the readout is the loop's length in bars. Snap off, the same strip is a
plain seconds ruler — the graph has a time axis either way now, where before it
had none.

Two things came out of building it. The ADV graph was drawing the **raw**
parameter times while the engine played snapped ones, so a synced envelope
showed a shape the synthesiser was not running; it now draws through the same
`dsp::snapSeconds` the audio thread calls. And the AHDSR graphs, whose axis is
the knobs' own travel rather than seconds — deliberately, so a 5 ms attack is
visible beside a 5 s release — cannot carry a ruler honestly, so their stage
marks name the note instead: `A 1/16  D 1/8  S  R 1/4`.

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

### Phase 4 presets

Seven more, three that use the new feedback controls musically and four that
do not hold back. Measured at A1 and A3: every one is finite, peaks between
0.62 and 0.87, and decays to under 1e-10 after release.

| preset | what it is |
|---|---|
| **FM reese -- teeth without unison** | The reese made with operator feedback instead of a big detuned stack: same bite, a third of the voices, and mono-compatible because the harmonics are generated per voice rather than by cancellation between them. Env 1 opens the feedback after the transient. |
| **Operator bass -- two sines, all index** | Two sines and nothing else -- no filter movement, no unison, no comb. The counter-argument to every other preset here: the cleanest way to a hard bass is often not to mangle a saw. |
| **Cross-bite -- the loop, on a leash** | A modulates B *and* B modulates A, at depths where the loop growls rather than tears. Solo each direction to hear the asymmetry: the reverse path is a sample late, so they do not sound alike at the same number. |
| **SCREAMFACE -- every guard at once** | Both operators at full feedback, the loop closed hard both ways, folder wide, ring, kargyraa, resonant comb. No clean signal anywhere in it. 24 partials, brightness centroid **7.49** against a plain saw's 5.73. |
| **Neural collapse -- the loop, re-decided per step** | The *sequencer* drives the reverse PM depth, so the loop's character is re-decided sixteen times a bar -- and a loop through two nonlinearities does not respond linearly to its own depth, so each step lands somewhere unrelated to the last. Deterministic, so it prints. |
| **Tearout larynx -- a throat that screams** | Formant filter locked to the note's own harmonics, kargyraa doubling the period the way a real kargyraa singer's false folds do, both operators feeding back hard enough to give the vowel something to filter. The vowel walks on a synced LFO. |
| **Gravel storm -- noise with a pitch bolted on** | No oscillator plays the note at all: the pitch is the comb's resonance tracking the key, and the note number chooses a delay length. A feedback sine ring-modulates the noise for teeth. Brightness centroid **11.91**, the brightest thing the instrument makes. |
| **One knob reese -- MACRO 1 does all of it** | One control wired to five destinations at once: cutoff, filter *morph*, operator feedback, detune, and comb time — in both matrices. At 0 a dull close reese; at full a screaming one. Measured across the knob: 360 → 601 → 1038 → 1561 → 1373 zero crossings per 0.5 s, peaks 0.376 to 0.851 |
| **Morphing pluck -- the filter changes type, not just cutoff** | ADV 2 drawing the filter along lowpass → bandpass under the note, so it starts as a thud and arrives as a whistle. No cutoff sweep does that |
| **Scale drone -- the comb belongs to the tuning** | The one preset that needs a microtuning loaded to show what it does. Comb scale lock on, key tracking high: on 12-TET a pleasant resonant drone, on Partch 43 or a Persian scale a comb that stops sitting between the notes |

These lean on every bound in the instrument at once -- the feedback cap, the
folder's ADAA, the comb's limit, the safety limiter -- and that is deliberate.
A preset that exercises every guard simultaneously is the honest stress test.

### Phase 4 showcase presets, and a headroom pass

Four more, one per feature added since the phase-3 batch: the sixteen-point ADV
envelopes twice (as a gate and as a shape), the macros, and the filter morph.
Rendered through the JUCE layer at note 45, five seconds, note held for three:

| preset | rms | peak | what it shows |
|---|---|---|---|
| **Sixteen-step gate -- one bar, drawn** | 0.135 | 0.847 | ADV 1 at sixteen points, all 1/16, looped and snapped, straight onto Level. Per-1/16 RMS across the bar: `0.27 0.16 0.17 0.12 0.17 0.14 0.04 0.12 0.25 0.18 0.17 0.14 0.25 0.23 0.07 0.11` -- sixteen distinct steps, and the second bar repeats them |
| **Bar riser -- sixteen legs of climb** | 0.124 | 0.411 | The same ceiling drawn as a monotone climb into cutoff, morph and drive at once. Brightness per 1/8 across the bar: `0.043 0.052 0.066 0.067 0.065 0.073 0.115 0.125` |
| **Two-hand macro -- aggression and size** | 0.214 | 0.812 | MACRO 1 to four destinations, MACRO 2 to four more across both matrices. Macro 1 from 0 to 1 takes brightness `0.027 → 0.145`; macro 2 takes RMS `0.165 → 0.208`. Peak across the whole 5 × 5 grid of the two knobs: **0.934** |
| **Morph wah -- the filter type is the LFO** | 0.232 | 0.744 | A 1-bar triangle LFO on filter morph at 0.85 depth. Brightness per 1/4: `0.027 0.048 0.041 0.024 0.026 0.036` -- one cycle a bar, and the fifth quarter repeats the first |

**Measuring them found four older presets above full scale on a single note**,
which on a sixteen-voice instrument means no headroom at all for a chord. Fixed
in the same pass, by trimming Output and nothing else:

| preset | was | peak | now | peak |
|---|---|---|---|---|
| Reese -- the classic | -9 dB | 1.132 | -12 dB | 0.801 |
| Clockwork wobble | -4 dB | 1.089 | -7 dB | 0.771 |
| Morphing pluck | -4 dB | 1.153 | -7 dB | 0.817 |
| Scale drone | -6 dB | **1.949** | -14 dB | 0.776 |

Scale drone is the one worth naming: a comb at 88% feedback locked to the
tuning is a resonator being fed its own notes, so it builds, and 1.949 is nearly
+6 dB over. It shipped that way in the phase-4 close-out and no test could have
caught it -- a preset's level is not a claim any test makes, which is why the
rule is now written down beside the table in `PluginProcessor.cpp`.

**Init is deliberately untouched** at 1.065. It is the parameter defaults by
definition, so trimming it would change the plugin's neutral output gain for
every new instance rather than edit a preset. One saw at unity is that loud.

### DICEROLL

A seventh tab, rainbow, and **RANDOMIZE** on it: every unlocked control to a
uniform random value across its whole range. Both extremes, no restraint. Most
rolls are unusable — the point is finding the one in twenty that is not, faster
than three hundred controls can be turned by hand.

Around it, four things that make that usable rather than merely exciting:

| | |
|---|---|
| **PREV / NEXT** | Steps through the last 32 rolls. The oldest entry is the patch you had before the first roll, so the dice can always be undone completely. Hand edits made between two rolls are recorded too, because what gets stored is the state going *into* a roll, whatever put it there. Rolling from a step back replaces everything ahead of it, as an undo history does. Session-only: it is a minute's worth of undo, not something worth writing 41 kB of snapshots into every project file for |
| **LOCK**, per section | OSC · FILTER · ENV · MOD · MANGLE · PLAY · OUTPUT. A locked section is held still. **OUTPUT is locked by default**, and that is not caution for its own sake — `output` runs to +12 dB, an instrument has no limiter after it, and an unlocked roll on headphones is a hazard |
| **SOLO**, per section | Rolls *only* that section, by locking the other six in one press. Pressing it again unlocks everything, so an exclusive target costs one click each way rather than six. A button rather than a modifier on the lock, deliberately: a modifier is a thing you have to know about |
| **AMOUNT / SPREAD** | AMOUNT is how far each control moves — 100% is the full-strength roll, 15% is a variation on the sound you have. SPREAD is how *many* of them move at all. They do not sound alike: three hundred controls nudged 10% is a patch that drifts, five controls thrown anywhere is a patch that surprises you |

The locks and the two strengths are **state, not parameters** — a lock that was
a parameter would be randomised by the very button it restrains, and reset by
every preset you load. They are saved with the project; the roll history is not.

Which section a control belongs to is read from its parameter id
(`plugins/Sonitus/Dsp/DiceSections.hpp`), and a test holds that function to the
whole 324-id list: **every id lands in exactly one section and none in
`unknown`**. An id that matched nothing would be treated as locked — the safe
direction — and the sixteen-point ADV envelopes are why it is a function rather
than a table: they would have needed 48 new rows in one, and forgetting them
would have silently made an "envelopes locked" roll change the envelopes.

Rolling is uniform on each parameter's **normalised** range, which is what "0
to MAX" has to mean for a control whose own range is skewed: a skewed knob
spends most of its travel at the fine end, so rolling in its own units would
land in the coarse end nearly every time. Choices and switches come out
uniform over their entries for the same reason.

**Nothing is excluded, and there is nothing to exclude** — which is worth
saying because it is a property rather than an oversight:

- Sonitus is an instrument, so it has **no bypass parameter** to silence.
- The **tuning is unreachable**. The scale and the concert pitch were
  deliberately never made parameters, because a scale is a rig decision that
  presets must not reset — so the same property that keeps presets off them
  keeps the dice off them too. No list to maintain, nothing to forget.

Two things to know before using it:

- **There is no undo.** COPY the patch to the other A/B slot in the header
  first if it is worth keeping.
- **A roll can land the output at +12 dB with everything else at maximum.**
  Sonitus has no safety limiter, so that reaches the DAW as sustained
  full-scale clipping. Bounded at 0 dBFS by the interface, but sudden. Mind the
  monitors.

Later revisions are planned to randomise by a percentage of the current values,
and to roll one section at a time — envelopes only, oscillators only.

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
