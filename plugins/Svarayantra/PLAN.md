# Svara-yantra — the SoundFont player with the tuning engine

A build plan written so any session can pick it up cold. Read `CLAUDE.md`
first; every rule there applies. **Queued after Sonitus phase 4** (tasks
P4-1..P4-6) — do not start this while those are open unless the user says
otherwise.

**Name.** `Svara-yantra` — the user's own Sanskrit compound: *svara* (स्वर),
musical note, the term of Indian music theory that the twenty-two shruti are
divisions of; *yantra* (यन्त्र), machine, instrument, device — as in the
Jantar Mantar observatories, which are yantras the size of buildings. **The
note-machine.** Product name "Tezla Svara-yantra"; folder, CMake target and
code identifiers use the unhyphenated `Svarayantra` (C++ names cannot carry a
hyphen), with class prefixes kept short as `Svara*`. Code `Tzsv`, bundle
`tech.tezla.Svarayantra`, vendor `Tzla` — registered in `plugins/README.md`.
The user has approved the name; a trademark search is theirs to do before
anything ships.

---

## The thesis

**A sample player that takes tuning as seriously as a synthesiser.**

SoundFont players are everywhere and microtonal ones barely exist: the format
itself hard-wires 100 cents per key (the `scaleTuning` generator bends it
crudely, per-zone, and almost nothing uses it). Svara-yantra replaces the key-to-pitch
map wholesale with the suite's `Tuning` — the same 44 built-in scales with
their stories, `.scl`/`.kbm` loading, the concert-pitch control and the tuning
panel that Sonitus has — so a sampled piano can play Rast with Zalzal's third,
a choir can sing in nīd qabli, and a Bohlen–Pierce tritave works on a sampled
flute. That is the product; SF2 playback is the vehicle.

The tuning stack is framework-free shared code, so this costs an integration,
not a port. The real work is the SF2 format itself, done to the spec, with the
suite's honesty: measured interpolation quality, a parser that refuses partial
loads, and no soundfont data shipped by us, ever.

---

## What SF2 is, in one paragraph

A RIFF file: an INFO list, an `sdta` block of 16-bit PCM sample data (optional
`sm24` extension to 24-bit), and a `pdta` block of nine cross-referencing
tables — the "hydra": presets (`phdr`/`pbag`/`pmod`/`pgen`), instruments
(`inst`/`ibag`/`imod`/`igen`) and sample headers (`shdr`). A preset zone
selects an instrument; an instrument zone selects a sample; both kinds of zone
carry **generators** (pitch, envelopes, filter, pan, key/velocity ranges,
loops...) and **modulators** (velocity, wheels → destinations). Two rules
carry most of the semantic weight and most third-party bugs: **preset-level
generators are relative** (added to instrument-level values), **instrument-
level are absolute**; and each level may have a **global zone** supplying
defaults. Units are the spec's own: timecents for durations, cents for
frequency, centibels for attenuation and Q.

---

## Sources — CLAUDE.md §9 applies with teeth

| Source | Licence | Role |
|---|---|---|
| SoundFont Technical Specification 2.04 (E-mu/Creative) | freely published | **The primary source.** Generator list, default modulators, envelope shapes, zone semantics — this is the "knowledge measurement cannot verify" category where a faithful copy beats a re-derivation. **If the proxy refuses it, stop and ask the user for the PDF** (`sfspec24.pdf`) rather than building from memory — the long tail of generator semantics is exactly where memory is not evidence |
| TinySoundFont | MIT | Compact reference implementation, safe to read and compare against |
| FluidSynth | LGPL-2.1-or-later | The behavioural reference for spec ambiguities (what does the ecosystem actually do). Compatible licence; check per-file headers anyway |

Record all three in `docs/DSP-REFERENCES.md` with access honesty — including
"consulted for behaviour, no code taken" where that is what happened — before
any of them influences a line.

We ship **no soundfont data**. Tests synthesise their own SF2 files (see S1).
The README points at well-known freely licensed soundfonts (GeneralUser GS,
MuseScore's) for users, with their licences named, and bundles none of them.

---

## What is reused, and what is new

**Reused as-is (the point of §4):**

- **`Tuning`, `Scales`, `ScalaFile`, concert pitch** — the entire microtuning
  stack, including `nearestFraction` and every scale's construction, story and
  pitch-standard lore. `frequencyFor(note)` is the only call the voice makes.
- **`SvfFilter`** — SF2's filter is a two-pole resonant lowpass;
  the SVF's LP output with Q mapped from centibels covers it.
- **`Lfo`** — SF2's vibLfo and modLfo are triangle LFOs with a delay.
- **`SmoothedValue`, `DcBlocker`** where needed; the JUCE-layer patterns
  (parameters, state, presets, pages) throughout.
- **`TuningPage`** — extracted from Sonitus into `shared/tezla-ui` in S6, so
  both instruments share one panel and Sonitus keeps behaving identically
  (same commit swaps it in there; the render-tool screenshots prove parity).

**Deliberately NOT reused:**

- **`Adsr`.** SF2 defines its own envelope: delay-attack-hold-decay-sustain-
  release, times in timecents, volume sustain as attenuation in centibels,
  and the spec's own curve conventions. Bending our musical AHDSR to fake it
  would mis-render existing soundfonts subtly and forever. `Sf2Envelope` is
  written to the spec instead, and tested against the spec's arithmetic.

**New, in `plugins/Svarayantra/Dsp/` (framework-free, tests in the same commit):**

| file | what |
|---|---|
| `Sf2File.hpp` | RIFF walker + hydra parser + sample access. Refuses a file it cannot fully parse, naming the chunk — the `.tzref`/`.scl` lesson |
| `Sf2Model.hpp` | Zone resolution: key/velocity range intersection, global zones, preset-relative + instrument-absolute generator stacking, default modulators |
| `SamplePlayer.hpp` | Interpolated playback: 4-point Hermite baseline, loop modes (off / continuous / until-release), sample-exact loop joins, stereo pairs |
| `Sf2Envelope.hpp` | The spec's DAHDSR in the spec's units |
| `SvaraVoice.hpp` | One note: player + envelopes + LFOs + filter + pan, pitch from `Tuning` |
| `SvaraEngine.hpp/.cpp` | Voice pool and stealing (Sonitus's manager as the pattern), exclusive classes (hi-hat choke), bank/program selection, MIDI: pitch bend with bend range, mod wheel → vibrato, **sustain pedal (CC64)** — a sample player without it is broken for its main audience |

---

## The tuning integration, precisely

- The SF2 key→pitch mapping (`scaleTuning`, 100 cents/key) is **replaced** by
  `Tuning::frequencyFor(note)`. Playback ratio = target Hz / root Hz, where
  root Hz comes from the sample's `originalPitch` (or `overridingRootKey`)
  plus `coarseTune`/`fineTune`/`pitchCorrection` — those still apply, because
  they describe the *sample*, not the keyboard.
- Non-octave scales work by construction: the voice never assumes an octave.
  A Bohlen–Pierce tritave on a sampled sine is a test, not a hope (S4).
- Concert pitch rides through `Tuning` untouched. The tuning panel — scales,
  stories, Hz table, A4, Apply — arrives whole via the shared component.
- A `.kbm` map applies exactly as in Sonitus, holes and all: an unmapped key
  plays nothing.

---

## State and memory — two decisions made now

- **The project state stores the `.sf2` path, not the data.** The opposite of
  the `.scl` decision, and for the same reason pointed the other way: scale
  text is bytes, soundfonts are hundreds of megabytes, and embedding one in
  every project save would be hostile. Consequence, stated in the UI and the
  README: a project opened where the file is missing says so plainly, by
  path, and plays nothing rather than something wrong. Store bank/program
  alongside the path.
- **Whole-file load into RAM, on the message thread, swapped in RT-safe** —
  the tuning-publish pattern at larger scale. No disk streaming (that is a
  different product); the ceiling this implies goes in the README with a
  number once measured.

---

## Build order

Each step is one commit with tests written and run in the same commit, each
mechanism break-checked, x86-64 Windows-first per §2.3 throughout.

- **S1 — `Sf2File`.** The parser, plus a minimal SF2 *writer inside the
  tests*: build a tiny soundfont in memory (a sine sample, one instrument,
  one preset), round-trip it, and assert every hydra cross-reference. Then
  the refusals: truncated at each chunk boundary → refused with that chunk
  named; dangling zone indices → refused; a real-world file (user-supplied,
  not committed) parsed as a manual smoke test.
- **S2 — `SamplePlayer`.** Hermite interpolation with the aliasing
  *measured*: a synthesised sine soundfont pitched across ±2 octaves, swept,
  images pinned under a number in `tests/` (the Anvil probe discipline). Loop
  joins bit-continuous; until-release mode releases correctly.
- **S3 — `Sf2Model` + `Sf2Envelope`.** Spec arithmetic under test: timecents
  to seconds, centibels to gain, preset-relative stacking onto instrument-
  absolute values, range intersection, global-zone defaults, the default
  modulator set. This is the long-tail commit — the spec open on the desk.
- **S4 — Voice + engine + tuning.** Polyphony, stealing, exclusive classes,
  CC64, pitch bend; `Tuning` wired in. Tests: activity-based voice
  retirement (the Adsr lesson — assert voices die, not that they are quiet);
  a Bohlen–Pierce tritave and a 432 concert pitch through a sampled sine,
  measured at the output.
- **S5 — JUCE layer.** Parameters, path-based state with the missing-file
  behaviour, bank/preset selection, MIDI plumbing. Render-tool driveable from
  day one.
- **S6 — Editor + the shared tuning panel.** Extract `TuningPage` to
  `shared/tezla-ui`; Sonitus switches to it in the same commit, screenshots
  proving parity. Svara-yantra's page: load button, bank/preset browser (from
  `phdr`), polyphony, output trim, the panel.
- **S7 — Close-out.** `tezla-measure svarayantra` (aliasing, CPU per voice at N
  voices), README with the memory ceiling and soundfont pointers, validator
  47/47, registry status flip, docs current.

**Second pass, explicitly deferred:** reverb/chorus send generators (ignored
and documented in pass one), `sm24` 24-bit samples, SFZ, MTS-ESP, disk
streaming. Each is additive; none blocks the above.

---

## Risks, and what to do about them

- **The spec's long tail.** Odd soundfonts in the wild will exercise
  generators nobody sets. Mitigation: implement the defined core faithfully,
  default the exotic *to the spec's stated defaults* (not to guesses), and
  when a user file renders wrongly, that is a bug report with a file attached
  — the parser's refusal messages and a `svarayantra dump` mode in the render tool
  make it diagnosable.
- **Memory, not CPU.** Playback is cheap next to Sonitus; a 500 MB orchestral
  soundfont is not. Measure, document the ceiling, and refuse gracefully.
- **Spec access.** If `sfspec24.pdf` cannot be fetched from the container,
  ask the user for it *before* S3 — S1 and S2 stand on structure that is
  safely known, S3 is where the spec must be on the desk.

---

## Verification

| check | how |
|---|---|
| Parser round-trip | test-built SF2 in, every table cross-checked back |
| Partial file | refused, with the failing chunk named — never half-loaded |
| Interpolation | swept sine soundfont, images below a pinned dBFS figure at ±2 octaves |
| Loop joins | bit-continuous across the boundary, all three loop modes |
| Envelope arithmetic | timecents/centibels against the spec's own formulas |
| Zone stacking | preset-relative + instrument-absolute, asserted per generator |
| Tuning | 12-TET null against untuned playback; BP tritave exact ×3; A432 scales every note by 432/440 exactly |
| Voices | activity-based retirement (the Adsr lesson), exclusive classes choke |
| Sustain pedal | held, released, notes under and over it |
| Block-size independence | 64 vs 512, parameters moving, per §7 |
| Validator | 47/47 |
| Panel parity | Sonitus screenshots identical before/after the TuningPage extraction |

What none of that proves: how it sounds with real soundfonts in FL Studio.
That loop is the user's ears on the Windows build, same as ever.
