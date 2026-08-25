# Continuous integration

[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) runs on every push and
pull request. It does three things.

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

Reproduce it locally with `gcc-aarch64-linux-gnu` and `qemu-user` installed:

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

Find them under **Actions → the run → Artifacts**, at the bottom of the page.
They are kept for 30 days.

JUCE is cloned once and cached, keyed on the version read out of
[`cmake/FetchJUCE.cmake`](../cmake/FetchJUCE.cmake) — so bumping the pin there
is the only change needed and the cache invalidates itself.

## 3. Releases, on a version tag

Pushing a tag beginning with `v` additionally packages the artifacts into zips
and creates a GitHub Release:

```bash
git tag v0.3.0
git push origin v0.3.0
```

The release notes are generated from
[a template in the workflow](../.github/workflows/ci.yml) and include the macOS
quarantine instructions, because without them a downloaded plugin will not load
and the DAW's error message blames the plugin.

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

If that becomes a problem, restrict the `build` job to tags and manual runs by
changing its trigger, leaving the cheap `test` job on every push. The tests are
where most of the value is anyway — all the DSP is framework-free, so they cover
the part that actually makes the sound.

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
