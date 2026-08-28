# Continuous integration

[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) **does not run by
itself.** There is no push trigger and no pull-request trigger: it starts when
you ask for it, or when a version tag is pushed. It does three things.

### Starting a run

**Actions → CI → Run workflow.**

The **Run workflow** button only appears once **CI** is selected in the left
sidebar — the "All workflows" view does not show it, and that is the usual
reason for not being able to find it. Type a version into the box to cut a
release, or leave it empty to just test and build.

Pushing a tag matching `v*` starts the same run and always cuts a release.

### Why it is not automatic

Every commit used to start one. Because `cancel-in-progress` kills the previous
run, a normal working session left a column of cancelled runs and burned macOS
minutes — billed at 10× on private repositories — on builds nobody was going to
download. Thirty-five runs had accumulated this way, all but a handful of them
cancelled, before the trigger was changed.

The cost of the change is real and worth stating: **nothing now checks a push.**
Run the tests locally — `scripts\build.bat NONE -test` on Windows,
`./scripts/build.sh NONE --test` elsewhere — and start CI by hand before
tagging.

---

## 1. DSP tests, on all three platforms

Linux, Windows and macOS each build the framework-free DSP with
`-DTEZLA_PLUGINS=NONE`, run the full test suite, and run `tezla-measure
selftest` to confirm the measurement harness itself is sound before anything
trusts a number it produces.

**No JUCE, no download, no GUI stack.** This is what the DSP/wrapper split in
[`CLAUDE.md`](../CLAUDE.md) §4 buys: a numerical difference between MSVC and
clang shows up in about a minute rather than after a full plugin build, and the
job needs nothing installed on the runner.

## 1b. The same tests, cross-compiled for ARM64

A Linux runner cross-compiles the suite for AArch64 and runs it under
`qemu-user`. Apple Silicon is ARM, and an x86-only assumption in the DSP
compiles cleanly, runs happily and does nothing there — which is exactly how a
denormal guard that only handled x86 reached a release. This job catches that
class of bug in about a minute on a 1× runner, rather than waiting on a Mac.

**Currently switched off, deliberately — see CLAUDE.md §2.3.** The job is
skipped on the default `windows` run, and the local reproduction below should
not be run either, until the x86-64 Windows build is bug-squashed and
feature-complete. The technique is right; the timing is not, and every minute
spent here is a minute not spent on the platform the plugins are actually being
played on. It is written down so it is ready when the gate lifts.

Reproduce it locally, once it does, with `gcc-aarch64-linux-gnu` and `qemu-user`
installed:

```bash
cmake -B build-arm -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
      -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
      -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
      -DCMAKE_EXE_LINKER_FLAGS=-static -DTEZLA_PLUGINS=NONE
cmake --build build-arm
qemu-aarch64 build-arm/bin/tezla-tests
```

## 2. Plugin builds, downloadable

Windows and macOS build the actual plugins and upload them as run artifacts:

| Platform | Formats | Architecture |
|---|---|---|
| Windows | VST3 | x86-64 |
| macOS | VST3 + Audio Unit | universal (arm64 + x86_64) |

**Only Windows, unless you ask for more.** The "platforms" box on the Run
workflow form takes `windows` (the default) or `all`; a tag push covers Windows.
`all` adds the macOS DSP tests, the macOS plugin build and the emulated ARM64
suite.

That is a phase rather than a policy, and CLAUDE.md §2.3 says when it ends: the
x86-64 Windows build being finished. The rig is Windows 11 and FL Studio and
the loop that matters is build → load → play → say what is wrong; nobody has
loaded any of these in a Mac DAW yet, so a macOS build is an artefact nobody
downloads. It is also the most expensive thing in the workflow — see **Cost**.
When Mac testing starts, run `all` and this paragraph is what changes.

A Windows-only release says so in its own notes rather than quietly shipping
half of what the notes describe.

Find them under **Actions → the run → Artifacts**, at the bottom of the page.
They are kept for 30 days.

JUCE is cloned once and cached, keyed on the version read out of
[`cmake/FetchJUCE.cmake`](../cmake/FetchJUCE.cmake) — so bumping the pin there
is the only change needed and the cache invalidates itself.

## 3. Releases

There are two ways to ask for one, and if you were expecting a release and got
a skipped job, it is because neither happened.

### Push a version tag

```bash
git tag v0.5.0
git push origin v0.5.0
```

### Or run the workflow by hand

**Actions → CI → Run workflow**, then type a version into the box. Useful when
you want a build to hand to someone without committing to a tag first — the tag
is created for you, pointing at the exact commit that was built and tested.

Leaving the box empty just builds, and cuts no release.

### Either way

The version must look like `v1.2.3`, optionally with a pre-release suffix
(`v1.2.3-beta.1`). Anything else fails the job rather than creating a tag you
did not mean. Both routes package the artifacts into per-platform zips and
attach them to a GitHub Release, with notes that include the macOS quarantine
instructions — without those, a downloaded plugin will not load and the DAW's
error message blames the plugin.

The build job writes all of this into the run summary, so the reason a release
did or did not happen is on the run page rather than only in this file.

---

## Downloaded builds and macOS quarantine

**CI builds are not code signed.** macOS attaches a quarantine attribute to
anything downloaded, and Gatekeeper refuses unsigned quarantined plugins — the
DAW then reports the plugin as damaged, or does not list it at all.

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/*.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/*.component
```

Signing properly needs a paid Apple Developer account; see
[`BUILD-MACOS.md` §4](BUILD-MACOS.md#4-gatekeeper--read-this-before-filing-a-bug).
Locally built plugins are never quarantined, so this only affects downloads.

---

## Cost

macOS runner minutes bill at **10×** on private repositories; Windows at 2×,
Linux at 1×. The macOS plugin build is the expensive job, and building a
universal binary compiles everything twice.

Measured, from run 38:

| Job | Duration |
|---|---|
| DSP tests (Linux) | 47 s |
| DSP tests (macOS) | 1 m 05 |
| DSP tests (Windows) | 1 m 17 |
| DSP tests (ARM64, emulated) | 6 m 31 |
| Plugins (Windows) | 9 m 20 |
| **Plugins (macOS)** | **6 h 04, then cancelled** |

**Two lessons in that table, and neither is the obvious one.**

The emulated ARM64 job is the one people notice, and it is 2% of the wall time.
Cutting it saves nothing worth having, and it is the only thing here that
catches an AArch64-specific numerical bug — it found the FMA-contraction one,
which was invisible on x86-64 and always active on ARM.

The macOS plugin build is everything, and the cause was **link-time
optimisation**, not the platform. Identical work took Windows nine minutes under
MSVC's LTCG. JUCE's recommended flag is a plain `-flto`, which on Apple clang
means *monolithic* LTO — the whole program merged into one module and optimised
on one core — for six plugins in two formats, twice over for a universal binary.
`TEZLA_LTO` is now **off** by default; see [`BUILD.md`](BUILD.md#a-note-on-tezla_lto).

If you later want the cheap `test` job back on every push while leaving the
expensive `build` job manual, split it into a second workflow file with its own
`on: push` — a single workflow cannot give one job a trigger the others do
not have.

---

## What CI does not do

- **It does not run the plugin validators.** Steinberg's `validator` and Apple's
  `auval` both catch real problems, and wiring them in is a natural next step —
  it was left out to keep the runs quick. Locally, both are documented in
  [`BUILD-MACOS.md` §6](BUILD-MACOS.md#6-validating).
- **It does not test the plugin in a host.** Nothing automated does; that is
  what the standalone build and your ears are for.
- **It does not sign or notarise.** That needs an Apple Developer account and
  secrets in the repository.

---

## A caveat about this workflow

It has been reasoned about carefully and its shell steps were run locally
against real build output, but **it has never executed on a Windows or macOS
runner** — this project is developed in a Linux container. The first push may
well need a fix. The likeliest candidates:

- **Bundle collection on Windows.** A VST3 bundle there is a *folder* containing
  `Contents/x86_64-win/Name.vst3`, a file with the same extension. The workflow
  matches `-type d` for exactly this reason, but that specific case could not be
  verified here.
- **Path and quoting behaviour under Git Bash on the Windows runner.** All steps
  use `shell: bash` for consistency; the plugin names contain spaces.
- **macOS universal builds** may need a longer timeout than the default.
