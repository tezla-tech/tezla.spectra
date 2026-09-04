<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# What's new — the controls added recently, how to use them, and where they came from

A player's guide rather than a changelog. Each plugin's own README carries the
measurements and the reasoning; this says **what the control is for, how to get
a sound out of it, and what tradition it comes out of** — because most of these
are old ideas, and knowing what people did with them in 1978 or 1985 is usually
faster than turning the knob at random.

Every control here defaults to neutral. A project saved before any of them
existed reopens sounding identical — that is a rule of the house
([`../CLAUDE.md`](../CLAUDE.md) §7) and it is verified by rendering every
existing preset and comparing bit for bit, not by intending it.

**On the history below:** it describes *techniques and eras* — what studios and
composers actually did, and which academic papers established the method. It
names no product and copies nothing from one. The plugins here are built from
physics and published papers ([`DSP-REFERENCES.md`](DSP-REFERENCES.md)), and
the sonic targets are things to aim at by ear, never things to reverse engineer
([`../CLAUDE.md`](../CLAUDE.md) §2.1).

---

## Sonitus — the synthesiser

### Stack (per oscillator)

**What it is.** The unison copies, placed at musical intervals instead of a few
cents apart. *Detune* is the original behaviour and is bit-exact. The rest put
the copies at octaves, fifths, tritones, minor seconds, diminished stacks, or on
the degrees of whatever tuning is loaded — and the detune still rides on top, so
a stacked chord can still churn.

**How to use it.** Turn Unison up; that is now how many notes you get, not how
thick one note is. Exactly one copy always sits on the played pitch, so the
instrument never detunes itself as you add copies. *Octaves* is an organ
registration. *Fifths* is hollow and enormous under a saw. *Tritones* and
*Diminished* are symmetric — they have no root to resolve to, which is why they
sit under tension and never release it. *Cluster* is minor seconds and is a
texture rather than a chord.

**Where it comes from.** Fixed-interval doubling is the drawbar organ's whole
idea, and it moved into synthesisers as the "octave/fifth" oscillator switch on
1970s monosynths. The symmetric-interval version is older still and comes from
composition rather than engineering: the whole-tone and octatonic writing of the
late nineteenth century onwards used intervals that divide the octave evenly
precisely because they refuse to establish a key. Film scoring in the 1970s and
80s took that straight over — a tritone drone under a scene is doing the same
job it did in an orchestral score, with a VCO instead of a section.

### Scale mode and Step

**What it is.** Copies placed on the *keys* of the loaded tuning rather than at
a fixed interval. Step is how many keys apart.

**How to use it.** In twelve-tone equal temperament a step of 1 is a chromatic
cluster, 4 is stacked major thirds, 7 is stacked fifths. Under a keyboard map it
is a scale degree, so 2 becomes "stacked thirds *in that scale*" and the chord
changes shape as you move up the keyboard, exactly as it would if you played it.
In Bohlen–Pierce — which repeats at 3/1 and has no octave at all — a step is a
degree of a scale with nothing to resolve to.

**Where it comes from.** Just-intonation and microtonal keyboard work; the
Scala scale format the tuning page reads has been the lingua franca for that
since the 1990s. Bohlen–Pierce is from the 1970s (Heinz Bohlen, and
independently Kees van Prooijen and John Pierce) and was designed around the
3/1 "tritave" instead of the octave.

### Shepard mode, Speed, Sync and Division

**What it is.** The endless rise. Copies an octave apart, all sliding in the
same direction, each fading in at the bottom of its octave as another fades out
at the top — so the pitch climbs forever and never arrives.

**How to use it.** Set Stack to Shepard on an oscillator, then Speed in octaves
per second: 0.02 is a fifty-second riser, 4 is a siren. The sign is the
direction. **Zero is a real setting**, not the bottom of the range — it gives a
held octave stack with its ends rolled off, which is an organ registration of
its own. Sync locks one octave of climb to a note division, so the riser lands
with the bar. It wants at least three copies; below that the level beats at the
glide rate rather than holding steady.

Speed is one control for the whole instrument on purpose: a held chord has to
climb as one thing, and per-voice phases smear a rise into a wash.

**Where it comes from.** Roger Shepard described the tone in 1964 (*Journal of
the Acoustical Society of America*) as a demonstration that pitch perception has
a circular component — the *chroma* of a note is separable from its *height*.
Jean-Claude Risset turned the discrete version into a continuous glissando in
1969 and used it in his own tape works; the same trick applied to tempo is the
"Risset rhythm". Film and television took it up as the sound of dread that never
resolves, and it is one of the defining textures of 1970s and 80s horror and
science-fiction scoring — a rise under a scene that the audience keeps waiting
to peak, which it never does. It is also why it works so well as a build in
drum and bass: the drop lands as *release* from a tension that had no other exit.

### Origin — Centre / Up / Down (new)

**What it is.** Which side of the played note the stack builds on. Centre is
what it always did. Up puts every copy above the note; Down puts them all below.

**How to use it.** This moves the stack's *weight*, never its tuning — exactly
one copy stays on the played note in every setting. **Up** is the one to reach
for on anything that has to live over a sub: the low end stays exactly one note
wide, so the sub-bass has a lane nothing is competing for, and the chord opens
above it. **Down** thickens underneath, which on a lead is the difference
between bright and enormous. It is greyed in Detune mode, which is symmetric by
definition and so has no side to build on.

**Where it comes from.** Registration, in the organ sense — the choice of which
ranks to draw and whether they sit above or below the played pitch. Upward
stacking is the norm there for the same reason it is here: a rank three octaves
below middle A is 13.75 Hz, which is a rattle rather than a pitch.

### Shear (new)

**What it is.** How far oscillator B's Shepard climb runs against A's. At 0 the
two share one phase exactly (what shipped). At 0.5 B stands still while A
climbs. At 1 B falls at exactly the rate A rises.

**How to use it.** Both oscillators on Shepard, both audible, then turn it up.
What you hear is the two stacks passing *through* each other — and because both
are endless, they never finish passing. Partials converge, beat, cross and
separate, and the beat rate comes from the distance between the two speeds
rather than from any tuning. It is the way to make a riser that is
unmistakably two things rather than one thick one. Pair it with Origin Down on
B and the falling half is the one you feel while the rising half is the one you
hear.

**Where it comes from.** Two Shepard glissandi in opposite directions is the
counterpoint version of the illusion, and phase music is the wider tradition:
two identical patterns running at slightly different rates against each other,
which Steve Reich built pieces out of from the mid-1960s. Here the "patterns"
are endless, so the phasing has no cycle to come back into alignment on.

### Phase pan (new)

**What it is.** Pans each Shepard copy by *where it is in its climb* rather than
by its rank.

**How to use it.** Turn it on for any Shepard patch with Spread up — and the
result is the opposite of what the description suggests, which is the reason to
use it. With it **off**, each copy sits at a fixed place in the field while its
pitch climbs through it, so position and pitch are out of step and the picture
churns as the tones move. With it **on**, position and pitch move together, and
because the copies are evenly spread around the cycle one always arrives where
another leaves — so the image *stops* moving and becomes a fixed fan across the
field, low at one side and high at the other.

Measured over five seconds of a slow rise, the 60–130 Hz band's balance swings
−0.16 → −0.52 → +0.50 with it off, and reads −0.182 three times with it on.

So: off is a rise that wanders around the stereo field; on is a rise that climbs
through a stationary picture. The second is far easier to sit under a mix, and
much more like the illusion the pitch is already performing. Spread still sets
how wide the fan is, so at Spread 0 it does nothing audible.

**Where it comes from.** Spectral spatialisation — mapping frequency to position
so that a sound has a fixed shape in the field rather than a fixed place. It is
standard practice in electroacoustic composition and in the "spectral" school
(Grisey, Murail and the IRCAM circle from the 1970s on), where the partials of a
sound are treated as separate objects to be placed. Here it falls out of the
Shepard construction for free.

### Retrigger (new)

**What it is.** Whether a note starts its own Shepard climb from the bottom, or
joins the one already running.

**How to use it.** Off is the original behaviour and the right default: one
glide clock for the instrument, so a chord climbs as a single gesture. Turn it
on when you want a riser to *land* somewhere — each note then begins at the
bottom of the octave and counts from its own start, so you can place a rise
against a bar line instead of catching the clock wherever it happens to be.

Notes already sounding are never disturbed: the clock itself does not reset,
each voice just remembers where it came in. So a chord struck together still
climbs together, and a note added later climbs its own line through the others.

**Where it comes from.** The same question every free-running modulator raises —
does a new note sync to it or start its own? Analogue sequencers and LFOs have
had a "retrigger / free" switch since the 1970s for exactly this reason, and
this is that switch applied to a glide instead of a waveform.

### Tract

**What it is.** One ratio scaling all three of the vowel filter's formants at
once, which is the physics of a longer or shorter vocal tract. 1.0 is neutral
and bit-exact.

**How to use it.** Below 1 makes the throat *bigger* and every formant lower;
above 1 makes it smaller and higher. The vowel stays the same vowel — it is the
same mouth shape on a different-sized head, which is why a value near 0.6 reads
as a very large creature rather than as a different vowel. Bandwidths scale with
it, so the Q of each formant is preserved and the resonances do not smear as
they move.

**Where it comes from.** Straight acoustics: a uniform tube closed at one end
resonates at `F_n = (2n−1)c / 4L`, so every formant scales with 1/L. A 17.5 cm
adult tract puts the first resonance near 500 Hz. Speech synthesis has used this
since the 1960s; horror sound design has used it since it became possible to
pitch-shift formants independently of pitch, which is roughly the arrival of
digital processing in the early 1980s — and the "big creature" sound is exactly
this: a normal vowel with the tract length wrong for it.

### Sag

**What it is.** One slow instability shared common-mode by every voice — a
random walk that nudges pitch, cutoff and level together.

**How to use it.** A little (10–30%) is a machine with a power supply that is
not quite keeping up, which is most of why old gear sounds alive. A lot is a
machine in trouble. The rate is the period of the walk in seconds; slow (20–60 s)
reads as instability, fast reads as vibrato. It is shared across voices rather
than per voice on purpose: a whole instrument sagging together is a power
supply, while independent per-voice wander is just chorus.

**Where it comes from.** Power-supply sag and thermal drift in analogue
polysynths — the reason a 1978 instrument never plays a chord the same way
twice, and the reason its owners loved it and its manufacturers did not. The
walk's target distribution is deliberately skewed so the machine occasionally
lurches rather than always drifting smoothly, which is what a real supply under
a loud chord does.

### Sing — the filter oscillates on its own (new)

**What it is.** The filter driven past its own damping until it becomes an
oscillator, at the Cutoff frequency. The thing a real analogue filter does when
you turn the resonance past the end — given its own control so that Resonance
still means what it meant.

**How to use it.** Turn **Resonance up first**, then Sing. Above the point where
it cancels the damping, the filter is a sine oscillator, and everything that
already pointed at Cutoff now points at a *pitch*: Key track makes it playable
across the keyboard, an envelope on the cutoff makes it howl and settle, the FM
knob makes it scream. The oscillators do not have to be doing anything at all —
with both levels at zero the filter is the only sound in the patch, which is a
second instrument hiding inside this one (preset *The Filter Sings*). Used
gently under the oscillators instead, it puts a resonant note inside the chord
that drifts against the played note as you move up the keyboard (preset
*Overtone*).

**The one thing to know:** where Sing bites depends on Resonance, because it has
to cancel the damping that is there before it can go past it. At full Resonance
it sings in the first percent of the travel; at Resonance 0 it takes almost all
of it. That is the physics, not a taper worth fixing.

It is not a stuck note — the filter sits before the amplifier, so the envelope
silences it and the voice retires like any other. The level is fixed and the
pitch is the cutoff at every session rate: measured, it settles at −1.9 dBFS to
six figures and lands on the corner frequency to 0.0000% at 44.1, 48, 96 and
192 kHz. It costs one square root per sample **only while singing**.

**Where it comes from.** Self-oscillation is as old as the resonant filter: any
filter with enough positive feedback becomes an oscillator, and players
discovered this the moment resonance knobs went past about 90%. It became a
technique rather than an accident in the 1960s and 70s — a self-oscillating
filter is a free sine oscillator that tracks the keyboard, which is why it was
the standard way to get a pure tone out of an instrument whose VCOs were all
busy. It is also the classic laser/whistle/wind sound of that era's sound
effects work. On the theory side the modern reference is Zavalishin's *The Art
of VA Filter Design*, which reaches it through an "antisaturator" in parallel
with the damping; this implementation gets to the same place from the other end
and the README says why.

### Render quality

**What it is.** A separate oversampling factor used only while the host is
bouncing offline. Default is *Same as live*, which changes nothing.

**How to use it.** Set it to ×8 for the bounce and leave the live setting where
your CPU can afford it. A render at a factor is the live graph at that factor,
bit for bit; latency is re-declared, so the host's delay compensation still
lines up.

**Where it comes from.** The oldest trick in offline rendering: quality settings
you cannot afford in real time but can afford when time is not the constraint.

### Drift (per oscillator, and on the filter)

**What it is.** Slow random wander in cents — per copy on the oscillators, and
paired cutoff-and-resonance wander on the filter (the "voice card's
temperature").

**How to use it.** Up to about 40 cents is a warm analogue polysynth. Past that
it is a creative control. Crucially it **carries on between notes** rather than
restarting with them, so two presses of the same key are two different notes.
The knobs wear a pastel rainbow ring, which is the house mark for a control
whose job is to add colour.

**Where it comes from.** Component tolerance and thermal drift again, but at the
level of the individual oscillator and the individual voice card. Separate VCOs
wander *against* each other, and that relative motion is what the ear actually
hears as "analogue" — which is why per-copy drift sounds like an instrument and
one global drift sounds like a tuning error.

### Conflux — the converging riser (a preset, not a control)

**What it is.** Preset 45. The sound that opened films in the eighties: a mass
of voices that begins as a tight cluster, wanders slowly, then opens out and
arrives all at once on an enormous chord that simply sits there.

**How to use it.** Play a **wide** chord — the wider the better, three or four
octaves — and **hold it**. It takes about twelve seconds to open, and it is not
finished until it has. The arrival is the whole point, so give it the time.

**How it is made.** The width is *modulated detune*, not a fixed stack. Both
banks start at 0 cents, so a note begins as a genuine unison — one pitch,
fourteen copies of it. Both mod envelopes then run a long attack to a held
sustain, opening `detune A` to 660 cents and `detune B` to 840 cents over
eleven and thirteen seconds. Drift is high on both banks and keeps running
after the arrival, which is what stops the held chord being static.

**Where it comes from, and what one instance cannot do.** The method was
published by its author as a sketch: thirty voices at random pitches in a narrow
band between **200 and 400 Hz**, each moving slowly and randomly, then all
proceeding direct to their target note — three slightly detuned voices per note,
and two per note in the bass, over a chord spread across several octaves. It is
narrow-to-wide, not wide-to-narrow; what reads as "converging" is that every
voice stops moving at the same instant.

The part a single instance genuinely cannot reproduce is that **a voice's
starting pitch is unrelated to its target**: everything begins near 300 Hz
whether it ends in the bass or three octaves up. That needs a displacement
proportional to the played note, faded out over time — a *product* of key
tracking and an envelope. Sonitus's matrix adds sources, it does not multiply
them, so there is no route to it from one instance.

**The faithful route is several instances**, which is worth doing once:

1. Put three or four Sonitus instances on their own tracks — one per register
   (bass, low-mid, high-mid, top).
2. Load *Conflux* on each, and on each one add a route from **Mod env 1 → pitch**
   with a long decay to sustain 0 (so it starts displaced and lands).
3. Set that route's **depth per instance** so the instance's start pitch lands
   in the 200–400 Hz band: a large negative depth on the top instance so it
   starts far below its notes, near zero in the middle, a large positive depth
   on the bass instance so it starts far above.
4. Give every instance the *same* decay time, so they all arrive together, and
   hold each instance's own chord tones.

That reproduces the structure exactly: everything starts in one band, everything
ends where it belongs, and the arrival is simultaneous.

### The NOTES page (new)

**What it is.** An eighth tab. It shows what the loaded preset is, how to play
it, what is worth automating, and where the sound comes from — and it follows
the preset, so changing program changes the page.

**How to use it.** Open it when a preset does something you like and you want to
know *why*, or when you have loaded one and are not sure what to hold down. Each
one says what to reach for first and what is worth an automation lane.

Below the preset's own notes is a **glossary** of the terms the panel uses
without stopping to explain them — Shepard tone, shear, phase pan, retrigger,
stack, origin, kargyraa, formant, tract, sag, sing, drift, reese, ADAA,
oversampling, render quality, just intonation, Bohlen–Pierce, phase modulation
and hard sync. This document and that page overlap on purpose: this one is for
reading away from the rig, that one is for reading while the sound is playing.

---

## Ictus — the drum synthesiser

### Bass mode, Gate and Release on the kick

**What it is.** Every key plays Kick 1 through the loaded tuning, with a gate
and a release so it holds as long as the key does.

**How to use it.** A synthesised kick is a pitched sine with an envelope, so it
is already a sub-bass instrument — this makes that explicit. Play the kick's
body as a bassline and layer the pattern's actual kicks on top.

**Where it comes from.** The oldest sub-bass technique in the genre: an 808's
kick, tuned and held, played as a bassline. It is how the sub in a great deal of
1980s hip-hop and, later, drum and bass was actually made — the kick *is* the
bass, and the decay is the note length.

### Note snap on Tune

**What it is.** Snaps the kick's and snare's Tune to the nearest degree of the
loaded tuning.

**How to use it.** Turn it on and the drum lands in key rather than near it. The
NOTE lamp shows what it snapped to. Off, Tune is continuous, which is what you
want when the drum's pitch is a texture rather than a note.

**Where it comes from.** Tuning drums to the track is standard studio practice
and predates synthesis entirely — a kit is tuned to the key of the song, and a
kick whose fundamental clashes with the bass is the most common reason a mix
will not sit down.

### Ghost snare with LINK

**What it is.** A second snare pad on its own key that can follow the main
snare's drum identity — same tuning, spread, tone and snappiness — while keeping
its own level, decays and rattle.

**How to use it.** It is the *same drum, different stroke*: put the ghost hits
on their own key and program them quietly between the backbeats. Linked, the two
stay the same instrument when you retune it; unlinked, the ghost becomes a
second drum.

**Where it comes from.** Ghost notes are the whole vocabulary of funk and jungle
drumming — the quiet strokes between the accented ones, which is what makes a
break move rather than march. A sampled break gives them to you for free; a
synthesised kit has to model the fact that a soft stroke on the *same* drum is
not the same sound quieter.

---

## Where the numbers live

Every claim above is measured, and the measurements are in the repository rather
than in this document:

- `plugins/<Name>/README.md` — the changelog entry for each control, with its
  numbers.
- `tests/` — the assertions, including the ones that were seen to fail first.
- `shared/tezla-dsp/include/tezla/dsp/*.hpp` — the derivations, at the point of
  use.
- [`DSP-REFERENCES.md`](DSP-REFERENCES.md) — every source consulted, its licence,
  and whether it was read first-hand or only through search results.

**What is not proved:** none of these controls has been heard in a DAW on macOS,
and only some of them have been heard on the Windows rig. Sonitus's Shepard mode
has (2026-09-03, and the *Slow Descent* preset is the player's own patch from
that session); the phase-6 controls above have not yet.
