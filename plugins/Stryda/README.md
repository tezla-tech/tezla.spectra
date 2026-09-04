# Stryda

**Code `Tzst` · "Tezla Stryda" · `tech.tezla.Stryda` · instrument.**

An FM synthesiser for neurofunk drum and bass: six operators in a 6×6 index
matrix with feedback on the diagonal and a noise row, where every operator
carries one **Character** control that runs continuously from classic phase
modulation to ModFM.

> **Status: F4.** The engine, the matrix, a playable panel, phase distortion,
> the formant operator, key scaling and velocity. Not yet built: the protected
> sub lane, the ratio sequencer, the vowel lane, the mangle chain, the
> modulation layer, and microtuning at the ratio. Plan and phase table:
> [`PLAN.md`](PLAN.md).

---

## What is different about it

**One knob instead of two techniques.** The operator is Lazzarini & Timoney's
extension of ModFM (JAES 58(6), 2010, Eq 19):

```
    x(t) = e^(r·k·cos(ω_m t) − r·k) · sin(ω_c t + s·k·sin(ω_m t))
```

At **Character 0** the exponential is exactly 1 and this is classic phase
modulation — the sound of the eighties six-operator workstations, bit for bit,
asserted against `sin(2π·phase)` over 48000 samples and against the closed form
`sin(ω_c t + k sin ω_m t)` to 0.000e+00.

At **Character 1** it is ModFM, whose partials are scaled by *modified* Bessel
functions. Those do not oscillate, so the partials fall away smoothly instead of
each one pumping through a null as the index rises — moving the index sounds
like opening a filter rather than like a timbre flickering. Measured across the
control, the partial-order reversals fall from **2 to 0**.

For a bass that is usually the end you want: a modulator envelope sweeping the
index does not step through Bessel zeros on the way, so a growl growls instead
of chattering. Matching the spectral centroid at the ModFM end takes **122 %
more index**, which is the paper's ~50 % claim measured by our own metric at our
own operating point, and ours is the figure quoted.

**A bandwidth readout that means something.** The panel predicts, in closed
form, where the top of the spectrum sits, and shows it against the internal
Nyquist. For one modulator on one carrier the prediction is **exact** — +0 Hz
against a rendered spectrum on 45 combinations spanning three carriers, three
ratios and three indices. For a stack it is an **upper bound**, over-estimating
by about 2× at two deep and up to 19× at three, and the panel says so rather
than pretending otherwise.

**A matrix, not an algorithm list.** Any operator can modulate any other. The
rule that makes that computable: operators run 6 → 1, so a modulator with a
*higher* number arrives instantly and a *lower* one arrives one sample later.
`4 → 2` and `2 → 4` therefore do not sound alike at the same setting, which is a
design statement rather than a bug, and the tooltip says so.

---

## Controls

**Fold** is phase distortion: it bends the phase ramp so the waveform races
through part of its cycle and crawls through the rest, which grows a leading
edge on a sine and turns it saw-like. It is a filter-sweep gesture from one
oscillator and no filter. Per Electric Druid's analysis, phase distortion *is*
phase modulation with a piecewise modulator locked to the operator's own cycle —
which is why it costs a transfer function here rather than an operator. Exactly
the identity at 0; measured, it adds upper-harmonic energy monotonically from
−300 to −10.4 dBc across its travel.

**Formant mode** turns an operator into a self-contained resonance at a settable
frequency, whatever note is played. Two carriers on adjacent harmonics,
crossfaded by the fractional part, place the peak *between* them rather than
snapping to one — the paper's phase-synchronous ModFM. That is a whole
two-operator pair collapsed into one slot, which is what makes a three-formant
vowel reachable on six operators. It needs room: about eight harmonics above the
note, and ten to be exact, because below that the resonance skirt folds through
DC and drags the centre sharp. Measured placement: 0 cents at eleven harmonics
of room, 20 at eight.

**Key scaling** is the eighties mechanism — a break point with a signed depth on
each side. Applied to a carrier you hear a volume change; applied to a modulator
you hear a timbral one, which is what makes a patch behave like an instrument
across the keyboard rather than like one sound transposed. Two octaves above the
break at depth +0.5 measures exactly 2×. Flat is bit-exactly flat.

Per operator: **Ratio** and **Fine** (its frequency as a multiple of the note),
**Character**, **Fold**, **Level**, **Pan**, **Formant** and its **Width**, a
**Mode** switch, an AHDSR — **Attack**, **Decay**, **Sustain**, **Release** —
and, on the SCALING plate, **Break**, **Below**, **Above**, **Vel level** and
**Vel index**. The envelope scales the operator's **output**, so it
scales the modulation depth of everything that operator feeds as well as its own
place in the mix. That is what FM is: the modulator's envelope *is* the timbre
envelope.

The **matrix** holds the thirty off-diagonal cells in cycles of phase deviation,
the six **feedback** cells on the diagonal, and a **noise** column — one shared
noise source into any operator's phase, which is the grit no amount of
sine-on-sine will give you.

Globally: **Oversampling** and **Render quality** in the header bar, as
everywhere else in the suite, beside **Master**; **Voices** and the **Index cap**
on the panel itself.

Oversampling has **Off / ×2 / ×4 / ×8** as well as Auto, and Off is a real
setting rather than a formality — it is the first thing to reach for if the CPU
meter climbs, and at 176.4 or 192 kHz sessions it is what Auto picks anyway. The
tooltip reads the session's actual rate and says what Auto is doing right now.

### The panel is paged

**OPERATORS · MATRIX · VOICE · SEQ · MANGLE · ADV · MOD · TUNING.** Each page
gets the whole window,
so the window only has to fit the largest page rather than the sum of them, and
each page lays its rows out from the height it is given — a small window gets
denser, not clipped. Minimum 860 × 520.

### Ratio modes, and microtuning that reaches the ratio

Every operator has a **Ratio mode**: *Free* (continuous, and bit-for-bit what
you set), *Harmonic* (the nearest simple p:q — Chowning's `N₁:N₂`), or *Scale*
(the nearest degree of the loaded tuning, octave-extended).

Scale is the one no other FM synth has, and the reason is worth a sentence.
Snapping a *frequency* to a scale is ordinary; snapping a **ratio** is not,
because a ratio is an interval rather than a pitch — so the answer is the same
at every key, and a snapped modulator puts its **entire sideband ladder** on
the loaded scale's degrees, everywhere on the keyboard. In 19-TET or a Persian
dastgah that is the difference between a growl that belongs to the track and
one that fights it.

Fixed-Hz operators and the formant mode are exempt and always will be: a
formant centre is a vocal-tract resonance, not a musical interval.

### The ratio sequencer

Sixteen steps whose value is a **ratio**, locked to the transport by note value.
Stepping a modulator's ratio does not sweep a timbre — it swaps one harmonic
identity for another, which is the neuro growl as a rhythm rather than as a
filter sweep.

- **A step change never retriggers the phase.** It is a frequency step: the
  accumulator runs on and the spectrum jumps.
- **Glide is geometric**, because steps are stored logarithmically. A glide
  from 1 to 4 passes through 2 at the halfway point — an octave a beat.
- **The engine cuts its sample loop at the step edge**, so a jump lands on the
  beat rather than up to a control chunk late. Measured: the output first
  changes at host sample 4139 against a computed edge of 4138, where the next
  control chunk would have been 4144.

### Braids

Six topologies — **Stack, Twin, Fan, Ring, Pairs, Solo** — as buttons rather
than a mode list. Pressing one *writes* the matrix and the operator levels and
then gets out of the way: every cell is still a knob, nothing is locked, and
your ratios, envelopes and Character are left exactly as they were. A full 6×6
matrix contains every fixed algorithm of the era as a special case, which is
strictly more capable and strictly harder to start from — thirty-six zeroes is
not a sound.

### The filter, the sub lane and unison

**FILTER** is one state-variable filter per voice, after the whole operator
matrix and before the sub. Morph crossfades lowpass → bandpass → highpass rather
than switching, Key tracks the note, Env opens the cutoff by a signed number of
**octaves** on its own AHDSR, and Sing pushes the resonance towards
self-oscillation. At 20 kHz it is not merely transparent, it is **skipped** —
bit for bit the same samples a build with no filter would produce, tested.

**SUB** is the reason a Stryda bass survives a system. A sine or triangle with
its own envelope, level, and octave, which goes through **nothing**: not the
matrix, not the filter, and not the mangle chain when that lands. Drive the
operators as hard as the patch wants; the fundamental stays exactly where you
put it. At level 0 the whole lane is skipped.

**UNISON** plays several copies of the patch per note, and they come out of the
same voice budget — at 4 copies and 8 voices of polyphony you get two notes.
Detune spreads their pitch and Spread their stereo position, as usual. **Index
spread is the one that is not usual**: it offsets each copy's *modulation index*,
so the copies differ in **timbre** rather than only in pitch. That is what a
reese actually is, and it is close to free. Only cells that are already doing
something get offset, so it cannot switch on a path the patch never asked for.

Two behaviours worth knowing:

- **Only one copy carries the sub.** Eight detuned sub oscillators fighting over
  one octave is a mush; one solid fundamental under a wide stack is a bass.
- **A thick stack punches harder than a thin one at the attack.** The copies
  start in phase and drift apart over about a second, so the onset sums
  coherently and the sustain does not. Measured: 8 copies are 2.56× the level of
  one over the first 85 ms and 1.07× over two seconds. The 1/√n compensation
  targets the sustain deliberately — flattening the onset instead would leave
  everything after it 8 dB quiet.

**Split is not here yet.** It arrives with the vowel lane and the mangle chain in
F7, because a crossover with nothing between its two bands is an allpass: it
would cost phase and buy nothing until there is a stage to keep out of the low
end.

### Split, the vowel lane and the mangle chain

**SPLIT** keeps the bottom of the sound out of everything on the MANGLE page.
Below the corner the signal goes round the vowel lane and the whole chain
untouched, so a growl can be destroyed above 150 Hz while the fundamental stays
exactly where the sub lane put it. Measured: a 50 Hz sine through a chain
running the folder at full, drive at full and a 3-bit crusher comes out at
**0.4489** of the 0.4500 it went in as with Split at 300 Hz — and at **0.0145**
without it.

**VOWEL** places three resonances where a vocal tract puts them and sweeps them
through ee → eh → ah → oh → oo. It has its own sixteen-step pattern on its own
division, so the bass can talk in time without the ratio sequencer having to
agree with it. This is what makes an FM growl sound like it is *saying*
something rather than merely buzzing.

**MANGLE** is fold → crush → downsample → comb → phaser → drive → compressor,
in that order. The genre's basses are made by resampling and reprocessing
rather than in one synth pass; having the chain inside the instrument doesn't
replace that, it removes the three most common round trips.

Two things about it are worth knowing:

- **Every stage is skipped at neutral, bit for bit** — not run with a
  coefficient that happens to be the identity. Ten different spellings of "do
  nothing" are tested sample by sample, because six almost-identities in series
  is six chances at changing every project the day the plugin updates.
- **Crush and downsample alias on purpose.** They run at the host rate with no
  oversampling and no antialiasing at all: the folded-back images *are* the
  sound. Everything else that generates harmonics is antialiased.

### Operator waveforms, and the display that shows what they do

Every operator has a **SHAPE**: Sine, Bright, Triangle, Square, Saw, Half sine.
Sine is the default and it is the one to reach for — **in FM a non-sine
operator is not a cosmetic choice.** The harmonics of a modulator *multiply* the
sideband ladder rather than adding to it, so a Saw at index 4 is a different
order of brightness, and of aliasing, than a sine at the same index.

They are safe to use anyway, and the reason is worth knowing because it is what
makes them different from the obvious implementation. Each shape is a fixed sum
of sine harmonics — 2 for Bright, 8 for Triangle and Square (odd only), 16 for
Saw, 8 even ones for Half sine — rendered into a one-cycle table and read with
interpolation at whatever phase arrives. That makes it band-limited by
construction, readable at any phase (which a BLEP oscillator is not: BLEP
corrects a discontinuity from the phase *increment*, and an operator's phase
jumps around), and — the point — its bandwidth is a **known integer** that the
predictor multiplies by. Measured: a 16-harmonic saw modulator moves the
predicted top from 14 740 Hz to 232 540 Hz, **15.8×**, and the index cap that
sits at exactly 1.000000 for the sine drops to 0.218 for the saw.

Expect the cap to bite sooner on Saw, and expect Auto oversampling to earn its
CPU. Measured inharmonic energy in the audible band at the internal rate: sine
−300 dB, half sine −96.4, triangle −102.9, saw −90.7, square −88.3 — all well
under the −60 dB gate.

**Above each operator strip is one cycle of what that operator actually puts
out.** Not a drawing of a sine that gets bent: a real `dsp::FmOperator` — the
same class the voice runs — stepped through a cycle and fed the same three
numbers the matrix feeds it, built from that operator's row of the patch. SHAPE,
FOLD, CHAR and its own feedback all appear exactly as they sound.

It also answers the question the panel would otherwise keep provoking. **CHAR
only does something on an operator that something else modulates**, and that is
the mathematics rather than a limitation: Character is the ModFM exponential
`exp(r·k·cos(ω_m t) − r·k)`, and `k` is the index *arriving* at the operator.
Nothing arriving means k = 0, the exponential is 1, and the operator is its
plain waveform at every Character setting. Turn CHAR up on a carrier nobody
modulates and you will hear nothing — the display says **unmodulated** and shows
you an unchanged wave, instead of leaving you to conclude the knob is broken.

### The modulation layer

Two shared **ADV envelopes**, two **LFOs**, four **macros**, and eight slots of
source → destination → amount. That is 146 parameters and it is skipped
entirely — the destinations are not read at all — unless a slot has **all
three** of a source, a destination and a non-zero amount. A patch that uses none
of it sounds bit-for-bit like one from before the layer existed.

**ADV** (its own page) is a 16-breakpoint envelope with a sustain point and a
loop region, drawn with the DSP's own tension arithmetic so what you see is what
plays. Click a breakpoint to aim the three knobs beneath at it. Two of them
rather than one per operator: six would have been 288 parameters for a feature
used on two operators at a time, so each operator *chooses* its envelope source
instead.

**LFOs** run free or lock to the bar, and **Retrigger** is the interesting
switch: off, an LFO free-runs, so two notes played a bar apart sit at different
points in its cycle — which is what makes a bass line vary rather than repeat.

**Macros** are knobs with no job until a slot gives them one. Assign the same
macro to four destinations with different amounts, some negative, and one hand
movement opens the filter, deepens the matrix and detunes an operator together.

The **destination list holds continuous controls only**, by construction: a
choice or a switch reconfigures rather than adjusts, so modulating one would
mean rebuilding a filter graph per control chunk. **Matrix depth** scales every
live cell together rather than adding to them, which is why it can never switch
on a path the patch did not ask for.

Two things that took work and are worth knowing:

- **The index cap answers the modulated numbers, not the patch.** A slot that
  multiplies the matrix depth multiplies the sideband ladder with it, and a cap
  resolved from the patch would be protecting a spectrum nobody is hearing.
  Measured: a patch whose own index needs no capping resolves to a scale of
  exactly 1.0; the same patch with a slot at matrix depth ×25 resolves to 0.320.
- **A sequencer step edge does not run the modulators.** The engine refreshes a
  voice twice inside a chunk that contains a step edge, and letting the extra
  refresh advance an LFO would make its rate depend on the sequencer's division
  — a 1/32 sequence at 174 BPM ran every LFO in the patch fast.

**Character only does something on an operator that something else modulates.**
That is the mathematics rather than a limitation: Character is the ModFM
exponential `exp(r·k·cos(ω_m t) − r·k)`, and `k` is the index *arriving* at this
operator. Nothing arriving means k = 0, the exponential is 1, and the operator
is a plain sine at every Character setting. Turn CHAR on a carrier nobody
modulates and you will hear exactly nothing.

### The index cap, and why it is Off by default

The cap scales every index down until the predicted top sits under the internal
Nyquist — 1980s key scaling, derived from the arithmetic rather than dialled in
by hand. When it is not binding it is **exactly** inert: the same samples to the
last bit, tested.

It ships **Off** because the prediction it acts on is an upper bound and a loose
one on stacks, so leaving it on would clamp patches that never needed it. Switch
it on when a patch played high starts to sound gritty; the readout will tell you
when it is doing work.

It is also, now, genuinely cheap. Resolving it costs **4.6 µs** and happens once
per voice every 512 internal samples, which measures as no overhead at all
against an uncapped render. That was not true of the first build the cap shipped
in: it cost two to three *seconds* per resolution and ran per voice per
32-sample chunk, which froze FL Studio on the rig. `plugins/Stryda/PLAN.md` has
the numbers and what was wrong.

### Oversampling matters more here than anywhere else in the suite

Measured — a two-operator pair, ratio 7, at 48 kHz:

| index (radians) | host rate | ×2 | ×4 | ×8 |
|---|---|---|---|---|
| 4 | −63.0 dB | **−180.3** | −180.3 | −180.3 |
| 16 | +2.6 dB | −75.8 | **−114.6** | −114.6 |
| 64 | +9.8 dB | +7.7 | +6.2 | **−112.9** |

At index 16 the host rate alone puts the aliasing **louder than the signal**.
Auto picks ×4 at 48 kHz, which is what the middle row is measured at. The bottom
row is why the index cap exists at all: oversampling alone does not cover the top
of the range.

---

## Presets

*Init* (one operator, bit-exactly a sine — start here), *Neuro Growl*,
*Bell*, *Sub Stack*. Each carries its own notes: what it is, what to automate,
and what to listen for.

---

## Sources

Everything is derived from published mathematics — Chowning's 1973 paper, the
two Lazzarini & Timoney ModFM papers, the DAFx-11 exponential-FM criterion,
Tomisawa's feedback patent, and public descriptions of six-operator key scaling
and Casio phase distortion. All nine were read first-hand from
`technical references/stryda/`, and each is attributed twice — at the point of
use and in [`docs/DSP-REFERENCES.md`](../../docs/DSP-REFERENCES.md).

Nothing is taken from any product's binary, presets or panel; no brand name
appears in a control, a preset or a document.
