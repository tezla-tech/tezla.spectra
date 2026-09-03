# Plugins

One self-contained folder per plugin. A plugin folder should be deletable
without breaking anything else in the repository.

The top-level `CMakeLists.txt` discovers any folder here that contains a
`CMakeLists.txt`. Nothing else needs editing to add one.

---

## Registry

**Every plugin claims a unique 4-character plugin code here before it is
written.** The code plus the manufacturer code `Tzla` generates the VST3 unique
ID. Two plugins sharing a code produce the same ID, and the host will load
whichever it happened to scan first — a confusing failure that looks like a
corrupt install. The build cannot detect this; this table is the only guard.

| Code | Folder | Product name | Type | Status |
|------|--------|--------------|------|--------|
| `Tzem` | [`Emberdrive`](Emberdrive/) | Tezla Emberdrive | Effect — saturation, destruction, 3-band limiter, with a modulation layer | v0.4.0, builds and validates |
| `Tzha` | [`Halo`](Halo/) | Tezla Halo | Effect — harmonic exciter and bass enhancer, with a Chebyshev precision mode and a modulation layer | v0.3.0, builds and validates |
| `Tzcp` | [`Capstone`](Capstone/) | Tezla Capstone | Effect — true-peak brickwall limiter and clipper for the end of the chain | v0.1.0, builds and validates |
| `Tztr` | [`Transpectus`](Transpectus/) | Tezla Transpectus | Analyser — loudness, true peak, spectrum and stereo image. Does not touch the audio | v0.1.0, builds and validates |
| `Tzan` | [`Anvil`](Anvil/) | Tezla Anvil | Effect — valve amplifier, speaker load and cabinet, modelled from the mechanisms | v0.1.0, builds and validates |
| `Tzso` | [`Sonitus`](Sonitus/) | Tezla Sonitus | **Instrument** — growl and reese synthesiser: hard sync, unison, operator feedback and cross PM, a morphing filter, a scale-locked comb, four macros, sixteen-point ADV envelopes, and Scala microtuning | v0.1.0, phase 4 complete, builds and validates |
| `Tzsv` | [`Svarayantra`](Svarayantra/) | Tezla Svarayantra | **Instrument** — SoundFont (.sf2) player with the full microtuning engine: the 44 built-in scales, `.scl`/`.kbm`, concert pitch. Sanskrit *svara-yantra*, "the note-machine". Plan: [`Svarayantra/PLAN.md`](Svarayantra/PLAN.md) | v0.1.0, builds and validates |
| `Tzfe` | [`Ferrite`](Ferrite/) | Tezla Ferrite | Effect — the tape machine: Jiles–Atherton hysteresis, wavelength losses, head bump, wow and flutter, calibrated hiss. Plan: [`Ferrite/PLAN.md`](Ferrite/PLAN.md) | v0.1.0, builds and validates |
| `Tzml` | [`Malleus`](Malleus/) | Tezla Malleus | **Instrument** — the impossible-object percussion synthesiser: modal physical modelling (bar/membrane/plate/bell tables derived from the physics), morphable material, **Overtone Lock** (the object's own partials quantised onto the loaded tuning), mallet/pluck/bow/roll exciters in **two blendable slots**, velocity-picked hardness, per-hit tension drop, **Bloom** (nonlinear mode coupling: a hard hit builds after contact), **Damp** (a played hand, loss proportional to frequency), **two listening positions** for stereo from the geometry, sympathetic bank, vactrol LPG. Plans: [`Malleus/PLAN.md`](Malleus/PLAN.md), [`Malleus/PLAN-PHASE2.md`](Malleus/PLAN-PHASE2.md) | v0.1.0, phase 2 complete, builds and validates |
| `Tzcb` | [`Crossbar`](Crossbar/) | Tezla Crossbar | **Instrument** — the telephone tone generator: every DTMF pair and call-progress tone to the published standards (ITU-T Q.23, the Bell Precise Tone Plan, the BT set), laid out drum-sampler style across the keyboard, with a dialler and a G.711 line — 300–3400 Hz, 8 kHz, 8-bit companded. Plan: [`Crossbar/PLAN.md`](Crossbar/PLAN.md) | v0.1.0, builds and validates |
| `Tzps` | [`Phonoss`](Phonoss/) | Tezla Phonoss | Effect — the vocal channel strip (Greek *phone*, voice, plus a second **s** for the de-esser; called `Syrinx`/`Tzsy` until it was renamed, before anything had ever been saved with it): gate, de-esser, two compressors and tone, in the order a vocal is actually mixed. The de-esser detects sibilance as a RATIO of high-band to body energy rather than an absolute level, so the same /s/ is reduced identically across 30 dB of input (0.000 dB of spread measured) and a bright vowel never triggers it. Saturation, air and limiting stay with Emberdrive, Halo and Capstone. Plan: [`Phonoss/PLAN.md`](Phonoss/PLAN.md) · Placement: [`docs/VOCAL-CHAIN.md`](../docs/VOCAL-CHAIN.md) | v0.1.0, builds and validates |
| `Tzmb` | [`Membrana`](Membrana/) | Tezla Membrana | Effect — the microphone stage (Latin *membrana*, the diaphragm; the artificial tympanum that feeds the suite's malleus and incus), sitting **before Phonoss** in the vocal chain. Three mechanisms, all physics, none inventing signal: capsule body diffraction (the exact rigid-sphere series at finite range) plus grille resonance as a minimum-phase FIR; distance and off-axis through the exact first-order gradient model (proximity, pattern level and their coupling); and presence as **dynamics** — a level-tracking shelf that leans in when the singer backs off, and a floored upward expander that lifts consonants but never hiss. No named-mic emulation, ever. Plan: [`Membrana/PLAN.md`](Membrana/PLAN.md) · Placement: [`docs/VOCAL-CHAIN.md`](../docs/VOCAL-CHAIN.md) | v0.1.0, builds and validates |
| `Tzic` | [`Ictus`](Ictus/) | Tezla Ictus | **Instrument** — the drum synthesiser for drum and bass (Latin *ictus*: the stroke, and in music the beat itself). Purpose-built kick, snare, hat and clap engines derived from the published circuit analyses and membrane physics (never from a product), a per-pad punch chain — transient, drive, weight and tilt, one-knob smash, clip — that builds in the layering the user did by hand, per-hit humanise and velocity, a sample layer with onset alignment and Render-to-WAV, and per-pad output buses for FL Studio's Auto map outputs. Plan: [`Ictus/PLAN.md`](Ictus/PLAN.md) | **in progress** — I4.1: the kick, the snare, the ghost snare, both hats (dense, damped, gated, with the hiss rung through the metal) and the clap (a burst pattern over a cavity that rings) (v0.1.0; Perc on the snare engine's defaults), Note snap, the plated panel with its pictures, builds and passes the validator 47/47; the I2 kick **has been played on the rig**, whose first ear round added Bass mode, Gate + Release and the tuning page; nothing since has been played there yet |

Codes are case-sensitive and conventionally start with `Tz`.

### Reserved names

Held for the plugins already sketched out, so they do not get used for
something else in the meantime:

| Name | Code | Intended for |
|------|------|--------------|
| `Prism` | `Tzpr` | The multiband enhancer — four bands of harmonics plus per-band stereo width. Deliberately its own plugin rather than a mode of Halo: it costs considerably more CPU and signal complexity than belongs in a tool used on a single channel |

---

## Layout of a plugin folder

```
plugins/<Name>/
├── CMakeLists.txt     # one tezla_add_plugin() call
├── README.md          # what it is, every control, design notes, changelog
├── Dsp/               # framework-free C++20. No JUCE includes. Testable alone.
├── Source/            # thin JUCE layer: parameters, state, editor
└── Resources/         # graphics, presets
```

The `Dsp/` and `Source/` split is not optional — see
[`../CLAUDE.md`](../CLAUDE.md) §4. `Dsp/` must compile with plain g++/clang/MSVC
and nothing else, so that its behaviour can be measured offline before it ever
reaches a DAW.

---

## Adding a plugin

1. Claim a code in the table above.
2. Create the folder and a `CMakeLists.txt`:

```cmake
tezla_add_plugin(
    NAME            Example
    PRODUCT_NAME    "Tezla Example"
    PLUGIN_CODE     Tzex
    DESCRIPTION     "One line the DAW will show"
    VST3_CATEGORIES Fx Distortion
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    DSP_SOURCES
        Dsp/ExampleEngine.cpp)
```

3. Write the DSP in `Dsp/`, with tests in `../../tests/`.
4. Write the JUCE wrapper in `Source/`.
5. Build it: `scripts\build.bat Example -install` on Windows, or
   `./scripts/build.sh Example` on macOS and Linux. Nothing depends on
   PowerShell — see `CLAUDE.md` §5.
6. Update the table above and the status table in the root `README.md`.
