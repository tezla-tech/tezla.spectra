# Syrinx — the vocal channel strip

**Code `Tzsy` · "Tezla Syrinx" · `tech.tezla.Syrinx` · effect.**

The syrinx is the voice: the organ a bird sings with, and the nymph Pan chased
into the reeds he then made his pipes from. It keeps the anatomical line
already running through Malleus (the hammer) and Anvil (the incus).

## The thesis

This suite owns both **ends** of a vocal chain and none of the middle.
Character is covered three times over — Emberdrive, Ferrite, Anvil. The
ceiling is Capstone's. The checking is Transpectus's. Between the microphone
and the limiter there is **nothing**: no gate, no de-esser, no compressor. The
only dynamics primitive in `shared/` is `GainComputer`, and it is a limiter
with a soft knee and no ratio control at all.

Syrinx is that middle, as **one strip** rather than five plugins, because a
vocal chain is genuinely a chain: the stage order, the gain staging between
stages, and one preset set that makes them work together are the deliverable.

Deliberately **not** here, because the suite already does them better:
saturation, air, limiting, metering. `docs/VOCAL-CHAIN.md` (V7) documents
exactly where those go around it. Pitch correction is ruled out permanently.

## The chain

Fixed order, which is the point of a strip:

```
in → trim → HPF → GATE → DE-ESS → COMP 1 (level) → COMP 2 (peak) → EQ → trim → out
```

- **HPF first**: proximity and rumble should never reach a detector.
- **Gate before de-ess**: no reason to analyse sibilance in room tone.
- **De-ess before compression**: a compressor that ducks on an "s" makes the
  sibilance *louder* relative to the word after it. This ordering is most of
  why de-essers get blamed for lisping.
- **Two compressors**: a slow low-ratio leveller then a fast peak catcher is
  how vocals are actually compressed, and two instances of one class is less
  code than one clever one.
- **EQ last**: tone-shaping after the dynamics, so it is not fighting them.

Defaults and presets aim at **rap first**, with sung presets included.

## The distinctive piece: sibilance is a ratio, not a level

Most de-essers threshold the absolute level of a high band. That fails twice:
a loud passage re-triggers it, so it over-esses when the singer pushes; and a
bright vowel triggers it, so it lisps.

An "s" is not *loud HF* — it is **HF energy high relative to the body of the
voice**. So the detector compares two fast RMS followers and thresholds their
ratio:

```
sibilance_dB = 20 log10( rms(5 kHz and up) / rms(200 Hz .. 3 kHz) )
```

which tracks the singer getting louder without re-triggering, and which a
bright-but-not-sibilant vowel never crosses.

The reduction is applied **subtractively**, which is what makes section 7's
bit-exact bypass hold through a crossover that does not sum to unity:

```
out = x - (1 - g) * highBand      g == 1 -> subtract exactly 0.0 -> out == x
```

and it also leaves the body band untouched *by construction*, so "it does not
lisp" becomes a bit-exact assertion rather than a hope.

## What is reused rather than rebuilt

| piece | path | for |
|---|---|---|
| `EnvelopeFollower` | `shared/.../EnvelopeFollower.hpp` | attack/release **and program-dependent release, already correct** — its slow path attacks slowly too, which is the part usually got wrong |
| `GainComputer` | `shared/.../GainComputer.hpp` | the soft-knee curve, **generalised with a ratio in V1** |
| `LinkwitzRiley4` | `shared/.../Crossover.hpp` | the de-esser's band split |
| `Biquad` + `design::` | `shared/.../Biquad.hpp` | HPF, the EQ bands, every sidechain filter |
| `SmoothedValue`, `DcBlocker`, `Exact`, `Denormals` | `shared/.../` | the usual |
| `BypassMixer`, `ui::HeaderBar`, `ui::LevelMeter`, Ferrite's `ControlPage` | shared + `plugins/Ferrite/Source/` | bypass, panel, meters, page grid |

**No external sidechain bus.** No plugin here has one, FL Studio's routing for
it is idiosyncratic, and every detector in this strip wants the vocal itself.
Internal sidechain *filters* are in; an external input is out of scope for v1.

## Phases

Each phase is one commit: tests written and run in the same commit, every
mechanism seen red (or break-checked), numbers quoted, whole tree built, "the
qemu-aarch64 cross-check was not run" noted per section 2.3.

- **V0 — this file**, the registry claim, and the DSP-REFERENCES rows —
  including the Reiss & Giannoulis row that `GainComputer` has cited in a
  comment since it was written but that the references file never recorded.
- **V1 — `GainComputer` gains a ratio.** The standard soft-knee compressor
  curve reduces *exactly* to the present limiter when 1/ratio = 0, so the
  default stays infinite and **Capstone's output must be bit-identical**.
  Tests: 4:1 above the knee is exactly 4:1; the knee is continuous in value
  and slope at both corners; infinite ratio is bit-exact against the current
  curve across a swept level range; Capstone's own tests pass untouched.
- **V2 — `SibilanceDetector.hpp` + `DeEsser.hpp`.** The ratio measure, the
  split-band subtractive reduction, a Listen mode. Tests: a vowel swept
  -40 -> 0 dBFS produces **no** reduction at any level; an "s" at those levels
  produces the **same** reduction at every level (break-check by swapping in a
  plain HF threshold and watching the figure track the level); the body band
  is bit-exact through a de-ess event; zero reduction is bit-exact identity.
- **V3 — `CompressorCore.hpp` (shared) + `Gate.hpp`.** Compressor: detector ->
  ratio'd `GainComputer` -> `EnvelopeFollower` -> makeup -> parallel mix, with
  a sidechain HPF. Gate: threshold **with hysteresis**, attack/hold/release,
  and a Range that attenuates rather than mutes. Tests: measured ratio matches
  the asked ratio, pinned per ratio; parallel mix at 0 is bit-exact dry; the
  gate does not chatter on a signal sitting exactly on the threshold
  (break-check by removing the hysteresis and counting transitions); Range is
  exact; makeup at 0 dB is bit-exact.
- **V4 — `SyrinxEngine.hpp`.** The whole chain, gain staging, per-stage
  gain-reduction readouts. Tests: **every stage neutral is bit-exact identity
  end to end** (section 7, and the reason the de-esser is subtractive);
  block-size independence at 64 against 512 with every stage working; silence
  in -> silence out exactly; CPU measured.
- **V5 — JUCE layer.** Schema-v1 parameters, choice lists append-only from
  birth, state, and presets: *Rap Lead* · *Rap Ad-lib* · *Rap Double* ·
  *Aggressive Forward* · *Sung Verse* · *Sung Chorus* · *Gentle Leveller* ·
  *De-ess Only* · *Gate Only* · *Neutral* (bit-exact).
- **V6 — editor.** House panel with its own accent, laid out **as the chain**:
  the stages left to right in signal order, each with its own gain-reduction
  meter, so it is visible which stage is doing the work. The de-esser gets the
  identity display: the sibilance ratio against its threshold, over time.
- **V7 — close-out.** `tezla-measure syrinx`, `plugins/Syrinx/README.md`,
  registry flip, validator 47/47 on all ten, and **`docs/VOCAL-CHAIN.md`**:
  signal order with the existing plugins around Syrinx (Emberdrive multiband
  on the body for grit without fizz, Halo for air, Ferrite for glue,
  **Capstone last**, Transpectus to check), with concrete starting settings
  for rap and for singing, and honest notes on where each is the wrong tool.

## Risks

- **Changing `GainComputer` touches Capstone.** Mitigated by the default being
  infinite ratio, by the reduction being exact rather than approximate, and by
  a test pinning Capstone's curve bit-for-bit across a swept range. If it
  cannot be made bit-exact, V1 becomes a separate `CompressorCurve` and
  `GainComputer` is left alone.
- **The de-esser is the hard part**, as the bow was for Malleus. The fallback
  is shipping the strip with gate + compressors + EQ and landing the de-esser
  as its own later phase. The level-independence test is the gate that decides
  whether it works, and it runs early (V2).
- **A strip is a lot of controls.** Mitigated by rap-first defaults that are
  usable before anything is touched, and by an editor that shows which stage
  is actually working.

Sonitus P4 and Prism remain parked. This plan does not touch them.

## Continuity — how any session resumes this work

This section is the handoff. It is updated **in the same commit as each
phase**, so whichever assistant session picks the work up — after a context
loss, a model change, or a fresh clone — needs nothing beyond this file and
CLAUDE.md.

**Phase status** (flip `pending` -> `done` in the phase's commit):

| phase | status |
|---|---|
| V0 plan + registry + references | done |
| V1 GainComputer ratio | done |
| V2 SibilanceDetector + DeEsser | done |
| V3 CompressorCore + Gate | pending |
| V4 SyrinxEngine | pending |
| V5 JUCE layer + presets | pending |
| V6 editor | pending |
| V7 close-out + VOCAL-CHAIN.md | pending |

**To resume**: read CLAUDE.md in full, then this file; take the first
`pending` phase. The non-negotiables that every phase here honours, in one
place:

- One phase = one commit. Tests written and RUN in that commit; every
  mechanism seen red first or break-checked (edit -> red -> revert), with the
  measured numbers pinned in the test comments and quoted in the commit
  message.
- Build the whole tree before pushing (`./scripts/build.sh NONE --test` or the
  cmake equivalent with no `--target`), run all tests, and run Steinberg's
  validator on any plugin whose bundle changed.
- The qemu-aarch64 cross-check is **deliberately not run** (CLAUDE.md 2.3
  gate); say so in every commit message rather than implying coverage.
- New first-party files carry the six-line licence header copied from a
  neighbour. No model identifiers in anything pushed. The commit footer comes
  from the harness — use whatever the current session mandates.
- Derive DSP and measure it; anything taken from a source is attributed at the
  point of use AND in `docs/DSP-REFERENCES.md` (CLAUDE.md section 9). Setters
  that clear or re-aim state carry no-op guards (`dsp::isExactly`). Continuous
  parameters are smoothed. Silence in -> exact zeros out. Neutral settings are
  bit-exact, not merely transparent.
- The prior art to copy patterns from: Malleus (`plugins/Malleus/`) for the
  phase discipline and the PLAN/Continuity shape, Capstone
  (`plugins/Capstone/Dsp/CapstoneEngine.hpp`) for a dynamics engine, Ferrite's
  editor for the page grid, Malleus's editor for a custom display.
