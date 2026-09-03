# Ictus — the drum synthesiser for drum and bass

**Code `Tzic` · "Tezla Ictus" · `tech.tezla.Ictus` · instrument.**

**Ictus** is Latin for the stroke, the blow — and in music and verse, the *beat
itself*, the stress of the downbeat. A drum is a stroke and a drum machine is
the beat; one word says both, and it sits beside Malleus (the hammer) and Anvil
(what it lands on).

## The thesis

The kicks and snares in the user's tracks come from 44.1 kHz / 16-bit samples,
layered by hand with envelopes, EQ and saturation until they are thick and
punchy. Synthesising them gives control over punch, tuning and harmonics, and
the whole session's fidelity — 48 or 192 kHz, 64-bit inside the plugin, no
aliasing, real dynamics — for the two sounds a DnB track lives on. The brief:
a **FAT, punchy kick and snare** first; hi-hats with controllable harmonics;
humanisation and subtle per-hit variance; velocity that changes the sound, not
only the level; each drum on its own FL Studio mixer track; and a sample
loader, so the synth and the user's library layer inside one instrument.

Four purpose-built engines (kick, snare, hat, clap) rather than a general
synth talked into a kick; on every pad a sample layer and a **punch chain** —
transient, drive, weight and tilt, one-knob smash, clip — which is the layering
the user was doing by hand across four plugins, built in and bit-exact at
neutral.

Decisions made with the user before this plan was approved (2026-09-02):
name **Ictus / `Tzic`**; scope **A** (four engines, sample layer, punch chain,
multi-out); sample playback by **windowed sinc, not Hermite** (measured, below);
**start with I0 and the kick engine**, and put a kick-only Windows build in the
user's hands before the snare exists.

**IP guard (CLAUDE.md §2.1)**: physics and published analysis only. No
product's binary, samples, presets or panel; no brand names in presets or
controls. The registry's "sonic targets" section is the only place a product
is named, and only as a target for the sound.

---

## Research material — status

Every drum-synthesis and FL Studio source was **blocked at the network layer**
on the first pass (qub.ac.uk, dafx.de, soundonsound.com, image-line.com,
forum.juce.com, cim.mcgill.ca, nuxx.net and others all refused). Per CLAUDE.md
§9 the URLs were given to the user, who fetched them; they live in
`technical references/drumsynth/` and were **read first-hand from there on
2026-09-02**. The full table, with what each
source settled, is in `docs/DSP-REFERENCES.md` ("Drum synthesis — Ictus").
The short form:

| Source | Status | What it settles here |
|---|---|---|
| Werner, Abel, Smith — TR-808 bass drum, DAFx-14 | read first-hand | Two separate pitch effects: a ≈6 ms attack jump of more than an octave (heard as punch, not pitch) and a slow sigh; the retrigger pulse; tone → level → high-pass output; no machine-gun effect; unit variation is tolerance. |
| Werner, Abel, Smith — TR-808 cymbal (CC BY 3.0) | read first-hand | Six rectangular oscillators at 205.3, 369.6, 304.4, 522.7, ~800, ~540 Hz, duty 47.98 %, band-passes ≈3440 and ≈7100 Hz; rendered at 4× oversampling because squares alias. |
| Reid, SOS Synth Secrets — bass drum, snare drum, percussion | read first-hand | Kick pitch drops "a couple of semitones"; beater click as a short HF burst; snare = two quasi-harmonic series plus a fast-decaying 0,1 pair at ≈180/330 Hz; velocity → noise cutoff and → tuned/noise mix. |
| Nord Modular percussion chapter | read first-hand | Resonant-filter kick rung by a pulse, sync so every hit starts alike; clap = four noise bursts ≈11 ms apart. |
| 9090 / TR-909 kick notes, PCB page and schematic | read first-hand | The 909 kick's blocks: diode-rounded triangle, Pitch / Tune Decay / Tune Depth, noise click with the Attack knob, Decay, Level. |
| FL Studio manual (Plugin Wrapper) and the multi-output page | read first-hand | Auto map outputs = the tracks following the plugin's own; offsets for manual assignment; inactive outputs are skipped unless told otherwise. |
| JUCE forum threads on multi-out; JUCE's MultiOutSynth demo | read first-hand | Declare every bus enabled; the `isBusesLayoutSupported` shape. |
| Bilbao, JASA 2012 (snare FD model) | abstract only | Why the rattle is the one nonlinearity worth keeping. |

**Nothing is outstanding.** If a later phase needs another source, the rule is
CLAUDE.md §9: stop, list the URLs with one line each on what they change, and
carry on with what does not depend on them.

---

## Two decisions that settle most of the design

1. **Per-hit parameter snapshot.** Every engine knob — pitch, drop, decay,
   click, tone, harmonics, sample start — is resolved at note-on into the hit,
   the way Malleus resolves hardness (`MalleusVoice.hpp`, the `hardness_`
   line). Nothing inside a hit is smoothed or re-read; only the chain, level
   and pan are continuous and smoothed. A hit is then a pure function of
   (parameters, velocity, seed): bit-exact neutral is a branch per hit,
   humanise is a snapshot plus deviations, and the replay test and
   Render-to-WAV are honest.
2. **Everything renders at the internal (oversampled) rate** — engines, click
   resonators, sample playback, chain — into per-bus oversampled buffers via
   `Oversampler::internalBuffers()` exactly as Sonitus does; only decimation
   is per bus. An 8 kHz click band-pass then sits at Fs/24, where a biquad is
   rate-independent (CLAUDE.md §6).

---

## The model, in full

### Pads, MIDI, retrigger and choke

Eight pads, fixed engines: **Kick 1 (36), Snare 1 (38), Closed hat (42), Open
hat (46), Clap (39), Perc (37; the snare engine with tom defaults, wires 0),
Kick 2 (35), Snare 2 (40)** — the General MIDI notes, so any drum pattern plays
without setup. Note, choke group and sample path are **state-tree properties**
(`pads/pad<N>`), not parameters: not automatable, not in presets (a preset must
not remap the user's notes), written by note-learn from the message thread.

A pad owns **two hit slots**. A retrigger fades the old hit linearly to
exactly 0.0 over 1 ms at the internal rate, then resets it, while the new hit
starts from its own reset state — the sum *is* the crossfade; no weights. The
same `fadeOut` serves choke groups (5 ms; closed hat chokes open hat by
default; a choke landing mid-fade takes the shorter remaining time). The chain
sits below the slot sum, so a retrigger never resets a compressor or a shelf.
`Hit::isActive()` = amp or tail envelope active, click energy > 1e-12, noise
active, sample playing, or fading — with the `Adsr` **killed the moment it
reaches its zero sustain** (`Adsr` at sustain 0 lands in the sustain stage
with `isActive()` still true; the Sonitus zombie lesson), so retirement is
exact and the test asserts an *activity count*, not silence.

### Kick engine — body, drop, sigh, harmonics, tone, click, tail

- **Body**: a phase accumulator sine, `phase += inc`, pitch updated per
  control chunk (32 internal samples on the engine's own grid; a hit that
  starts mid-chunk gets exact endpoints for its partial chunk) **with the
  increment interpolated linearly between exact values at the chunk's two
  ends** — a 2 ms drop from +60 semitones spans ~11 chunks and a frequency
  staircase there is audible grit, while the chord between two exact points
  has no lag and misses the exponential by (dt²/8)·f″, 0.35 cents at the
  steepest point (measured: 0.016 cents against the closed form over cycles
  3–30 at every rate, I1). Start phase 0–90°, default 0 (90° with a
  0 ms attack is a full-scale step by design; the tooltip says "a click by
  construction" — the NM chapter's sync-so-every-hit-starts-alike, as a knob).
- **Pitch**: two `TensionDrop`s (promoted to `tezla::dsp`, cents summed, exact
  1.0 once both have snapped): **Drop** = *Start* semitones above the end
  pitch (0 to +60; velocity scales it as `depth·((1−a)+a·v)`) over *Drop time*
  2–200 ms — the 909's Tune Depth / Tune Decay and the 808's attack jump; and
  **Sigh**, signed ±12 semitones over 100 ms–2 s — the 808 paper's R161
  leakage and SOS's "a couple of semitones from start to finish". The stated
  time is the *landing* time (τ is a fifth of it; the tooltip says so). End
  pitch: *Tune* in Hz/note or *Follow key*.
- **Harmonics**: parallel and oversampled,
  `y = x + h·(even·Adaa1<SoftEven>(x) + (1−even)·Adaa1<SoftOdd>(x))` — exact
  at h = 0. `dsp::SoftOdd` is new and shared (tested like SoftEven):
  `f(u) = u|u|/(1+u²)`, bounded, odd, third-harmonic dominant, with the
  small-u series guard. SoftEven's envelope-shaped DC → a per-hit `DcBlocker`
  engaged only when even > 0 (10 vs 20 Hz corner chosen from measure table 1 —
  a 2 Hz DC bump through a 10 Hz first-order HPF is only −14 dB). This is the
  909's diode clamp and the "reads on a small speaker" control; the *parallel
  excess* form, never a series folder (`SineFolder` through `Adaa1` is not
  bit-exact at gain 0 — its negligible branch turns into a midpoint average).
- **Tone**: `SvfFilter` low-pass at `tone × currentHz`, retuned per chunk
  (guarded, one `tan`) so the attack is bright and the tail pure; *Off* at the
  top of travel is a per-hit branch — exact and click-free because it is
  decided at note-on.
- **Click**: `ModalResonator` with one mode (*Click tone* 1–8 kHz, T60 ≈3 ms,
  `energy()` for activity) plus `ClickNoise` (Ictus-local: `dsp::SmallRandom`,
  exponential envelope 0.5–8 ms, one-pole high-pass) — the 909's Attack knob
  and SOS's beater click, and the thing velocity is most about.
- **Amplitude**: `Adsr` attack 0–20 ms, hold, decay 20 ms–2 s, sustain 0,
  `decayTension = 1 − shape` (exponential ↔ linear), killed at zero.
  **Tail**: a second `Adsr` on the *same* body with attack = Drop time (so it
  only carries the landed pitch) and decay = Tail time;
  `gain = amp·(1−tail) + tail·tailEnv` — a lerp exact at both ends. No
  crossover (it would not sum exactly at neutral and smears the phase of a
  sine sweeping through 80 Hz); the sub-band share is measured instead.
- **Order**: body × gain → harmonics → DC blocker (if even) → tone → + click +
  noise → hit fade → chain. **Neutral is bit-exact by construction**: start 0,
  sigh 0, phase 0, harmonics 0, tail 0, tone Off, click 0.

### Snare engine — shell, wires, rattle, crack

- **Shell**: `ModalResonator`, 3 modes, ratios `1 + (r₀−1)·spread` with
  `r₀ = {1, 1.6, 2.2}` from the SOS measurement (the 0,1 pair and the two
  series' next partials), so *Spread* 0 is a single tone and 1 is a snare;
  T60s = *Decay* × {1, 0.7, 0.5}; *Tone* sets the `excite` amounts (the SOS
  article's fast-decaying 0,1 pair is the physics behind the shorter upper
  T60s). **Drop**: one `TensionDrop`; while it is active, `setMode` per chunk
  (state-preserving, no-op guarded; ~3 rebuilds × 4 transcendentals × 6 k
  chunks/s ≈ 1.4 ms per second *of drop*, and drops last ≤ 200 ms; the landed
  state costs nothing — the test asserts the retune count is 0 once landed).
- **Wires**: `SmallRandom` white → `SvfFilter` high-pass/band-pass (*Snappy*
  1–8 kHz) → `Adsr` (attack 0, decay 50–400 ms, killed at zero). **Rattle**:
  `g = 1 + rattle·(follower/strikeAmount)`, follower = |shell| through a 1 ms
  one-pole, normalised by the strike so the knob is per unit velocity — the
  one Bilbao nonlinearity worth keeping; exact at 0.
- **Crack** = the kick's click pair. *Body/Wires* balance is a lerp. Velocity →
  wires level and Snappy cutoff (the SOS recipe), crack, drop depth, level.

### Hat engine — the 808's metal, with the harmonics on a knob

Six `dsp::Oscillator` pulses (polyBLEP, width 0.5 — the paper's 47.98 % duty
rounded; polyBLEP is the default because a naive square at 192 k internal
aliases at only ≈−48 dB in-band before the band-pass, and the "aliasing is the
instrument" exception needs a test that says so) at ratios to a *Tune*, summed
→ `SvfFilter` band-pass (*Colour*; the paper's two centres 3.4 and 7.1 kHz are
the range's landmarks) → high-pass → `Adsr`. **Ratio sets are an append-only
choice** whose *Harmonics* knob morphs between adjacent sets by rank in log
frequency, `ratio_i = A_i^(1−t)·B_i^t`, stored as an **absolute position 0–7**
(positions past the last set clamp) so appending a set never repoints a saved
value: *808* (the six paper frequencies), *Bell* (nearer harmonic), *Trash*
(wider), *Wide* (spread octaves). *Spread* detunes the six against each other;
*Air* blends noise. **Closed / Open share one engine parameter set with two
decays** (30–80 ms / 200 ms–1 s) and the choke between them; velocity → decay
and colour. No FM set: a two-operator pair cannot be morphed continuously
against six ratios, and ring-modulated pairs are not the 808 sound.

### Clap engine

A `BurstScheduler` (countdowns for three pre-bursts spaced *Flam* 8–14 ms
apart, then the main burst — NM's four pulses ≈11 ms apart), one noise source,
one summed excitation envelope (bursts + *Tail* decay), one `SvfFilter`
band-pass (*Colour* 1–2 kHz): `noise × Σenv → bandpass`. Humanise jitters the
spacing ±1 ms.

### Humanise and velocity — per hit, never the same twice

Per pad, a `SmallRandom` seeded by Malleus's rule (`MalleusEngine.hpp`:
`kSeedBase + golden × noteOnCount`, salted per pad): reproducible from
`prepare()`/`reset()`, never repeating in a session, and Malleus's
`per_hit_seeds_vary_but_the_take_replays` test transfers verbatim. One
*Humanise* knob per pad (0–100 %, wearing `ui::spectralKnob`) scales a fixed,
documented set: pitch ±20 c (±30 c snare/hats), decay ±15 %, tone/colour ±10 %
of range, level ±1.5 dB, click ±20 %, timing 0–2 ms **late only** (a one-shot
cannot start before its note). Humanise 0 → every deviation exactly 0 →
`exp2(0) = 1.0`, bit-exact.

Velocity per pad, all of the Malleus form `x·((1−a) + a·v)`: level 100 %,
click/crack 60 %, drop depth 30 % (kick), tone/colour 40 % (hat, snare wires),
decay 0 %. **One global velocity curve** (soft / linear / hard): programmed DnB
uses fixed velocities and a per-pad curve is eight parameters nobody touches.

### The punch chain — the manual layering, built in, per pad

Per pad, at the internal rate, N = 1 channel for synth-only pads and 2 once a
stereo sample is loaded (switched only while the pad is idle). Order — shaping
before saturation, the order the user layers by hand, and the tooltip says so:
**Transient → Drive → Weight + Tilt → Smash → Clip → level / pan → bus.** Each
stage bit-exact at neutral, tested bit for bit as Sonitus tests its tilt.

- **`dsp::TransientShaper`** (new, shared): detector `e = max|x_ch|`; fast
  (0.5 / 20 ms) and slow (20 / 200 ms) followers with Halo's coefficients;
  `t = clamp((fast−slow)/max(slow,floor), 0, 1)`,
  `s = clamp((slow−fast)/max(slow,floor), 0, 1)` (level-independent);
  `gDb = attackDb·t + sustainDb·s` at control rate, through a 0.5 ms
  `SmoothedValue`. Exact at ±0 (`0·t + 0·s = 0`, `pow(10,0) = 1.0`). No
  lookahead, no latency; time constants in ms so rate-independent by design.
- **Drive**: `Adaa1<SoftClipExcess>` with pre-gain g and trim 1/g (small-signal
  unity — harmonics, not loudness), knee ≈0.3. Not `TriodeStage`: its 33 ms
  bias drift is a per-hit DC shift on a sub. Exact at 0 dB and branched out
  there, so a hot peak is never clipped by a knob reading "off".
- **Weight** (`design::lowShelf`, dB ± with a 40–200 Hz corner, default 80)
  and **Tilt** (Sonitus's 700 Hz pair): exact at 0 dB by the `a0/a0`
  normalise.
- **Smash**: `CompressorCore`, one knob a: threshold −6 − 24a dB, ratio
  1 + 9a, knee 6 dB, attack 8 ms, release 80 ms, makeup 8a dB (verified from
  measure table 2 so a −6 dBFS RMS loop holds within ±1 dB). At a = 0
  `isIdentity()` → skipped.
- **Clip**: `Adaa1<SoftClipExcess>` at 0 dBFS, knee 0–100 % default 0 (a hard
  ceiling, identity for |x| ≤ 1). The removed excess is exposed and measured —
  Capstone's lesson: measure what the guard had to do.
- Latency: none in the chain. **The instrument's latency is half the
  oversampler's round trip, and a half-sample** — found at I1 on the first
  kick: `Oversampler::getLatencySamples()` is the up-and-down figure an
  effect incurs (47/63/71), while a generator writes into
  `internalBuffers()` and runs only the decimation half, 23.5/31.5/35.5 host
  samples. The engine delays its internal signal by `factor / 2` samples
  before decimating and declares the whole number that results: **24 / 32 /
  36** (0 at ×1), measured against the undecimated render to a residual of
  4e-8 / 8e-7 / 1.7e-6 with the neighbouring integers a thousand times
  worse. Sonitus, the one other instrument on that path, declares the round
  trip; `docs/ROADMAP.md` §10.

### Oversampling and multi-out — one `Oversampler` per bus

Each pad renders into a scratch at the internal rate, its chain runs, and the
result is panned into its bus's `internalBuffers()`; each of the five buses
owns a stereo `Oversampler` at the shared factor (Auto / manual, plus
`RenderOversampling` from day one), `downsample`d only when the bus is enabled.
A bus with no active hit routed to it and ≥ 95 × factor internal samples of
zeros is skipped exactly (its FIR line is all zeros by then). Decimation at ×4
from 48 k ≈ 0.5 % of a core per channel — All-to-Main costs one bus, four pads
on four buses cost five; the Output tooltip says so.

`BusesProperties().withOutput("Main", stereo, true).withOutput("Kick", …)
.withOutput("Snare", …).withOutput("Hats", …).withOutput("Perc", …)`;
`isBusesLayoutSupported` = no inputs, Main stereo (never disabled — it is the
fallback), every other bus disabled or stereo. Per block a pad's bus that the
host disabled falls back to Main; nothing goes silent. Tests: (i) a lone pad on
its own bus is **bit-identical** to the same pad on Main alone; (ii) with other
pads sounding, `main' + aux` nulls against `main` at the double-rounding floor
— not bit-exact, because two linear-phase decimators sum only to rounding.

**FL Studio routing, from the manual's own words**: *"Auto map outputs —
Auto-assign plugin outputs to mixer tracks following the plugin's own Mixer
track. The routing will incrementally map to Mixer-tracks following the
plugin's selected Mixer track."* Manual assignment is in *"Mixer-track offsets
(relative to the Mixer track the plugin is on)"*. FL skips outputs a plugin
marks inactive unless "Process inactive inputs and outputs" is on — hence every
bus enabled from the constructor. What cannot be verified from here is FL's
handling of a JUCE VST3 declaring five stereo buses (JUCE types every synth
output bus `kMain`); the Main default means the worst case is "no aux", never
silence, and I7's build is the test on the rig.

### The sample layer

- **`shared/tezla-dsp/include/tezla/dsp/WavFile.hpp`** (framework-free):
  `readWav(bytes, size) → WavResult{ok, message, sampleRate, channels,
  std::vector<double> ch[2]}` with a `RiffCursor` on the `Sf2File.hpp`
  pattern; PCM 8u/16/24/32, float 32/64, WAVE_FORMAT_EXTENSIBLE; mono/stereo
  (>2 channels refused with a message); odd-chunk padding; a data size past
  the file end clamped and reported. `writeWav` moves here from
  `tools/include/tezla/measure/WavWriter.hpp` with a forwarding alias so the
  tools are untouched. Tests: writer→reader bit-exact for float32, 16/24-bit
  within 1 LSB, extensible header, stereo, 44.1/48/96 k. The JUCE layer uses
  this reader for WAV and `juce::AudioFormatManager` for AIFF/FLAC/OGG into
  the same struct — the DSP never sees a format.
- **Player — windowed sinc, not Hermite.** Measured (kernel responses computed
  numerically in source-rate units, 44.1 k file):

  | Interpolator | Droop at 15 / 18 / 20 kHz | First image for content at 10 / 15 / 20 kHz | MACs per output sample |
  |---|---|---|---|
  | 4-point Hermite (Catmull-Rom), direct | −1.65 / −3.11 / −4.44 dB | −27 / −16 / −8 dB | 4 |
  | 32-tap Kaiser sinc, direct | 0 / 0 / −0.38 dB | −96 / −96 / −28 dB | 64 |
  | 64-tap Kaiser sinc, direct | 0 / 0 / 0 dB | −101 / −93 / −91 dB | 128 |
  | **2× pre-upsample at load + 16-tap Kaiser sinc** | **0 / 0 / 0 dB** | **−106 / −103 / −101 dB** | **32** |

  Hermite rolls the top of every sample off by 4 dB and leaves images at −8
  to −27 dB. At unity tune on a 48 k host those images sit above 24 kHz and
  the decimator removes them, but they intermodulate in Drive and Clip before
  the decimator sees them, and tuned down an octave they land in the band (a
  15 kHz component played at 7.5 kHz puts a −16 dB image at 14.5 kHz).
  **Chosen: `dsp::upsample2x`** at load time (the house 95-tap halfband at
  100 dB from `HalfbandFir.hpp`, direct convolution, its 47-sample delay
  removed so the onset stays put; message thread, allocates) **plus
  `dsp::SincInterpolator`** at run time: 16 taps, Kaiser β for 100 dB, cutoff
  at the 2× grid's Nyquist so the source band ≤ 0.25 is flat and the images
  ≥ 0.75 sit in the stopband, a 512-phase table with linear interpolation
  between phases (phase-quantisation error below −110 dB), designed at
  `prepare` from `detail::kaiserBeta`/`besselI0`. Flat to 20 kHz, images
  ≤ −100 dB at every tune ratio, a quarter of a direct 64-tap's cost, ×2
  memory (a 2 s stereo 48 k file is 3 MB as doubles; files over 30 s are
  refused with a message — this is a drum layer). No latency: the kernel is
  centred on the read position and the whole file is in memory.
  `SampleVoice` (Ictus-local) reads the 2× data with increment
  `2·fileRate / internalRate × 2^(tune/12)`, mono/stereo, start offset, no
  loop, finished flag. Because playback happens inside the oversampled
  section, tuning **up** never aliases until the source's top maps past the
  internal Nyquist — +24 semitones at ×4 — and the decimator removes what
  crosses the host's; with oversampling *Off* at 44.1/48 k the layer aliases
  when pitched up like any host-rate sampler, and the tooltip says so. Test: a
  swept sine through a rendered 44.1 k file at ratios 0.5, 0.84, 1, 1.19 and 2
  — passband within ±0.01 dB to 20 kHz, images below −95 dB, and the
  ns-per-sample figure. Svarayantra keeps its Hermite; adopting the sinc there
  is roadmap item 9.5.
- **Controls**: level, tune (semitones), fine (cents), start, attack (≥ 0.5 ms
  floor), decay (`Adsr`, killed at zero), polarity (×−1, exact), delay ms (a
  countdown, ≥ 0). **Align** sets Start to the onset (−24 dB of peak, then
  back to the last zero crossing within 2 ms) and suggests polarity; Delay is
  for pushing a sample *later* than the synth click.
- **Render pad to WAV**: on the message thread, a separate `Pad` prepared at
  the session rate with the pad's snapshot and its own `Oversampler`, run
  until `!isActive()`, written 32-bit float; deterministic by seed, so it
  equals what the plugin plays.
- **Ownership**: Svarayantra's scheme × 8 — `std::atomic<SampleData*>`
  pending/retired per pad, adoption at block top **only while that pad's
  sample voice is idle** (else `fadeOut(0.001)` first, adopt next block). Path
  per pad in the state tree; a missing file keeps its path and an error
  string. "No allocation with a pending swap" proved with
  `test_Allocation.cpp`'s counting allocator.

---

## Parameters

≈ 295 parameters: engines ≈ 98, chain 11 × 8, sample 8 × 8, humanise + four
velocity amounts × 8, global (oversampling, renderOversampling, master,
velocity curve). IDs: house camelCase with a two-letter pad prefix — `k1Pitch`,
`k1Drop`, `s1Snappy`, `hcDecay`, `hoDecay`, `cpFlam`, `pcPitch`, `k2Pitch`,
`k1Drive`, `k1SmpStart` — display names "Kick 1 · Pitch" so a host list groups
by pad. `kSchemaV1` on every parameter from birth (Malleus's
`PluginProcessor.cpp`); every choice list (`output`, `hatSet`,
`velocityCurve`, oversampling) `static_assert`ed against its enum; presets
recalled by index, new ones on the end. Presets in the house voice: *Init Kit*
(the clean one), *DnB Tight*, *Sub Long*, *Jungle Snap*, *Neuro Bite*,
*Halftime Weight*, *Liquid Soft*, *Roller Push* — every preset's peak at full
velocity on all pads is a measured number (the Sonitus level rule).

## CPU — measured, not promised

Estimates in ns per internal sample, replaced by `CHECK_CPU_BUDGET`s as each
phase lands: kick ≈ 80 (≈ 40 with harmonics 0), snare ≈ 60, hat ≈ 6
oscillators + 20, clap ≈ 15, chain fully engaged ≈ 100 (the compressor's
per-sample log/pow dominates; ≈ 10 neutral), decimation ≈ 0.5 % per channel.
At 192 k internal, 100 ns/sample is 1.9 % of a core, so a kit hit (kick +
snare + hat + clap, chains engaged, Main only) lands near **8–10 % at 48 k
×4**, ≈ 4 % with neutral chains; the chain is the cost, and it is the layering
the user was doing by hand across four plugins. Idle budget < 0.05 %. The hat
is the one engine that could miss its budget; measured before I4 closes.

---

## What reuses what

Paths under `shared/tezla-dsp/include/tezla/dsp/` unless said otherwise.

**Used as they are:** `ModalResonator` (snare shell, click), `Oscillator`
(hat pulses), `Adsr`, `SmoothedValue`, `SmallRandom` (`UnisonBank.hpp`),
`Biquad` + `design::{lowShelf, highShelf, peak}`, `SvfFilter`,
`DcBlocker::retune`, `Adaa1` + `SoftEven` / `SoftClipExcess`,
`CompressorCore`, `Oversampler` + `RenderOversampling` +
`effectiveOversamplingMode`, `ScopedNoDenormals`, `Exact`, `Decibels`,
`Tuning`, `VuMeter` + `ui::LevelMeter`, `ui::LampButton`, `ui::styleKnob`,
`ui::spectralKnob`, the shared `HeaderBar` (oversampling + render quality),
Sonitus's page/cell classes, Crossbar's `KeypadView` as the pad-grid
precedent.

**Promoted to shared (own commit each, owner's tests moved and green):**
`TensionDrop` from `plugins/Malleus/Dsp/` (Malleus keeps
`namespace tezla::malleus { using dsp::TensionDrop; }`); the WAV writer from
`tools/` into `dsp/WavFile.hpp` beside the new reader.

**New shared:** `dsp::SoftOdd`, `dsp::TransientShaper`, `dsp::WavFile`,
`dsp::SincInterpolator` and `dsp::upsample2x` (both designed with
`detail::kaiserBeta` / `besselI0` from `HalfbandFir.hpp`, the halfband's own
coefficients for the 2× stage).

**New in `plugins/Ictus/Dsp/`:** `Hit`/`Pad` (two slots, fade, hit-relative
control chunking), `KickEngine`, `SnareEngine`, `HatEngine`, `ClapEngine`,
`ClickNoise`, `BurstScheduler`, `PunchChain`, `SampleVoice`, `IctusEngine`
(pads, MIDI map, choke, per-bus oversamplers, idle skip).

**JUCE-layer patterns copied:** Sonitus's sample-accurate MIDI split and
`handleMidi`, latency declaration; Svarayantra's message-thread load + atomic
swap + path-in-state; Sonitus's preset table and schema constants;
`tezla-render` and `tezla-measure` registration.

---

## Phases — kick in the user's hands first

Each phase one commit: tests seen red first, numbers in the message, whole
tree built (`./scripts/build.sh NONE --test`, plus the plugin targets and the
tools), validator on the bundle, and "the qemu-aarch64 cross-check was not run
(CLAUDE.md 2.3 gate)" in every message.

- **I0** — branch restarted from `origin/master`; this file; registry row
  `Tzic`; DSP-REFERENCES rows with the first-hand statuses; CLAUDE.md §11
  in-flight note; ROADMAP §9 (the parked ideas).
- **I1 — Kick engine and the engine skeleton**: `TensionDrop` promotion,
  `SoftOdd`, `Hit`/`Pad` with the two slots and fade, `IctusEngine` rendering
  into per-bus `internalBuffers()` (one bus declared, five in the API), idle
  skip, `KickEngine`; `tezla-measure ictus` table 1 (pitch trajectory from
  zero crossings at 44.1/48/96/192 k, cents spread between rates, rise time,
  sub-band share, post-blocker DC at 10/20 Hz, inharmonic floor at max
  harmonics). Tests: trajectory within ±5 c of the closed form at four rates
  (tolerance set from the measurement — the 176.4 k vs 192 k chunk-length
  residual is second order after interpolation); exact death by activity
  count; retrigger step ≤ the signal's own max step, both orders and a
  retrigger during a choke; 64 vs 512 block bit-identity; CPU budget.
- **I2 — Minimal JUCE layer, kick only, Main output**: parameters, GM note,
  one page in the house look, four presets. **Built for Windows and played on
  the rig** — then an explicit **ear round** before I3, because the kick is
  the sound the plugin exists for.
- **I3 — Snare engine** + SNARE page; mode ratios, drop retune count 0 when
  landed, rattle 0 exact.
- **I4 — Hat and clap engines**, choke groups, pages; alias floor per set,
  choke fade time, morph continuity across a set boundary; hat budget.
- **I5 — Punch chain** (`TransientShaper` its own shared commit); every stage
  bit-exact at neutral, transient gain on a synthetic hit, Smash loop, clip
  excess; table 2.
- **I6 — Humanise and velocity**; vary/replay test, humanise 0 bit-exact.
- **I7 — Multi-out buses** and the per-pad Output choice; the two bus tests
  and the disabled-bus fallback; **tested on the rig with Auto map outputs**.
- **I8a — Sample DSP**: `WavFile`, `upsample2x`, `SincInterpolator`,
  `SampleVoice`; WAV round trips, the interpolation sweep (droop and image
  floor at five tune ratios, quoted), onset tests. **I8b — Loader**: slots,
  Align, Render pad to WAV, pad UI, the no-allocation swap test.
- **I9 — Editor close-out**: pad grid, per-pad pages (chain and sample on
  their own page per pad), waveform strips, meters; README, `docs/DRUMS.md`
  placement note, registry flip, validator 47/47 on all thirteen,
  photographed; table 3 (per-engine ns/sample, kit total at 48 k ×4 and
  192 k ×1, decimation per bus, multi-out null residual).

Parked at the plan stage, in `docs/ROADMAP.md` §9: a per-session unit
tolerance; a kick *restart / add* retrigger mode; a shifted-series shell; a
"Both" output; Svarayantra adopting the sinc.

## Risks

- **FL multi-out** with five `kMain` stereo buses cannot be tested here; the
  Main default and the disabled-bus fallback mean the worst case is "no aux",
  not silence. Rig at I7.
- **Loader**: the ×8 slot scheme, deferred adoption while a pad sounds,
  `releaseResources`/destructor drain, the allocation-counting test.
- **Retrigger vs choke**: one `fadeOut`, shortest remaining wins; test both
  orders and a retrigger *during* a choke.
- **Rate match**: linear increment interpolation between exact chunk
  endpoints; the residual between 176.4 k and 192 k internal rates is the
  chunk-length difference (8.8 %), second order after interpolation.
  Measured at I1: 0.016 cents against the closed form and 1.95 µs of
  crossing-time spread between the four rates; the test bounds are 0.1 cents
  and 10 µs.
- **`Adsr` at sustain 0** must be killed (the CPU-zombie lesson); assert the
  hit count.
- **SoftEven DC** on a sub: measure the post-blocker DC bump per corner in
  table 1 before choosing 10 vs 20 Hz.
- **Hat cost / aliasing** is the one engine that could miss its budget;
  measure before I4 closes, with naive squares as the documented fallback only
  if the alias floor passes.

## Verification

Per phase: the whole DSP tree via `./scripts/build.sh NONE --test`, the tools
and the plugin targets built, the new `tezla-measure ictus` tables quoted,
break-checks noted. At the JUCE phases: `tezla-measure selftest`, the render
tool's editor shots and `dump:` strings, the Steinberg validator, and — the
point of I2 — the plugin loaded in FL Studio on the rig, with routing checked
at I7 through Auto map outputs. The §2.3 gate holds: no ARM64 or macOS runs.

---

## Continuity — how any session resumes this work

This section is the handoff. It is updated **in the same commit as each
phase**, so whichever assistant session picks the work up — after a context
loss, a model change, or a fresh clone — needs nothing beyond this file and
CLAUDE.md.

**Phase status** (flip `pending` → `done` in the phase's commit):

| phase | status |
|---|---|
| I0 plan + registry + references + roadmap | done |
| I1 kick engine + engine skeleton + TensionDrop promotion + SoftOdd | done |
| I2 minimal JUCE layer, kick only, rig build + ear round | done — **played on the rig 2026-09-02** ("wow that sounds great"); the round's asks are the I2.1 row |
| I2.1 the rig's first ear round: Bass mode, Gate + Release, tuning page | done in code; not yet played on the rig |
| I3 snare engine + SNARE page (and the Perc and Snare 2 pads on the same engine) | done in code; not yet played on the rig |
| The rounds after I3 (user asks, 2026-09-03): Note snap; the panel; the ghost snare on the second snare pad with LINK | done in code; not yet played on the rig |
| I4 hat + clap engines, choke | pending |
| I5 punch chain + TransientShaper | pending |
| I6 humanise + velocity | pending |
| I7 multi-out buses | pending |
| I8a sample DSP (WavFile, upsample2x, SincInterpolator, SampleVoice) | pending |
| I8b sample loader (slots, Align, Render, pad UI) | pending |
| I9 editor close-out | pending |

**Measured at I1** (`tezla-measure ictus` and `tezla-tests kick`, 48 kHz
host unless said; the container's CPU figures are noisy, quoted as a range):

| claim | figure |
|---|---|
| neutral kick against sin(2π·phase) × envelope, 24000 samples, oversampling off | **bit-identical** |
| pitch against the closed form, cycles 3–30, at 44.1 / 48 / 96 / 192 kHz (Auto) | **0.016 / 0.015 / 0.015 / 0.015 cents** |
| crossing-time spread between the four rates over the whole hit | **1.95 µs** |
| declared latency ×2 / ×4 / ×8, residual against the undecimated render | **24 / 32 / 36**; 4e-8 / 8e-7 / 1.7e-6 (one sample either side: 1.7e-2) |
| retrigger: max step of a single hit / retriggered / what a cut would be | 0.0347 / **0.0347** / 0.8163 |
| 64-, 97- and 512-sample blocks, both kicks, everything on | **bit-identical** |
| hit retirement (everything on, half tail) | last non-zero at 1.031 s, active hits **0** |
| two kicks, everything on, 48 kHz ×4, decimation included | **4.3–7.0 %** of a core |
| kick engine at 192 kHz, everything on / neutral | **70.5 / 14.2 ns per sample** (1.35 % / 0.27 %) |
| idle instrument | 0.001 % |
| even-curve DC bump after the blocker at 5 / 10 / 20 / 40 Hz | −16.2 / **−20.3** / −25.5 / −29.0 dB re peak (10 Hz stays the default: 20 Hz would cost 0.64 dB at 50 Hz) |
| harmonics stage, 55 Hz full scale at 192 kHz, gain 4 | THD −17.9 dB, inharmonic **−155 dB** (−189 dB audible) |
| default kick: rise to −3 dB / energy below 80 Hz | 1.10 ms / 41.8 % |

**I2 shipped** (2026-09-02): `plugins/Ictus/CMakeLists.txt` (`Tzic`, synth,
MIDI in, `Instrument|Drum`), the processor (26 Kick 1 parameters at
`kSchemaV1`, `output`, `oversampling`, `renderOversampling`; pad notes as
state-tree properties; a HIT button hand-off as one atomic bit per pad; A/B
compare; the Sonitus oversampling and render-quality plumbing with the
latency re-declared per block), four presets (*Init Kit*, *DnB Tight*,
*Sub Long*, *Jungle Snap*), and a one-page editor in the house look (the
shared header, a pad strip with HIT and the hit count, seven columns of
knobs and two lamp switches, greying for Even / Tone / Tail time). Built and
photographed through `tezla-render-Ictus editor`: a scripted press and
release on HIT, 150 ms of audio and two timer ticks read "1 hit sounding".
Steinberg's validator: 47 of 47. Loaded and played in FL Studio on the rig
the same day.

**I2.1 — the rig's first ear round** (2026-09-02). Two findings from the
user, both about what happens between the keyboard and the pad:

1. *Follow key* only ever sounded on the pad's own note, so it was a fixed
   transposition rather than a keyboard. The engine's `startKick` now takes
   the landed pitch from `dsp::Tuning::frequencyFor (note)` (the shared
   tuning, 12-TET at A4 = 440 Hz until a scale is loaded) whenever the hit is
   keyed, and **Bass mode** (`bassMode`, a global switch in the strip) makes
   every key strike Kick 1 at the key's pitch with the other pads silent — a
   tuned sub-bass instrument made of the kick. Microtuning came with it for
   free: `IctusProcessor` is a `ui::TuningHost` exactly as Malleus is
   (publish under a `SpinLock`, `swapScale` on the audio thread, scale and
   map as text in the state), and the editor's second page is the shared
   `ui::TuningPanel`. The Key and BASS tooltips are live (`describeKeying`,
   `describeFollowKey`): which scale, and what C1, C2 and G2 play through it.
2. A one-shot's tail piles up under a fast fill. **Gate** (`k1Gate`) makes a
   note-off release the hit from wherever its envelopes are, over
   **Release** (`k1Release`, 0–2 s, skewed to 100 ms); Release 0 is a 1 ms
   cut — `KickEngine::kMinimumReleaseSeconds` — the shortest that does not
   click. Both AHD envelopes (amp and tail) get the release with the decay's
   tension. A note-off after the hit has landed changes nothing, bit for bit,
   and the pad releases only the hit *that key* started (`Pad::release
   (note)`, the slot remembers its note), so a legato bass line holds.

   Parameters appended at `kSchemaV2`, all neutral by default; preset *Bass
   Keys* on the end of the list. Measured (`tezla-tests bass`, `gated`,
   `note_off`): notes 36 / 43 / 48 land on 65.406 / 97.999 / 130.813 Hz,
   within 0.001 cents; note-off at 200 ms into a 2 s decay with a 50 ms
   release → last non-zero at 251.3 ms, max step 0.0133 = the body's own;
   Release 0 → gone 2.29 ms after the note-off (the 1 ms cut, half a host
   sample of alignment and the decimator's tail), same step; the one-shot
   plays on; the late note-off is a null. Break-checks, each seen red then
   reverted: `noteOn` ignoring Bass mode (the bass test went silent — and
   first crashed on an empty crossing list, so the test now fails on its
   check instead), `release()` emptied (gate test red on activity and on the
   last non-zero), `Pad::release` ignoring which key started the hit (legato
   test red, D cut with C).

**I3 shipped** (2026-09-02): `plugins/Ictus/Dsp/SnareEngine.hpp` — three
modes of a `ModalResonator` at `1 + (r0 − 1)·spread`, r0 = {1, 1.6, 2.2}
(Reid's measured snare, rounded; attributed at the point of use), T60s of
Decay × {1, 0.7, 0.5}, Tone as the upper modes' strike amounts (0 runs one
mode), one `TensionDrop` retuning the bank per control chunk while it moves
and once more as it snaps, the shell cut exactly at −120 dB per chunk; the
wires as seeded noise through an `SvfFilter` (Snappy the corner, Snap a
morph from high-pass to band-pass, exact at both ends) under an AHD `Adsr`
killed at zero; the **Rattle** as a second, *additive* drive on the wires
following |shell| through a 1 ms one-pole, so with it up the wires buzz as
long as the drum rings (the first draft scaled the wires' gain instead and
could not outlive their own envelope: +24 % at full, which is not a knob);
the crack as the kick's click pair, lifted into `Click.hpp` as `ClickPair`
and proved bit-identical on a golden kick render; the gate as a release
ramp on the whole hit, engaged only at note-off. Three pads run it: Snare 1
(38) with its 24 parameters at `kSchemaV3`, Snare 2 (40) and Perc (37, the
tom defaults of `tomSettings()`) on their defaults until I9. The editor
gained the SNARE tab, a HIT button that strikes the page's pad, and greying
for the wires', crack's and gate's dependents. Presets: the three kits got a
snare each; nothing reordered.

Measured at I3 (`tezla-tests snare`, `spread_zero`, `rattle`, `kit`;
`tezla-measure ictus` table 2):

| claim | figure |
|---|---|
| modes at Spread 1, 200 Hz fundamental, 0.18 Hz bins | **199.95 / 320.07 / 440.00 Hz — 1 : 1.601 : 2.201** |
| Spread 0 shell at 44.1 / 48 / 96 / 192 kHz, cycles 2–100 | **200.0000 Hz at all four**, worst single cycle 0.000 cents |
| retunes for a 12 st / 50 ms drop at 192 kHz (snap predicted at 702 chunks) | 121 by 20 ms (at 219.67 Hz), **704** by 600 ms, 704 by 1.1 s; landed on **exactly 200 Hz** |
| Rattle 0: hit == shell + wires, bit for bit; follower | **0 mismatches** (a leak of one part in 10¹² is caught); follower exactly 0 |
| Rattle 1: wires over the first 20 ms / at 100 ms where the plain burst has ended | **×1.789** / −28.7 dB re their start, plain wires exactly over |
| rattle table (wires 0.3 s), rattle 0.25 / 0.5 / 1, first 20 ms | ×1.129 / 1.260 / 1.528; at 200 ms ×1.012 / 1.024 / 1.048 |
| wires centroid, high-pass at 2 / 4 / 6 kHz; band-pass at 2 / 4 / 6 kHz | 7837 / 7805 / 8918 Hz; 2581 / 4721 / 6736 Hz |
| retirement, everything on, 0.3 s shell | last non-zero at **0.601 s** (1.1e-17), active hits **0** |
| gate, 50 ms release, note-off at 100 ms / Release 0 | silent at 151.3 ms / 2.29 ms after; max step = the strike's own |
| snare engine at 192 kHz, everything on / bare tom | **15.0 / 7.3 ns per sample** (0.29 % / 0.14 %) |
| the kit — two kicks and three snares, everything on, 48 kHz ×4 | **4.8–5.2 %** of a core; idle 0.001 % |
| the kick through `ClickPair` against the I2 render (click 0.4, noise 0.3) | **bit-identical** |

**Note snap** (2026-09-03, the user's ask after I3): `noteSnap` on
`KickSettings` and `SnareSettings`; `Engine::startKick`/`startSnare` land an
un-keyed Tune on `dsp::Tuning::nearestScaleHz` when it is lit — the tuning's
nearest degree, 12-TET until a scale is loaded — so a drum can sit in the key
of the bass line. `k1NoteSnap` / `s1NoteSnap` at `kSchemaV4`; NOTE lamps beside
Key with live tooltips naming the note Tune lands on (`describeNoteSnap`,
`noteNameFor`); the wires' `s1Snap` knob is displayed as *Shape* so the two
cannot be confused (the ID is frozen). Measured: 52 Hz → 51.913 Hz (G#1),
205 Hz → 207.652 Hz (G#3), and with 5-TET swapped in 52 Hz → 55.000 Hz, the
scale's degree. Break-check: both snaps removed from the engine → the test
red on all three.

**The panel** (2026-09-03, the user's ask: "everything is the same size and
colour"): the flat seven-column grid became `PlatePage` — control plates in
the house look (`ui::paintPlate` / `paintPlateHeading`, lifted from the
Sonitus editor into `shared/tezla-ui/include/tezla/ui/Plate.hpp` with the
plate colour as the caller's), one plate per group with its hue from
`design::tintFor` (pitch/shell on the accent, then colour/wires, click/strike,
amplitude/out, velocity at 18° steps), a spine, a heading with an
explanation, and the lead control of each group at 1.32× (Tune, Harmonics,
Click, Decay, Level; Wires, Crack) with the set-and-forget ones at 0.74×
(the times, the velocity amounts). Four pictures drawn from the knobs, not
the audio, so they are right before the first hit (`Displays.h`): the kick's
**pitch trajectory** on log time × log frequency with the tuning's notes as
a ruler and the landed note named and its cents offset in the caption; the
snare's **three modes** as bars on a log axis with the drop's start ghosted
and the note ruler; an **envelope** per drum computed by running the
engine's own `dsp::Adsr` at a display rate (the kick's AHD, its tail dashed,
the mix filled; the snare's fundamental and upper modes on their T60s and
the wires' burst with the rattle's drive on top); the **wires' filter
response** from the engine's own `SvfFilter::magnitudeAt`. A **pad strip**
(`PadStrip.h`) of eight lit plates naming pad and note (with the MIDI number,
since DAWs disagree on octaves) that flash on a hit as bright as its velocity
— per-pad hit counters and last velocity copied out of the engine each block
— and open the pad's page; HIT strikes the selected pad; the KICK/SNARE tabs
are gone. Tune knobs read as the note when Note snap is lit. Default window
1000 × 720. Every display refreshes at 15 Hz and repaints only when an input
moved.

**The ghost snare** (2026-09-03, the user's ask: a ghost snare on its own key,
linkable to the main snare's parameters "such as tone" or fully its own): the
second snare pad (`PadIndex::snare2`, note property `snare2Note`, E1 by
default) is the GHOST, with its own 24 parameters `g1*` at `kSchemaV5` plus
`g1Link` (default on). `SnareIds` names one snare-engine pad's IDs so the
snare page, its three pictures and `pullSnare` are written once and run for
both; with LINK lit the pull copies the drum identity from Snare 1 — Tune,
Follow key, Note snap, Spread, Tone, Snappy, Shape — into the ghost's
settings and the page greys those controls, so the ghost is the same drum
under a different stroke (Decay, Start, Drop, Body, Wires, Wires decay,
Rattle, the crack, Level, Gate, velocity stay its own). The pictures read
through the link too (`DrumDisplay::readLinked`), so the ghost's mode bars
and wires curve show what will sound. Defaults are a ghost's: 110 ms decay,
Body 50, Wires 80 over 80 ms, Rattle 20, Crack 20, Level 60. The pad strip
names it GHOST and opens its page; the three kits carry a ghost each. No DSP
changed: the engine already ran the pad on Snare 1's defaults.

Break-checks at I3, each seen red then reverted: mode ratio 1.6 → 1.5
(ratio test red), Spread ignored (spread-0 test red at all four rates), the
drop retuning forever (drop test red, 3601 retunes), the follower run at
Rattle 0 (exact-zero check red), the shell leaking into the wires by one
part in 10¹² (sum check red — a first attempt at this break was overwritten
by the envelope assignment and proved nothing, so it was redone), the
shell's floor at −400 dB (retire test red, "active hits 1"), `release()`
emptied (gate test red on both releases).

Break-checks at I1, each seen red then reverted: a staircase increment
(pitch test red), the control grid restarted per callback (block-size test
red), no alignment delay (latency test red at ×2/×4/×8), the blocker placed
in the neutral path (neutral test red), the envelope kill removed (retire
test red, "active hits 1").

**To resume** (a fix, or a later phase): read CLAUDE.md in full, then this
file; take the first `pending` phase. The next is I4 — but the user's ears
run this project: the rig has not yet heard I2.1 or I3, and that report comes
before the hats. The non-negotiables every phase here
honours, in one place:

- One phase = one commit. Tests written and RUN in that commit; every
  mechanism seen red first or break-checked (edit → red → revert), with the
  measured numbers pinned in the test comments and quoted in the commit
  message.
- Build the whole tree before pushing (`./scripts/build.sh NONE --test` or
  the cmake equivalent with no `--target`), run all tests, and run
  Steinberg's validator on any plugin whose bundle changed.
- The qemu-aarch64 cross-check is **deliberately not run** (CLAUDE.md 2.3
  gate); say so in every commit message rather than implying coverage.
- New first-party files carry the six-line licence header copied from a
  neighbour. No model identifiers in anything pushed. The commit footer comes
  from the harness — use whatever the current session mandates.
- Derive DSP and measure it; anything taken from a source is attributed at
  the point of use AND in `docs/DSP-REFERENCES.md` (CLAUDE.md §9). The one
  table taken here is the 808 cymbal generator's oscillator frequencies (CC BY
  3.0), attributed in `HatEngine.hpp`. Setters that clear or re-aim state
  carry no-op guards (`dsp::isExactly`). Per-hit settings are snapshotted at
  note-on; only the chain, level and pan are smoothed. Silence in → exact
  zeros out. Neutral settings are bit-exact, not merely transparent — by
  predicate branch, never arithmetic.
- Parameter string IDs and every choice list are frozen at birth and
  append-only (CLAUDE.md §8). MIDI note, choke group and sample path are
  state-tree properties, never parameters.
- The sources are user-supplied PDFs in `technical references/drumsynth/`,
  read first-hand 2026-09-02; statuses in `docs/DSP-REFERENCES.md` ("Drum
  synthesis — Ictus"). If another source is needed, ask the user with URLs
  per CLAUDE.md §9 — do not work around it silently.
- The prior art to copy patterns from: Sonitus (`plugins/Sonitus/Dsp/
  SonitusEngine.cpp`) for the generator-side oversampling, the control-chunk
  cut, the idle skip and the latency declaration; Malleus
  (`plugins/Malleus/Dsp/MalleusVoice.hpp`, `MalleusEngine.hpp`) for the
  per-hit snapshot, seeded exciters, `TensionDrop` use and activity-based
  retirement; Svarayantra (`plugins/Svarayantra/Source/PluginProcessor.cpp`)
  for the atomic file hand-off; Membrana for the PLAN/Continuity discipline.
