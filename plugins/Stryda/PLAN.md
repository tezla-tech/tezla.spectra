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
- **F9** — editor and close-out; validator 47/47 on all fourteen.

---

## Continuity — how any session resumes this work

This section is the handoff, updated **in the same commit as each phase**, so
whichever session picks the work up — after a context loss, a model change, or a
fresh clone — needs nothing beyond this file and CLAUDE.md. **Resume from the
first `pending` row.**

| phase | status |
|---|---|
| F0 plan + registry + references + Ictus pause + roadmap | done |
| F1 `FmBandwidth` + `tezla-measure stryda`, four tables | done |
| F2 `FmOperator` + `OperatorMatrix` + voice + engine + index cap | pending |
| F3 minimal JUCE layer, rig build + ear round | pending |
| F4 `PhaseShaper`, exponential cells, formant operator, key scaling | pending |
| F5 sub lane, Split, per-voice filter, unison index spread | pending |
| F6 microtuning at the ratio, ratio sequencer, named braids | pending |
| F7 vowel lane + mangle chain | pending |
| F8 modulation layer + dice gate | pending |
| F9 editor + close-out | pending |

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

**Risks held open**, to be answered by measurement rather than argument:

1. Whether the matrix recursion over the classic criterion holds for deep
   stacks — F1 table 3 decides, and if it does not, the cap falls back to a
   measured lookup rather than a formula.
2. The `exp` cost per operator at 8 voices — F2's budget decides whether
   Character needs a coarse quantisation to hit a fast path.
3. Whether Scale-mode ratios stay musical at wide detunes.
4. Whether six operators at ×4 fit the CPU budget on the rig. Only the rig can
   answer that, and F3 is when it is asked.

**Parked deliberately** (in `docs/ROADMAP.md`, each with an unpark condition):
higher-order FM as a seventh character; a resample/freeze lane; per-operator ADV
envelopes; an eighth and ninth operator; Sonitus and Svarayantra adopting
`FmBandwidth`'s readout.
