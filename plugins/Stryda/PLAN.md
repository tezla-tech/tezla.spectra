# Stryda — the FM synthesiser for neurofunk drum and bass

**Code `Tzst` · "Tezla Stryda" · `tech.tezla.Stryda` · instrument.**

## The thesis

Sonitus covers the subtractive growl: two band-limited oscillators, one PM
index, a reverse path, feedback, unison, a morphing filter. What it cannot do is
*FM proper* — a braid of six operators where the harmonic identity of the sound
is **built out of ratios** rather than filtered out of a saw. That is the sound
the eighties workstations made, and it is also, fifty years after Chowning, the
engine under a neurofunk bass: a growl is an index and a ratio moving in time,
not a filter sweep.

The brief, in the user's words (2026-09-04): an FM synthesiser for **neurofunk
drum and bass basses**, with **the advanced envelopes from Sonitus**, able to
reach **the eighties six-operator sounds** as well as the modern matrix ones,
with **our own ideas** on top, and with **the house microtuning engine
integrated** as it was for Sonitus and Malleus.

Six operators in a 6×6 index matrix with feedback on the diagonal and a noise
row — which contains every fixed eighties algorithm as a special case — plus a
**protected sub lane** that never enters the braid or the distortion, a
**ratio step sequencer** that jumps harmonic identity in time with the drums, a
**vowel lane** so the bass talks, and a **bandwidth predictor** that says, live,
where the top of the spectrum actually is.

Decisions taken with the user before this plan was approved (2026-09-04):
name **Stryda**; code **`Tzst`**; **six operators, full matrix**; **all four**
of the non-classic extras; **Ictus paused at I4.1**; microtuning integrated
**at the ratio**, not only at the keyboard.

**IP guard (CLAUDE.md §2.1)**: mathematics and public analysis only. Nothing
from any product's binary, samples, presets or panel; no brand name in a
control, a preset or a document; the operator topologies get **our own names**,
never a numbered algorithm list. Everything taken is attributed twice — at the
point of use and in `docs/DSP-REFERENCES.md`.

---

## Research material — status

Every source was **blocked at the network layer**: ten domains refused
(`ccrma.stanford.edu`, `web.eecs.umich.edu`, `web.uvic.ca`, `www.dafx.de`,
`arxiv.org`, `en.wikipedia.org`, `electricdruid.net`, `yamahasynth.com`,
`tildearrow.org`, `mural.maynoothuniversity.ie`), and so did **Google Drive on
all four of its hosts** (`drive.google.com`, `drive.usercontent.google.com`,
`docs.google.com`, `googleusercontent.com`) when the user tried that route after
GitHub refused the upload for size. `curl` was no help: the block is the proxy.
Per CLAUDE.md §9 the URLs were listed for the user, who uploaded the papers to
**`technical references/stryda/`** in two sessions; they were **read first-hand
from there on 2026-09-04**. That access route is part of the record. The full
table is in `docs/DSP-REFERENCES.md` ("FM synthesis — Stryda"); the short form:

| Source | Status | What it settles here |
|---|---|---|
| Chowning, JAES 21(7) 1973 | read first-hand | `J_n(I)` sidebands at `f_c ± n·f_m`; `BW ≈ 2(d+m)`; higher orders need larger indices; **negative-frequency sidebands reflect around 0 Hz with a phase inversion and add algebraically**; `c/m` rational → harmonic. |
| Lazzarini & Timoney, JAES 58(6) 2010 (ModFM) | read first-hand | The r/s extension (the **Character** control), the phase-synchronous **formant operator**, and Eq (12): a closed-form **max index that avoids aliasing**. |
| Timoney, Lazzarini & Lysaght, DAFx-08 | read first-hand | `y = e^(k·cos ωt − k)·cos ωt`, and why it matters: modified-Bessel partials fall away **smoothly**, so the index behaves like a filter opening. |
| Timoney & Lazzarini, DAFx-11 | read first-hand | The −80 dB bandwidth criterion for **exponential** FM — a different technique, so it de-risks an extra mode rather than correcting the linear one. |
| Lazzarini et al., higher-order FM, JNMR 2024 | read first-hand | True second-order-and-above FM with an operator formulation. Parked as a candidate character. |
| Electric Druid, phase distortion | read first-hand | **PD is PM with a knee/triangle modulator at the carrier's own frequency.** A phase-transfer function, not an operator mode. |
| Tomisawa, US 4,249,447 | read first-hand | The feedback averaging device eliminates the Nyquist-rate "hunting" — confirms what `Oscillator::setFeedback` already does. |
| JonDent, DX7 keyboard level scaling | read first-hand | Break point A1–C8; four curves (linear/exponential × positive/negative) independently left and right; depth 0–99, 0 flat. |
| MusicRadar ×2, BassGorilla | read first-hand | Production practice for presets and sequencer defaults. Taste, not physics. |

---

## The two findings that shaped the design

### 1. One operator, one Character knob — not a mode list

The ModFM paper's extension (its Eq 19) is

```
    x(t) = e^(r·k·cos(ω_m t) − r·k) · cos(ω_c t + s·k·sin(ω_m t))
```

with `0 ≤ r ≤ 1` and `−1 ≤ s ≤ 1`, whose spectrum is

```
    ½ ΣΣ I_a(r·k)·J_b(s·k)·[ cos(ω_c t + (a+b)ω_m t) + cos(ω_c t − (a−b)ω_m t) ]
```

At `r = 0, s = 1` the exponential is `e⁰ = 1` and this is **exactly classic phase
modulation** — the eighties operator, bit for bit. At `r = 1, s = 0` it is
ModFM, whose partials fall away smoothly instead of each one pumping through the
Bessel zeros: the paper's own comparison shows bell tones decaying in an orderly
way where classic FM's partials break into dashes on the spectrogram, and DAFx-08
puts it plainly — moving the index gives "a perceptual effect more reminiscent of
an opening/closing lowpass filter".

So the operator gets **Character** (classic FM ↔ ModFM) and **Tilt** (the sign
and depth of `s`, which makes the spectrum asymmetric) rather than a mode
switch. One continuous, automatable, modulatable control across both flavours —
and for a bass patch the ModFM end is the one that behaves, because a filter-like
index is exactly what a growl wants. The paper also warns that ModFM needs an
index about **50 % higher** than classic FM for the same steady-state brightness;
F1 measures that rather than assuming it.

**Neutral is bit-exact by construction**: Character 0 makes `r = 0`, the
exponential is branched out entirely, and the operator is the classic one.

### 2. Three aliasing criteria, all published, all in closed form

- **Classic PM**: Chowning's `BW ≈ 2(d+m)`, sharpened by the Bessel decay bound
  `k_max ≈ I + 2·I^⅓ + 1`, composed over the matrix as
  `fTop(j) = f_j + Σ_i (I_ij + 2·I_ij^⅓ + 1)·fTop(i)` over the acyclic part.
- **ModFM**: Eq (12) directly — the largest `k` with
  `20·log₁₀(Iₙ(k)/I₀(k)) ≤ −60 dB`, `n = (f_sr/2 − f_c)/f_m`.
- **Exponential FM**: Eq (16) —
  `BW₋₈₀dB = f_c·e^(V₀ln2)·2^(V_m−1)·p₀₁ + (p₁₁ + V_m)·f_m`,
  `p₀₁ = 2.771`, `p₁₁ = 4.3030`; the fit deliberately over-estimates.

`dsp::FmBandwidth` implements all three. **None is trusted until F1 measures it
against a real spectrum** — a predictor nobody has checked is a decoration, and
this repository has shipped three of those already.

---

## The design

```
        noise ──┐
                ▼
   ┌──── 6×6 index matrix (diagonal = feedback) ────┐
   │  op6  op5  op4  op3  op2  op1                   │
   └────────────────┬────────────────────────────────┘
                    │ output column (level + pan per operator)
                    ▼
              SvfFilter (per voice, morph + Sing)
                    ▼
   ┌────────── Split crossover (Linkwitz–Riley) ─────────┐
   │  low band ──────────────────────────── clean ───────┤
   │  high band → vowel lane → mangle chain ─────────────┤
   └──────────────────────┬──────────────────────────────┘
                          ▼
        + SUB lane (own oscillator + AHDSR, never modulated)
                          ▼
                       output
```

**The matrix ordering rule** is Sonitus's, already written and tested
(`SonitusVoice.hpp:1155`): operators evaluate **6 → 1**, so a cell **below** the
diagonal is instantaneous within the sample and a cell **above** it is **one
sample old**. That single-sample delay is what turns an algebraic loop into a
computable one. It is a design statement, not a bug, and it belongs in the
tooltip: *op 4 → op 2 and op 2 → op 4 do not sound alike at the same index.*

**Per operator**: Character, Tilt; ratio (coarse × fine cents) or **Fixed Hz**;
level and pan; **envelope source** (own AHDSR, ADV 1, or ADV 2); **key scaling**
— break point, left curve, right curve, left depth, right depth, rate scaling,
the four-curve structure the DX7 article documents; velocity → level and
velocity → index. `dsp::Ratio` prints the running ratio in lowest terms with the
error in cents, on every operator strip.

**The matrix**: 36 index cells in cycles of phase deviation, plus a **noise row**
of 6 so one shared noise source can modulate any operator's phase — the grit no
amount of sine-on-sine gives. The diagonal drives `Oscillator::setFeedback`,
whose bound is structural (PM cannot diverge in amplitude) and whose two-sample
average is now cited first-hand.

**Named braids**, our own, each a button that writes the matrix and leaves it
editable: *Stack*, *Twin*, *Fan*, *Ring*, *Pairs*, *Solo*.

**Phase distortion comes free**: `dsp::PhaseShaper` is a piecewise phase-bend at
the operator's own read, and the header states the PM equivalence rather than
pretending it is a separate engine.

**Exponential FM** is a per-cell switch, off by default, bounded by Eq (16) —
the analogue cross-mod sound, clangorous and pitch-shifting with depth.

**The formant operator** uses the paper's phase-synchronous form: `f_c = n·f_m`
with `n = int(f_f/f_m)`, two carriers at `n` and `n+1` crossfaded by
`a = f_f/f_m − n`, plus a frequency shift `ω_s` for inharmonicity while the ratio
stays locked.

**The protected sub lane** is a `dsp::Oscillator` sine or triangle with its own
`Adsr`, level and octave, that **never enters the matrix and never enters the
mangle chain**. **Split** is a `dsp::Crossover` pair so the vowel and mangle
stages only see content above it, with the low band passed through and re-summed.
Bit-exact when Split is off and at sub level 0. On a DnB rig this is the
difference between a bass that survives a club system and one that collapses the
moment the growl bites.

**Filter per voice** (`SvfFilter`: morph LP→BP→HP, drive in the loop, **Sing**).
**Vowel lane and mangle chain global** — bass patches are played monophonically
far more often than not, and per-voice mangle would triple the cost of the most
expensive thing here. Documented, not silent.

### Microtuning — at the ratio, not just at the keyboard

Sonitus routes notes through `dsp::Tuning` and locks its comb to the loaded
scale; Malleus places its mode ratios against the tuning through
`ui::TuningHost`; Ictus snaps drum pitches with `Tuning::nearestScaleHz`. Stryda
goes further, because **in FM the ratio *is* the interval** — sidebands land at
`f_c ± n·f_m`, so the ratio decides where the whole ladder sits.

- **Notes through `Tuning`**, with concert pitch, the built-in scales, Scala
  import and the shared tuning page — the baseline every instrument here has.
- **Ratio mode** per operator, append-only: **Free** (continuous), **Harmonic**
  (integer and simple ratios — Chowning's `N₁:N₂`), and **Scale** — snapped to
  the loaded scale's degrees, octave-extended. In 19-TET or a Persian scale the
  modulator's whole sideband ladder then lands on that scale's degrees rather
  than on 12-TET's. This is the scale-locked comb and Overtone Lock applied to
  the thing FM is built from.
- **The ratio sequencer snaps through the same path**, so a sequenced harmonic
  jump is in tune with the track.
- **Fixed-Hz operators and the vowel lane are exempt, and say so.** A formant
  centre is a vocal-tract resonance, not a musical interval; snapping it would be
  wrong, and the tooltip explains why rather than leaving a dead control.
- **The readout shows both**: lowest-terms ratio with cents error, plus the
  scale-degree name in Scale mode.

### The ratio sequencer — the growl as rhythm

`dsp::StepSequencer` already does 16 steps, tempo sync via `setPhaseFromPpq`,
glide and a step-boundary position. Here its value is a **ratio**, snapped
through the mode above. Two rules, both learned the hard way in this repository:

- **The sample loop is cut at the step boundary, not the block boundary**
  (CLAUDE.md §7). Emberdrive measured **0.296 of full scale** of block-size
  disagreement before its loop was cut at the timer, and a ratio jump is a far
  bigger event than a voicing rebuild.
- **A ratio change never resets the phase accumulator.** It is a frequency step,
  not a retrigger: the phase stays continuous and the spectrum jumps, which is
  the point. Glide interpolates the ratio, not the phase.

A second sequencer drives the **vowel lane**: 16 steps of `dsp::Formant` vowels
with their own length and division, so the bass talks in time.

### The index cap and the bandwidth readout

- **Auto oversampling** per house policy (×4 at 48 k, ×2 at 96 k, off at 192 k),
  plus `dsp::RenderOversampling` from day one.
- **Index cap** (*Off / Soft / Hard*): scales indices so the predicted top
  sideband stays under `0.9 ×` the internal Nyquist. Smoothed; **exactly inert
  when it is not binding**, tested bit for bit. This is eighties key scaling
  derived from the arithmetic instead of dialled in by hand, and it is why the
  same patch survives being played four octaves up.
- **A bandwidth readout** on the panel: the predicted top sideband for the note
  being played, against the internal Nyquist, with the cap shown when it bites.

### Parameters and state

≈425. Operators 6 × 24 ≈ 144; matrix 36 + 6 noise; sub 10; filter 7; ratio
sequencer 23; vowel lane 23; mangle ≈18; two LFOs 16; **two** ADV envelopes
(`dsp::MultiEnvelope`) 96; macros and modulation matrix 28; global ≈17.

Two shared ADV envelopes rather than one per operator: six would be 288
parameters for a feature used on two operators at a time. Each operator
*chooses* its envelope source — the Sonitus advanced envelopes the brief asked
for, at a fifth of the cost.

House rules unchanged: `kSchemaV1` on every parameter from birth; every choice
list `static_assert`ed against its enum; `dest::` and `division::` append-only;
presets recalled by index with new ones on the end; **every preset carries its
NOTES text**, non-defaultable, so a preset without notes does not compile; a
`DiceSections` classifier from the first parameter, with `tezla-render dice`
run after every phase that adds parameters.

**Unison index spread**: `UnisonBank` already detunes pitch, spreads stereo and
drifts. Spreading the **index** across the stack as well gives each copy a
different harmonic content — a Reese where the copies differ in timbre, not only
in pitch. Close to free, and the thickest thing in the plan.

### CPU

Target **8 voices at 48 kHz ×4** in a sensible share of a core, with a neutral
path costing one `Oscillator::advance`. Estimates to be replaced by
`CHECK_CPU_BUDGET` assertions in F2 and F9: operator ≈25 ns/sample at the
internal rate (the `exp` is the cost, and Character 0 branches it out), so six
≈150; matrix ≈40; filter ≈30; mangle ≈100 engaged and ≈10 neutral; decimation
≈0.5 % per channel. Idle < 0.05 %. `TEZLA_EMULATED=1` marks emulated runs
`NOT ASSERTED` rather than dropping the requirement.

---

## Reuse map

Paths under `shared/tezla-dsp/include/tezla/dsp/` unless stated.

**Used as they are:** `Oscillator` (the operator: PM, band-limited shapes, morph,
noise, sync, bounded feedback), `Ratio`, `Adsr`, `MultiEnvelope`, `Lfo`,
`StepSequencer`, `Divisions`, `SvfFilter`, `Formant`, `Comb`, `Phaser`,
`Crossover`, `UnisonBank`, `Adaa1` + `Waveshapers`, `SineFolder`, `Bitcrusher`,
`CompressorCore`, `DcBlocker::retune`, `Oversampler` + `RenderOversampling` +
`effectiveOversamplingMode`, `SmoothedValue`, `SmallRandom`, `ScopedNoDenormals`,
`Exact`, `Decibels`, `Tuning` + `Scales` + `ScalaFile`, `SpectrumAnalyser`,
`VuMeter`, `Fft`. `HalfbandFir.hpp`'s `detail::besselI0` is already in the tree
and is what ModFM's normalisation needs, extended to order `n` by the same
series and checked the way `dsp::besselJ` was.

**`shared/tezla-ui/`:** `KnobLookAndFeel`, `HouseControls`, `Plate`, `LampButton`,
`PanelDesign`, `HeaderBar`, `ModStrip` / `ModRing` / `ModulationIds`,
`SpectrumDisplay`, `TuningPanel` / `TuningHost` and the shared tuning page,
`LevelMeter`, `ScrollWheel`.

**New shared, own commit each:** `dsp::FmOperator` (the r/s core),
`dsp::PhaseShaper`, `dsp::FmBandwidth` (three criteria, the matrix recursion,
the cap).

**New in `plugins/Stryda/Dsp/`:** `OperatorMatrix`, `StrydaVoice`,
`VoiceManager`, `StrydaEngine`, `RatioSequencer`, `VowelLane`, `MangleChain`,
`DiceSections`.

**JUCE-layer patterns from Sonitus:** schema constants, the sample-accurate MIDI
split, latency declaration on oversampling change
(`PluginProcessor.cpp:1511`), the preset table with non-defaultable notes, the
NOTES page, the page/cell editor classes, `tezla-render` and `tezla-measure`
registration.

---

## Phases

Each phase is one commit: tests **seen red first**, numbers quoted in the
message, the whole tree built (`./scripts/build.sh NONE --test`, plus the plugin
targets, the tools and `ui-preview` — the two most easily forgotten), the
validator run on the bundle, `tezla-render dice` after any parameter addition,
and "the `qemu-aarch64` cross-check was not run (CLAUDE.md §2.3 gate)" in every
message.

- **F0** — branch restarted from `origin/master`; this file; registry row
  `Tzst`; `docs/DSP-REFERENCES.md` rows; CLAUDE.md §11 note; **Ictus paused at
  I4.1**; roadmap entries for what is parked.
- **F1** — `dsp::FmBandwidth` + `tezla-measure stryda`, four tables. No plugin.
- **F2** — `FmOperator`, `OperatorMatrix`, voice, engine, index cap.
- **F3** — minimal JUCE layer, **played on the rig**, then an ear round.
- **F4** — `PhaseShaper`, exponential-FM cells, formant operator, key scaling.
- **F5** — sub lane, Split, per-voice filter, unison index spread.
- **F6** — microtuning at the ratio, the three ratio modes, ratio sequencer,
  the named braids.
- **F7** — vowel lane and mangle chain, every stage bit-exact at neutral.
- **F8** — two ADV envelopes, two LFOs, macros, the matrices, the dice gate.
- **F9** — editor and close-out; validator 47/47 on all fourteen. **Two items
  were added to it by the user on 2026-09-04** and they lead it: a per-operator
  waveform display, and a shape choice per operator. See "F9's two asks" below.

---

## F9's two asks, and how each is answered

Asked directly on 2026-09-04, while F8 was being finished: *"dont we have a
choice of waveforms for the operators and some visual feedback of whats going on
with the sound?"* and then *"visual feedback like how the waveform looks based
on our choices"*.

Both are fair. Neither exists today: every operator is a sine shaped by
Character, Fold and Mode, and the only thing on the panel that says anything
about the sound is the bandwidth readout on the MATRIX page.

### 1. The waveform display — and it must be the real wave

The tempting version draws a sine and bends it a bit. The honest one **runs a
real `dsp::FmOperator` for one cycle**, and it costs nothing to do properly:
256 samples times six operators, on the editor timer.

What it has to be fed is the thing that makes it truthful. `OperatorMatrix`
drives each operator with three numbers per sample:

    pm   = sum over modulators of  index * sin(theta_from)
    am   = sum over modulators of  index * cos(theta_from)
    norm = sum over modulators of  index * gain_from

So the display models the same three, from the patch: for each cell into this
operator, a term at that modulator's ratio scaled by the cell's index. That
gives the operator's actual output, feedback and phase distortion included —
Fold bends the ramp before anything reads it, so it shows up exactly as it
sounds.

**And it teaches the thing the destination sweep found.** Character is the ModFM
exponential `exp(r·k·cos - r·k)`; with nothing modulating an operator, k = 0 and
the exponential is 1, so the wave stays a sine at every Character setting. A
player turning CHAR on a carrier nobody modulates hears nothing and reasonably
concludes the knob is broken. On a display that draws the real wave, they see
the reason instead.

### 2. The shape choice — additive, so the aliasing story stays honest

A non-sine operator is not a cosmetic change: in FM the carrier's harmonics
multiply the sideband ladder, so a saw modulator at index 4 is an aliasing
catastrophe rather than a brighter sound. Two constraints follow:

- **A BLEP oscillator cannot be used.** BLEP corrects a discontinuity using the
  phase *increment*, and an FM operator is read at a phase that jumps around
  under modulation. The correction is wrong the moment the phase is not walking
  forward at a constant rate.
- **So the shapes are built additively**, a fixed number of harmonics summed
  into a one-cycle table read with linear interpolation at the modulated phase.
  Band-limited by construction, readable at any phase, and — the point — the
  harmonic count is a *known number* that `FmBandwidth` can multiply the
  predicted top sideband by. The index cap then protects a non-sine operator
  correctly rather than by luck.

Shapes, all odd/even structure chosen for what a bass patch wants:
sine (1 harmonic, the default and bit-exactly what ships today), half sine,
triangle-ish (odd, 1/n²), square-ish (odd, 1/n), saw-ish (all, 1/n), and a
two-harmonic "bright" that is the cheapest useful step away from a sine.

**The list is a choice parameter, so it is append-only and frozen from the
commit it ships in** (CLAUDE.md section 8), and sine must be **index 0** and
**bit-exactly** today's operator, or every existing Stryda patch changes.

### 3. The spectrum, with the predicted edge drawn on it

Already in the plan, and it is the other half of "what is going on with the
sound": `dsp::SpectrumAnalyser` into `ui::SpectrumDisplay`, with the predicted
top sideband, the internal Nyquist and the cap's current scale drawn over it.
The predictor has been measured to 0 Hz against a real spectrum since F1
(bin width 6.0 Hz); this is where that measurement becomes something a player
can see.

## Continuity — how any session resumes this work

This section is the handoff, updated **in the same commit as each phase**, so
whichever session picks the work up — after a context loss, a model change, or a
fresh clone — needs nothing beyond this file and CLAUDE.md. **Resume from the
first `pending` row.**

| phase | status |
|---|---|
| F0 plan + registry + references + Ictus pause + roadmap | done |
| F1 `FmBandwidth` + `tezla-measure stryda`, four tables | done |
| F2 `FmOperator` + `OperatorMatrix` + voice + engine + index cap | done |
| F3 minimal JUCE layer, rig build + ear round | pending |
| F4 `PhaseShaper`, formant operator, key scaling, velocity | done in code; not yet played on the rig. **Exponential-FM cells deferred** — see below |
| **FIX** index cap hung the audio thread; oversampling control attached | done — see "The rig freeze" below |
| F5 sub lane, per-voice filter, unison index spread | done — **Split deferred to F7**, see below. Not yet played on the rig |
| F6 microtuning at the ratio, ratio sequencer, named braids | done — and the panel is **paged**, at the user's request. Not yet played on the rig |
| F7 vowel lane + mangle chain, and Split arrives | done — not yet played on the rig |
| F8 modulation layer + dice gate | done — not yet played on the rig. Two new pages, ADV and MOD |
| F9 editor + close-out, **plus the two things the user asked for on 2026-09-04**: an operator waveform display and a per-operator shape choice | pending |

**Measured at F1** (`tezla-measure stryda`, `tezla-tests`; the reference
operator is the published closed form written out as plain arithmetic, so what
is measured is the mathematics the predictor claims to predict):

| claim | figure |
|---|---|
| predictor vs the rendered -80 dB edge, 27 combinations (55/220/880 Hz x ratios 1/3/7 x indices 1/4/16 rad) | **+0 Hz on every row** — exact to the bin |
| the same sweep as a suite test, 18 combinations at 393216 Hz | **+0 Hz**, bin width 6.0 Hz |
| Kapteyn feedback coefficients vs a numerical solve of Kepler's equation, beta 0.25–0.95, harmonics 1–9 | **2.665e-15** |
| `besselJn` sum rule `J_0^2 + 2*sum J_n^2 = 1` at x = 100.53 | **1.000000000000** (the fixed 256-point rule reads 1.770586) |
| `besselI` against the ModFM paper's own worked number `I_0(5 ln2)` | **7.16882** vs the paper's 7.17 |
| alias floor, index 4 rad, ratio 7, 48 kHz: x1 / x2 | −63.0 / **−180.3 dB** |
| alias floor, index 16 rad, ratio 7, 48 kHz: x1 / x2 / x4 | +2.6 / −75.8 / **−114.6 dB** |
| alias floor, index 64 rad, ratio 7, 48 kHz: x1 / x2 / x4 / x8 | +9.8 / +7.7 / +6.2 / **−112.9 dB** |
| feedback, measured -80 dB edge at beta 0.25 / 0.9 / 1.0 / 6.28 rad (192 kHz) | 2637 / 18896 / 25928 / **95997 Hz** (Nyquist) |
| feedback peak output across the whole range Oscillator accepts | **1.000**, bounded everywhere |
| Character 0 -> 1 at index 5: spectral centroid | 4.14 -> **2.69** harmonics |
| Character 0 -> 1: partial-order reversals (the Bessel oscillation) | 2 -> **0** |
| index needed at Character 1 to match Character 0's centroid | 11.12 vs 5.00 — **+122 %** |

### F7, and what "bit-exact at neutral" costs to actually check

Delivered: **Split** (the deferral from F5), the **vowel lane** with its own
sixteen-step pattern and division, and the **mangle chain** — fold, crush,
downsample, comb, phaser, drive, compressor. Thirty-two parameters at
`kSchemaV5`, and a MANGLE page.

**Split arrives now because now there is something to keep out of the low
band.** A Linkwitz–Riley crossover summed straight back is an *allpass*, not an
identity, so the chain deliberately does **not** count Split alone as engaged —
with nothing after it, it would cost phase and buy nothing. The test asserts
that inertness as the design rather than treating it as a failure.

Measured, and it is the number the whole lane exists for: a 50 Hz sine and a
3 kHz sine through a chain running the folder at full, drive at full and a
3-bit crusher —

| | 50 Hz component out |
|---|---|
| asked for | 0.4500 |
| Split at 300 Hz | **0.4489** |
| Split off | 0.0145 |

The fundamental survives essentially untouched with Split on, and is destroyed
without it.

**Where the chain runs, and why.** After the decimator, at the **host rate**,
not inside the oversampled section. Crush and downsample are CLAUDE.md §7's
documented aliasing exception — their whole character is folded-back images, so
oversampling them would remove the effect rather than clean it up. The fold and
the drive are ADAA, which band-limits them where they are.

**The rule, checked stage by stage.** Ten spellings of "do nothing" — each one
*not* the struct's own default, so the test proves the skip rather than the
initialiser — and every one returns its input to the last bit across 4096
samples of noise plus a sine. Then the same nine stages engaged, each changing
more than a quarter of the samples, because a chain that was never wired up
would pass every bit-exactness check ever written.

### F8, and the four things the sweep found

The modulation layer is 146 parameters: two 16-point ADV envelopes with a
sustain point and a loop, two LFOs (free or synced), four macros, and eight
slots of source → destination → amount. `dest::` holds 37 continuous
destinations and is frozen and append-only; `source::` the same. Two new pages,
ADV and MOD.

**The whole layer is skipped when no slot has all three of a source, a
destination and a non-zero amount** — the destinations are not read, let alone
written with a zero — so a project saved before F8 is bit-identical. Four
spellings of "off" are asserted, each with two of the three so the guard is what
is proved rather than the struct's initialiser.

Four things the tests found, in the order they surfaced:

1. **A step edge advanced the modulators.** The engine refreshes a voice twice
   inside a chunk that contains a sequencer step edge — once for the edge, once
   for the chunk. The edge is an *extra* refresh, so letting it run the
   modulators makes an LFO's rate depend on the sequencer's division: a 1/32
   sequence at 174 BPM ran every LFO in the patch fast, and the faster the
   sequence the faster they went. `applyParameters` gained an
   `advanceModulators` flag and the engine passes `chunkDue`.

2. **The index cap read the patch, not the modulated copy.** `matrixDepth` and
   an operator's feedback move exactly the numbers the bandwidth prediction is
   made of, so a cap resolved from the patch protects a spectrum nobody is
   hearing. `refreshIndexCap` now reads the voice's own modulated copy, which
   `applyParameters` has always just written. Measured: a patch whose own index
   needs no capping at all resolves to a scale of **exactly 1.0**, and the same
   patch with a slot multiplying the matrix depth by 25 resolves to **0.320** —
   the cap removing 68 % of the modulated index.

3. **Two of my own tests were decorations, and the break-checks said so.**
   - The step-edge test was a *unit* test on the voice. Changing the engine's
     call site to a hard `true` left it green, so it proved the voice honours
     the flag and nothing about whether the engine ever passes false. It now
     also renders through the engine with a sequencer **enabled but targeting
     nothing** — still cutting the loop, still firing the extra refresh,
     changing no ratio — and asserts that against a sequencer switched off, bit
     for bit.
   - That engine assertion was itself a decoration on its first draft, because
     it used division index **2**, which is "2 bars". At 174 BPM the next step
     was 529,655 internal samples away and the render was 192,000 long: not one
     edge fired and it compared two identical renders of nothing. 1/32 gives 23
     edges across the same render.

4. **The destination sweep needs a bed that can hear every destination.** The
   first bed wired two operators of six and the sweep reported **18 of 37
   destinations inert** — correctly, because "Op 5 ratio" cannot change a render
   in which operator 5 has no level and no matrix cell. With a full
   6 → 5 → 4 → 3 → 2 → 1 chain one remained: **Op 6 character**, and that one is
   the mathematics rather than a defect. Character is the ModFM exponential
   `exp(r·k·cos(ω_m t) − r·k)`, and `k` is the index *arriving* from the
   operators that modulate this one. An operator nothing modulates has k = 0,
   the exponential is exp(0) = 1, and its Character does nothing at any setting.
   Operator 6 sat at the top of the chain with nothing reaching it. Closing the
   chain into a ring (op 1 back round to op 6, one cell above the diagonal, so
   one sample old and still computable) takes the sweep to **37 swept, 0
   inert**.

   Worth carrying into the panel: **Character only means something on an
   operator that something else modulates.** A player who turns CHAR on a
   carrier nobody modulates will hear exactly nothing, and will reasonably
   conclude the knob is broken.

Six tests, all six seen red against a matching break first.

### F6, the paged panel, and a sequencer that was silently inert

Delivered: `Scale::snapRatio` and the **three ratio modes** (Free / Harmonic /
Scale) per operator, the **ratio sequencer** with the step-boundary loop cut,
the **six named braids**, and the shared **tuning page** with the processor as
its `ui::TuningHost`. Twenty-six parameters at `kSchemaV4`, appended, all inert
by default.

**The panel is now paged** — OPERATORS / MATRIX / VOICE / SEQ / TUNING — and the
window minimum came back down from 980×760 to **860×520**. That was not a
design preference: the user reported the F5 panel did not fit their screen, and
one page of everything was also the wrong shape for a phase that adds a
sequencer and a tuning page. Each page lays its rows out from the height it is
given, so a small window gets denser rather than clipped.

**Three bugs, and the shape of each is worth keeping.**

1. **The sequencer was completely inert and three tests said it was fine.**
   `dsp::StepSequencer` is a *modulation* sequencer: `setStep` clamps to −1..1,
   because that is what a step driving a depth or a pan means. Handing it a
   ratio of 4 stored 1. A block-size test, a phase-continuity test and an
   is-it-enabled test all passed — the first two because a signal that never
   changes is trivially smooth and trivially buffer-independent, the third
   because step 0 still differed from the patch ratio. What caught it was a
   test that asked **where** the jump landed. Steps are stored logarithmically
   now, which also makes glide geometric: a glide from 1 to 4 passes through 2
   at the halfway point, which is an octave a beat rather than an arbitrary
   2.5.

2. **The step-boundary cut reached the parameters but not the voices.** Pushing
   the new ratio into `parameters_` and waiting for the next control chunk to
   apply it puts the jump back where it would have been without the cut. The
   engine now applies parameters at a step edge as well as at a chunk boundary;
   the cap stays on its own coarser sub-grid, because it costs a bisection
   where applying parameters costs arithmetic.

3. **Harmonic mode snapped to the simplest ratio, not the nearest.**
   `dsp::nearestRatio` returns the simplest p:q *inside the tolerance*, falling
   back to the nearest when none qualifies — so a wide tolerance made
   everything qualify and 3.49 snapped to 3/1 rather than 7/2. A quantiser
   wants the nearest, always, so the tolerance is deliberately unreachable.

**Two more decorations, found by breaking them.** The block-size test passes
with the step cut removed — the control-chunk grid is already stream-anchored,
so buffer independence was never what the cut protects. And the scale-mode test
passed with the repeat-above scan removed, because none of its cases sat just
under a repeat boundary. Both now assert what they claim: the first differing
sample lands at the edge (4139 measured against a computed 4138, the difference
being the decimator's own smear) rather than at the next chunk (4144), and 1.95
snaps up to 2/1 rather than down to 3/2.

**What Scale mode is for.** `Tuning::nearestScaleHz` snaps an absolute
frequency against the root's pitch. A ratio is a different quantity — an
interval above the note — so it snaps against 1/1 and the answer is the same at
every key. That is the whole point: a modulator snapped to the loaded scale
puts its entire sideband ladder on that scale's degrees, everywhere on the
keyboard. Fixed-Hz operators and the formant mode are exempt and say so: a
formant centre is a vocal-tract resonance, not a musical interval.

### F5, and the one thing it deliberately does not ship

Delivered: the **per-voice filter** (morph, resonance, key tracking, an envelope
in octaves, drive and Sing), the **protected sub lane** (own oscillator, own
AHDSR, octave and shape), and **unison** with pitch detune, stereo spread and
**index spread**. Twenty-two parameters at `kSchemaV3`, all appended, all
neutral by default.

**Split is deferred to F7**, and the reason is not scheduling. A Linkwitz–Riley
crossover summed straight back together is an allpass, not an identity — so
until the vowel lane and the mangle chain exist to sit in the high band, a Split
control would cost phase and buy nothing, and it would be a dead knob on the
panel in the meantime. It unparks the moment F7's chain lands, which is the
first time there is anything for the two bands to be different about.

Three things the implementation decided:

- **The unison amounts are global, not per-copy offsets.** Each voice knows
  which copy of the stack it is and works out its own share every control
  chunk, so turning Detune up moves copies that are already sounding. Sonitus
  had the other arrangement and shipped a bug where the spread did not apply
  until the detune knob happened to move.
- **One copy carries the sub.** Eight unison voices each adding a sub is eight
  detuned oscillators fighting over the one octave that has to be solid. A test
  silences the matrix and asserts the lane is bit-identical at one copy and at
  four.
- **Two more zombie paths, found while wiring it.** The filter envelope shapes
  something that must itself be sounding, and the sub envelope only makes noise
  on the carrying copy while the lane has a level — but both were counted
  towards "is this voice still doing anything". A patch with a short operator
  release and a long sub release kept silent voices alive for the difference.
  Same shape as the Sonitus zombie, invisible to every silence-based test, so
  the assertion is on the voice **count** (CLAUDE.md §7).

**And one measurement that changed a test rather than the code.** The 1/√n
unison compensation looked broken: eight copies measured **2.56×** the level of
one. They were not incoherent. Every copy starts at the same phase, so at the
onset they sum coherently, and they drift apart over roughly a second:

| window | 5 cents | 15 cents | 40 cents |
|---|---|---|---|
| 85 ms | 7.91 / 8 | 7.25 / 8 | 4.98 / 8 |
| 500 ms | 5.80 / 8 | 3.47 / 8 | 2.24 / 8 |
| 2000 ms | 3.03 / 8 | 3.03 / 8 | 2.84 / 8 |

(raw sum factor with the 1/√n divided back out; 8 is fully in phase, √8 = 2.83
is fully random). So 1/√n is exactly right for the steady state — 1.07× at two
seconds — and the loud onset is what makes a detuned stack punch. Flattening it
with 1/n would leave the sustain 8 dB quiet. The test asserts both ends.

### The rig freeze, 2026-09-04 — and what it cost to find

The user played an F3 build (`eaf72bb`), where the index cap defaulted to
**Soft**, and reported: *"when i play some neuro growl preset and adjust the
operator 1 knob, the CPU usage slams past 100"*, and then *"it seems to happen
mostly when i adjust the knob around a low value, but it is hard to test
properly as it freezes FL when i do"*.

It was not oversampling, which was the obvious suspect. It was the index cap,
and the "low value" detail was the whole diagnosis:

| what | measured | after |
|---|---|---|
| `fm::feedbackOrder` at `beta < 1` rad | **30–55 ms** per call | 5.3 ns (tabled) |
| `fm::feedbackOrder` at `beta >= 1` rad | 0 — saturating early-out | unchanged |
| `fm::significantOrderExact` at index 64 rad | **5.7 ms** per call | **1.0 us** |
| one `FmBandwidth::indexScaleFor` with any feedback | **2.0–2.8 s** | **4.6 us** |
| how often the engine resolved it | per voice per 32-sample chunk | per voice per 16 chunks |

Two traps worth keeping written down:

- **The cost hid on the cheap-looking side of a boundary.** `feedbackOrder`
  returns immediately at or above 1 radian and walks 512 orders below it, so a
  patch with *more* feedback was free and one with less was not — which is
  exactly what "around a low value" meant.
- **And the bisection dragged every patch onto the slow side anyway.**
  `indexScaleFor` scales every index down as it searches, so a beta set above
  the boundary crosses below it partway through and pays the full walk. The
  1.885-radian case measured 2.8 s, the *slowest* of the four.

The fixes, in order of how much they were worth:

1. **`dsp::besselJLadder`** — Miller's downward recurrence, so
   `significantOrderExact` gets every order in one O(n) pass instead of an
   O(n) integral per order. Agrees with `besselJn` to **2.232e-15** over the
   whole range Stryda uses.
2. **`fm::feedbackOrderExact` bisects** rather than counting, which is the same
   answer because the Kapteyn coefficients fall monotonically in `n` for
   `beta < 1` — checked at 99 × 512 points, zero violations, and asserted
   against the counting form in the suite.
3. **Memo tables** for both at the default threshold, rounded **up** so the
   bound never errs low. Built once per process, 3.5 ms and 78 ms, warmed in
   `StrydaProcessor`'s constructor so no audio callback ever pays for them.
4. **A sample-counted cap sub-grid**, `StrydaEngine::kCapChunks = 16`. Counted
   in control chunks rather than callbacks, so the output stays independent of
   the host's buffer size — asserted both ways.

**Four tests, each seen red first.** The wall-clock one was written with a 1.5x
bound, and resolving the cap every chunk again measured 1.30x and sailed
through it; the bound is 1.10x now against a baseline of 0.84/0.85/0.87. The
cost test in `test_FmBandwidth.cpp` was worse — a single loose bound passed with
the table deliberately bypassed — and is now two bounds that each fail when the
thing they name is removed. A neutrality test for F5's filter was a decoration
for the same reason and was rewritten around the skip boundary.

**And the control the user actually asked for.** *"we need the option to
disable oversampling just like the other synths we made"* — it was built in F3
and never attached: `HeaderBar::attachSuiteControls` was simply not called. OS,
RENDER and the master trim are in the header now, with the live tooltip that
says what Auto is doing at the session's rate.

**Three things the measurement decided, and one it corrected:**

1. **The house Auto policy is right, and it is not sufficient on its own.** x4
   at 48 kHz holds a hard patch (index 16 rad, ratio 7) at −114.6 dB, comfortably
   inside section 7's −60 dB. But at index 64 rad even x4 reads **+6.2 dB** —
   the aliasing is louder than the signal — and only x8 recovers. Oversampling
   alone cannot cover the top of the index range, which is what makes the index
   cap a requirement rather than a convenience.
2. **The feedback model saturates exactly where it should.** Above beta = 1
   radian the measured edge is Nyquist itself at every value up to the 6.28
   radians `Oscillator::kMaxFeedback` allows, so "wide, and no longer predicted"
   is the literal truth rather than a hedge. The operator stays bounded at 1.000
   throughout, so section 7's undefeatable bound holds.
3. **The paper's ModFM claims hold, with one number of ours that differs.** The
   partial-order reversals fall from 2 to 0 across Character, which is exactly
   the "smoothly from one to the next, without the unexpected appearance of
   insignificant partials" the DAFx-08 paper describes. The paper also says
   ModFM needs "a distortion index 50 % higher" for matched steady-state
   brightness; measured here by spectral centroid at index 5 it is **+122 %**.
   Both can be true — theirs is a listening judgement at their operating point,
   ours is a centroid at ours — and the figure quoted anywhere in this project
   is ours, with the metric named.

**What F1 corrected in the design**, found by measurement and not by review:
`significantOrder` first compared `|J_n(I)|` against a level relative to
**unity**, which read the predictor **4 % short** — up to 6152 Hz at 880 Hz,
ratio 7, index 16. FM spreads energy, so the loudest partial at index 16 is
about 0.21 rather than 1, and "80 dB below the peak" is some 13 dB lower in
absolute terms than "80 dB below unity". Normalising by the peak took the error
to zero on every row. Under-estimating is the one direction a bandwidth bound
may not err in.

**Two instrument bugs found before any number was trusted** (CLAUDE.md section
10, "check the instrument before trusting it"):

- `analyseHarmonics` is the wrong tool for an FM spectrum at a ratio like 2:7,
  because the true fundamental — `f_c/2` — **has no energy in it**: the partials
  land on every odd multiple of it and none of the even ones. Dividing by an
  absent fundamental produced alias figures of **+160 and +327 dB**. The
  replacement generates the sideband set from the closed form instead, negative
  frequencies reflected as Chowning describes.
- The decimator's halfband FIR has to fill before its output means anything.
  47 host samples of ramp inside a 65536-point transform is
  `10*log10(47/65536)` = **−31.4 dB** of broadband energy, and it read as a
  *uniform* −31 dB alias floor at every factor and every rate — which looks
  exactly like a real result and would have been quoted as one. The render now
  discards 4096 host samples first.

---

**Measured at F2** (`tezla-tests`, 48 kHz host, x4 internal unless said):

| claim | figure |
|---|---|
| Character 0 against `sin(2 pi phase)`, 48000 samples | **bit-identical** |
| Character 0 against the closed form `sin(wc t + k sin(wm t))` | **0.000e+00** |
| Character 1 against the closed form `e^(k cos - k) sin(wc t)` | **1.665e-16** |
| ModFM peak over index 0..8 cycles (the normalisation) | **1.000000**, never above |
| `5 -> 0` against an explicit instantaneous reference | **2.220e-16** |
| `0 -> 5` against an explicit one-sample-late reference | **2.220e-16** |
| 64-, 97- and 512-sample blocks, fixed patch | **bit-identical** |
| chunk countdown after 2048 samples at three block sizes | **0 / 0 / 0** |
| index cap when off, and when on but not binding | **bit-identical output** |
| index scale, ratio 11 at index 6: C7 / C2 | **0.0116** / **1.0000** |
| voice retirement one second after note-off, sustain 0 | **0 active** |
| 8 voices, 6 operators, x4, classic FM | 47-57 % of a core (container, noisy) |
| 8 voices, 6 operators, x4, half ModFM | 41-50 % of a core |
| idle instrument | **1.0 %** |

**Three tests were decorations, and the break-checks are what said so.** Each
passed while the thing it claimed to cover was removed:

1. The Character-0 sine test survived deleting the branch that skips the
   exponential, because `std::exp (0.0 * finite)` is 1.0 anyway. Replaced by a
   test that drives the ModFM inputs to **infinity** -- where `0.0 * inf` is NaN
   -- and asserts the classic operator is still bit-exactly a sine.
2. The matrix-ordering test survived making both directions read the current
   sample, because **`outputs_` already holds the previous value** for an
   operator that has not run yet. That is not only a test bug: it meant the
   `previousOutputs_` / `previousQuadratures_` / `previousGains_` copies were
   redundant, three six-element array copies per sample for nothing. Removing
   them left the audio bit-identical. Replaced by a test that compares each
   direction against an *explicit* reference built with and without a one-sample
   delay -- 2.220e-16 both ways.
3. The block-size test survived cutting the render loop at the block boundary,
   because with nothing changing between chunks both cuts produce the same
   samples. Replaced by a test on the mechanism: the chunk countdown after a
   fixed number of samples must not depend on the route taken. Broken, it reads
   **-224 / -2016 / -12** at block sizes 64 / 512 / 97.

**An optimisation that measured worse, recorded so it is not tried again.**
Classic FM measures *slower* than ModFM here -- 47-57 % against 41-50 % of a
core, repeatably over ten interleaved runs -- which is the opposite of what an
extra exponential and cosine predict. The likeliest cause is `std::sin` argument
reduction: at full tilt the phase reaches tens of radians. Wrapping the argument
with `std::floor` first is mathematically exact and bit-exact at neutral, and it
made **both** figures worse. Reverted; the cause is recorded as unconfirmed.

---

**Risks held open**, to be answered by measurement rather than argument:

1. **Answered at F3, and the answer is no.** The matrix recursion is exact for
   a two-operator pair and a loose upper bound for a stack: measured at 440 Hz
   against the rendered −80 dB edge, it over-estimates by **2.3× at depth 2 and
   19.1× at depth 3** (table in `FmBandwidth.hpp`, assertion in
   `the_predictor_is_an_upper_bound_at_every_stack_depth`). The cause is
   structural — each stage multiplies the modulator's *whole top* by the
   sideband order, and a spread modulator has already had that widening applied
   once, so the factor compounds by roughly 2π per stage.
   **What was done:** the bound is kept, because conservative is the safe
   direction for a cap; the panel labels it an upper bound rather than a
   reading; and the index cap now defaults to **Off**, so a loose bound does not
   clamp patches that never needed it.
   **Still open:** tightening it. Composing in frequency *deviation* (Carson's
   `D + W`) with the Bessel widening applied once at the end is the right shape;
   both obvious forms were tried against that table and one under-estimated at
   depth 2, which is the one direction a bound may not err in. Work, not a
   guess.
2. The `exp` cost per operator at 8 voices — F2's budget decides whether
   Character needs a coarse quantisation to hit a fast path.
3. Whether Scale-mode ratios stay musical at wide detunes.
4. Whether six operators at ×4 fit the CPU budget on the rig. Only the rig can
   answer that, and F3 is when it is asked.

**Measured at F3:**

| claim | figure |
|---|---|
| Steinberg validator on the bundle | **47 of 47** |
| predictor vs a rendered stack, depth 1 / 2 / 3 | 1.0× / 2.3× / 6.9× over (19.1× at the deepest case) |
| parameters | 101, all at `kSchemaV1` |
| plugin build | warning-clean on GCC |

**Three defects the editor screenshot caught**, none of which a unit test would
have: the operator strips left a third of the window empty; every value read
`0.99999...`, because a skewed range round-trips its default through the 0–1
normalisation and JUCE printed it in full — fixed at the parameter, so the
host's automation lane reads correctly too; and the bandwidth readout was blank
until audio ran, which made the panel's most important number invisible with the
transport stopped. It is computed on the message thread now.

---

**Measured at F4:**

| claim | figure |
|---|---|
| `PhaseShaper` at amount 0 against its input, 100000 phases | **bit-identical** |
| phase map monotonic and in range, amount 0→1 in 0.01 steps | holds everywhere |
| Fold 0 → 1: harmonic energy above the 4th | −300 → −34.8 → −28.1 → −13.4 → **−10.4 dBc**, monotonic |
| formant placement, magnitude-weighted centroid, 10 combinations with ≥8 harmonics of room | **≤20 cents**; 0 at n = 21.8, +3 at n = 10.9, +20 at n = 8.2 |
| formant peak over depth 0→16 cycles | **0.998**, never above full scale |
| key scaling two octaves above the break at −0.5 / 0 / +0.5 | 0.5001 / 1.0001 / **2.0002** |
| break point moved while both depths are flat | **bit-identical output** |
| parameters | 155, the new 54 at `kSchemaV2` |

**What the formant operator taught, and the vowel lane will need.** The two
carriers place the resonance between adjacent harmonics, and the centroid is
exact — until the exponential's skirt reaches below harmonic zero. Those
sidebands **fold through DC and add to their positive twins** (Chowning's
reflection, in a new place), which breaks the symmetry and drags the centre
sharp: at a depth of 0.5 cycles the formant needs about **eight harmonics of
room**, and ten to be exact for any purpose. That is a property of the
technique, not of this implementation, and it is why a vowel stops sounding
like a vowel on a very high note.

**The instrument was wrong twice before the number was.** Measuring the formant
by its **strongest bin** read 153 cents flat at 220 Hz — because with two
carriers crossfaded 0.545/0.455 the peak simply snaps to the louder one. That is
the measurement snapping, not the formant moving. Measuring by the
**whole-spectrum centroid** then read 312 cents sharp, because the skirt is wide
in absolute Hz and the spectrum is not symmetric in log frequency. The windowed
centroid is the instrument that answers the question actually asked.

**Deferred from F4, deliberately: the exponential-FM cell switch.** The DAFx-11
criterion is implemented and tested (`fm::exponentialBandwidthHz`), so the risky
half is done; what is not built is the operator mode and the per-cell switch.
It is the least musical of F4's four items — a novelty flavour rather than
something the brief asked for — and F4 was already the largest parameter
addition in the plugin. Roadmap, with the criterion waiting for it.

---

**Parked deliberately** (in `docs/ROADMAP.md`, each with an unpark condition):
higher-order FM as a seventh character; a resample/freeze lane; per-operator ADV
envelopes; an eighth and ninth operator; Sonitus and Svarayantra adopting
`FmBandwidth`'s readout.
