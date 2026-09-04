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

Globally: **Oversampling** and **Render quality** as everywhere in the suite,
**Voices**, **Master**, and the **Index cap**.

### The index cap, and why it is Off by default

The cap scales every index down until the predicted top sits under the internal
Nyquist — 1980s key scaling, derived from the arithmetic rather than dialled in
by hand. When it is not binding it is **exactly** inert: the same samples to the
last bit, tested.

It ships **Off** because the prediction it acts on is an upper bound and a loose
one on stacks, so leaving it on would clamp patches that never needed it. Switch
it on when a patch played high starts to sound gritty; the readout will tell you
when it is doing work.

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
