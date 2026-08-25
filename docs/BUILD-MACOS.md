# Building on macOS

The plugins build natively on macOS as **VST3 and Audio Unit**, universal for
Apple Silicon and Intel. Everything needed is free.

> Windows instructions are in [`BUILD.md`](BUILD.md). The DSP is identical on
> both — only the wrapper and the install locations differ.

---

## 1. Install the tools

| Tool | Why | How |
|---|---|---|
| **Xcode Command Line Tools** | The C++ compiler (clang). The full Xcode app is *not* required. | `xcode-select --install` |
| **CMake** ≥ 3.22 | The build system. | `brew install cmake`, or the app from <https://cmake.org/download/> |
| **Ninja** *(optional)* | Faster builds. | `brew install ninja` |
| **Git** | Comes with the Command Line Tools. | — |

No Homebrew? The Command Line Tools come from Apple directly, and CMake has a
standalone `.dmg`. If you install the CMake app rather than the Homebrew
formula, add it to your `PATH`:

```bash
sudo "/Applications/CMake.app/Contents/bin/cmake-gui" --install
```

### Verify

```bash
clang --version      # Apple clang
cmake --version      # 3.22 or newer
```

**There is no Steinberg SDK to install**, and JUCE is fetched by CMake unless
you point it at your own copy — see [`BUILD.md` §4](BUILD.md#4-juce-getting-it-or-using-one-you-already-have),
which applies unchanged on macOS.

---

## 2. Build

```bash
git clone https://github.com/tezla-tech/tezla.spectra.git
cd tezla.spectra

./scripts/build.sh Emberdrive --install
```

That builds both formats and copies them into your user plug-in folders. Open
your DAW and rescan.

### Options

```bash
./scripts/build.sh --list                 # what plugins exist
./scripts/build.sh Emberdrive             # build one, do not install
./scripts/build.sh ALL --install          # everything, installed
./scripts/build.sh Emberdrive --test      # run the DSP tests too
./scripts/build.sh --config Debug         # debug build
./scripts/build.sh --clean                # wipe build/ first
./scripts/build.sh --native               # this Mac's architecture only
./scripts/build.sh                        # DSP + tests only, no JUCE, seconds
```

**`--native` halves your build time while iterating.** By default the build is
universal (arm64 + x86_64), which compiles everything twice. You only need that
for something you are going to hand to someone else.

---

## 3. Where the plugins go

macOS has fixed locations, and hosts only scan these:

| Format | Per-user (what `--install` uses) | System-wide |
|---|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` | `/Library/Audio/Plug-Ins/VST3/` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components/` | `/Library/Audio/Plug-Ins/Components/` |

`--install` uses the **per-user** folders deliberately: no `sudo`, nothing left
behind in a system directory, and no chance of a stale copy in one location
shadowing a fresh one in the other.

`~/Library` is hidden in Finder. Press <kbd>⌘⇧G</kbd> and paste the path, or
hold <kbd>⌥</kbd> while opening Finder's **Go** menu.

### Which format does your DAW want?

| DAW | VST3 | AU |
|---|---|---|
| Logic Pro, GarageBand | ✗ | **✓ required** |
| Ableton Live | ✓ | ✓ |
| Reaper, Bitwig, Studio One | ✓ | ✓ |
| FL Studio (macOS) | ✓ | ✓ |

Logic loads **nothing but Audio Units**, which is why AU is built by default
here. If you never use Logic, `-DTEZLA_BUILD_AU=OFF` skips it.

---

## 4. Gatekeeper — read this before filing a bug

**This is the single thing that goes wrong on macOS**, and the error message
never says what is actually happening.

macOS attaches a `com.apple.quarantine` attribute to anything that arrives via a
download, a zip, AirDrop or an email attachment. A quarantined plugin that is
not signed and notarised by an Apple Developer account is refused by Gatekeeper,
and the DAW reports it as *"damaged"*, *"incompatible"*, or simply does not list
it at all. The plugin is fine; the attribute is the problem.

A plugin you built yourself locally is **not** quarantined and just works. One
you downloaded — including from this project's CI — is.

To clear it:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Tezla Emberdrive.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Tezla Emberdrive.component"
```

To check whether a bundle is quarantined:

```bash
xattr -l ~/Library/Audio/Plug-Ins/VST3/"Tezla Emberdrive.vst3"
```

`scripts/build.sh --install` clears the attribute automatically, so a locally
built plugin never hits this.

### Signing it properly (optional)

If you have an Apple Developer account (£79/$99 a year), you can sign and
notarise instead, and the plugin will load anywhere with no `xattr` step:

```bash
codesign --force --deep --options runtime --timestamp \
         --sign "Developer ID Application: Your Name (TEAMID)" \
         ~/Library/Audio/Plug-Ins/VST3/"Tezla Emberdrive.vst3"

# Notarisation needs the bundle zipped first
ditto -c -k --keepParent "Tezla Emberdrive.vst3" Emberdrive.zip
xcrun notarytool submit Emberdrive.zip --apple-id you@example.com \
      --team-id TEAMID --password APP_SPECIFIC_PASSWORD --wait
xcrun stapler staple "Tezla Emberdrive.vst3"
```

Without an account this is not possible, and clearing the quarantine attribute
is the supported route. Nothing in this project requires signing.

---

## 5. Building without the script

Everything the script does, by hand:

```bash
cmake -S . -B build -DTEZLA_PLUGINS=Emberdrive
cmake --build build --parallel
```

The result is in `build/plugins/Emberdrive/Emberdrive_artefacts/Release/`, with
`VST3/` and `AU/` subfolders.

Useful flags, in addition to the ones in [`BUILD.md` §3.2](BUILD.md#32-choosing-what-to-build):

| Flag | Effect |
|---|---|
| `-DTEZLA_UNIVERSAL_BINARY=OFF` | Build for this Mac only — much faster |
| `-DCMAKE_OSX_ARCHITECTURES=x86_64` | Intel only |
| `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0` | Raise the minimum macOS version (default 11.0) |
| `-DTEZLA_BUILD_AU=OFF` | Skip the Audio Unit |
| `-DTEZLA_BUILD_STANDALONE=ON` | Also build a standalone `.app` |

> `CMAKE_OSX_ARCHITECTURES` and `CMAKE_OSX_DEPLOYMENT_TARGET` are read by CMake
> *before* `project()` runs, so they only take effect on a **fresh** build
> folder. Changing them in an existing one silently does nothing — delete
> `build/` first.

Installing by hand — note the `-R`, because these bundles are directories:

```bash
cp -R "build/plugins/Emberdrive/Emberdrive_artefacts/Release/VST3/Tezla Emberdrive.vst3" \
      ~/Library/Audio/Plug-Ins/VST3/
cp -R "build/plugins/Emberdrive/Emberdrive_artefacts/Release/AU/Tezla Emberdrive.component" \
      ~/Library/Audio/Plug-Ins/Components/
```

Checking a universal build really is universal:

```bash
lipo -info "build/plugins/Emberdrive/Emberdrive_artefacts/Release/VST3/Tezla Emberdrive.vst3/Contents/MacOS/Tezla Emberdrive"
# Architectures in the fat file: x86_64 arm64
```

---

## 6. Validating

### Audio Unit — `auval`

Apple's validator ships with macOS. For an effect the type is `aufx`, and the
two codes are the plugin code and the manufacturer code:

```bash
auval -v aufx Tzem Tzla
```

A clean run ends with **"AU VALIDATION SUCCEEDED"**. If the plugin is not listed
at all, the AU cache is stale — see the troubleshooting below.

### VST3 — Steinberg's validator

Not shipped with macOS; build it once from the (MIT-licensed) SDK:

```bash
git clone --recursive --depth 1 https://github.com/steinbergmedia/vst3sdk
cd vst3sdk
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
      -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
      -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF
cmake --build build --target validator

./build/bin/Release/validator ~/Library/Audio/Plug-Ins/VST3/"Tezla Emberdrive.vst3"
```

---

## 7. Troubleshooting

**Logic or GarageBand does not see the Audio Unit**
macOS caches AU scans, and the cache is not invalidated by replacing a bundle.
Force a rescan:
```bash
killall -9 AudioComponentRegistrar
```
Then reopen the host. `scripts/build.sh --install` prints this reminder.

**"…is damaged and can't be opened"**
Quarantine, not damage. See [section 4](#4-gatekeeper--read-this-before-filing-a-bug).

**The plugin loads on your Mac but not on someone else's**
Either quarantine again, or you built with `--native` and they are on a
different architecture. `lipo -info` on the binary will tell you which.

**`xcrun: error: invalid active developer path`**
The Command Line Tools are missing or were removed by a system update:
```bash
xcode-select --install
```

**CMake picks the wrong architecture, or ignores `CMAKE_OSX_ARCHITECTURES`**
Those variables are only read on a fresh configure. `rm -rf build` and try
again.

**Rosetta confusion**
If you run the build from a terminal that is itself running under Rosetta, CMake
will detect `x86_64` as the native architecture. Check with `uname -m` — it
should print `arm64` on Apple Silicon. Right-click Terminal in Finder → *Get
Info* → untick *Open using Rosetta*.

**Everything else**
The general troubleshooting in [`BUILD.md` §8](BUILD.md#8-troubleshooting)
applies on macOS too, apart from the MSVC-specific entries.

---

## 8. Prebuilt binaries

Every push to this repository builds Windows and macOS plugins in CI, and they
are downloadable from the run's **Artifacts** section. Tagged versions get a
GitHub Release with zips attached. See [`CI.md`](CI.md).

CI builds are downloaded, so they **are** quarantined — clear the attribute as
in [section 4](#4-gatekeeper--read-this-before-filing-a-bug) before the plugin
will load.
