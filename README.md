# tezla.tech

64-bit **VST3** audio plugins for Windows 11 / FL Studio 2026 — built for
dubstep and DnB/jungle production.

Analogue character first: tube, tape and transformer saturation, cabinet
colouration, musical dynamics. Clean digital when you want it to disappear.
Everything is written from scratch, from physics and published research —
no reverse engineering of anyone's product.

The first plugin, **Emberdrive**, measures identically to within 0.01 dB at
44.1, 48, 96 and 192 kHz, holds its output level within 0.33 dB across a 30 dB
drive range, and keeps audible-band aliasing below −220 dB. It also has a
wavefolder with a ×100 range for when none of that is the point. Numbers, not
adjectives: see [its README](plugins/Emberdrive/README.md).

---

## Status

| Plugin | Type | Status |
|---|---|---|
| **[Emberdrive](plugins/Emberdrive/)** | Saturation, wavefolder, rectifier, crusher, feedback, 3-band | v0.3.0 — 100 tests pass, 47/47 on Steinberg's validator |

See [`plugins/README.md`](plugins/README.md) for the plugin registry.

---

## Quick start (Windows 11)

Install [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/),
[CMake](https://cmake.org/download/) and [Git](https://git-scm.com/download/win) —
all free. Then:

```bat
git clone https://github.com/wingit33/tezla.tech.git
cd tezla.tech

scripts\build.bat                        :: build everything, Release
scripts\build.bat Emberdrive             :: build one plugin
scripts\build.bat Emberdrive,Foo         :: build a list
scripts\build.bat -install               :: also copy .vst3 to the system folder
scripts\build.bat -list                  :: show available plugin targets
scripts\build.bat NONE -test             :: DSP + tests only, seconds, no JUCE
```

`build.bat` is a plain batch file: it runs in ordinary `cmd.exe`, needs no
PowerShell, and **does not ask you to relax any script execution policy**. If
you would rather run no script at all, the whole build is two CMake commands —
`docs/BUILD.md` §3 has the manual recipe.

Built plugins land in `build\plugins\<Plugin>\<Plugin>_artefacts\<Config>\VST3\`.
`-install` copies them to `C:\Program Files\Common Files\VST3\` (run the prompt
as Administrator), which is where FL Studio scans by default.

Full instructions, including where to get every tool and how to fix a failed
build: **[`docs/BUILD.md`](docs/BUILD.md)**.

## Quick start (macOS)

Needs only the Xcode Command Line Tools and CMake, both free. Builds **VST3 and
Audio Unit**, universal for Apple Silicon and Intel:

```bash
xcode-select --install
brew install cmake ninja

git clone https://github.com/wingit33/tezla.tech.git
cd tezla.tech
./scripts/build.sh Emberdrive --install
```

Full instructions, including the Gatekeeper quarantine issue that stops
downloaded plugins loading: **[`docs/BUILD-MACOS.md`](docs/BUILD-MACOS.md)**.

## Prebuilt binaries

Every push builds Windows and macOS plugins in CI, downloadable from the run's
artifacts; version tags cut a GitHub Release. See
**[`docs/CI.md`](docs/CI.md)**.

---

## How it's put together

```
shared/tezla-dsp/     Header-only, framework-free C++20 DSP. No JUCE.
plugins/<Name>/Dsp/   That plugin's DSP. Also framework-free.
plugins/<Name>/Source/  Thin JUCE layer: parameters, state, editor.
tests/ + tools/       Unit tests and offline measurement (THD, aliasing, response).
```

The DSP is deliberately independent of any plugin framework. It compiles and
runs with plain g++/clang/MSVC, so aliasing, distortion and frequency response
are **measured offline before anything reaches a DAW** — and so moving to CLAP
or a standalone app later costs a wrapper rather than a rewrite.

Design rules, sample-rate/oversampling policy and audio-quality standards live
in [`CLAUDE.md`](CLAUDE.md).

---

## Sample rates

Sessions here run at 48, 96 or 192 kHz, and the plugins must sound identical at
all of them. Nonlinear stages run at a controlled internal rate, with
oversampling set to **Auto** by default: ×4 at 48 kHz, ×2 at 96 kHz, off at
192 kHz — all landing near the same ~192 kHz effective rate. Every oversampling
control's tooltip reports what Auto is actually doing at your current session
rate. Manual override is always available if you want to spend or save CPU.

---

## Documentation

- [`docs/BUILD.md`](docs/BUILD.md) — toolchain setup and build guide (Windows)
- [`docs/BUILD-MACOS.md`](docs/BUILD-MACOS.md) — the same for macOS, plus AU and Gatekeeper
- [`docs/CI.md`](docs/CI.md) — what continuous integration builds and where to get it
- [`docs/PLUGIN-CONVENTIONS.md`](docs/PLUGIN-CONVENTIONS.md) — parameters, presets, UI, versioning
- [`docs/DSP-REFERENCES.md`](docs/DSP-REFERENCES.md) — papers and open-source references used, with licences
- [`plugins/README.md`](plugins/README.md) — plugin registry and how to add a new one

---

## Licensing

Plugin source in this repository is ours. Third-party dependencies and any
externally derived DSP are listed with their licences in
[`docs/DSP-REFERENCES.md`](docs/DSP-REFERENCES.md).

VST is a trademark of Steinberg Media Technologies GmbH. Any commercial plugin
named in this repository is referenced only to describe a *sound* or a
*workflow*; no proprietary code, artwork or data has been used.
