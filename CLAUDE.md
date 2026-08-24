# CLAUDE.md — tezla.tech plugin workshop

Standing instructions for Claude working in this repository. Read this before
touching anything. If a rule here conflicts with a request in chat, the chat
wins for that task, but say so and update this file if the change is permanent.

---

## 1. What this repository is

A collection of **64-bit VST3 audio plugins** — effects first, synths later —
written from scratch for a Windows 11 / FL Studio 2026 production rig.

The music being made is **dubstep and DnB/jungle**. That biases every design
decision: the plugins must survive brutal input levels, hold together on sub
bass below 60 Hz, add weight and grit without smearing transients, and stay
usable on drum busses at high gain.

The reference points the user cares about are the plugins from an older rig:
PSP VintageWarmer, Steinberg Warp, Antares Tube, Waves L1 / Renaissance Verb,
Bitcrusher, and instruments like Pro-53 and Vanguard. Those are *targets for
the sound and the workflow*, never a source of code.

### Priorities, in order

1. **Analogue character** — tube/tape/transformer saturation, cabinet
   colouration, musical dynamics. This is the first and main focus.
2. **Clean digital** — every plugin should also be able to get out of the way.
   A "clean" or low-drive setting must be genuinely transparent, not a
   quieter version of the dirty setting.
3. **Fidelity** — no aliasing, no DC drift, no zipper noise, no surprises at
   any host sample rate.
4. **CPU sanity** — these run 20 at a time in a project. Efficiency matters,
   but never at the cost of points 1–3.

---

## 2. Hard rules

### 2.1 Legal / IP boundary — non-negotiable

- **Never reverse engineer, decompile, disassemble, or inspect the binaries of
  any commercial plugin.** Not PSP VintageWarmer, not Steinberg Warp, not
  anything else. Do not extract impulse responses, curves, presets, or
  parameter tables from a commercial product the user owns.
- **Never copy proprietary artwork, branding, panel graphics, fonts, preset
  names, or manual text.** Our plugins get their own names, own UI, own
  identity.
- What *is* allowed and encouraged:
  - Reading **published manuals and specs** to understand what a control
    *does* (e.g. "drive interacts with knee") — this is public documentation.
  - Reading **academic papers** (DAFx, JAES, AES conventions) and
    **open-source code with a compatible licence** (see §9).
  - **Measuring our own plugin** and comparing it, by ear or by our own
    measurements, against a target the user plays back. Trusting the user's
    ears is the primary tuning loop.
  - Modelling **physical circuits and tape physics** from first principles.
    Valve transfer curves, transformer hysteresis, and speaker impedance are
    physics, not anybody's intellectual property.
- Any third-party source we lean on must have its licence recorded in
  `docs/DSP-REFERENCES.md` before a line of it influences our code.

### 2.2 Technical non-negotiables

- **VST3 only, x86-64 only.** No VST2 (SDK is not licensable), no 32-bit.
  Adding CLAP later is fine and cheap; do not add it without being asked.
- **Sample-rate independent.** A plugin must sound the *same* at 44.1, 48, 96
  and 192 kHz. See §6 — this is the rule most easily broken and the one that
  matters most on this rig.
- **Real-time safe audio thread.** No allocation, no locks, no file I/O, no
  logging, no exceptions in `processBlock`. Ever.
- **64-bit internal processing.** `double` through the nonlinear and dynamics
  path. `float` is acceptable only in bulk convolution/FFT where it is
  measurably transparent and clearly documented.
- **Denormals off** (FTZ/DAZ) for the duration of every audio callback.
- **Report latency to the host.** Oversampling and lookahead both add latency.
  FL Studio's PDC only works if we declare it, and it must be declared again
  whenever it changes.
- **Bypass must be click-free and latency-matched**, so A/B comparison is honest.

---

## 3. Repository layout

```
tezla.tech/
├── CLAUDE.md                 # this file
├── README.md                 # human-facing overview
├── CMakeLists.txt            # top-level; aggregates everything
├── cmake/                    # shared CMake helpers, JUCE fetch, plugin macro
├── shared/
│   └── tezla-dsp/            # header-only, framework-free DSP library
│       └── include/tezla/dsp/
├── plugins/
│   ├── README.md             # plugin registry: names, codes, UIDs
│   └── <PluginName>/         # one folder per plugin, self-contained
│       ├── CMakeLists.txt
│       ├── README.md         # what it is, controls, design notes
│       ├── Source/           # JUCE processor + editor (thin)
│       ├── Dsp/              # this plugin's DSP (framework-free)
│       └── Resources/        # graphics, presets
├── tests/                    # DSP unit tests + measurement harness
├── tools/                    # offline analysis (THD, aliasing, response)
├── scripts/                  # build.bat (primary) / build.sh / build.ps1
└── docs/
    ├── BUILD.md              # toolchain setup + build guide
    ├── DSP-REFERENCES.md     # sources, papers, licences
    └── PLUGIN-CONVENTIONS.md # parameters, presets, UI, versioning
```

**Every plugin lives in exactly one folder under `plugins/`.** No cross-plugin
includes except through `shared/tezla-dsp`. A plugin folder should be
deletable without breaking anything else.

---

## 4. Architecture: framework-free DSP, thin JUCE wrapper

This split is mandatory and is the single most important structural rule here.

- **`shared/tezla-dsp/` and `plugins/*/Dsp/` are pure C++20.** Header-only
  where practical. No JUCE, no VST3 headers, no `<iostream>` in the hot path,
  no platform code. They take `double*` buffers and a sample rate.
- **`plugins/*/Source/` is the JUCE layer**: parameter definitions, state
  save/load, the editor, and a `processBlock` that does little more than
  unpack buffers and call the DSP.

Why: it lets the DSP be compiled, unit-tested and *measured* with plain
g++/clang on any machine — including CI and the Linux container Claude often
runs in — without a DAW, without a GUI, and without JUCE. Aliasing SNR, THD
curves and frequency responses get verified offline *before* anything reaches
the user's machine. It also means a future move to CLAP or a standalone app
costs a wrapper, not a rewrite.

**Consequence for Claude:** when you write DSP, write a test or a measurement
tool for it in the same commit, and run it. "It compiles" is not evidence.

---

## 5. Toolchain — must build easily on Windows 11 with free tools

The whole build must work from a plain Windows 11 box using only free
downloads, from a command prompt, with no IDE required and no manual SDK
hunting.

Required (all free — install links in `docs/BUILD.md`):

- **Visual Studio 2022 Build Tools** (the standalone command-line compiler;
  the full Visual Studio Community edition also works) with the
  "Desktop development with C++" workload.
- **CMake ≥ 3.22** (3.28+ preferred).
- **Ninja** (optional, faster; the MSVC generator works without it).
- **Git**.

Rules:

- **CMake is the only build system.** No checked-in `.sln`/`.vcxproj`.
- **Dependencies are fetched by CMake** (`FetchContent`), pinned to an exact
  tag or commit. Never require the user to download an SDK by hand — but always
  let them supply their own copy if they have one, and say how in
  `docs/BUILD.md`. Validate what they supply and fail with a message that names
  the fix, rather than letting a wrong path fail deep inside the dependency.
- **Never break the "one command" build.** `scripts\build.bat` must build
  everything from a clean clone.
- **PowerShell is never a requirement.** `scripts\build.bat` is a real batch
  script, not a wrapper that shells out to `powershell -ExecutionPolicy Bypass`.
  Windows blocks unsigned `.ps1` files by default and the user does not want that
  guard relaxed on their machine — which is correct, and compiling a plugin is
  no reason to. `build.ps1` may exist as a convenience, but nothing may depend
  on it, and every documented path must work without it.
- **Every script must have a documented manual equivalent.** `docs/BUILD.md` §3
  spells out the raw CMake and MSVC invocations for anyone who would rather run
  no script at all. If a script grows a step, document the manual form in the
  same commit.
- The build script targets **one plugin, all plugins, or a named list**:
  ```
  scripts\build.bat                        :: all plugins, Release
  scripts\build.bat Foo                    :: just Foo
  scripts\build.bat Foo,Bar                :: a list
  scripts\build.bat -config Debug -install
  scripts\build.bat NONE -test             :: DSP + tests only, no JUCE
  ```
- `-install` copies the built `.vst3` bundles to
  `C:\Program Files\Common Files\VST3\` (needs an elevated prompt), which is
  where FL Studio scans by default. It checks for elevation first rather than
  failing halfway through.
- **No Developer Command Prompt required.** The Visual Studio CMake generator
  finds MSVC itself; the developer prompt is only needed for the Ninja path,
  which the script uses when it detects `ninja` and `cl` already on `PATH`.
- Keep the tree warning-clean on MSVC at `/W4`. Warnings are how DSP bugs
  announce themselves early.
- **Update `docs/BUILD.md` whenever a build step or tool version changes.**

### Framework: JUCE

JUCE is the house framework, pinned in `cmake/FetchJUCE.cmake` (currently
9.0.1, minimum 8.0.0). There are three supported ways to get it and all are
documented in `docs/BUILD.md` §4: downloaded by CMake (default),
`-DTEZLA_JUCE_PATH=` for a source tree the user already has, or
`-DTEZLA_JUCE_SOURCE=System` for one installed with `cmake --install`. Never
assume the download is the only route — a user with their own JUCE must not be
forced to fetch a second copy, and an air-gapped machine must still be able to
build. It is dual-licensed AGPLv3 /
commercial, with a free tier that covers personal and small-revenue use, and it
bundles the VST3 wrapper so no separate SDK install is needed.

For reference: Steinberg's VST3 SDK itself has been MIT licensed since v3.8
(October 2025), so a direct-to-SDK plugin is also legally unencumbered if we
ever want to drop JUCE. Because §4 keeps all DSP framework-free, that switch
costs a wrapper rather than a rewrite. JUCE 9.0.1 does **not** have native CLAP
output — adding CLAP means `clap-juce-extensions`. Do not claim otherwise.

---

## 6. Sample rate and oversampling policy

The user's DAW runs at 48, 96, or **192 kHz** depending on the session. This is
squarely the plugin's problem, not the host's, for two reasons:

1. Any nonlinearity generates harmonics above Nyquist that fold back as
   inharmonic aliasing. Distortion is where this is worst, and dubstep-grade
   drive is the worst case of the worst case.
2. If we set filter corners, hysteresis time constants, or the effective
   bandwidth of a saturation stage in terms of raw sample rate, the plugin
   changes character between 48 k and 192 k. That is unacceptable.

### The policy

- **Design every stage against an explicit target internal rate**, not against
  whatever the host hands us. Nonlinear stages run at a controlled effective
  rate so the harmonic structure is identical at every host rate.
- **Oversampling defaults to `Auto`**, which picks the factor from the host
  rate to land near the same effective rate:

  | Host rate | Auto factor | Effective rate |
  |-----------|-------------|----------------|
  | 44.1/48 k | ×4          | ~176–192 k     |
  | 88.2/96 k | ×2          | ~176–192 k     |
  | 176.4/192 k | ×1 (off)  | ~176–192 k     |

- The control also offers manual `Off / ×2 / ×4 / ×8`, so the user can trade
  CPU for headroom deliberately.
- **The tooltip must state the consequence in plain language**, e.g.:
  *"Auto — oversamples to ~192 kHz internally. Your session is at 96 kHz, so
  this is running ×2. At 192 kHz sessions Auto turns oversampling off, since
  the extra headroom is already there; forcing ×2 or ×4 there costs CPU for
  little gain."*
  The tooltip should read the **actual current host rate** and say what Auto is
  doing right now. Do not make the user work it out.
- Tone-shaping filters are **minimum-phase by default**, so pre-ringing never
  lands in the audible band.
- **Oversampling filters are the documented exception: linear-phase halfband
  FIRs.** A halfband's transition band sits at Nyquist, so its pre-ringing is
  above 20 kHz and inaudible, and a windowed-sinc designed at run time can be
  verified by measurement in a way a table of IIR coefficients cannot. Declare
  the latency either way. `shared/tezla-dsp/include/tezla/dsp/Oversampler.hpp`
  picks tap counts per stage (95/65/65) so the round-trip latency is a **whole
  number of base-rate samples** at every factor — 47/63/71 for x2/x4/x8.
  A fractional latency cannot be reported honestly to a host.
- Filter coefficients are **always** computed from the actual sample rate.
  Never hard-code a coefficient derived at 44.1 or 48 k.
- **But that alone is not enough, and this has been measured.** The bilinear
  transform warps the frequency axis, so an ordinary biquad diverges as it
  approaches Nyquist even when its coefficients are recomputed correctly. A
  4 kHz lowpass at Q 0.707 reads −0.25 dB at 2 kHz at every rate, but at 15 kHz
  reads −29.9 dB at 48 k against −23.3 dB at 192 k — and the 192 k curve is the
  one tracking the analogue prototype. **Trust a plain biquad to be
  rate-independent only below about Fs/8.** Anything whose high-frequency shape
  actually matters — a cabinet response, a tape head bump, the tone stack inside
  a saturation stage — goes inside an oversampled section. Numbers pinned in
  `tests/test_Biquad.cpp`; table in `docs/DSP-REFERENCES.md`.
- **Test at 44.1, 48, 96 and 192 kHz** and compare. A null test between rates
  (resampled) should show the same character; an aliasing sweep should show
  the same SNR. This belongs in `tests/`.

---

## 7. Audio engineering standards

Every plugin must satisfy these before it is considered done:

- **Aliasing:** a 1 kHz → 20 kHz sine sweep at maximum drive shows no
  inharmonic component above −60 dBFS in the audible band. Measure it, don't
  assume it — `tezla-measure` exists for exactly this.
  The baseline to beat, already measured: a *naive* hard clipper at 4× drive
  produces −47 dB of inharmonic energy at 48 kHz and −65 dB at 192 kHz. Four
  times the rate buys ~18 dB and no more, because a hard clipper has infinite
  bandwidth. So oversampling alone never gets there: the shaper itself must be
  band-limited too (ADAA, or a smooth shaper). Both numbers are pinned in
  `tests/test_Measurement.cpp`.
- **DC:** nonlinearities — especially asymmetric ones, which we want for
  even-harmonic warmth — produce DC. A gentle high-pass (typically 5–20 Hz,
  first order) after the nonlinearity, chosen so it does not thin the sub.
  Never a steep HPF; sub bass is the point of this music.
- **Gain compensation:** drive controls must have an auto-output-trim option
  so the user judges tone, not loudness. Loudness sells distortion; we do not
  want to be fooled.
- **Parameter smoothing:** every continuous parameter is smoothed
  (~10–50 ms). No zipper noise, ever. Discrete switches use crossfades.
- **Level metering:** where a plugin claims analogue behaviour, give it honest
  metering — a VU-ballistic meter (300 ms integration) reads very differently
  from a peak meter and is part of how these units are used.
- **Stereo:** default to linked/stereo-correct behaviour. Independent
  per-channel nonlinearity destroys the centre image; if a plugin offers it,
  it is an explicit option, off by default.
- **Reset properly:** `prepareToPlay` clears all state. No pops when transport
  restarts, no state leaking between projects.
- **`prepare()` runs before any parameters are known**, so anything it
  configures from a parameter must be re-checked against what it actually built
  — not against "have parameters been set yet". Getting this wrong made the
  oversampling control silently inert on load, and no test caught it because the
  measurement and the reference were wrong in the same way.
- **Silence in, silence out** (barring intentional noise/hiss features, which
  must be defeatable).

---

## 8. Parameters, presets, UI

Detail lives in `docs/PLUGIN-CONVENTIONS.md`; the short version:

- Parameters use stable string IDs and a **versioned** parameter layout. Never
  reorder or renumber existing parameters — old projects must reopen correctly.
  New parameters are **appended** and carry the schema version they were
  introduced at; existing ones keep theirs forever, because the version hint
  feeds the VST3 parameter ID and bumping it on a live parameter is
  indistinguishable from renaming it. Every new parameter defaults to neutral,
  so a project saved before it existed reopens sounding the same.
- Ranges are musical, not mathematical: dB where the user thinks in dB, ms
  where they think in ms, skewed so the useful range sits mid-travel.
- **Every control has a tooltip** that says what it does *and* what it costs.
  Tooltips are how this workshop documents itself.
- Ship a small, opinionated preset set aimed at the actual use cases: drum bus,
  sub bass, reese, mix glue, and a genuinely clean setting.
- UI is resizable and readable on a high-DPI display. Function before flourish;
  clear metering beats skeuomorphic decoration.
- The plugin's own name, vendor "Tezla Tech", and a unique 4-character plugin
  code — **registered in `plugins/README.md`** so codes never collide. That file
  also holds reserved names for plugins already sketched out (`Ferrite` for the
  tape machine, `Anvil` for amp/cabinet), so they do not get spent on something
  else.

---

## 9. Reference material — allowed sources

Research is expected and encouraged. Use the web. Prefer papers and
permissively licensed code. Record everything used in
`docs/DSP-REFERENCES.md` with its licence, and honour that licence.

Starting points known to be good:

| Source | Licence | Useful for |
|---|---|---|
| Airwindows (Chris Johnson) | MIT | Saturation, console, tape, dither — small readable algorithms |
| Surge XT | GPLv3 | Filters, waveshapers, oversampling, overall plugin architecture |
| Calf Studio Gear | LGPL/GPL | Classic effect topologies |
| ChowDSP `chowdsp_wdf` | BSD-3 | Wave digital filters for circuit modelling |
| ChowDSP AnalogTapeModel | GPLv3 | Jiles–Atherton tape hysteresis — read the paper first |
| DAFx paper archive | papers | ADAA, virtual analogue, oversampling theory |
| Vadim Zavalishin, *The Art of VA Filter Design* | free PDF | TPT/ZDF filters — the standard text |
| Faust libraries | permissive | Reference implementations to compare against |
| musicdsp.org, `olilarkin/awesome-musicdsp` | mixed | Index of everything else |

**GPL awareness:** reading a GPL project to understand a *technique* is fine.
Copying GPL code into ours makes ours GPL. Prefer deriving from the paper. If
GPL code is ever pasted in, say so loudly and record it.

---

## 10. Testing and validation

- `tests/` holds framework-free unit tests, buildable with plain
  g++/clang/MSVC and runnable with no DAW.
- `tools/` holds offline analysis that renders and measures: THD vs level,
  aliasing SNR sweeps, frequency/phase response, step response, null tests.
  Output goes to CSV so results can be diffed between commits.
- Before declaring a DSP change done: **run the measurement, quote the
  numbers.** "Sounds better" is the user's call to make; our job is to show
  it is not measurably worse.
- **Check the instrument before trusting it.** `tezla-measure selftest` must
  pass first. Measurement code has bugs like any other: this harness has already
  produced a filter "failure" that was really peak-picking under-reading a
  16 kHz tone at 48 kHz (three samples per cycle reads 0.866, which looks
  exactly like a filter 1.2 dB down — use RMS, never peak, for sine amplitude),
  and a 3 dB scaling error in its own dBFS reference.
- Validate the final `.vst3` with Steinberg's `validator` before handing it
  over. Build it from the (MIT-licensed) VST3 SDK with:
  ```
  git clone --recursive --depth 1 https://github.com/steinbergmedia/vst3sdk
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
        -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
        -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF
  cmake --build build --target validator
  ```
  The examples and VSTGUI need GTK and xcb dev packages that the validator
  itself does not; turning them off is what makes this build anywhere.
- **A Linux build is a cheap dress rehearsal for the Windows one.** The plugin
  target builds and validates on Linux with the X11/ALSA dev packages listed in
  `docs/BUILD.md`, which catches wrapper mistakes long before they reach the
  user's machine. It does not replace building on Windows — MSVC finds things
  g++ does not — but it removes most of the round trips.
- Report honestly. If a test fails or a step was skipped, say so with the
  output.

---

## 11. Workflow

- Work on the branch specified for the task; commit in coherent steps with
  descriptive messages; push when the work stands on its own.
- One plugin per branch/PR where possible. Shared-DSP changes that affect
  multiple plugins get their own commit.
- Keep `README.md`, `docs/BUILD.md` and `plugins/README.md` current in the
  same commit as the change that dated them.
- Prefer a working, measurable, minimal version early over a large unproven
  one. Get it building on Windows, get it loading in FL Studio, then refine
  the sound with the user in the loop — the user's ears are the acceptance test.
