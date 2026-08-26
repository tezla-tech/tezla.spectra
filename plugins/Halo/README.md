# Tezla Halo

A harmonic exciter and bass enhancer. Code `Tzha`, version 0.2.0.

It adds harmonics that were not in the source, and — unlike the structure
every exciter since 1979 has used — it does not also add a copy of the source
while doing it.

Two generators sit behind one panel and answer that differently:

- **Curve** picks a shape and takes the series that falls out of it, with the
  linear term removed analytically. Its wet path measures **−271 dB** at the
  fundamental.
- **Chebyshev** picks the series and derives the shape. Ask for the 5th and you
  get the 5th: every other component measures **123 dB** below it.

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
| **Generator** | **Curve** or **Chebyshev** — two different instruments behind one panel. See below. Drive, Colour and Track grey out in Chebyshev mode, because it replaces all three. |
| **Mode** | Which side of Focus gets excited. **Above** is the classic exciter. **Below** is a bass enhancer: harmonics of a 40 Hz sub land at 80 and 120 Hz, where a phone or a laptop can actually reproduce them, and the ear supplies the fundamental it cannot hear. |
| **Focus** | Where the band starts or ends. A 24 dB/octave Linkwitz-Riley split, so it is decisive rather than a tilt. |
| **Drive** | How hard the band is pushed into the generator. Sets the *recipe* — how far up the harmonic series the energy goes — not the level. At 0 the generator is exactly the zero function. |
| **Colour** | Odd (third, fifth: edge and bite) through even (second, fourth: octave shimmer, and the surgically clean half). Level-matched to 1.4 dB across the whole Drive range. |
| **Amount** | How much of the generated harmonics get added. At the bottom of its travel it reads Off and the output is the input, **bit for bit**. |
| **Track** | How much the harmonics follow the source level. At 0 they behave like a real nonlinearity; at 100% the harmonic-to-source ratio holds constant at every level. |
| **Punch** | Transient discrimination — harmonics arrive on the hits and leave the sustain alone. What stops an exciter turning a jungle break into a wash of cymbals. At 0 it is bit-exact. |
| **Width** | Stereo width of **the harmonics alone**, from Mono through Normal to 200 %. The source keeps its own image whatever this does. At Normal it is bit-exact. |
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

### CHEBYSHEV

Precision mode. Chebyshev harmonic synthesis, Le Brun, *Digital Waveshaping
Synthesis*, JAES 27(4), 1979.

| Control | What it does |
|---|---|
| **H2 … H8** | Level of each harmonic, Off to +20 dB. At Index = Exact, asking for the 5th gives you the 5th and nothing else — measured through the whole plugin, every other component sits **123 dB** below it. |
| **Index** | Le Brun's waveshaping index. **Exact** (1.0) is the point the mode is built around. Below it the harmonics blend into one another and the recipe breathes with the material; Off is a true zero. Above it the input clamps and this stops being synthesis. |
| **Tilt** | One knob across all seven levels, pivoting on the 5th. 4 dB per step at full deflection, 24 dB end to end. Flat multiplies by exactly one. |

Every other exciter, Halo's own Curve mode included, picks a *shape* and accepts
whatever series falls out of it. This picks the series and derives the shape.
Because `T_n(cos t) = cos(n t)`, a unit-amplitude sine through the nth Chebyshev
polynomial is exactly the nth harmonic — so a weighted sum of them is a recipe
written in numbers.

The precondition is unit amplitude, and Halo already had it: Track at 100%
divides the band by `sqrt(2·meanSquare)`, which for a sine is its amplitude.
Track is pinned there in this mode for that reason.

---

### Why Width is on the harmonics only

Every conventional exciter mixes a filtered copy of the source back in, so
widening its wet path widens the source with it: the top end of the mix drifts
out of the centre, the bass follows the treble, and a mono fold-down loses more
than the effect. That is why the control is usually absent, or offered as a
whole-plugin "stereo base" that has to be used sparingly.

Halo's wet path has no fundamental in it, so there is nothing of the source to
widen — the side signal being scaled is made entirely of harmonics that were not
there before. The sub stays exactly where it was, mono, at any Width setting,
because the dry path is untouched by this control and by everything else on the
page.

It is written as a departure from unity rather than as a mid/side rebuild:

```
side = (L - R) / 2
L' = L + (w - 1) * side        w = 1 multiplies the side by exactly zero
R' = R - (w - 1) * side
```

An encode/decode round trip rounds twice and lands within an LSB of the input,
which is not the same as the input. This form adds `0.0 * side` at Normal, so
Width joins Amount and Punch on the list of controls whose neutral setting is
bit-exact rather than merely inaudible.

---

## MOD — three LFOs and a level follower

Everything above is static once set, and on this material the interesting
settings are the ones that move: harmonics that bloom with the note, a Focus
that sweeps on the bar, an octave that comes and goes on the half-bar. The MOD
strip under the tabs adds four sources and eight assignments.

It is closed by default and costs 24 px there, which still shows all four
sources and what they are set to. Open it takes 120 px and the window grows to
find it rather than taking the room out of the spectrum.

### The sources

**LFO 1–3.** Sine, triangle, saw up, saw down, square, sample & hold and smooth
random. Each has a rate, a phase offset, a smoothing amount, and a SYNC switch
with a note division from 8 bars down to 1/32, triplets and dotted values
included.

Synced, the phase is **taken from the host's song position every block rather
than accumulated from a clock**. There is nothing to drift, so bar 33 is the
same phase as bar 1, a loop repeats identically, and a bounce matches what you
heard. The random waveforms are hashed from *which* cycle it is rather than
drawn in sequence, so they repeat with the loop too. Without a transport — a
standalone, or a stopped host — an LFO free-runs at its rate setting instead of
freezing.

**ENV — the level follower.** Attack, release and a sensitivity that says what
input level reads as full travel, over a 40 dB range.

This is a level detector, not an envelope generator. There is **no MIDI and no
note trigger anywhere in Halo**; the audio is what moves it, exactly as it is
for a compressor's detector or an auto-wah. Halo already contained one — Punch
is a fast follower minus a slow one, hard-wired to a single destination. This
unhardwires it.

### Assigning

Click a source to arm it. Every knob that can be modulated grows a ring in that
source's colour; drag one up or down to set how far the source moves that
control. Up is positive, down inverts it — a follower at negative depth into
Amount is a compressor made of harmonics rather than of gain. Shift is a fine
drag. Double-clicking a ring, or dragging it back to zero, gives the slot back.

With nothing armed, a knob that something is pointed at keeps a thin ring
showing how far it can move, in the colour of whichever source owns it, with a
dot riding it where modulation has the control right now. A glance at the page
then says what is moving without arming anything.

One ring at a time is deliberate. The CHEBYSHEV page puts nine controls in seven
columns; four concentric rings on a 100 px knob would be a texture rather than a
reading.

### What it costs when you are not using it

Nothing, and that is measured rather than intended:

| Check | Result |
|---|---|
| Nothing assigned, against a build from before modulation existed | **byte-identical**, 96 000 samples |
| A slot assigned with its depth at exactly 0 | **byte-identical** to the same slot at Off |
| Modulation running, host block size 512 against 64 | **byte-identical** |

The first two hold because the matrix reports itself inactive until a slot has
both a source and a non-zero depth, and the whole per-chunk path is skipped when
it is — one block, one parameter push, the same samples. A depth of exactly zero
multiplies by exactly zero, which puts modulation on the same list as Amount,
Punch and Width: neutral means bit-exact, not nearly.

Modulation never writes to a parameter. The parameter is the base value and a
source adds an offset downstream; the knob shows where you set it and the ring
shows where modulation has moved it. Writing back would fight host automation
and feed into recorded automation lanes.

### Some things to try

- **ENV into H3 and H5** on the CHEBYSHEV page — grit that blooms on the attack
  of a reese note and settles back, instead of sitting on it constantly.
- **ENV into Amount at negative depth** — harmonics that back off when you play
  hard.
- **LFO 1 into Focus, synced to 1 bar, saw up** — a sweep that lands on the grid.
- **LFO 2 into H2 in BELOW mode, synced to 1/2** — an octave that comes and goes
  on the half-bar without touching the sub underneath it.

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

### Chebyshev: what you asked for, and what else arrived

400 Hz tone, one harmonic requested at a time, harmonics soloed, Index Exact.
Absolute dBFS — the requested harmonic is at −6.4 in every row.

| Asked for | DC | H1 | worst other harmonic |
|---|---|---|---|
| H2 | −316.6 | −266.6 | −149.1 |
| H3 | −298.7 | −137.0 | −143.1 |
| H5 | −287.6 | −147.7 | −133.5 |
| H8 | −305.5 | −235.8 | −129.9 |

So the worst contaminant anywhere is **123 dB** below the harmonic that was
asked for, and within 0.2 dB of that at 44.1, 48, 96 and 192 kHz.

Sweep debris — a 1 k → 18 k sweep, worst inharmonic below 900 Hz — is **−92.6 to
−98.5 dBFS** with Auto oversampling at every session rate.

The fundamental holds at −128 to −147 dB relative to the loudest harmonic at
*every* Index setting, not only at the exact point. That is not free: away from
Index 1 the odd polynomials put energy back at the fundamental by construction —
`T_3(a·cos t) = 3a(a²−1)·cos t + a³·cos 3t`, so at Index 0.6 the fundamental
would be five times the third harmonic. It is cancelled, the same way the Curve
generator cancels its describing function.

Above Index 1 the input clamps, the composite stops being a polynomial, and it
aliases like the distortion it has become — around −60 dB on a bass band. That
is the crazy end and it is meant to sound like one.

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
| Width at Normal | the side is multiplied by exactly zero; `x + 0.0 * side == x` over 4000 values, where the mid/side rebuild differs on most of them |
| Width at Mono, stereo harmonics | side energy in the wet path to zero |
| Width at any setting, mono source | output still identical in both channels |
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
8. **ADAA was wrong for the Chebyshev generator** — the opposite of the answer
   everywhere else here. A degree-n polynomial on a band-limited signal is
   already exactly band-limited, so there is nothing to remove; measured, ADAA
   changed the audible aliasing by 0.2 dB and moved the fundamental from −292 dB
   to −42 dB, because its difference quotient averages the curve over a segment
   in *x* and the map from time to x is nonlinear. The antiderivative it would
   need is not written at all rather than left to be reached for.
9. **The band envelope's own ripple was leaking a fundamental.** Chebyshev mode
   divides by that envelope, so the residual ripple at twice the tone frequency
   amplitude-modulated the normalised band and put energy straight back where
   the mode claims there is none: −64 dB at 100 Hz, which is exactly the
   material it is for. Sweeping the tone found the cause outright — the leak
   tracked at 11.8 to 12.0 dB per octave, which is two poles, and the envelope
   is two cascaded poles. A third pole on the Chebyshev path alone, one
   multiply-add, bought 37 to 55 dB.
10. **Three tests passed with the feature they covered deleted.** The
    click test needed the bar set by the signal itself *and* the switch swept
    across a cycle; the aliasing test cannot see a missing band limit at a
    96 kHz internal Nyquist, so a second test forces oversampling off where it
    can; and a DC test read a non-zero mean from a window holding a fraction of
    a cycle, which looks exactly like the fault it was testing for.

---

## Roadmap

Halo stays what it is: a focused exciter and bass enhancer with one band and a
side. The multiband enhancer is **not** a phase 2 of this plugin -- it is a
bigger plugin of its own, with four bands, per-band width and considerably more
signal complexity and CPU than belongs in a tool you reach for on a single
channel. It is reserved in the registry as `Prism`.

What might still land here:

**Precision mode is built** — see CHEBYSHEV above. It went to harmonics 2 to 8
rather than the 2 to 5 originally sketched, because the limit turns out to
depend on the band rather than on the method: `T_n` of a band topping out at B
reaches `n·B`, so at a ~96 kHz internal Nyquist a 120 Hz bass band allows
harmonic 800 and a full-bandwidth treble band allows about 4. High-order
synthesis is nearly free on the material this plugin exists for.

**Modulation is built** — see MOD above. Three LFOs and a level follower, eight
assignments, and no MIDI: a note-triggered ADSR would mean adding MIDI input to
a bus effect, which is deliberately not in scope.

**Known duplication, now resolved.** The editor's meter, note and page
components were Halo's own copies of Emberdrive's. The header, palette, A/B,
spectrum display, MOD strip and assignment rings now live in `shared/tezla-ui`
and compile into every plugin target; the remaining page and meter classes
should follow when a third plugin needs them.

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

Input and output drawn over each other, and **the gap between them shaded** —
because that gap is the plugin. Everything Halo does shows up as the output
standing above the input, so filling it draws the effect itself rather than
leaving it to be worked out from the distance between two lines.

Three things are marked, and each is only drawn when it is doing something:

- **Focus**, as a solid line in the accent colour, with the side being excited
  shaded. **Drag it.** The line is the control: click anywhere on the graph and
  Focus goes there, which is faster than reading a frequency off the axis and
  then finding the knob that sets it. The pointer thickens the line, and a
  reading in Hz appears while the drag is live.
- **FLOOR** and **CEIL**, dashed and in the harmonics colour, showing where the
  generated content is allowed to live. Dashed rather than solid so they cannot
  be mistaken for Focus: Focus decides what gets excited, these two decide where
  the result may land, and they are different kinds of boundary.

The whole drag is wrapped in one host gesture, so a DAW recording automation
writes a single move rather than the several hundred separate jumps a
per-pixel callback would produce. The frequency reported for a pixel is the
inverse of where the display draws a marker, which is what makes the line land
under the pointer instead of near it — measured at **0 px of error** across the
full width by `tezla-ui-preview focus-drag`, which drives the gesture through
the component and checks the round trip.

In Chebyshev mode the display earns its keep twice over: the harmonics you asked
for stand up out of the input curve exactly where the numbers say they will, so
the recipe and the picture are the same thing.

The analysis is framework-free and lives in `shared/tezla-dsp`, so the same code
can drive a standalone analyser later without a GUI framework attached.

`tezla-ui-preview` renders the display offscreen against synthetic audio. This
repository is developed in a container with no sound card, so a plugin
standalone only ever draws a flat line at the floor — enough to check a layout
and useless for checking what the picture says. Rendering it with a known signal
found three faults the empty display had hidden: the axis labels were printed on
top of the trace, the input curve was completely invisible because the output
was drawn over it, and the CEIL marker's label printed straight through the word
OUT. It found a fourth when the drag readout was added, which shared the top row
with the IN/OUT legend and printed through it anywhere Focus sat between roughly
4 and 10 kHz. The legend now steps aside for the duration of a drag.
