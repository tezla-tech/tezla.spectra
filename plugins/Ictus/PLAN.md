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
| **Fletcher & Rossing, *The Physics of Musical Instruments*** (the whole book, 628 pp., `technical references/drumsynth/new/`) | **read first-hand 2026-09-05**, §3.6 and ch. 20 | The plate's mode law: f = c (m + 2n)^p, p = 2 for a flat plate, p < 2 for cymbals; **Table 20.1**, the 14-inch thick cymbal at p = 1.47 (taken); the free-plate ratio table 3.2; the cascade (2–10 kHz builds 10 dB in 50 ms, sub-700 Hz drains in 200 ms, 3–5 kHz shimmer dominates 1–4 s); per-mode decay times. **See "What else the book bears on" below.** |
| Perrin, Swallowe, Zietlow & Moore, "The normal modes of cymbals", Proc. IoA 30(2) 2008 | read first-hand 2026-09-05 | Measured modes of an 18-inch crash and a 12-inch ridged cymbal — (3,0) 64 Hz to (23,0) 1864 Hz; (2,0) 62, (3,0) 135, (10,0) 971, (13,0) 1421 Hz — Q of 10³–10⁴, split doublets, mixing only between near neighbours. Where the circle-family offset β = 7.4 was read off: (2,1) on (9,0), (1,1) on (8,0). |
| Chaigne, Touzé & Thomas, "Nonlinear vibrations and chaos in gongs and cymbals", Acoust. Sci. & Tech. 26(5) 2005 | read first-hand 2026-09-05 | The cascade's mechanism: quadratic (curvature) nonlinearity, combination resonances pΩ = a_i ω_i + a_j ω_j with |a_i|+|a_j| = 2, 5–10 active modes, periodic → quasiperiodic → chaotic and low-dimensional (not random). Why the shared bank's Bloom was tried on the plate, and the physics the parked build-up would model. |
| Ducceschi & Touzé, "Modal approach for nonlinear vibrations of damped impacted plates: application to sound synthesis of gongs and cymbals", JSV 344, 2015 | read first-hand 2026-09-05 | The damping law the plate's per-mode T60 follows, c_p = 0.007 ω^0.7 for the cymbal (taken as the exponent); the stick as a 1 ms raised cosine; ~1000 modes and 3 h of CPU per second for a full cymbal — the number that says 64 modes plus a noise layer is the honest budget here, not a corner cut. |
| Harrison & Hill, "A scientific approach to microphone placement for cymbals in live sound", Proc. IoA 35(2) 2013 | read first-hand 2026-09-05 | Nothing numeric taken. Confirms for 14-inch hi-hats specifically that the low frequencies of the first 25 ms give way to rising highs, more so on heavy cymbals — the build-up is a hat property, not only a crash's. |
| Reid, "Analysing Metallic Percussion", SOS May 2002 | read first-hand 2026-09-05 | "Hundreds of energised modes... at discrete frequencies", the strike → few hundred Hz → a few kHz over hundreds of ms → mid frequencies last; at high amplitude the spectrum is "in essence noise", so the drum-machine designers "were not completely wrong to use white noise". Background; nothing numeric taken. |

**The access route, again.** Every one of the six was refused by the proxy in
one pass on 2026-09-05 (J-STAGE, HAL, ScienceDirect, AIP, ResearchGate, Sound
On Sound, ensta-paris). Per CLAUDE.md §9 -- now a standing instruction in the
user's words -- the block was reported before any design was started, the URLs
listed, and the user fetched all six plus the complete book into
`technical references/drumsynth/new/`.

### What else the book bears on

Fletcher & Rossing is the standard text and the user now has the whole of it on
disk, so it is worth saying which chapters would change which engine if read
against them. None of this is done; each is a candidate for the round it names.

| Chapter | Engine it bears on | What it would settle |
|---|---|---|
| **18.9 Bass drums, 18.12 Onset and decay of drum sound** | Ictus kick | The real kick's modes and how its pitch and spectrum move in the first 100 ms -- a check on the Drop and Sigh ranges, and on the "shifted series" shell parked in `docs/ROADMAP.md` §9.3. |
| **18.10 Side drums, 18.13 Snare action, 18.11 Tom-toms** | Ictus snare, ghost, Perc | Measured membrane-mode frequencies for snare and tom heads (the (0,1) fundamental, the (1,1) pair, air loading), and the physics of the wires -- the snare shell's 1 : 1.6 : 2.2 ratios and the rattle model were built from Reid and Bilbao's abstract; these chapters are the primary source. |
| **18.5–18.7** (timpani air loading, radiation and decay) | Ictus kick and snare | Why the low modes of a drum radiate the way they do -- a check on the kick's Tail and the snare's body decay. |
| **20.3, 20.6, 20.7** (cymbal transients, nonlinear coupling, build-up) | Ictus hats -- **the parked build-up** | The equations (20.1–20.6) for how mode j grows from mode i and peaks at t*: the coherent per-mode drive the parked item needs, with the timescale measured. |
| **20.5, 20.8–20.9** (tam-tams, gongs, pitch glide) | Malleus (plates, gongs, Bloom) | Measured tam-tam and gong modes and the hardening/softening pitch glide -- Malleus's Bloom was derived from the von Kármán argument alone; this is the measurement to check it against. |
| **19 Mallet percussion** (bars, tuning, resonators, chimes, gamelan) | Malleus | The bar and tube mode tables Malleus computed in-house, now checkable against the book's measured ones; gamelan tunings for the tuning library. |
| **21 Bells** | Malleus | The church-bell partial series (hum, prime, tierce, quint, nominal) that `docs/DSP-REFERENCES.md` still records as trusted second-hand -- it can now be pinned first-hand, and the handbell and Chinese two-tone bell tables are new material. |
| **3.5–3.8 Plates** | the whole suite's modal work | The clamped, free and simply-supported circular-plate tables (3.1–3.3), Chladni's law, and the rectangular-plate cases -- the ground the plate, the snare shell and Malleus's plate mode all stand on. |

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
| I4 hat + clap engines, choke | done in code; not yet played on the rig |
| I4.1 the rig's verdict on the hats: depth, and the noise made part of the metal | **played on the rig**; the round's findings are the I4.2 row |
| I4.2 the rig's second round: Drive fixed, a gate on the clap | done in code; not yet played on the rig |
| I4.3 the rig's third round (2026-09-05): "thin and tinny" → **Plate** (a 64-mode cymbal from the published mode law) and **Grit**, six sources fetched by the user and read first-hand | done in code; not yet played on the rig. Plate 0 / Grit 0 is the old hat bit for bit (golden render) |
| I4.4 the rig's fourth round (2026-09-05, the user's asks after hearing the plate): more say over the hiss (**Air tilt, Air attack, Grain, Vel > Air**), the open pad's own **Open hold** behind a Link lamp, **Under** and **Knock** on the kick, **Ring** and **Thump** on the snares, and a **Pan** per pad on a MIX page | done in code; not yet played on the rig. Every default neutral: the round's golden render (four rates, stereo, every existing stage on) is byte for byte the schema-10 engine |
| I4.5 the rig's fifth round (2026-09-05, after playing I4.4: "amazing stuff"): the pads render **mid and side** -- **Air stereo, Metal stereo** (hats), **Wires stereo** (snares), **Stereo** (clap), a **Width** and a **Mono below** per pad and a **correlation readout** on the MIX page; a **Room** (shared early reflections) on the kick, the snares and the clap; the plate's **Wash**; the snare's **Head**, **Wires tilt** and **Bed**; the rattle's own **Rattle decay, Rattle tone** and **Tension** (asked for while the round was in flight); the kick's **Drop curve**; the **clap layered under Snare 1** | done in code; not yet played on the rig. Every default neutral: the golden render still byte for byte (md5 `d933a800…`) with the whole mid/side path in place. `tezla-measure ictus` table 5 carries the round's numbers; the plate pages scroll when taller than the window |
| **PAUSED 2026-09-04 at the user's request** while Stryda is built (`plugins/Stryda/PLAN.md`). **Resume at I5.** I4.3 and I4.4 were direct asks on 2026-09-05 (CLAUDE.md §1, the chat wins for that task) and do not lift the pause | — |
| I5 punch chain + TransientShaper | pending |
| I6 humanise + velocity | pending |
| I7 multi-out buses | pending |
| I8a sample DSP (WavFile, upsample2x, SincInterpolator, SampleVoice) | pending |
| I8b sample loader (slots, Align, Render, pad UI) | pending |
| I9 editor close-out | pending |

**Paused at I4.1, 2026-09-04.** The user asked for the FM synthesiser next, so
Ictus stops here and this file is the handoff — the same precedent as Phonoss,
paused at V2 while Crossbar was built and resumed at V3 with nothing lost.
Nothing is abandoned and nothing is half-applied: the tree is green, the
validator passes 47/47, and every phase through I4.2 is committed. **The resume
point is I5** (the per-pad punch chain), and the rows below it are untouched.

What is worth knowing on resuming, beyond the table: **I2.1, I3, the rounds
after I3, I4 and I4.2 are done in code but have not been played on the rig**, so
the first thing an ear round should cover is everything after the hats. The six
presets still ship Drive at 20–55 % and were voiced through the broken Drive
stage that I4.2 fixed, so they need re-voicing before they mean anything.

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

**I4 shipped** (2026-09-03): `plugins/Ictus/Dsp/HatEngine.hpp` -- six
`dsp::Oscillator` pulses at a set's ratios times Tune, polyBLEP and at the
paper's measured 47.98 % duty, summed into two `SvfFilter` band-passes
(Colour, and the upper one at the paper's 7100/3440 spacing) under a
high-pass at half of Colour, then an `Adsr` killed at its zero sustain. Four
**ratio sets** -- *Metal* (the paper's six frequencies, the one table taken;
named for what it is, never for the product), *Bell*, *Trash*, *Wide* -- with
**Harmonics** a continuous 0..7 position that morphs geometrically by rank
between adjacent sets, positions past the last clamping, so appending a set
never repoints a saved value. **Spread** detunes the six along a pattern that
sums to zero; **Air** adds seeded noise before the filters. The two hat pads
share every control but their decay, and a closed hit chokes the open pad
over 5 ms with `HatSettings::choke` arming it.
`plugins/Ictus/Dsp/ClapEngine.hpp` -- a `BurstScheduler` counting in samples
to four bursts a **Flam** apart, their exponential envelopes summed over one
seeded noise, a **Tail** `Adsr` starting with the fourth, one band-pass
(**Colour**). Both are one-shots: their `release()` does nothing and a
note-off on them is a no-op. 17 parameters appended at `kSchemaV6`, the HATS
page (one page, both pads) and the CLAP page, `PartialsView` and `BurstView`,
the three hat and clap pads opened in the strip, and hat and clap settings in
the three kit presets.

Measured at I4 (`tezla-tests hat`, `clap`, `a_kit_of_all_eight_pads`;
`tezla-measure ictus` table 3):

| claim | figure |
|---|---|
| inharmonic energy below 20 kHz at the internal rate Auto runs, the four sets at Tune 900 Hz | **-77.2 / -77.4 / -76.5 / -74.2 dB** (section 7's gate is -60) |
| the same at 48 kHz with oversampling Off, against a naive pulse bank | -35.5 / -35.5 / -33.7 / -35.5 against -15.9 / -17.3 / -14.6 / -12.3 -- the polyBLEP is worth **18 to 23 dB**, and Off costs the other 35 |
| a hat at 44.1 / 48 / 96 / 192 kHz through the instrument at Auto | centroid **5544 / 5562 / 5562 / 5563 Hz**, audible energy within 2.7 % |
| the ratio sets at an integer position | **bit-exact** the table; halfway between two, the geometric mean to 1e-12 |
| Spread 0 partials | **exactly** Tune x ratio; at Spread 1 the six move by the pattern and their cents sum to 0 |
| hat retirement, everything on, 0.3 s open decay | last non-zero at **0.300 s**, active 0, exact zeros after |
| the closed hat's choke | open pad's hits **0** within the 5 ms fade; **1** with Choke dark |
| the clap's bursts at 44.1 / 48 / 96 / 192 kHz | fired on **exactly** samples 0, S, 2S, 3S for S = round(Flam x rate); in the audio, 0.0 / 12.1 / 24.1 / 36.0 ms for a 12 ms Flam |
| clap retirement, 0.25 s tail | last non-zero at **0.283 s**, active 0; a 0.6 s tail runs 1.8x longer |
| hat / clap engines at 192 kHz | **41.7 / 8.6 ns per sample** (0.80 % / 0.16 % of a core) |
| the kit -- all eight pads, everything on, 48 kHz x4 | **6.04 %** of a core against 4.8-5.2 % for five pads; idle 0.001 % |

Two bugs the measurements found before any ear did, both worth keeping:

1. **`SvfFilter::setResonance` takes the CONTROL, not Q**, and the control is
   geometric from Q 0.5 to Q 500. The clap's band-pass was set to 0.8 meaning
   "gentle" and got **Q 125**, which rings for 33 ms at 1.2 kHz: the four
   bursts smeared into one another and the spacing test found twenty-five
   onsets in four bursts. The hat's bands were 0.9, or Q 250 -- two whistles
   rather than a band of metal. `SvfFilter::resonanceForQ` is the fix and the
   engines now name their Q.
2. **A Hann window cannot measure an alias floor under a thousand harmonics.**
   Its first sidelobe is 31 dB down and the skirts pile up: the hat read
   -57 dB however good the generator was. Blackman-Harris, 92 dB down, reads
   -77. The measure also counted the transform's mirror half at first, which
   reads -3 dB for every signal ever.

Break-checks at I4, each seen red then reverted: the morph made linear rather
than geometric (16 checks red), Spread ignored (6), the hat's envelope kill
removed (retire test red), the closed hat's choke removed, the oscillators
started from a seed-hashed phase (the reproducibility test red), the clap's
burst floor removed so nothing reached exact zero (3), the scheduler spacing
fixed at 512 samples rather than the Flam (15). One break-check FAILED to go
red and changed a claim: removing the `airOn_` branch left every sample
identical, because `0.0 * bipolar()` is already exact -- so that branch is a
cost saving, the test's name now says what it really proves, and the engine's
comment says which of its two branches is which.

The notes below were the handover written before the phase, kept because
they are still the map of how the pieces fit.

*What already exists for the three pads, so it is not built twice:*

- `PadIndex` in `IctusEngine.hpp` already has `hatClosed`, `hatOpen` and
  `clap` (the enum order is kick1, snare1, hatClosed, hatOpen, clap, perc,
  kick2, snare2 — the choke group stores these as indices, so never reorder),
  and `padNoteProperty` already names `hatClosedNote` / `hatOpenNote` /
  `clapNote` (42 / 46 / 39). The pad strip (`PadStrip.cpp`, `kPads`) already
  shows HAT C, HAT O and CLAP with `hasPage false` — flip it when the page
  exists; their component IDs are `pad-hat-c`, `pad-hat-o`, `pad-clap`.
- `Engine::choke (PadIndex)` fades every hit of a pad over
  `Pad::kChokeFadeSeconds` (5 ms) and its `switch` must name every
  `PadIndex` — a mistyped case label once compiled silently as a bool
  comparison; the tree is `-Wall -Wextra -Wshadow` clean and stays so.
  `Engine::nextSeed (PadIndex)` hands out the per-hit seed **and** counts the
  pad's hits for the strip's flashes, and `padVelocity_[pad]` is set beside
  it, so every engine's `start` is reached through the same path as
  `startKick` / `startSnare` in `IctusEngine.cpp` (tuning, note snap,
  velocity, seed, then `pad.start (...)`).
- `Pad<Engine>` (`Pad.hpp`) is what an engine plugs into; the contract is
  `prepare (rate)`, `reset()`, `start (settings, velocity, seed, ...)` — a
  per-hit snapshot — `release()`, `advanceControl (numSamples)` on the
  32-sample control grid the pad restarts at note-on, `double process()` and
  an **exact** `bool isActive()`: the kick and snare kill their `Adsr`s the
  moment they reach zero sustain and cut at an energy floor, and every
  retirement test asserts the active-hit count, never silence.
- `EngineParameters` holds kick1, kick2, snare1, snare2, perc; it gains one
  `HatSettings hat` (shared by both hat pads, with `decayClosed` and
  `decayOpen`, the pad choosing) and one `ClapSettings clap`. The processor's
  `pullParameters` copies the tree into it each block; `pullSnare` with
  `SnareIds` is the shape to copy for a pad-agnostic pull.

*The decisions that settle the two engines:*

- **Hat**: six `dsp::Oscillator` pulses, width 0.5, polyBLEP, at the set's
  ratios × Tune; sum → `SvfFilter` band-pass (Colour, 1–12 kHz, the paper's
  3.4 and 7.1 kHz as landmarks in the tooltip) → high-pass → `Adsr` (attack 0,
  decay Closed or Open by pad, sustain 0, killed at zero). Ratio sets are an
  **append-only** table with a `static_assert` against its choice list
  (`hatSet::`); Harmonics is an absolute position 0–7 that morphs by rank in
  log frequency, `ratio_i = A_i^(1−t)·B_i^t`, positions past the last set
  clamping. The 808 cymbal frequencies (205.3, 369.6, 304.4, 522.7, 800 and
  540 Hz; Werner, Abel and Smith, CC BY 3.0, read first-hand) are the one
  thing copied: attribute them at the point of use in `HatEngine.hpp` **and**
  in the `docs/DSP-REFERENCES.md` row, which already promises that. Spread
  detunes the six, Air blends seeded noise (`SmallRandom`, seed ^ a salt as
  `SnareEngine::kWiresSalt`); both exact at 0 by a branch, never arithmetic.
  A closed-hat note-on chokes the open pad by default (`choke (hatOpen)`
  from the closed pad's start); velocity → decay and colour amounts.
- **Clap**: a `BurstScheduler` of countdowns — three pre-bursts `Flam` ms
  apart (8–14 ms), then the main burst — over one seeded noise; the sum of
  the bursts' envelopes × noise → `SvfFilter` band-pass (Colour, 1–2 kHz);
  `Tail` an `Adsr` decay that starts with the main burst. The spacing jitter
  waits for I6.
- **IDs** (frozen at birth, appended at `kSchemaV6`, the pad-prefix
  convention of §Parameters): `ht*` for the shared hat set (`htTune`, `htSet`,
  `htHarmonics`, `htColour`, `htSpread`, `htAir`, `htLevel`, `htVelDecay`,
  `htVelColour`), `hcDecay` and `hoDecay` for the two decays, `cp*` for the
  clap (`cpFlam`, `cpColour`, `cpTail`, `cpLevel`, `cpVelLevel`). Every one
  defaults to a sound, not to silence — these pads have never had parameters,
  so there is no saved project to keep neutral for; the three kit presets
  gain hat and clap settings, nothing reordered.
- **Pages**: `PlatePage` (`PlatePage.h`) builds a page from plates —
  `beginPlate (heading, detail, tintIndex, sameRow)`, `addKnob (id, name,
  tooltip, Emphasis)` with `Emphasis::lead` / `normal` / `trim`, `addLamp`,
  `addDisplay (unique_ptr, columns)`, then `setControlEnabled`, `setTooltip`,
  `setValueText`, `setNote` for the page's footnote; component IDs are the
  parameter IDs so the render tool can press them. One HATS page that both
  hat pads open (HIT strikes whichever pad is selected) and one CLAP page;
  `showPage` in `PluginEditor.cpp` is 0 kick, 1 snare, 2 ghost, 3 tuning —
  append 4 and 5 and route them in `selectPad`. Pictures follow
  `Displays.h`: subclass `DrumDisplay`, `gather` pushes **every** input that
  changes the picture (the ghost's caption once missed the LINK state and sat
  stale), `update` derives, `paint` draws with `plotArea` / `paintFrame` /
  `along`. Suggested: the six partials as bars on a log axis with the
  band-pass curve over them from `SvfFilter::magnitudeAt`, morphing as
  Harmonics turns; the clap's burst envelope against time.
- **Tests** (`tests/test_Ictus.cpp`; helpers already there: `crossings`,
  `maxStep`, `lastNonZeroSample`, `peakFrom`, `peakHzBetween` on a 2^18 FFT,
  `rmsOfDifference`, `meanHzOfCycles`, and `CHECK_CPU_BUDGET`): the six
  partials land at the set's ratios × Tune at 44.1 / 48 / 96 / 192 kHz; the
  alias floor of the polyBLEP pulses against a naive pulse, per set, pinned;
  morph continuity across a set boundary (a Harmonics step of 0.001 moves no
  ratio by more than its share); Spread 0 and Air 0 bit-exact against the
  bare sum; closed chokes open in 5 ms and a choke landing mid-fade takes the
  shorter remaining time; the clap's bursts at Flam spacing (onsets from the
  envelope) and its Tail; retirement by activity count for both engines;
  silence in → exact zeros; the block-size bit-identity test and the kit CPU
  test widened to hats and clap (the kit budget is 0.12 today — raise it only
  to what is measured, and quote it). `tezla-measure ictus` gets table 3
  (hat partials and alias floor per set, ns per sample; clap burst timing)
  beside tables 1 and 2 in `tools/measure_main.cpp`, where `renderPattern`
  renders a note list through the whole engine.
- **Rituals**, so nothing is rediscovered: every test seen red (edit → red →
  revert) and named in the commit; the whole tree built with
  `cmake --build build -j4` and no `--target`; `build/bin/tezla-tests` all
  green (1034 today); the validator at
  `/home/user/vst3sdk/build-val/bin/Release/validator` on
  `build/plugins/Ictus/Ictus_artefacts/Release/VST3/Tezla Ictus.vst3`
  (47 of 47); screenshots through `xvfb-run -a
  build/plugins/Ictus/IctusRender_artefacts/Release/IctusRender editor` with
  the verbs `press:<id>@x,y`, `release:<id>@x,y`, `tick:n`, `audio:secs`,
  `shot:path`, `dump` — clicks dispatch **asynchronously**, so put `tick:1`
  between a release and `audio:` or the hit is not there yet; the docs in the
  same commit (`plugins/Ictus/README.md`, `plugins/README.md`, the root
  `README.md` test-count line, `docs/DSP-REFERENCES.md`, this table); and
  "The qemu-aarch64 cross-check was not run (CLAUDE.md 2.3 gate)" plus "not
  yet played on the rig" in the message.

*Two facts recorded nowhere else:*

- The pads name notes with middle C as C3 (36 = C1, `noteNameFor`), and FL
  Studio's piano roll calls MIDI 60 C5 — the strip shows the MIDI number for
  that reason. An octave-naming option was offered to the user, not built.
- Panel ideas offered to the user on 2026-09-03 and not built, for when they
  are asked for: draggable handles on the envelope pictures; a live waveform
  of the last hit and a level meter per pad; a pad grid with note-learn; a
  "Both" output option once I7 lands; choke groups exposed with I4.

**I4.1 -- "the hats sound crap"** (2026-09-03, the user, within an hour of I4
landing). Two asks: more controls for a wider range of sounds, "including
more lush", and -- after a correction from the clap to the hats -- "the hats
need whitenoise/other hiss working along with the harmonics". Then: a gate
with a release, on both pads.

The diagnosis, and it was not a bug: six pulses through a band-pass is a
SPARSE COMB. It reads as a metallic chord rather than a cymbal, it has no
transient, it decays at one brightness from the moment it starts, and the
noise sat beside the metal rather than belonging to it. Five things fixed it,
each a control:

| control | what it does | measured |
|---|---|---|
| **Ring** | the low three oscillators times the high three: a ring modulator holds the sum AND difference of every pair of harmonics, so one multiply turns two sparse combs into a dense inharmonic wash | energy off the six harmonic series: **-77.2 dB bare, -6.5 / -2.6 / -0.7 dB at Ring 25 / 50 / 100** |
| **Sizzle** | the hiss through a band-pass at each of the six partials, so the noise rings where the metal already does -- the plate speaking rather than a second instrument | share of the hiss within a semitone of a partial: **16.6 / 23.4 / 32.3 / 39.2 / 40.9 %** across the control |
| **Damp** | a low-pass closing as the hit decays: on a real cymbal the high modes die first | corner **17.9 kHz at the strike to 2.2 kHz at 200 ms** at Damp 100; out of the path at 0 |
| **Drive** | the overdrive the Nord chapter's cymbal patch ends with, antialiased | tail centroid moves from 5954 Hz undamped to 2091 Hz damped |
| **Strike** | the stick, a short loud transient over the body | first 8 ms **1.455 -> 2.886**, the ring after it unchanged within 0.1 % |

Also **Air tone** and **Air decay** (the noise layer's own colour and length,
so a shimmer can outlive the metal or a chiff sit on the front of it),
**Width** and **Highpass** as their own controls, **Hold** and **Shape** on
the envelope, **Vel > Strike**, and **Gate + Release on both hat pads** -- a
ramp on the output rather than a release on the envelopes, because the
envelopes are before the filters here and releasing them would leave the
bands ringing after the key came up. The clap gained **Bursts** (2 to 6),
**Skew**, **Snap**, **Noise** and **Noise tone**, **Width**, **Tail tone**,
**Drive**, and a **Body**: three inharmonic modes struck by every burst, so a
clap has a pitch under the hiss. 25 parameters appended at `kSchemaV7`; both
pages rebuilt; a *Lush Hats* preset on the end.

Three bugs the measurements found before any ear could:

1. **The ring modulator's band-limit was a FRACTION of the sample rate.** So
   the products were built from a 24 kHz band at 192 kHz and a 6 kHz band at
   48 kHz -- the same patch was a different instrument at a different host
   rate, which is exactly what CLAUDE.md section 6 forbids. Measured before
   the fix: the spectral centroid moved 6951 Hz to 4849 Hz, 30 %. It is an
   absolute 20 kHz now, with a quarter-of-the-rate ceiling for rates too low
   to give it that, and the four host rates agree within 0.6 %.
2. **The clap's cavity was retired before it had ever sounded.** The energy
   check ran at the first control tick, before the first burst had struck the
   bank, so a body-only clap rendered pure zeros. It is gated on the burst
   pattern being finished now.
3. **The per-engine CPU figures were measuring denormals, not DSP.** A bare
   engine loop has no `ScopedNoDenormals`, and the clap's filters spend most
   of a second with near-zero input: it read **196.8 ns a sample** against
   8.6 for the version before it. With the guard the plugin always has, it is
   **16.3 ns**. The hat's real figure is 112.4 ns, 2.2 % of a core, and the
   whole eight-pad kit is **7.62 %** at 48 kHz x4 against 6.04 % before.

Break-checks at I4.1, each seen red then reverted: the ring product never
added; Damp's corner frozen; the sizzle bank bypassed; the stick never added;
the hat's gate ignoring note-off; the clap's skew ignored; the ring
band-limit made a fraction again. One FAILED to go red and changed a test:
the cavity's mode ratios set to a harmonic 1 : 2 : 3, which the test could
not catch because it computed what it expected from the same table -- so it
now asserts inharmonicity against the integers instead, and that assertion
promptly rejected the third ratio (2.13, only 6 % off a harmonic 2), which
moved to 2.41.

**I4.2 -- the rig's second round** (2026-09-03). Three findings, two fixed
here and one deliberately left.

1. **Drive was broken in both engines, and the report described the bug
   exactly.** "At 100% it makes some audible driven clap with no noise tail,
   as I reduce it to 0 the sound goes almost completely silent, at 0 I hear my
   good clap." `dsp::SoftClipExcess` evaluates to what the clipper CHANGES --
   `clip(x) - x` -- and not to the clipped signal, and it was being taken
   ALONE: below the knee that is exactly zero, so the pad went silent at low
   drive; above it, only the harsh residue survives and every quiet part, the
   tail included, is gone; at exactly 0 the branch skips the stage and the pad
   is right. The excess is ADDED back to the driven signal now, which is what
   the ADAA adaptor's own comment describes and what `KickEngine` already did.
   The trim went with it: `g^-0.75`, chosen by measurement because a clipper's
   output depends on where the signal already sat against the threshold and no
   single exponent holds both engines level -- the clap's sum peaks at 0.37 and
   is barely clipped, the hat's reaches 1.5 before the stage. Measured over the
   whole control: the clap rises **2.1 dB** and the hat falls **2.7 dB**, with
   peaks falling 0.367 -> 0.175 and 1.486 -> 0.672.
2. **The clap has a Gate and a Release**, at `kSchemaV8`, matching the kick,
   the snare and (from I4.1) the hats. A ramp on the output rather than a
   release on the envelopes, for the hat's reason: the layers are filtered
   after they are enveloped, so releasing the envelopes would leave the bands
   ringing past the key.
3. **The presets are bad and were LEFT THAT WAY, at the user's instruction**
   ("your default parameters for the hats and clap are really bad in the
   presets, but if I do my own changes, I can make it sound good ... besides
   fixing drive, gate, release, you may leave it"). Worth knowing before
   touching them: every kit ships Drive between 20 and 55 %, so **every one of
   them was voiced through the broken drive** and none has been heard since
   the fix. Re-judge them on the rig before changing a number -- they may now
   be wrong in a different direction, or closer than they were. The user can
   dial in a good sound by hand, so the engines are not the problem; the
   starting points are.

Break-checks at I4.2, each seen red then reverted: the drive taking the
clipper's residue alone (the original bug, 4 checks red across both engines);
the clap's gate ignoring note-off.

**I4.3 -- "thin and tinny"** (2026-09-05, the user: *"the hi hats sound really
thin and tinny with the controls we have, is it possible you can make the hihats
better and the possibility to make them sound fatter and more like the [classic
sampled drum machine] hi hats? they are nice and chunky"*). Two controls, both
appended at `kSchemaV10` and both exactly neutral at 0, and one preset.

**The diagnosis needed no paper.** Six pulses through a band-pass is the
classic analogue cymbal circuit, and it is thin by construction: open the bands
and you hear a pulse chord, not a cymbal, because the source has no body that
survives them. The chunky hat the user means is a *recording of a real pair of
cymbals* through a six-bit sample path. A real 14-inch plate has a mode every
20-30 Hz -- some 900 below 20 kHz by thin-plate theory -- so above 2 kHz it is
structured noise and below that it has body. Six oscillators cannot make that
whatever the filters do. **The design did need papers**, and per CLAUDE.md §9
(now in the user's own words) the six were listed and fetched before a line was
written; what each settled is in the sources table above.

**Plate** crossfades the six pulses against a 64-mode `dsp::ModalResonator` --
the bank Malleus and the snare shell already use. The modes follow the
modified Chladni law the literature fits real cymbals to, `f = c (m + βn)^p`:
`p = 1.47` is **taken** from Fletcher & Rossing's Table 20.1 (the 14-inch thick
cymbal, the closest thing in the table to a hat top and the only cymbal a
single line fits); `β = 7.4` is design read off Perrin et al.'s two cymbals
(Chladni's flat plate has 2; a domed cymbal's circle families sit far higher);
Tune sets `c` so (2,0) sits AT Tune, as the lowest pulse does; a deterministic
±1.2 % jitter stands in for the doublet splitting Perrin measured and keeps
every pair of modes incommensurate. Each mode's own T60 follows Ducceschi &
Touzé's cymbal damping law, `c_p ∝ ω^0.7` (the exponent taken; anchored so a
mode at 1.5 kHz rings twice the pad's decay). The strike hits every mode at
once, in phase, falling as f^-0.5. Both ends of the crossfade are branches:
at 0 the bank is never built and the path is the old engine; at 1 the metal's
weight is exactly 0.

**Grit** quantises the summed layers before Drive, 16 bits at 0 (the crusher's
exact bypass) to 4 at 100, geometric, with about six bits two thirds of the way
up. It is quantisation used as a saturator at the internal rate -- the images
the decimator removes are gone and the in-band, signal-correlated error is what
stays -- and the header says so rather than calling it a bit-crusher; CLAUDE.md
§7's host-rate rule is for the folded images, and a per-pad host-rate stage
waits for I7's buses.

**Fat Hats** is the preset: Plate 85, Grit 55, Colour 1.8 kHz, Width 100,
Highpass 350, a hard stick. The kit presets are untouched, as instructed at
I4.2.

Measured (`tezla-tests plate`, `grit`, `hat`; `tezla-measure ictus` table 3):

| claim | figure |
|---|---|
| Plate 0 / Grit 0 against the engine before the change: five renders, three rates, 412 800 samples | **byte for byte identical** (md5 `bba80239…` both sides) |
| modes placed at Tune 205.3 | **64**, from 205.3 Hz (exactly Tune) to 9404 Hz; (3,0)/(2,0) = 1.5^1.47 within the jitter |
| the low twelve modes within 30 cents of a harmonic of the lowest | **1** (the law's own 3^1.47 = 5.02); a harmonic series would be twelve |
| Spread 1 against Spread 0 | moves a mode by up to **20.9 cents**; the lowest never moves |
| energy off the six pulses' harmonic series above 4 kHz, Tune 900 | metal **−73.0 dB**, plate **−0.8 dB** |
| Grit at 4 bits: energy off the plate's own 64 modes | **−64.6 → −2.5 dB**; level **+0.13 dB** (texture, not volume) |
| level, plate 1 against metal through the default chain | at 1 / 0.07: +2.2 to +3.2 dB (fat chain −0.4 to −0.7); at 12: +2.1 / +1.5 dB; at **10**: **+1.5 dB closed, +1.1 dB open** — the default Drive compresses the plate's hotter strike, so it does not follow the gain linearly |
| a plate hat with Grit, 0.3 s open decay | last non-zero at **0.361 s**, active 0, exact zeros after |
| through the instrument at Auto, 44.1 / 48 / 96 / 192 kHz | centroid **4845 / 4798 / 4795 / 4793 Hz**, audible energy within 3.3 % |
| one second at 192 kHz | test: plate 1 grit 0.6 **169–191 ns/sample** across runs (3.2–3.7 % of a core); `tezla-measure`: hat **122 ns**, with the plate **183 ns** (2.3 → 3.5 %); Bloom would have added about 100 more |

**What was tried and removed, with the numbers.** The bank's Bloom -- the von
Kármán quadratic coupling, exactly the mechanism Chaigne et al. describe and
the thing that makes Rossing's 2-10 kHz band build 10 dB in the first 50 ms --
was wired in at `0.6 × velocity`. On this bank it does not work. Struck at unit
power the plate peaked at 0.55, ten times the level the coupling was calibrated
on, and saturated; struck at the calibrated 0.07 it still collapsed: at full
velocity the first 80 ms had a **spectral centroid of 128 Hz on a plate whose
lowest mode is 205 Hz**, and the level read 12-17 dB under the metal because
the high-pass then removed what the bank had become. The mechanism: 64 modes a
few tens of hertz apart put strong difference tones into the coupling term, and
a resonator driven far below its own frequency answers with a forced response
about `1/(2 sin(ω/2))` times the drive -- 150× for 205 Hz at 192 kHz. At a tenth
of the amount nothing collapsed and nothing measurably built either. So the
plate is linear; the fall from bright to dark is the damping law's and Damp's;
the build-up is parked in `docs/ROADMAP.md` §9 with the route that would work
(a coherent per-mode drive -- a slow unipolar push cannot excite a fast mode,
which is why "spread the impulse over 50 ms" is not it). It also saved 100 ns a
sample.

**Also fixed, with the latitude the user gave to revisit the earlier hat work:
Sizzle did nothing it claimed at the default chain.** It rang the hiss through
band-passes at the six *fundamentals* (205–800 Hz) — and the chain then
high-passes at 1.2 kHz and listens through bands at 3.4 and 7.1 kHz, so those
resonances sat 15–30 dB under the floor. Measured at the default chain: Sizzle
100 cut the hiss by **16.5 dB** (RMS 0.171 → 0.026) and put **0.0 %** of its
energy within a semitone of a partial; what it did was dull the hiss (86 % above
6 kHz → 57 %). The I4.1 figure (16.6 → 40.9 %) was measured on the noise layer
alone with Highpass 200 and Colour 4 kHz — a chain that passes the fundamentals,
which the default one does not. The bank now sits at each partial's harmonic
nearest the band it is assigned to (the low three partials at Colour, the high
three at the upper band), `HatEngine::sizzleCentres`, shared with the picture;
the make-up came down from 2.6 to 0.65 for resonances that are inside the bands.

| claim | figure |
|---|---|
| Sizzle's six centres at the default chain | **3490 / 3348 / 3326 Hz** and **7318 / 7020 / 7200 Hz** — every one a harmonic of a partial, every one inside a band; `tezla-measure`'s noise-only rig (Tune 400, Colour 4 kHz) reads 18.1 → 54.4 % on a centre across the control |
| the hiss's level, Sizzle 100 against 0, default chain | **+1.3 dB** (was −16.5 dB) |
| the hiss within a semitone of a centre, Sizzle 0 → 100 | **15.3 % → 66.9 %** |

Seen red: the centres put back on the fundamentals fails the level and the
placement check. Sizzle 0 is unchanged; a saved project with Sizzle up sounds
different, and better — the user's words at I4.2 were that the hat presets were
bad anyway, and on 2026-09-05 that the earlier hat work could be undone or
tweaked.

**A gap in the presets gate, found and parked.** `tezla-render presets` plays
note 36 — the kick — so the two hat-only presets (*Lush Hats*, *Fat Hats*) read
as the default kick, identical to *Init Kit*, and their hats are not gated at
all. *Fat Hats* was checked by hand instead: closed **−5.9 dBFS**, open
**−5.2 dBFS** at velocity 1 and Level 70, so Level went to 85 (about −4). A
per-pad audit for the drum machine is a roadmap item.

**Two test claims were wrong and the measurement said so** (CLAUDE.md §10: a
failing test is a claim until proven otherwise, and here the code was right):
the off-series measure first read **−6.3 dB** for the plate, because the
plate's lowest mode IS the first pulse partial by design and, with the longest
decay in the bank, carried 77 % of the energy over 1.3 s -- the test was
measuring one shared line; it looks above 4 kHz now. And "no low mode within 30
cents of a harmonic" was contradicted by the law itself; the test counts
coincidences (one) against a series (twelve).

Break-checks at I4.3, each seen red then reverted with the file checksummed
back: the crossfade branch forced on at Plate 0 (the bank built at 0); the law's
exponent set to Chladni's flat-plate 2; the jitter applied to the lowest mode;
Grit's map made linear; the plate's modes made a harmonic series of Tune; the
plate added outside the envelope so it rang past retirement.

**I4.4 -- the fourth round** (2026-09-05, the user, after hearing the plate:
*"what about some more control over the hiss/air/noise? also, an independent
hold for the closed and open hat? ... any more ideas for the kick and snare to
be able to create a different variety of thicker sounds?"*, and then *"go ahead
with the first round, push so i can pull and test"*). Everything appended at
`kSchemaV11`, every default neutral, so a schema-10 project reopens bit for bit.

**The golden render.** Before a line was changed, the whole kit was rendered
with every existing stage engaged -- harmonics, tone, click and tail on the
kick, rattle and crack on the snare, plate at 0.5, grit, sizzle, ring and drive
on the hats, body and drive on the clap, master at -3 dB, twelve hits over
1.6 s -- at 44.1, 48, 96 and 192 kHz, both channels, 1 216 320 samples (a
sixty-line program against `IctusEngine.hpp` in the session's scratch space,
md5 `d933a800…`). After every change of the round the
same program renders the same bytes. That is the whole neutrality claim, and
it held through the pan path becoming stereo because the balance law's centre
is a multiply by exactly 1.0 summed in the order the mono engine always summed.

**The hats.** Four controls on the hiss and one on the envelope:

- *Air tilt* (`htAirTilt`, -1..+1): a low shelf and a high shelf of opposite
  sign about **6 kHz**, +-12 dB at the ends, `Biquad` shelves designed per hit
  and branched out at 0. The pivot moved from 4 kHz: on a wide-open hiss the
  4 kHz version had nearly all the noise above the pivot and Bright read
  +10 dB -- a volume control, not a slope. At 6 kHz: RMS 0.078 / 0.063 / 0.182
  for dark / flat / bright (still up 9 dB at full bright, because the hiss is
  mostly above the pivot whatever the pivot; said in the tooltip).
- *Air attack* (`htAirAttack`, 0..500 ms): the air envelope's own rise.
- *Grain* (`htGrain`): the kept fraction of noise samples, geometric from 1 to
  300 events a second **per second, not per sample** (`grainDensityFor`), with
  make-up p^-0.25 -- half-way in dB between constant RMS and constant peak. At
  constant RMS the far end is +-30 impulses into the soft clip; at constant
  peak it is 30 dB quieter than the dense hiss. Grain 0 draws the old stream
  exactly.
- *Vel > Air* (`htVelAir`), default 0.
- *Open hold* (`hoHold`, 0..1 s) behind *Link* (`htHoldLink`, lit by default).
  The air envelope follows the pad it is on.

**The kick.** *Under* (`k1Under`, `k1UnderInterval`, `k1UnderDecay`,
`k1UnderAttack`): a second sine whose phase advances by the body's increment
*of this sample* times 2^(-interval/12), so it is locked to the body through
the drop and the sigh and can never beat; it joins after the harmonics and the
tone filter with its own AHD (hold = the body's, decay = the body's times the
multiple, released with the gate), and the hit stays active until it lands.
*Knock* (`k1Knock`, `k1KnockTone`, `k1KnockTime`): one `ModalResonator` mode
at 150..800 Hz with its own T60, struck with the hit, cut exactly after four
T60s as the click is, scaled by Vel > Click.

**The snares.** *Ring* (`s1Ring`/`g1Ring`): the upper two modes' T60s times
3^ring, exactly 1.0 at 0 by branch. *Thump* (`s1Thump`, `s1ThumpTone`,
`s1ThumpDecay`, and the ghost's): a one-mode `ModalResonator` under the shell,
not dropped, not driving the rattle follower, retired at the shell's energy
floor. Not part of LINK: a stroke property, like Decay.

**The pans.** `EngineParameters::pan[kPadCount]`, smoothed per pad over 20 ms
at the internal rate, on a **balance law** -- `balanceLeft (p) = p > 0 ? 1 - p
: 1`, `balanceRight` mirrored -- rather than constant power, because the centre
of a constant-power law sits 3 dB under the render every saved project was
mixed against. The eight pad outputs are summed per channel in the mono
engine's order with their gains applied; at centre every gain is exactly 1.0,
so the two channels are the old render bit for bit. **Primed, not ramped, on
the first block after a rebuild**: `prepare()` runs before any parameter is
known, so a smoother started there sits at centre, and the first version of
the test found a hard-left pad still 0.7 % into the right channel at its first
hit 0.1 s in -- a pad saved hard left would have opened every project drifting
across the field. The MIX page holds the eight knobs; Width and Mono Below
were deliberately left for the next round, since on a mono source a side
signal is zero and both would be inert.

Measured (`tezla-tests`: `open_hold`, `air_tilt`, `air_attack`, `grain`,
`velocity_to_air`, `under`, `knock`, `thump`, `ring`, `pan`; `tezla-measure
ictus` tables 1, 2 and 4):

| claim | figure |
|---|---|
| the schema-10 engine against this one, every new control at its default | **byte for byte** (md5 `d933a800…`, four rates, stereo, 1.6 s each) |
| Open hold 0.5 s on a 0.3 s open decay, Link dark: level at 50 ms / 400 ms | **0.296 / 0.297** (test), 0.301 / 0.302 (measure); Link lit **0.000000** at 400 ms; the closed pad byte for byte the same either way |
| Air tilt on a wide-open hiss, centroid dark / flat / bright | test (to 20 kHz) **5431 / 9965 / 12880 Hz**; measure (to 40 kHz) 3775 / 12877 / 18834 Hz; RMS 0.078 / 0.063 / 0.182 |
| Air attack 200 ms, the hiss's first 20 ms / at 200 ms | **0.627 → 0.041 / 0.062 → 0.671** (Air 1); 0.146 → 0.009 / 0.015 → 0.163 (Air 0.3) |
| Grain 1 at 192 kHz | **300 events/s**; crest factor **7.0 → 50.3**; level **−15.3 dB**; grain 0.5: 7589/s, crest 14.4, −7.1 dB |
| Vel > Air 1 at velocity 0.5 | the layer's level exactly **0.15** against 0.3; the heard hiss **0.500009** of the hard hit -- the bands' rails, see below |
| Under 1, an octave under a 50 Hz body | the sub alone peaks at **24.90 Hz** (0.37 Hz bins); through the whole engine its RMS **equals the body's** (0.2238); a fifth down lands at 33.33 Hz (33.41 expected) |
| Under through a 24 st drop | `getUnderHz()` exactly half `currentHz()` at every control tick while the pitch is still moving |
| Under with a 100 ms attack | the sub is **0.076** in the first 20 ms against 0.685 instant, and 0.685 at 100 ms |
| a 0.1 s body with a x4 sub | last sounds at **0.400 s**, active 0 after |
| Knock 350 Hz, 50 ms | peak **349.7 Hz**; cut at exactly **0.2000 s** (four T60s) at the engine; the host output silent from 0.201 s |
| Thump 100 Hz, 200 ms | peak **99.98 Hz** (99.79 through the engine at 48 k); **−57.0 dB** at its T60 (a 20 ms window's average); not sounding at 1 s; not placed at 0 |
| Ring −1 / 0 / +1 on a 0.4 s decay | the pair's T60s **0.093 / 0.067**, **0.280 / 0.200** (exactly), **0.840 / 0.600 s**; the fundamental 0.400 throughout; at 0.3 s the pair against the fundamental 85.6 at +1 vs 0.005 at −1 |
| Pan | centre: both channels equal the mono `render` **bit for bit**; hard left: every right sample **exactly 0.0**, the left unchanged; +0.5: the left exactly half; 64- and 512-sample blocks identical |

**Two things the measurement said that the design had not.** The SVF the
bands are made of is *exactly linear to +-1 and saturates above it* (its op-amp
rail, by design), and the hat's six pulses reach 1.0 where they coincide; add
the hiss and the sum brushes the rail, so "the hiss alone" as a difference of
two renders is not quite linear in the hiss's level: 0.500062 at Air 1,
0.500009 at Air 0.3. The test asserts the layer's level exactly and the heard
ratio to 1e-3, and says why. And a 20 ms one-pole smoother started at centre
takes 0.4 s to snap to its target, which is what turned the pan priming from a
nicety into a fix.

**The presets gate.** *Sub Kick* first read **+1.1 dBFS** over the kick and
its level came down from 78 to 60. The gate also read the untouched kit
presets *DnB Tight* at +0.7 dBFS and *Jungle Snap* at +4.2 dBFS -- pre-existing
(the kick path at neutral is the golden render) and left alone, since the kit
presets are the user's to trim (I4.2); noted in `docs/ROADMAP.md` §9.10. The
three hat-and-snare presets read as the default kick, as at I4.3.

Break-checks, each seen red then reverted with the file checksummed back: the
open hold forced to Hold whatever Link says; the tilt never applied; the air
attack ignored; Grain never thinning the stream; Vel > Air ignored; Under not
kept alive past the body; the knock never cut; the thump never retiring; the
ring factor always 1; the pan on a constant-power law; and, before the fix,
the pans ramped from centre (the hard-left check went red on its own).

**Deferred to the second round, on purpose:** Width and Mono Below (inert on a
mono source), the source spreads (plate modes, hiss, wires, clap bursts), Wash,
Room, Bed, the clap layer under the snare, Drop curve, and the F&R 18.10/18.13
reading for the snare head.

**I4.5 -- the fifth round** (2026-09-05, the user, after playing I4.4: *"amazing
stuff! what about decay control for the snare rattle?"* and *"maybe one more
control for the rattle? some sort of frequency control? ... use your intel to be
creative"*, on top of the round-2 list agreed before: *"could we work on some
widening stereo stuff? this will save us having to tediously do it in the FL
mixer effects chain"*). Everything appended at `kSchemaV12`, every default
neutral: the round-1 golden render (the same sixty-line program, four rates,
stereo, every existing stage on) is still md5 `d933a800…` with the whole
mid/side path, the rooms, the clap layer and the rattle controls in place.
Pushed first without the measure rows, at the user's request (a usage limit
was closing); the follow-up commit added `tezla-measure ictus` **table 5** --
the spreads' levels, the wash's energy, the rattle's tone, decay and tension,
the room, Width, Mono below and the drop curve, in one run -- and put the plate
pages in a **viewport**, so the snare page (six rows now) scrolls rather than
losing its last row off the bottom of the default window.

**The field.** Every engine's `process (double& side)` returns its mid and
writes a side that is exactly 0.0 unless a spread control is up, so a pad with
nothing spread is the mono render bit for bit: the engine forms `mid + side`
and `mid - side` only when the side is not exactly zero. Linear spreads (the
hats' *Metal stereo*: per-pulse and per-mode side weights; the clap's *Stereo*:
per-burst weights) leave the mid untouched, so a mono fold is the old hat or
clap exactly. Decorrelated spreads (*Air stereo*, *Wires stereo*, the clap's
tail) run a second, independent noise stream as the side and scale the mid by
`1 / sqrt (1 + s^2)`, so a channel's level holds and the mono fold of a full
spread is 3 dB down -- measured: hiss mid x0.707, left 0.991, right 1.006 of
the mono hiss; wires x0.707, 1.003, 0.984. Per pad on the MIX page: **Width**
(a smoothed gain on the side, 1.0 exact; measured x2.0000 at 200 %, and L == R
bit for bit at 0) and **Mono below** (a Butterworth high-pass on the side,
150 Hz by default, Off at 0, reset and skipped once the side has been exactly
zero for half a second; measured: the low band's correlation under a kick's
room 0.087 with it off, 0.999 at 150 Hz, through `dsp::StereoAnalyser`). The
same analyser reads the Main output at the host rate for the MIX page's
**Field** readout: full-band and sub-120 Hz correlation, a lamp for the low
band being mono-safe.

**Room.** `dsp::EarlyReflections` (shared, own commit): 48 taps a side at random
positions within equal cells, random signs, gains falling 30 dB over the span,
unit energy per channel, a one-pole tone, an activity counter. One per pad on
the kick, Snare 1, the ghost and the clap (`RoomIndex`, append-only), fed by
the pad's mid at the internal rate and returned as mid and side, the level
smoothed and its target guarded (unguarded, the smoother's moving flag kept a
room at 0 running -- a break-check found it). Lines are allocated at
`prepare()` for the largest factor; `setSampleRate`/`design` allocate nothing;
the taps are redrawn at the pad's note-on when Size differs. Measured: a
50 ms kick's RMS 150-250 ms after the hit 0.000000 dry, 0.00957 with a 200 ms
room; a room at 0 is never fed (`isActive()` false 100 ms in). The velvet-noise
paper this shape comes from was **not fetched** (the proxy refuses dafx.de);
nothing numeric is taken from it -- `docs/DSP-REFERENCES.md`.

**Wash** (hats). Noise driving every mode of the plate for half the pad's
decay, weights `amplitude[k] / sqrt (N)` over the drive's effective length and
a calibrated gain, so at Wash 1 the plate gets about the strike's energy again:
measured x1.93 on a 100 ms pad and x1.78 on 500 ms, feeding at 0.3 of the decay
and over exactly by 0.7. The first version (a 4 ms burst on steady-state
weights) measured 4 % of the strike -- inaudible -- and is why the drive now
lasts. Spectrally the tail is the same 64 lines; what changes is the onset and
the shimmer.

**The snare.** *Head* glides the upper modes' ratios from the snare set
(1.6, 2.2) to a tom's (2.16, 3.14 -- Fletcher & Rossing Table 18.7, **taken**,
read first-hand), geometrically; exact at 0 and to 1e-9 at 1. *Wires tilt*:
+-12 dB shelves about Snappy (centroids 6317 / 9490 / 11242 Hz). *Bed*: six
band-passes at 0.71-2.31 x Snappy, Q 8 (the strongest peak between 1.9 and
2.4 kHz lands at 2098 Hz against 2130 designed). *Wires stereo* as above. The
ghost's LINK now also copies Head, Wires tilt and Bed. The **clap layer**:
`Pad<ClapEngine>` inside the engine, started by a Snare 1 hit after an offset
counted in internal samples and landed on a chunk edge the render loop cuts
for it -- measured to the sample (asked for 1363, first touched 1363) -- with
the snare's velocity, at the snare's pan, through the snare's room, at its own
level (muting the clap pad does not mute it), counted as a hit.

**The rattle's own controls** (the two asks that arrived mid-round). *Rattle
decay*: 0 is "the shell's" -- the follower for as long as the drum rings, as
before, bit for bit; a time is a fall to -60 dB multiplied into the drive, cut
exactly at -120 dB. Measured: the shell's throw 200-300 ms in is 0.0456 RMS with
the shell's decay and exactly 0 with a 50 ms decay of its own, while the shell
still sounds. *Rattle tone*: the throw through a second filter on the same
white sample, summed with the stick's before the tilt and the bed; up, a
high-pass rising to 4 x Snappy; down, the corner falls AND the morph moves to
the band-pass, because a lowered high-pass only adds low end under the same top
and the centroid did not move (measured first, 9610 vs 9459 Hz) -- with the
band-pass: 3732 / 9566 / 13407 Hz at -2 / 0 / +2 octaves, made up by 2^-tone so
the level holds. *Tension*: Fletcher & Rossing 18.13, read first-hand -- the
snares leave the head only above a critical amplitude that rises with their
tension. The head's normalised motion (peak 0.86 on the default snare) minus a
threshold of 0.25 x Tension^2, half-wave (thrown once a cycle), crossfaded
against the follower; scaled by velocity through Vel > Wires. Measured: the
lift ends 117 / 73 / 28 ms into a 250 ms snare at 25 / 50 / 100 %, a 0.4
velocity hit at 19 ms against 29, and Tension 0 never evaluates a lift.

**Kick.** *Drop curve*: `TensionDrop`'s new third argument -- at half the drop
1.4142 (a line, exactly 2^(6/12)), 1.0585 (exponential), 1.6605 (snap).

**Table 5, as the tool prints it** (96 kHz engines; the engine at 48 kHz x4):
Air stereo 100: hiss mid x0.707, left x0.991, right x1.006 of the mono hiss;
Wires stereo 100: mid x0.707, left x1.003, right x0.984. Wash 100: the hit's
energy x1.925 on a 100 ms plate-only hat, x1.775 on 500 ms, the drive over at
half the decay exactly. Rattle tone (power centroid 200 Hz-20 kHz, 100-400 ms
in): 1106 / 6372 / 7558 / 8999 / 13415 Hz at -2 / -1 / 0 / +1 / +2 octaves, RMS
within +-1 dB of the flat throw at every setting. Rattle decay: 0.0456 RMS
200-300 ms in with the shell's, exactly 0 with 50 ms of its own. Tension: the
lift ends at 119 / 74 / 29 ms at 25 / 50 / 100 %, 19 ms for a 0.4-velocity hit
at 100 %, never at 0. Room 100 (200 ms) on a 400 ms kick: 0.45-0.5 s RMS
0.000000 dry, 0.00157 wet; left-right RMS 0 dry, 0.0418 wet. Mono below: the
low band's correlation 0.504 open, 0.990 at 150 Hz. Width: side RMS 0 (left ==
right bit for bit), 0.0418, 0.0835 at 0 / 100 / 200 %. Drop curve at half the
drop: 1.4142 / 1.0585 / 1.6605 for -1 / 0 / +1.

**Tests.** Sixteen in `tests/test_Ictus.cpp` under the I4.5 rule, five shared
(`test_TensionDrop.cpp`, `test_EarlyReflections.cpp`); the whole kit with
everything spread bit-identical across 64-, 97- and 512-sample blocks. Twenty
break-checks run, each seen red then reverted with the engine files
checksummed: three stayed green the first time and were re-done -- two were
weak breaks (the clap's tail still had a side; a 7 ms offset that happened to
sit on the 32-sample grid) and one was the room-level guard above, a real
defect. The clang build is warning-free and its suite passes but for the eight-pad
CPU budget: 15.2-15.7 % of a core against the 15 % budget, where gcc reads
13.7 % -- a compiler margin on a budget set at I4 with 2.5x headroom over a
6 % kit, not a defect the round introduced; the budget is being re-based against
the measured cost of both compilers. The `qemu-aarch64`
cross-check was not run (CLAUDE.md section 2.3 gate), and nothing of this has
been heard on the rig.

**I4.3 -- a hold on the snares' wires** (2026-09-03, the user). The wires had
a decay and nothing else, so the only way to get a long buzz was a long
decay, which washes rather than sits. `s1WiresHold` and `g1WiresHold` at
`kSchemaV9`, both **neutral at 0** so every saved project reopens unchanged:
the wires stay at full level for up to 300 ms and then fall at whatever Wires
decay says. It is `Adsr`'s hold stage, which the envelope already had and the
engine was passing 0 to.

Measured (`tezla-tests wires_hold`, the wires alone at 96 kHz, decay 100 ms):
at 100 ms into a 120 ms hold the wires read **0.6464 against 0.0016** without
it, and the level at the end of the hold is within **1.2 %** of the level at
the strike -- which is the definition of a hold and what the test asserts.
The hit runs 0.100 s to 0.220 s and still retires exactly.

The test failed twice on its own expectations before it failed on anything
real, and both are worth keeping: "the strike is unchanged" cannot be asked
of a four-millisecond window at 10 ms, because an exponential decay is
already a third down by then; and a snare with Body at 0 still runs its shell
(Rattle follows it), so `isActive()` stays true until the SHELL retires, not
until the audio stops. Break-check: the hold never applied, 4 checks red.

**To resume** (a fix, or a later phase): read CLAUDE.md in full, then this
file; take the first `pending` phase. The next is **I5, the punch chain**.
I4.1 above is the shape of an ear round arriving mid-phase: the user's verdict
is the acceptance test, and "sounds crap" is a diagnosis to make, not a defect
to look up.
The user asked on 2026-09-03 to go ahead through I4 *before* the rig's report
on everything since I2 (I2.1, I3, Note snap, the panel, the ghost, and now
the hats and the clap) — so that report's findings become an "I4.1" row when
they arrive, exactly as I2's did. **Nothing since the I2 kick has been
played on the rig**, and that is the biggest open item in this file. The
non-negotiables every phase here honours, in one place:

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
