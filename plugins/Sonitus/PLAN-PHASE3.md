# Sonitus Phase 3 — plan and handoff document

Six features, planned as a sequence of self-contained commits. Written so any
future session can pick this up cold, mid-sequence, without re-deriving the
design: every step names its files, its invariants, its tests, and a **probe**
— a grep that tells you whether that step has already been done. Run the
probes first, work the first unfinished step, tick the box, commit, repeat.

Read `CLAUDE.md` before touching anything. The rules that bite hardest here:
§2.3 (x86-64 only — no ARM64, no macOS CI until the gate lifts), §8
(append-only lists, frozen IDs, neutral defaults), §7 (aliasing, DC, zipper,
crossfaded switches), §10 (test in the same commit; a passing test is worth
nothing until seen to fail).

## Status

- [ ] 1. Sub split bypass
- [ ] 2. MOD page reorder + thicker scrollbars
- [ ] 3. Division table extracted to a framework-free header
- [ ] 4. LFO tempo sync (toggle + division per LFO)
- [ ] 5. Envelope snap-to-tempo (amp, mod 1, mod 2)
- [ ] 6. Morph plumbing + shared naive-waveform function (no new shapes yet)
- [ ] 7a. Shapes: Dome + Double saw
- [ ] 7b. Shape: Noise
- [ ] 7c. Shape: Vintage
- [ ] 7d. Shape: Shark
- [ ] 7e. Shape: Harmonic
- [ ] 8. Waveform preview + morph slider UI
- [ ] 9. ADV envelope DSP: delay stage + loop in `Adsr`
- [ ] 10. ADV Envelopes 1–3: params, sources, matrix, ENV page
- [ ] 11. Docs, measurements, validator, final sweep

Baseline at planning time: commit `4c379a1`, **622 tests, 0 failed**, all six
plugins validate 47/47, `scripts/check-editors.sh` green.

## Ground rules for every step

- **Schema.** `PluginProcessor.cpp:20` has `kSchemaV1` only. The first step
  that adds a parameter also adds `constexpr int kSchemaV2 = 2;` and bumps
  `kStateSchemaVersion` to it. Every parameter added by this plan carries
  `kSchemaV2`. Existing parameters keep V1 forever (§8: the version hint feeds
  the VST3 param ID).
- **Neutral defaults.** Every new parameter defaults to today's behaviour, so
  a project saved before this plan reopens sounding identical. Where a step
  can assert that bit-exactly, it must.
- **Append-only.** `OscShape`, `choices::shape`, `ModSource`, `modSource`,
  `GlobalSource`, `globalSource`, `ModDestination`, `modDest`, the division
  table — all indexed by stored values. New entries go at the end, before
  `count`; every touched enum's `static_assert` block in
  `plugins/Sonitus/Source/PluginProcessor.h` (lines ~229–297) is updated in
  the same commit.
- **Validate each commit**: `./scripts/build.sh NONE --test` (whole tree, not
  named targets), `./build/bin/tezla-measure selftest`, and for editor-facing
  steps `cmake --build build-plugin -j` + `./scripts/check-editors.sh` +
  Steinberg validator on Sonitus
  (`/home/user/vst3sdk/build-val/bin/Release/validator`, under `xvfb-run -a`
  in the container). Break-check every new test.
- Screenshots for UI steps:
  `xvfb-run -a build-plugin/plugins/Sonitus/SonitusRender_artefacts/Release/SonitusRender editor shot:out.png`
  (`size:WxH` to photograph other sizes, `hit:<id>` to prove a control is
  reachable, `<id>` to click it, `state:<name>` to read a non-param property).

---

## 1. Sub split bypass

**Probe:** `grep -n "subSplit" plugins/Sonitus/Source/PluginProcessor.h` —
hits mean done.

The user wants a "pure" mode: no crossover, no sub mono-ing, so they can split
on the DAW mixer bus instead. Today the engine always splits
(`SonitusEngine.cpp:480–500`, `LinkwitzRiley4 split_[2]`, `splitHz`,
`subMono`) — the sub leg bypasses the mangle, gets the only DC blocker, and
is mono-summed.

- Parameter `ids::subSplit`, bool, **default ON** (= today), kSchemaV2, name
  "Sub split". It is a switch, so it does NOT join `globalDest` (§8:
  destinations are continuous controls only).
- Engine: `bool subSplit { true }` in `EngineParameters`. When OFF: the full
  signal goes down the body/mangle path, the sub leg is silent, `subMono` and
  the sub DC blocker are skipped — and a **first-order 5 Hz DC blocker goes on
  the output sum instead**, because with the split gone the tube's asymmetry
  has no blocker anywhere and §7 calls DC a defect. First order only; sub bass
  is the point of this instrument.
- **Click-free toggle** (§7: discrete switches use crossfades): equal-power
  crossfade ~30 ms between the two routings. Both paths are computed only
  while a fade is in flight.
- The `isExactly` change-detection in `applyParameters`
  (`SonitusEngine.cpp:119ff`) gains the new flag; follow the `splitChanged`
  pattern.

**Tests** (in `tests/test_Sonitus.cpp`):
- Split OFF + mangle fully neutral (comb off, formant 0, tube 0 dB) ⇒ output
  is the voice sum through nothing but the output DC blocker — assert a null
  against a render with the blocker applied directly, < 1e-12. This is the
  "pure" guarantee the feature exists for.
- Split ON renders bit-exactly what today's engine renders (guard the neutral
  default).
- Toggling mid-note produces no step: max |sample-to-sample delta| across the
  fade bounded by the signal's own slew (reuse the BypassMixer test pattern).
- Silence in ⇒ exactly zero out, both settings.

**Editor:** pill toggle "SPLIT", component id `subSplit`, in the MANGLE page's
split group next to the split-frequency knob. Tooltip states the trade in
plain language: OFF = no crossover colouration and full-stereo sub, for
splitting on a DAW bus; ON = sub stays clean of the mangle and mono. Grey out
the split-frequency and sub-mono controls when OFF.

---

## 2. MOD page reorder + thicker scrollbars

**Probe:** `grep -n "GLOBAL MATRIX" plugins/Sonitus/Source/PluginEditor.cpp` —
done when its `addHeading` call precedes the VOICE MATRIX one.

- **Global above voice.** In the MOD page builder, the voice matrix is built
  at ~line 2096 and the global matrix at ~2118. The user reaches for global
  first. Move the whole GLOBAL MATRIX block (heading + rows + any explanatory
  strip) above the VOICE MATRIX block, as a unit. If the LFO/SEQ groups sit
  between them, keep them attached to whichever matrix they document —
  read the block before cutting. Component ids don't change; take a
  before/after screenshot.
- **Scrollbars.** The main page viewport is `PluginEditor.cpp:1737–1738`
  (`setScrollBarThickness (9)`). Raise to **14** and make the house
  `KnobLookAndFeel::drawScrollbar` (`KnobLookAndFeel.hpp:381`) more apparent:
  visible track rail (panel colour brightened), rounded thumb in
  `palette.accent` at ~0.45 alpha, ~0.8 on mouse-over
  (`bar.isMouseOver()`), minimum thumb length ~28 px. The L&F is shared, so
  any other viewport in the suite inherits; check Sonitus is the only
  scroller with `grep -rn "Viewport" plugins/*/Source/*.h`.

No parameters, no DSP. Verify with `check-editors.sh` and screenshots at
1000×660 and a short window (`size:1000x520`) where scrolling engages.

---

## 3. Division table → framework-free header

**Probe:** `ls shared/tezla-dsp/include/tezla/dsp/Divisions.hpp` — exists
means done.

Steps 4 and 5 need note-division durations inside the engine, which is
framework-free (§4) and cannot include tezla-ui headers (they declare JUCE
types). The house division table already exists —
`shared/tezla-ui/include/tezla/ui/ModulationParameters.hpp:42`, 15 entries,
append-only, stored-by-index in Halo/Emberdrive projects.

- New `shared/tezla-dsp/include/tezla/dsp/Divisions.hpp`: move the `Division`
  struct and `divisions[]` table verbatim (same names, same order — the
  stored indices in existing projects must keep their meaning), plus
  `secondsFor (int index, double bpm)` = `(60/bpm) / cyclesPerBeat`.
- `ModulationParameters.hpp` includes it and keeps `divisionNames()`; Halo and
  Emberdrive compile untouched. A `static_assert` on `numDivisions == 15`
  pins the count at the move.
- Behaviour-change-free: `tezla-render params` dump for Halo before and after
  must diff empty.

---

## 4. LFO tempo sync

**Probe:** `grep -n "lfo1Sync" plugins/Sonitus/Source/PluginProcessor.h`.

Per LFO (both of them): a **sync toggle** and a **division choice**. Tempo is
already plumbed — `engine_.setTransport (ppq, bpm, …)` at
`PluginProcessor.cpp:1121–1123`, stored as `bpm_` in the engine.

- Params (kSchemaV2): `lfo1Sync`/`lfo2Sync` bool default OFF (neutral);
  `lfo1Division`/`lfo2Division` choice over `divisionNames()` (15 entries,
  append-only), default "1 bar" (index 3) — inert until sync is on.
- Engine (`SonitusEngine.cpp:468` area, where `lfo1RateHz` is consumed):
  synced rate = `bpm_/60 · cyclesPerBeat[division]`. Key-track still applies
  after (it multiplies; document that in the tooltip). The 160 Hz ceiling
  clamp still applies last.
- **Phase discipline:** when sync is ON and retrigger is OFF, lock phase to
  the transport — `phase = fmod (ppq · cyclesPerBeat, 1)` — the same pattern
  the sequencer already uses (`PluginProcessor.cpp:1533` comment block).
  Restarting the transport then reproduces the same wobble against the same
  bar, which is the whole reason producers sync LFOs. When retrigger is ON,
  note-on resets phase as today and only the rate is synced.
- **UI:** compact — the LFO group's RATE knob slot swaps: sync OFF shows the
  rate knob, sync ON shows the division combo in the same bounds, with a
  small "SYNC" pill beside it (component ids `lfo1Sync`, `lfo1Division`).
  No second row; the group must not grow.

**Tests:** synced LFO at 120 bpm, "1/4" ⇒ measured period exactly 0.5 s at
every host rate (44.1/48/96/192 k); phase-locked mode reproduces identical
output for identical ppq after a transport rewind; sync OFF renders today's
output bit-exactly. Break-check by mis-scaling cyclesPerBeat.

---

## 5. Envelope snap-to-tempo (amp, mod 1, mod 2)

**Probe:** `grep -n "ampSyncT\|ampSnap" plugins/Sonitus/Source/PluginProcessor.h`.

A per-envelope toggle that snaps the time stages to note lengths, FL-style.

- Params (kSchemaV2): one bool per envelope, default OFF — `ampSnap`,
  and the mod-env equivalents (read the existing `addEnvelope` calls at
  `PluginProcessor.cpp:451ff` for the two mod-env prefixes and follow them).
  Extend `EnvelopeIds` with the snap id so the lambda stays the single path.
- **Where snapping happens: in the pull, not in the UI.** `pullEnvelope`
  (`PluginProcessor.cpp:919ff`) is the single place parameter seconds become
  engine seconds; when snap is on, quantise A, H, D, R there to the nearest
  entry of the duration table `secondsFor (i, bpm)` (all 15 divisions, plus
  half/quarter of the shortest for fast attacks), **except**: a value below
  half the shortest division snaps to itself (an instant attack must stay
  instant — never quantise 0.004 s up to 1/32). Sustain is a level, untouched.
  BPM changes retune the snapped times live at the same control rate the pull
  already runs at; the existing envelope-time smoothing conventions apply, so
  no zipper.
- Snapping in the pull means every writer — knob, EnvelopeEditor drag, preset,
  automation — lands on the grid with one code path, and the graph redraws
  from what the engine actually got. The ENV page shows a small "SNAP" pill
  per envelope (ids `ampSnap` etc.); when lit, each stage's readout in the
  block header shows the division name ("D 1/4") next to the ms value.
- ADV envelopes (step 10) reuse exactly this mechanism; build it so the
  snap-table function is shared, not copied.

**Tests:** snap ON at 120 bpm pulls decay 0.47 s → exactly 0.5 s ("1/4");
0.004 s attack stays 0.004; snap OFF is bit-exact with today; a bpm change
mid-note moves the value without a discontinuity in the envelope output.

---

## 6. Morph plumbing + shared naive-waveform function

**Probe:** `grep -n "morphA" plugins/Sonitus/Source/PluginProcessor.h` and
`grep -n "naiveShapeSample" shared/tezla-dsp/include/tezla/dsp/Oscillator.hpp`.

Foundation for the new shapes: the parameter, the destination, and the single
source of truth the on-screen preview will draw from. **No new shapes in this
commit** — that keeps the regression guard clean.

- `Oscillator.hpp`: add `double morph_ { 0.0 }`, `setMorph`, and extract the
  naive-waveform switch (~line 334, "The naive waveform, before any
  correction") into a **public static**
  `naiveShapeSample (OscShape, double phase, double width, double morph)`,
  which the member path calls. One definition serves DSP, tests, and the
  step-8 preview — a preview drawn from its own copy of the math would drift
  from the sound.
- **The four legacy shapes ignore morph entirely.** That is a design
  decision, not an omission: saw/pulse/triangle/sine are frozen for project
  compatibility, and their morphable descendants are the new shapes (Shark
  spans triangle→saw, Dome spans sine→pressed, Double saw spans saw→comb).
  Pulse keeps Width as its tweak, as today.
- Plumb `morphA`/`morphB` (kSchemaV2, 0–100 %, default 0) through
  `UnisonBank::setMorph` → every unison voice, exactly as `setWidth` flows
  today (`UnisonBank.hpp`; remember the `setSpread` lesson — any derived
  state recomputes inside the setter, behind an `isExactly` no-op guard).
- Mod destinations: append `morphA`, `morphB` to `ModDestination`
  (`SonitusVoice.hpp:113`, after `kargyraa`, count 17→19) and "Morph A",
  "Morph B" to `choices::modDest`; update the static_assert. Smoothed at the
  voice's existing control rate like the other destinations.

**Tests:** with morph at 0 **and** at 1, all four legacy shapes render
bit-exactly what they render today (the ignore is total); `naiveShapeSample`
agrees with the member path across shapes/phases to 1e-15. Break-check by
making sine read morph.

---

## 7. The new shapes

Append to `OscShape` (after `sine`, before `count`) and to `choices::shape`
in this order — the order is frozen the moment it ships:

| enum | name | character | morph (0 = canonical) | antialiasing |
|---|---|---|---|---|
| `dome` | Dome | pressed sine → rounded pulse | exponent k = 1 + 15·morph | band-limited **by construction** |
| `doubleSaw` | Double saw | saw → moving comb | phase offset of ramp 2 | two existing BLEP saws |
| `noise` | Noise | white → dark noise | one-pole colour 20 kHz→200 Hz | not applicable |
| `vintage` | Vintage | analogue saw, rounded reset | RC curve depth | BLEP step (+ BLAMP if measured necessary) |
| `shark` | Shark | triangle → saw skew | peak position 0.5 → edge | BLAMP corners, slope-scaled |
| `harmonic` | Harmonic | additive drawbar | roll-off exponent p = 1 + 2·morph | finite series + Nyquist fade |

Per shape, the same drill: implement in `naiveShapeSample` + the corrected
path, extend the shape-dependent switches (`Oscillator.hpp:256` sync step
direction, `:337` dispatch — grep for every `switch (shape_`), add the
tooltip line to the editor's shape combo, run the **swept non-divisor
aliasing probe** at max drive through the voice (the Anvil method — a probe
that divides the host rate is blind, `test_Anvil.cpp` shows the trap), pin
the number in a test, and break-check.

Specifics worth pre-deciding:

- **Dome** rides the identity already proven in the kargyraa work:
  `(0.5 − 0.5 cos φ)^k` has exactly k harmonics, so clamp k to
  `0.45 · internalRate / f0` and there is **no aliasing at all** — assert
  < −120 dB, not −60. Continuous k via `exp(k·ln x)`; remove the analytic DC
  and renormalise the peak; assert |DC| < 1e-12 across the morph range.
  This shape ships first because it is provable.
- **Double saw**: `0.5·(saw(φ) + saw(φ + m))`, m = morph·0.5. At morph 0 the
  ramps align — canonical saw. Second ramp gets its own BLEP correction and
  its own sync reset. Modulating morph is a one-oscillator flanger; that is
  the point of it.
- **Noise**: per-oscillator PRNG seeded from the unison scatter path (grep
  `scramble (seed` in `SonitusVoice.hpp:360`) so unison voices decorrelate
  and spread pans them wide. Frequency, PM, sync, detune and drift have no
  effect — the tooltip says so plainly rather than letting the user hunt.
  Aliasing spec does not apply (broadband by intent — the §7
  bitcrusher-exception pattern); test the morph tilt and flatness instead.
- **Vintage**: ramp through an RC discharge curve
  `2·(1−e^{−a t})/(1−e^{−a}) − 1`, a = 1.5 + 6·morph, mean-corrected
  analytically. The reset step keeps the saw's BLEP (−2.0 step); measure
  before adding a slope correction — if the sweep already clears −60 dBFS at
  the voice's Auto oversampling, BLAMP is not earned.
- **Shark**: peak position p = 0.5 → clamped near the edge so the falling
  segment stays ≥ 2 samples at the current increment (the `kMinimumWidth`
  pattern, frequency-dependent). Read how triangle is corrected today and
  reuse; both corners take slope-scaled BLAMP. This is the fiddliest one —
  it comes after the wins are banked.
- **Harmonic**: partials n = 1..16 at n^−p via the Chebyshev/cosine
  recurrence (`cos nφ = 2 cos φ · cos (n−1)φ − cos (n−2)φ`), each partial
  fading 1→0 as `n·f0` crosses 0.40→0.48 of the internal rate so pitch
  modulation cannot pop a partial in or out. Measure CPU with unison 7 —
  if it is heavy, drop to 12 partials before dropping the shape.

Sync-slave semantics for each new shape must be defined in the `:256` switch
(what the post-reset step direction is); noise returns 0 there.

---

## 8. Waveform preview + morph slider UI

**Probe:** `grep -n "WavePreview\|waveA" plugins/Sonitus/Source/PluginEditor.cpp`.

The user's ask: a small graphic of the shape, and Surge-style tweak sliders,
compact.

- `WavePreview` component (etch in `PluginEditor.cpp` beside the other custom
  components): ~64×28 px, draws one cycle — 128 evaluations of
  `Oscillator::naiveShapeSample` with the oscillator's current shape, width
  and morph — stroked in the page accent over a panel-dark ground, centre
  line at 0. Noise draws a fixed-seed buffer so it doesn't shimmer on
  repaint. Listens to the three parameters and repaints on change; no timer.
  Component ids `waveA`, `waveB`.
- **Morph slider**: small horizontal `juce::Slider` (LinearHorizontal, no
  text box, ~70×14) directly under each SHAPE combo, ids `morphA`/`morphB`,
  label "MORPH" in the dim text colour. Value readout lives in the tooltip
  and the drag popup, not a text box — compactness is the brief.
- **Honest greying**: the morph slider disables for the four legacy shapes;
  the WIDTH knob disables for every shape but Pulse. Both re-evaluate when
  the shape combo changes (there is precedent — the editor already reacts to
  shape changes; grep `updateForGenerator` in Halo for the pattern).
- Layout: the OSC group's shape row gains preview + slider in the existing
  `sameRow` machinery; the group's height budget must not grow — steal from
  the combo's width, not from a new row. Screenshot at 1000×660 and the
  minimum size; `hit:waveA hit:morphA` in the editor checks.

`scripts/check-editors.sh` gains a Sonitus line: click nothing, but
`hit:morphA`, `hit:subSplit`, `hit:lfo1Sync` prove reachability.

---

## 9. ADV envelope DSP: delay + loop in `Adsr`

**Probe:** `grep -n "setDelaySeconds\|setLoop" shared/tezla-dsp/include/tezla/dsp/Adsr.hpp`.

The ADV envelopes are ordinary `Adsr`s plus two abilities, added to the class
(so mod env 1/2 could use them later) and switched off by default:

- **Delay**: a silent stage before attack, 0–4 s. Append a `delay` stage to
  `AdsrStage` (append, like `hold` was — the enum is runtime state but the
  house rule is uniform). `noteOn` enters delay when the time is > 0.
- **Loop**: `setLoop (bool)`. When looping and decay completes (arrival at
  sustain, honouring `startedDecayAbove_` direction logic), re-enter attack
  **from the current level** — the segment-from-current-level machinery the
  tension rework already built. `noteOff` exits to release from wherever it
  is, loop or not. Loop OFF is today's behaviour, bit-exact.
- Loop + snap (step 5) compose: a looped AHD with snapped times is a synced
  rhythmic modulator — that combination is the FM8-style payoff, say so in
  the tooltips.

**Tests:** delay 0 + loop off ⇒ bit-exact against today across a random
parameter sweep; loop period = A+H+D exactly; loop with release-during-loop
releases from the mid-loop level without a step; silence…
`tests/test_Adsr.cpp` already has the harness patterns. Break-check the loop
by making it re-enter from zero (the step it must not produce).

---

## 10. ADV Envelopes 1–3: params, sources, matrix, page

**Probe:** `grep -n "adv1Enable" plugins/Sonitus/Source/PluginProcessor.h`.

Three extra per-voice envelopes, off until enabled — extra modulation control
in the spirit of FM8's extra envelopes (workflow inspiration only; nothing is
copied from NI — §2.1).

- Params per envelope (kSchemaV2), prefix `adv1`/`adv2`/`adv3`: `Enable`
  (bool, **default OFF**), `Delay`, `Attack`, `Hold`, `Decay`, `Sustain`,
  `Release`, `AttackT`, `DecayT`, `ReleaseT`, `Loop` (bool), `Snap` (bool).
  Reuse the `EnvelopeIds` struct + `addEnvelope` lambda
  (`PluginProcessor.cpp:370–382`) extended for the four new fields — one
  path, not a second copy.
- Voice (`SonitusVoice.hpp`): three more `Adsr` members. **Disabled costs
  nothing**: skip their `process` and `noteOn` when disabled; the matrix
  reads 0 from a disabled source. The 32-voice ceiling argument
  (`VoiceManager.hpp:77ff`) rests on idle voices being free — keep it true
  for idle envelopes too, and extend the `tezla-measure sonitus` CPU section
  with an "ADV ×3 enabled" row so the claim is a number.
- Sources, append-only, four lists + asserts:
  `ModSource` gains `advEnv1..3` (count 10→13), `choices::modSource` gains
  "ADV 1/2/3"; `GlobalSource` gains the same (count 8→11; the global side
  follows the loudest-note convention the existing envelopes already use —
  read the comment at `SonitusEngine.hpp:126`), `choices::globalSource`
  likewise. Update both static_asserts.
- ENV page: three collapsed rows under the existing three editors. Disabled =
  one compact strip (name + ENABLE pill, ids `adv1Enable`…); enabling swells
  it into a full `EnvelopeEditor` block with DELAY/LOOP/SNAP pills. The page
  scrolls (step 2's scrollbars are why that order). `getPreferredHeight`
  (`PluginEditor.cpp:1283`) must account for expanded state.

**Tests:** all-disabled renders bit-exact with pre-step-10 output; an enabled
ADV env routed to cutoff modulates it; loop+snap period is exact against
bpm; state save/load round-trips the enables.

---

## 11. Close-out

- `tezla-measure sonitus`: new-shape aliasing table (worst inharmonic per
  shape at max drive, swept probes), ADV CPU row, synced-LFO period check.
- README (`plugins/Sonitus/README.md`): shapes table with morph meanings,
  ADV envelopes section, sync/snap section, split-bypass note ("pure" path),
  updated parameter list. `docs/DSP-REFERENCES.md` if any source was
  consulted for the shape math (the Dome identity is already recorded from
  the kargyraa work).
- Full sweep: `./scripts/build.sh NONE --test` (expect 622 + new, 0 failed),
  build-plugin all six, `check-editors.sh`, validator 47/47 on **all six**
  (shared-header and L&F changes touch everyone), screenshots of OSC, ENV,
  MOD pages attached to the report.
- State plainly in the commit/report: **the ARM64 cross-check was not run**
  (§2.3).

## Cut lines, in order, if scope presses

1. **Harmonic** shape (most CPU risk, least unique character).
2. **Shark** (fiddliest antialiasing; Dome+Double saw already cover morphable
   classic timbres).
3. ADV **Snap** pills (ship ADV envelopes unsynced; snap lands with step 5's
   machinery later).
4. The division-name readout inside envelope headers (tooltip-only is
   acceptable for v1).

Never cut: the bit-exact regression guards, the neutral defaults, the
aliasing measurements, the append-only discipline.
