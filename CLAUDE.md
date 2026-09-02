# CLAUDE.md — tezla.spectra plugin workshop

Standing instructions for Claude working in this repository. Read this before
touching anything. If a rule here conflicts with a request in chat, the chat
wins for that task, but say so and update this file if the change is permanent.

---

## 1. What this repository is

A collection of **64-bit audio plugins** — effects first, synths later —
written from scratch for a Windows 11 / FL Studio 2026 production rig, and built
for macOS as well.

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

- **VST3 everywhere; Audio Unit additionally on macOS.** No VST2 (SDK is not
  licensable), no 32-bit. AU is not optional on a Mac in practice — Logic Pro
  and GarageBand load nothing else — but it is macOS-only and gated on
  `TEZLA_BUILD_AU`. Adding CLAP later is fine and cheap; do not add it without
  being asked.
- **Architectures: x86-64 on Windows and Linux; universal arm64 + x86_64 on
  macOS.** `CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET` are read
  by CMake *before* `project()`, so they must be set at the very top of the
  root `CMakeLists.txt` and only take effect on a fresh build folder. Setting
  them later, or in an existing folder, silently does nothing.
  **CI deviates from this, since 2026-09-01, at the user's request:** the
  macOS *CI* build is **arm64 only by default** (the workflow's `mac_arch`
  input), because a twelve-plugin universal build measured **6h04m** on run 51
  and GitHub kills a hosted job at six hours. It is the CI default that
  changed, not the target: a local build stays universal, and CI can be asked
  for universal one input away. Intel Mac users self-compile from a CI release
  for now, and its notes say so. Revisit when the release layout is settled.
- **macOS builds are not code signed**, so anything downloaded is quarantined
  and Gatekeeper refuses it — the DAW then reports the plugin as damaged. Every
  document and release note that offers a macOS download must say how to clear
  the quarantine attribute. A locally built plugin is never affected, which is
  exactly why this is easy to forget.
- **Sample-rate independent.** A plugin must sound the *same* at 44.1, 48, 96
  and 192 kHz. See §6 — this is the rule most easily broken and the one that
  matters most on this rig.
- **Real-time safe audio thread.** No allocation, no locks, no file I/O, no
  logging, no exceptions in `processBlock`. Ever.
- **64-bit internal processing.** `double` through the nonlinear and dynamics
  path. `float` is acceptable only in bulk convolution/FFT where it is
  measurably transparent and clearly documented.
- **Denormals off** (FTZ/DAZ) for the duration of every audio callback — on
  **every architecture**, not just x86. The control register differs: MXCSR on
  x86, FPCR bit 24 on AArch64. An x86-only guard compiles cleanly on Apple
  Silicon, runs happily, and does nothing at all; there is no error and no
  warning to notice. `ScopedNoDenormals::isSupported()` returning false is a
  build that must not ship, and the test asserts it rather than skipping.
- **Report latency to the host.** Oversampling and lookahead both add latency.
  FL Studio's PDC only works if we declare it, and it must be declared again
  whenever it changes.
- **Bypass must be click-free and latency-matched**, so A/B comparison is honest.

### 2.3 Order of work — x86-64 Windows first, and ARM64 comes later

**Do not build or test for ARM64 until the x86-64 Windows build is finished.**
Not the cross-compile, not `qemu-aarch64`, not the macOS or ARM64 CI jobs. This
is a standing instruction from the user and it overrides the cross-check
suggestions in §5 and §10 — those describe *how* to do it, and this says *when*.

The reason is throughput, in the user's words: it speeds up testing and
development. The rig is Windows 11 and FL Studio; a feature is only real once it
has been played there, and every ARM build is minutes spent on a platform nobody
is listening on yet. Six hours of macOS CI bought nothing that the user's ears
had not already bought faster.

The gate is explicit. ARM64 and macOS resume when **the x86-64 Windows build is
bug-squashed and feature-complete** — when the user says the features are
finalised and the bugs are gone, not when a milestone merely looks finished from
here. At that point, and only then:

1. Push out a macOS ARM64 build and test it.
2. Fix whatever the Apple clang and AArch64 toolchains turn up — compiler
   warnings and errors included. Expect some; that is what the stage is for.

Two things this does **not** license:

- **Do not remove or weaken architecture-correct code.** The denormal guard's
  AArch64 branch (§2.2, FPCR bit 24) stays exactly as it is, and so does every
  other non-x86 path. Writing portable code costs nothing; *running* the
  cross-build is the expense being deferred. Deleting the ARM path to "simplify"
  would turn a deferral into a regression, and it is invisible until it runs.
- **Do not claim ARM64 or macOS coverage that was not obtained.** Say "the
  ARM64 cross-check was not run" in the commit message and the report. §10's
  reporting rule applies with full force here: a skipped step is stated, never
  implied.

CI already agrees with this. The workflow's `platforms` input defaults to
`windows`, which skips `test-arm64` and the macOS matrix entries; choosing
`all` is the deliberate act that opts back in.

**The gate has been lifted once, narrowly, and it is still in force.** On
2026-09-01 the user asked directly for a macOS/Apple-Silicon build of Sonitus
and then a full-suite release through CI, which is §1's "the chat wins for that
task" — so `platforms: all` was run twice (`v0.88.8-sonitus`, then `v0.88.8`)
and the local `qemu-aarch64` cross-check with it. That was a checkpoint the user
asked for, not the gate lifting: the standing rule is unchanged and the next
ARM64 or macOS run needs the same kind of explicit ask, or the real lift, which
is the user saying the Windows features are finalised and the bugs are gone.
The workflow's `plugins` input exists because of that run — it builds a named
subset, so an expensive platform gets proved on one plugin before the suite is
committed to it. What still has not happened is the thing that matters: nobody
has loaded any of those bundles in a DAW on either platform.

---

## 3. Repository layout

```
tezla.spectra/
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
├── .github/workflows/        # CI: tests on 3 platforms, Windows + macOS builds
├── scripts/                  # build.bat (Windows) / build.sh (macOS, Linux)
└── docs/
    ├── BUILD.md              # toolchain setup + build guide (Windows)
    ├── BUILD-MACOS.md        # the same for macOS, plus AU and Gatekeeper
    ├── CI.md                 # what CI builds and where the binaries are
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
- **Update `docs/BUILD.md` whenever a build step or tool version changes** — and
  `docs/BUILD-MACOS.md` too, if the change is not Windows-specific. The two are
  separate documents because the toolchains and install locations share almost
  nothing; sections 3 and 4 of `BUILD.md` (manual CMake, supplying your own
  JUCE) are the cross-platform parts.
- **CI has run, and what it proves is narrower than "it works".** Observed on
  run 36 (`Release v0.8.1`, commit `f85bfae`): the DSP tests pass on Linux,
  Windows, macOS and ARM64 under emulation, and `tezla-measure selftest` passes
  on each. Observed earlier, on run 23: the plugin jobs build and upload a
  Windows VST3 bundle and a macOS universal VST3 + AU bundle. So MSVC and Apple
  clang both compile this tree, and the numbers agree across all four
  architectures.
  Verified locally since, with Anvil and Sonitus added: 579 tests pass on x86-64
  and identically under `qemu-aarch64`, and all six plugins pass Steinberg's
  validator 47/47 on Linux. The count has since grown to **1000 on x86-64** (Svarayantra, Ferrite, Malleus, Crossbar, Phonoss and Membrana included, all twelve plugins validating 47/47); the
  `qemu-aarch64` figure is deliberately stale, and stays that way until §2.3's
  gate lifts. Quote the two separately rather than letting the newer number
  stand for both.
  What is **still not observed** is the thing that matters most: nobody has
  loaded those bundles into a DAW on Windows or macOS from here. This project is
  developed in a Linux container, so "CI is green" means the code compiles and
  the DSP measures correctly — not that FL Studio scans it, not that Logic loads
  the AU, and not that Gatekeeper lets a downloaded build run. Say which of
  those you mean rather than saying "CI passes" and letting it stand for all of
  them.
- **CI does not run by itself.** No push trigger, no pull-request trigger: a
  `v*` tag or **Actions → CI → Run workflow**, and nothing else. So a green tick
  on a commit means somebody asked for one. Run the tests locally before
  pushing — `scripts\build.bat NONE -test`, or `./scripts/build.sh NONE --test`
  — because nothing else will.

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
- **A render-only override is allowed, and must be neutral by default.**
  `dsp::RenderOversampling` (*Same as live / Auto / ×2 / ×4 / ×8*) applies only
  while the host reports offline rendering, resolved through
  `dsp::effectiveOversamplingMode`, so a bounce can run ×8 that the session
  could not afford live. *Same as live* is the default and changes nothing; a
  render at a factor is the live graph at that factor, bit for bit; latency is
  re-declared. Sonitus has it — `docs/PLUGIN-CONVENTIONS.md`, "Render quality".
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

- **Aliasing is a defect everywhere except where it is the instrument.** Bit
  crushing and sample-rate reduction are the documented exception: their whole
  character comes from folded-back images, so they run at the host rate with no
  oversampling and no ADAA, and a test asserts their aliasing *rises*. Anything
  else that generates harmonics is antialiased.
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
- **`prepare()` also resets, so it is never how a parameter change is applied.**
  A filter's state is meaningful — a first-order highpass's memory *is* its last
  input and output — and zeroing it mid-stream steps the output by the whole
  previous sample. Give anything with a continuous, automatable control a
  state-preserving way to move it (`DcBlocker::retune`), and reserve `prepare`
  for building the graph. Emberdrive's expert DC corner had this bug from the
  day it shipped; it ticked once per change and nothing measured it.
- **Any setter that clears state must refuse a no-op, and the guard goes in the
  setter.** The rule above is the special case; this is the general one, and it
  has now bitten four times. `BypassMixer::setLatency` clears the dry delay
  line, correctly — a ring at a new length holds nothing meaningful — and its
  own doc comment ("safe from the audio thread") invites a caller to push the
  current latency every block. Capstone did, and every callback wiped the bypass
  path: bypassed at 64-sample blocks with 53 samples of latency, **83% of the
  output samples were exactly zero**, jumping 0.4985 between neighbours where
  the signal itself steps 0.0196. It reached the user, who heard it as buffer
  underruns before any test caught it.
  The guard belongs in the callee, not the caller, and that is not a style
  preference: guarding at the call site desynchronises the moment two callers
  disagree, and here `prepare()` sets the mixer to its *maximum* latency, so a
  caller comparing against its own previous value would skip the call and leave
  the dry path delayed by the worst case. One guard, in the object that knows
  what it is currently set to.
  The fourth bite widens "clears state" to *re-aims a running process*, and it
  reached the user as a CPU meter. Sonitus pushes all eight envelope settings
  at every voice every control chunk; `Adsr`'s unguarded setters re-aimed the
  running release from its current level each time, which turned the
  finite-time exit into a geometric crawl of ~11× the stated release. Voices
  retired slower than chords arrived and the meter pinned at 100% seconds
  after every key was up. Every silence-based test passed throughout, because
  the zombies were inaudible — assert **activity**, not silence, for anything
  whose cost is the claim.
- **Anything too expensive to recompute per sample gets a timer counted in
  samples, and the sample loop is cut at the timer's boundary** — not at the
  callback's. Rebuilding "once per block" makes the output depend on the host's
  buffer size, and no arrangement of a per-call timer fixes that. Measured on
  Emberdrive, whose voicing costs four biquads and a 512-point probe per band:
  64-sample and 512-sample blocks disagreed by **0.296 of full scale** while a
  parameter settled. Cutting the loop at the boundary took that to exactly zero.
- **Silence in, silence out** (barring intentional noise/hiss features, which
  must be defeatable). A feedback path must not be able to self-start from
  nothing.
- **Any stage permanently in the signal path needs a bit-exact bypass at its
  neutral setting**, not merely a transparent one. "Almost identity" means every
  existing project changes the day the plugin updates.
  The worked example is arithmetic rather than design, which is why it went
  unnoticed for five plugins: a shelf or peak at 0 dB produces a numerator equal
  to its denominator term for term, so it *should* be exactly the identity — but
  `Biquad`'s `normalise` divided through by `a0` as a reciprocal and five
  multiplications, and `a0 * (1 / a0)` is not exactly 1. That left `b0` a unit in
  the last place off, the first output not equal to the input, and the state
  never settling. `a0 / a0` **is** exactly 1. Five divisions at design time cost
  nothing. Assume nothing about a "neutral" setting until a test has fed it a
  signal and compared bit for bit.
- **A feedback loop around a nonlinearity needs a bound that cannot be
  defeated** — a soft clip inside the loop, plus a cap below unity on the amount
  — and a test that sweeps the whole parameter space rather than sampling it.

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
- **The panel design is one thing, shared.** `shared/tezla-ui/.../PanelDesign.hpp`
  holds the numbers, `HouseControls.hpp` the four functions that apply them, and
  `LampButton.hpp` the switch. A new plugin gets the house look by installing
  `ui::KnobLookAndFeel` and calling those; it does not restyle a knob by hand.
  **Never a tick box**, anywhere. Transpectus is the one exception to all of it,
  at the user's request: it is analysis windows rather than knobs on plates.
- **The wheel scrolls the panel and never moves a control** -- `ui::noWheel` at
  construction, `ui::sweepNoWheel` as a net. The documented exception is a
  *graph*, which may take the wheel as a view gesture (zoom, pan) because
  nothing it does is a parameter change; the conditions are in ScrollWheel.hpp.
- The plugin's own name, vendor "Tezla Tech", and a unique 4-character plugin
  code — **registered in `plugins/README.md`** so codes never collide. That file
  also holds reserved names for plugins already sketched out (`Ferrite` for the
  tape machine, `Anvil` for amp/cabinet), so they do not get spent on something
  else.

### Identifiers that are frozen forever

The project is **tezla.spectra**; the company is **Tezla Tech**; the domain is
**tezla.tech**. Those are three different things and only the first is a project
name. Renaming the project must not touch the other two, because four
identifiers below are load-bearing and changing any of them silently destroys
existing work:

| Identifier | Value | What breaks if it changes |
|---|---|---|
| `PLUGIN_MANUFACTURER_CODE` | `Tzla` | Combined with the plugin code it *is* the VST3 unique ID, and it is the AU manufacturer code. Change it and every saved project loses its plugin instances — the host looks for an ID that no longer exists and reports the plugin as missing. |
| `PLUGIN_CODE` | `Tzem` for Emberdrive | Same. |
| `BUNDLE_ID` | `tech.tezla.<Name>` | Reverse-DNS of the **domain**, not the project. The domain has not changed, so neither does this. |
| Parameter string IDs | e.g. `drive` | Renaming one resets that parameter in every project that uses it. |
| Choice-parameter option lists | `dest::`, `division::` | A choice parameter stores an **index**, not a name. Inserting or reordering an entry silently repoints every saved use of it. |

A rename touches URLs, titles, the CMake `project()` and prose. It does not
touch plugin identity. If the manufacturer code ever genuinely has to change,
that is a **new plugin** with a migration path, not a rename.

The one exception so far, and the shape of a legitimate one: `Syrinx`/`Tzsy`
became `Phonoss`/`Tzps` while the plugin had **never shipped, never been in a
project and never had a state saved from it** -- so there was nothing for the
code to protect. That is the whole test. A code may move only while nothing
exists that could look for it, which for every plugin here is a window that
closes the first time the user opens a session with it. Phonoss's is closed.

### Lists that are append-only, for the same reason

The last row of that table is newly easy to break, because an option list looks
like an ordinary array and nothing about `const char* names[]` says "frozen".
Three exist today:

- **`dest::` — the modulation destination list**, in each plugin's
  `PluginProcessor.h`. A modulation slot stores its destination as an index into
  it. Insert an entry and every saved modulation in every project points one
  control to the left; the plugin still loads, still runs, and quietly modulates
  the wrong thing.
- **`division::` — the tempo-sync note values.** Same mechanism: a synced LFO
  stores which division it chose, not what it means.
- Any choice parameter's `StringArray` — `Generator`, `Mode`, the oversampling
  factors. These are already frozen and have been since they shipped.

The rules:

- **New entries go on the end. Always.** Even when the order reads badly. The
  UI can sort what it displays; the stored index cannot be sorted.
- **A destination list holds continuous controls only**, and is built that way
  by construction rather than by care — choices and switches reconfigure rather
  than adjust, and modulating one means a filter rebuild or a crossfade per
  chunk. `oversampling`, `generator` and `bandMode` are excluded for that reason
  even though they are parameters like any other.
- **The list and its parameter IDs are checked against each other at compile
  time**, with a `static_assert` per array. A destination whose parameter was
  renamed is otherwise a null pointer that silently modulates nothing.
- **The modulation parameter IDs themselves live in one place** —
  `shared/tezla-ui/include/tezla/ui/ModulationIds.hpp` — because the MOD strip
  and the assignment rings are shared components that look them up by string.
  A plugin that spelt one differently would get a control that did nothing, and
  finding it would mean comparing two plugins by eye.

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

### When a source cannot be fetched, ask — do not work around it silently

The container's egress proxy blocks most of the web. `dafx.de`, `arxiv.org`,
`manualslib.com` and `audiofanzine.com` are all refused, at the network layer as
well as through the fetch tool, so `curl` is no help either. Web *search* works
and returns useful snippets; fetching the actual paper does not.

**So: whenever a needed source cannot be retrieved, stop and give the user the
URLs.** They will fetch them and hand the contents back. This is not a fallback
to reach for after exhausting alternatives — a paper read second-hand through
search snippets is a paper not read, and the difference matters most exactly
where §9 says copying beats deriving: a fitted coefficient table, a standard's
defined behaviour, a documented edge case.

The rules that follow:

- **List the URLs explicitly**, prioritised, with one line each on what it would
  change. Do not bury the ask at the end of a long message.
- **Say which claims rest on a source that was not read.** "Search snippets say
  X" and "the paper says X" are different statements and must not be blurred.
- **Carry on with what does not depend on it.** Being blocked on one reference
  is not a reason to stop; derive, build, measure, and mark the spot that a
  source would settle.
- **`docs/DSP-REFERENCES.md` records the access, not just the citation.** A row
  for a paper that shaped a design without being read says so.

### Our licence, and what we can take

**This project is AGPLv3** (SPDX `AGPL-3.0-only`) — see `LICENSE` — **with an
attribution-preservation term under AGPLv3 §7(b), stated in `NOTICE.md`**:
copyright is held by The Tezla, development assistance by Claude (Anthropic)
is credited, and redistributions must keep the notice intact. The AGPL is not
a choice so much as a consequence: we build against JUCE's free tier, which is
AGPLv3, so the plugins already were. Declaring it explicitly is what makes
other people's code safe to use.

**Every new first-party source file carries the standard header** — the six
comment lines at the top of any existing `.hpp`/`.cpp`/CMake/script file
(copyright, links, Claude credit, SPDX, the NOTICE pointer). Copy it from a
neighbour when creating a file; it is what makes the attribution term
enforceable file by file. Never stamp it on third-party material —
`technical references/` stays as its authors wrote it.

Compatible, and usable with attribution: **MIT, BSD, Apache-2.0, LGPL, GPLv3,
GPLv2-or-later.**

Incompatible, and to be refused however good it is: **GPLv2-only.** It cannot be
combined with AGPLv3. This is the one to watch for when reading a plugin's
source, because "GPLv2" and "GPLv2 or later" look identical at a glance and only
one of them is usable. Check the per-file header, not the repository's summary.

### Derive by default; copy only what measurement cannot check

The default is to build from the paper and measure the result. That is not
purity, it is what works here: the buffer-size dependence, the DC blocker reset,
the auto-trim overshoot and the LFO block-size drift were all found because the
code was derived and then measured. Pasted code arrives working, which is
exactly why its bugs are indistinguishable from its design choices — all four
would have been inherited silently.

**The exception is knowledge measurement cannot verify.** A published coefficient
table, a standard's exact defined behaviour, a documented edge case with a known
pitfall: take it, because a subtle reimplementation is strictly worse than a
faithful copy. The test is whether a measurement could tell you that you got it
wrong. If it could, build it. If it could not, take it and attribute it.

Anything taken is attributed **twice**: in a comment at the point of use, and in
`docs/DSP-REFERENCES.md` with its licence. Not once.

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
- **CI runs the DSP tests on Linux, Windows and macOS** on every push, which is
  cheap precisely because the DSP needs no framework. A numerical difference
  between MSVC and clang surfaces there before it reaches a DAW.
- **CI also cross-compiles for ARM64 and runs the suite under emulation.**
  Apple Silicon is ARM, and an x86-only assumption is invisible until it runs
  there. That job costs a Linux minute and is how this class of bug gets caught
  without waiting on a Mac runner.
  **Both the job and the local cross-check below are switched off for now —
  see §2.3.** They are the right technique at the wrong time; the gate is the
  x86-64 Windows build being finished. Left here because that is the command
  to use when it is:
  ```
  cmake -B build-arm -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
        -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
        -DCMAKE_EXE_LINKER_FLAGS=-static -DTEZLA_PLUGINS=NONE
  cmake --build build-arm
  TEZLA_EMULATED=1 qemu-aarch64 build-arm/bin/tezla-tests
  ```
- **`TEZLA_EMULATED=1` belongs on every emulated run and on no other.** A
  wall-clock CPU budget is a claim about real hardware; under an emulator it
  measures the emulator, and by 8.8× to 29.8× here (Ferrite 15.8% of a core
  reads 139.4%; the 64-mode resonator 0.37% reads 11.02%). All six budget
  assertions failed the first time the grown suite ran under qemu and not one
  was a defect. `CHECK_CPU_BUDGET` then still runs the work, still prints the
  figure, and marks it `NOT ASSERTED` — the instrument is declared invalid, the
  requirement is not dropped. The binary cannot detect this itself: the same
  AArch64 binary runs under qemu and on an Apple Silicon Mac, and **real ARM64
  hardware must assert**, so it sets nothing.
- **Build with clang before spending a macOS runner.** clang is installed in
  the container and costs a couple of minutes:
  ```
  cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release -DTEZLA_PLUGINS=NONE \
        -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
  cmake --build build-clang -j$(nproc) && ./build-clang/bin/tezla-tests
  ```
  It is not Apple clang and it is glibc rather than libc++, so it cannot
  promise everything — but it caught both macOS problems found so far, either
  of which would otherwise have cost a 10x runner: `std::cyl_bessel_j` **does
  not exist on libc++** (C++17 special maths was never implemented there, so
  Malleus had never once compiled on macOS, and neither libstdc++ nor MSVC had
  any reason to say so), and 15 `-Wunused-lambda-capture` warnings GCC does not
  emit at all. The Bessel evaluation is now `dsp::besselJ`, a trapezoidal rule
  on Bessel's integral, agreeing with `std::cyl_bessel_j` to 8.861e-15 over the
  range used. **Prefer a portable implementation to a C++17 library function
  whose support is patchy**, and check libc++ before relying on one.
- **A Linux build is a cheap dress rehearsal for the Windows one.** The plugin
  target builds and validates on Linux with the X11/ALSA dev packages listed in
  `docs/BUILD.md`, which catches wrapper mistakes long before they reach the
  user's machine. It does not replace building on Windows — MSVC finds things
  g++ does not — but it removes most of the round trips.
- **Build every target before pushing, not the ones you were working on.**
  `scripts\build.bat NONE -test` and `./scripts/build.sh NONE --test` pass no
  `--target`, so they build the whole tree -- which is the point of them. Naming
  targets by hand (`cmake --build build --target tezla-tests`) is faster and is
  how a rename reaches the user broken: `VoiceParameters`'s envelope fields
  became a nested struct, every call site in `plugins/` and `tests/` was
  updated, and `tools/measure_main.cpp` was not, because that target was never
  built here. It failed on MSVC on the user's machine. The tools and the
  `ui-preview` app are the two easiest to forget, because nothing else depends
  on them.
- Report honestly. If a test fails or a step was skipped, say so with the
  output.
- **A failing test is a claim about the code until proven otherwise.** Making it
  skip, loosen or special-case itself is only correct once the behaviour it
  asserts has been shown not to matter. The denormal guard is the worked
  example: the obvious reading was "this platform does not support the feature,
  so skip the test there", and the actual answer was "this platform was never
  implemented, so implement it".
- **A passing test is worth nothing until it has been seen to fail.** Break the
  thing it covers, watch it go red, put it back. This is not ceremony; it is the
  only way to tell a test from a decoration, and it has already caught one here.
  Capstone's ceiling sweep -- 972 combinations, the plugin's whole selling point
  -- passed with the limiter's minimum window deliberately halved against the
  smoother's support, because the clamp at the end of `LimiterCore::process`
  holds the ceiling whatever reaches it. Every peak reading landed exactly on
  the ceiling while the clamp was removing **1.02 of full scale**: the limiter
  had silently become a clipper and no peak measurement could say so.
- **A guard at the end of a chain makes every measurement of the guarded
  quantity true.** So measure what the guard had to *do*, not what came out
  after it. `LimiterCore::getClampExcess()` exists for exactly that, and it is
  the assertion with teeth: a correct chain leaves it 6.1e-15, a broken one 1.02.
  The same applies to any clamp, saturator or safety limiter placed last.

---

## 11. Workflow

- Work on the branch specified for the task; commit in coherent steps with
  descriptive messages; push when the work stands on its own.
- One plugin per branch/PR where possible. Shared-DSP changes that affect
  multiple plugins get their own commit.
- Keep `README.md`, `docs/BUILD.md` and `plugins/README.md` current in the
  same commit as the change that dated them.
- **Work in progress is tracked in the active plugin's `PLAN.md`** — most
  recently `plugins/Malleus/PLAN-PHASE2.md`, whose Continuity section carries a
  per-phase status table and the resume checklist. Any session (whatever assistant or model is
  driving) resumes from the first `pending` phase there; the status row is
  flipped in the same commit as the phase it describes. Phonoss was **paused at
  V2** while Crossbar was built, at the user's request, and resumed at V3;
  it is now **complete through V7**. `plugins/Malleus/PLAN.md`,
  `plugins/Crossbar/PLAN.md`, `plugins/Phonoss/PLAN.md` and
  `plugins/Malleus/PLAN-PHASE2.md` are complete and stay as the worked examples
  of the shape. Sonitus **phase 4 is complete**; Malleus **phase 2 is complete**
  (Bloom, Damp, two exciters with velocity-picked hardness, two listening
  positions); **Membrana is complete through MB7** — the microphone stage
  (`Tzmb`, before Phonoss in the vocal chain), `plugins/Membrana/PLAN.md` is
  the worked example of a plan whose three source papers were user-supplied
  PDFs read first-hand (statuses in `docs/DSP-REFERENCES.md`). No plugin is
  currently in flight; Prism remains parked at the user's request.
- **What is parked lives in `docs/ROADMAP.md`** — the other half of the PLAN.md
  rule. A plan tracks work in flight; the roadmap tracks work deliberately not
  started, and each item names the reason it was parked and the specific thing
  that would unpark it. An item nobody can act on without asking a question
  first has not been written down properly. The first entry is Membrana's
  threshold recalibration, waiting on one dry unnormalised vocal WAV from the
  user; it would move default numbers only, never the model, and §2.1 applies to
  a supplied recording exactly as it does to anything else.
- Prefer a working, measurable, minimal version early over a large unproven
  one. Get it building on Windows, get it loading in FL Studio, then refine
  the sound with the user in the loop — the user's ears are the acceptance test.
- **That loop is x86-64 Windows only until it is finished.** No ARM64
  cross-build, no `qemu-aarch64`, no macOS CI — see §2.3 for the rule and the
  gate that lifts it. It is the same principle as the bullet above, applied to
  platforms rather than to features: the fastest route to a plugin that sounds
  right is the shortest loop between a change and the user hearing it.
