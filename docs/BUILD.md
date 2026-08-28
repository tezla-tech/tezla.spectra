# Build guide

Everything here is free. No IDE is required, no SDK has to be downloaded by
hand, and a clean clone builds with one command.

> **On a Mac?** Use **[`BUILD-MACOS.md`](BUILD-MACOS.md)** instead — it covers
> VST3 *and* Audio Unit, universal binaries, and the Gatekeeper quarantine issue
> that stops downloaded plugins loading. Sections 3 and 4 of this document
> (manual CMake, and using your own JUCE) apply on every platform.
>
> **Want a prebuilt binary?** Every push builds Windows and macOS plugins in CI
> — see [`CI.md`](CI.md).

---

## 1. Install the tools (Windows 11)

| Tool | Why | Where |
|---|---|---|
| **Visual Studio 2022 Build Tools** | The C++ compiler (MSVC). The command-line-only package; the full Visual Studio Community edition works too if you prefer an IDE. | <https://visualstudio.microsoft.com/downloads/> → *Tools for Visual Studio* → *Build Tools for Visual Studio 2022* |
| **CMake** ≥ 3.22 | The build system. Tick **"Add CMake to the system PATH"** in the installer. | <https://cmake.org/download/> |
| **Git** | Fetches the repository and JUCE. | <https://git-scm.com/download/win> |
| **Ninja** *(optional)* | Noticeably faster builds. CMake ships with a copy, or install standalone. | <https://github.com/ninja-build/ninja/releases> |

**JUCE is not in this list on purpose.** It is fetched by CMake, and if you
already have a copy you can point the build at it instead — see
[section 4](#4-juce-getting-it-or-using-one-you-already-have). There is no
Steinberg VST3 SDK to install either; JUCE bundles it.

When installing the Build Tools, select the **"Desktop development with C++"**
workload. That single checkbox brings in the compiler, the Windows SDK and the
CMake integration. Nothing else is needed.

### Verify

Open a new Command Prompt (`cmd.exe`) and check:

```bat
cmake --version      :: 3.22 or newer
git --version
```

Two things you do **not** need:

- **A "Developer Command Prompt".** The Visual Studio CMake generator locates
  MSVC itself. You only need the developer prompt if you want to build with
  Ninja — see [section 3.3](#33-using-ninja-instead-faster).
- **PowerShell, or any change to its execution policy.** The build script is a
  batch file. See [section 3](#3-building-without-powershell).

---

## 2. Build

```bat
git clone https://github.com/tezla-tech/tezla.spectra.git
cd tezla.spectra

scripts\build.bat
```

`build.bat` is a plain batch file. It runs in ordinary `cmd.exe`, needs no
PowerShell, and does not ask you to change any execution policy — see
[section 3](#3-building-without-powershell) for why that matters and what it
does instead.

The first configure downloads JUCE (a few hundred MB, once — it is cached in
`build\`). Later builds skip that.

### Options

```bat
scripts\build.bat -list                    :: what plugins exist
scripts\build.bat                          :: all plugins, Release
scripts\build.bat Emberdrive               :: one plugin
scripts\build.bat Emberdrive,Foo           :: a list
scripts\build.bat -config Debug            :: debug build
scripts\build.bat --install                :: build, then copy to the VST3 folder
scripts\build.bat --installbuild           :: copy an existing build; no rebuild
scripts\build.bat -test                    :: run the DSP tests afterwards
scripts\build.bat -clean                   :: wipe build\ first
scripts\build.bat NONE -test               :: DSP + tests only; skips JUCE entirely
scripts\build.bat -vs                      :: force the Visual Studio generator
scripts\build.bat -ninja                   :: force Ninja (needs a developer prompt)
scripts\build.bat -juce C:\dev\JUCE        :: use a JUCE you already have
scripts\build.bat -juce-system             :: use a JUCE you installed
```

Options take **one dash or two** — `-install` and `--install` are the same
option. Before, a `--` prefix fell through to the "this must be a plugin name"
branch, so `build.bat --install` quietly configured a build of a plugin called
`--install`.

**`--installbuild` does no building at all.** It skips the tool checks, CMake and
the tests, looks in `build\` for `.vst3` bundles and copies them. That is the
right option when you have just built by hand and only want the bundles where FL
Studio will find them; going through CMake again to be told there is nothing to
do is a wait for nothing. It fails with a clear message if there is nothing
built.

By default the script uses the Visual Studio generator, which works from any
Command Prompt. It switches to Ninja only when it can confirm both `ninja` and
the MSVC compiler are already on `PATH` — that is, when you started from an
"x64 Native Tools Command Prompt". `-vs` and `-ninja` override the guess.

**`NONE` is the fast loop.** It builds the framework-free DSP, its tests and the
measurement tools, needs no JUCE and no download, and finishes in seconds. Use
it while working on DSP; only build a plugin target when you actually want to
load something into FL Studio.

A PowerShell version, `scripts\build.ps1`, exists with the same options in
PowerShell style (`-Plugins Foo,Bar`). It is entirely optional and nothing
depends on it.

---

## 3. Building without PowerShell

**Nothing in this repository requires PowerShell, and you should not have to
relax your machine's script execution policy to build a plugin.**

Windows blocks unsigned `.ps1` files by default. The usual workarounds are to
run `Set-ExecutionPolicy RemoteSigned`, which is a persistent machine-wide
change, or to pass `-ExecutionPolicy Bypass` on every invocation, which is
per-process but still means routinely running unsigned scripts with the guard
switched off. Neither is a reasonable prerequisite for compiling an audio
plugin, so `scripts\build.bat` is a real batch script rather than a wrapper
around the PowerShell one. It shells out to nothing but `cmake`, `git` and
`xcopy`.

If you would rather not run any script at all, the whole build is three CMake
commands. Everything the batch file does is listed below.

### 3.1 The short version

From a plain `cmd.exe` in the repository root:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTEZLA_PLUGINS=Emberdrive
cmake --build build --config Release
```

That is the whole thing. **No Developer Command Prompt is needed** — the Visual
Studio generator locates the MSVC toolchain itself. The result is in
`build\plugins\Emberdrive\Emberdrive_artefacts\Release\VST3\`.

### 3.2 Choosing what to build

`TEZLA_PLUGINS` takes `ALL`, `NONE`, or a comma- or semicolon-separated list:

```bat
:: everything
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTEZLA_PLUGINS=ALL

:: two named plugins
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTEZLA_PLUGINS=Emberdrive,Foo

:: DSP core, unit tests and measurement tools only -- JUCE is never fetched
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTEZLA_PLUGINS=NONE
```

Naming a plugin that does not exist fails the configure with a list of the ones
that do. To see the list without configuring, look in `plugins\` — every folder
there containing a `CMakeLists.txt` is a plugin.

Useful extra options, all optional:

| Option | Effect |
|---|---|
| `-DTEZLA_BUILD_TESTS=OFF` | Skip the unit tests |
| `-DTEZLA_BUILD_TOOLS=OFF` | Skip the measurement tools |
| `-DTEZLA_BUILD_STANDALONE=ON` | Also build a standalone `.exe` per plugin |
| `-DTEZLA_WARNINGS_AS_ERRORS=ON` | Treat warnings as errors |
| `-DTEZLA_LTO=ON` | Link-time optimisation. **Off by default and slow** — see below |
| `-DTEZLA_JUCE_PATH=C:/dev/JUCE` | Use a JUCE you already have — see [section 4](#4-juce-getting-it-or-using-one-you-already-have) |
| `-DTEZLA_JUCE_SOURCE=System` | Use a JUCE installed with `cmake --install` — see [section 4.4](#44-route-c--a-juce-you-installed-with-cmake---install) |

### A note on `TEZLA_LTO`

It is off, and the reason is a measurement rather than a preference. JUCE's
recommended flag is a plain `-flto`, which means **serial** link-time
optimisation on GCC and **monolithic** LTO on Apple clang — the whole program
merged into one module and optimised on a single core at link time. The macOS
plugin job does that for six plugins in two formats, twice over for a universal
binary, and CI run 38 spent **six hours and four minutes** in that one step
before it was cancelled. The same build on Windows with MSVC's LTCG took seven
minutes.

What it buys is cross-translation-unit inlining, which matters for a binary
somebody is going to run and matters not at all while the numbers are still
being measured — and the numbers come from `tezla-tests` and `tezla-measure`,
neither of which is built with it.

Turn it on for a release build when there is one to make. If you do, prefer
`-flto=auto` (GCC) or `-flto=thin` (clang) to the plain flag: both parallelise
the link across cores, and the plain one does not.

### 3.3 Using Ninja instead (faster)

Ninja builds noticeably faster, but unlike the Visual Studio generator it does
not find MSVC on its own — the compiler has to already be on `PATH`. Open
**"x64 Native Tools Command Prompt for VS 2022"** from the Start menu (the
Build Tools installer creates it) and run:

```bat
cmake -S . -B build -G "Ninja Multi-Config" -DTEZLA_PLUGINS=Emberdrive
cmake --build build --config Release
```

`build.bat` picks this path automatically when it can confirm both `ninja` and
`cl` are on `PATH`, and falls back to the Visual Studio generator otherwise.
`scripts\build.bat -ninja` forces it, `-vs` forces the other. That is the only
thing the "which generator" logic in the script does.

### 3.4 Invoking MSVC through the Developer Command Prompt manually

If you want the MSVC environment in a shell you already have open rather than
launching a new one, run `vcvarsall.bat` from your Visual Studio installation:

```bat
:: Build Tools 2022 (adjust the edition folder if you have Community/Professional)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

:: verify
cl
```

`cl` should print its version banner. After that, `ninja`, `cmake` and `cl` all
work in that shell. **Use `x64`** — this repository is 64-bit only and a 32-bit
build will not load in FL Studio.

If you do not know where Visual Studio is installed, `vswhere` will tell you:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
```

### 3.5 Running the tests manually

```bat
cmake --build build --config Release --target tezla-tests
build\bin\Release\tezla-tests.exe

:: or a single group
build\bin\Release\tezla-tests.exe oversampler
```

`ctest --test-dir build -C Release` works too.

### 3.6 Installing manually

A VST3 "file" on Windows is a folder. Copy the whole `.vst3` directory:

```bat
xcopy /E /I /Y ^
  "build\plugins\Emberdrive\Emberdrive_artefacts\Release\VST3\Tezla Emberdrive.vst3" ^
  "%CommonProgramFiles%\VST3\Tezla Emberdrive.vst3"
```

**Whether that needs an Administrator prompt is up to the machine.**
`%CommonProgramFiles%\VST3` is not writable by an ordinary account by default,
but it is a perfectly ordinary thing to grant once — right-click the folder,
Properties → Security, give your user **Modify** — and after that neither this
command nor `build.bat --install` needs elevation. The script does not check
before trying, precisely so that granting it is worth something; it explains the
fix only if the copy actually fails.

The other common failure is a DAW holding the old bundle open. Close it first.

Or avoid the question entirely by putting the bundle in your user VST3 folder and
pointing FL Studio at that instead:

```bat
xcopy /E /I /Y ^
  "build\plugins\Emberdrive\Emberdrive_artefacts\Release\VST3\Tezla Emberdrive.vst3" ^
  "%LOCALAPPDATA%\Programs\Common\VST3\Tezla Emberdrive.vst3"
```

Then in FL Studio, **Options → Manage plugins → Plugin search paths** → add that
folder.

---

## 4. JUCE: getting it, or using one you already have

### 4.1 There is nothing to install

JUCE is a source tree, not an SDK with an installer. Nothing has to go into
`Program Files`, no registry keys, no environment set-up. The build needs a
folder containing JUCE's `modules/` and `extras/` directories, and that is all.

It also **bundles the VST3 wrapper**, so there is no separate Steinberg SDK to
download either, whichever route you pick below.

| Route | Use when | Flag |
|---|---|---|
| **A — download** (default) | You have no JUCE and do not want to manage one | none |
| **B — a source tree you have** | You already keep a JUCE checkout | `-DTEZLA_JUCE_PATH=…` |
| **C — an installed JUCE** | You ran `cmake --install` on JUCE | `-DTEZLA_JUCE_SOURCE=System` |

All three end up with exactly the same targets. The configure output always
says which one it used and what version it found:

```
-- Tezla: using the JUCE source tree at C:/dev/JUCE
-- Tezla: JUCE 9.0.1
```

---

### 4.2 Route A — let CMake download it (the default)

Do nothing. The first configure clones JUCE into the build folder:

```
build\_deps\juce-src\
```

A few hundred MB, once per build folder. It is not re-downloaded on later
builds, and `git` is the only prerequisite. This is what `scripts\build.bat`
does when you give it no JUCE flag.

To throw it away, delete the build folder (`scripts\build.bat -clean`). To keep
it across build folders, use route B instead and point at the downloaded copy.

---

### 4.3 Route B — a JUCE source tree you already have

Get JUCE however you like — either is fine:

```bat
:: a git clone, shallow so it is quick
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE C:\dev\JUCE
```

or download the source zip for a release from
<https://github.com/juce-framework/JUCE/releases> and unzip it anywhere.

Then point the build at it:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DTEZLA_PLUGINS=Emberdrive ^
      -DTEZLA_JUCE_PATH=C:/dev/JUCE
cmake --build build --config Release
```

or with the script:

```bat
scripts\build.bat Emberdrive -juce C:\dev\JUCE
```

**The path is the root of the JUCE repository** — the folder that directly
contains `modules\` and `extras\`. Pointing at `modules\` is the easy mistake
and the build tells you so rather than failing later inside JUCE.

Forward or back slashes both work in the CMake flag.

#### Setting it once instead of every time

`TEZLA_JUCE_PATH` is a CMake cache variable, so it only needs to be given on the
**first** configure of a build folder; later `cmake --build` calls remember it.

If you would rather not repeat it for every new build folder, set an environment
variable once and the build picks it up automatically:

```bat
:: this session only
set JUCE_PATH=C:\dev\JUCE

:: permanently, for your user (open a new terminal afterwards)
setx JUCE_PATH C:\dev\JUCE
```

`TEZLA_JUCE_PATH` works as an environment variable too, and takes precedence.
An explicit `-DTEZLA_JUCE_PATH=` on the command line beats both.

---

### 4.4 Route C — a JUCE you installed with `cmake --install`

JUCE can be installed properly, producing a `JUCEConfig.cmake` that
`find_package` understands. If you have done that — or want to, so several
projects share one copy — this is the route.

To install JUCE itself:

```bat
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE C:\src\JUCE
cd C:\src\JUCE
cmake -B build -DCMAKE_INSTALL_PREFIX=C:/dev/juce-installed
cmake --build build --target install
```

Then build this project against it:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DTEZLA_PLUGINS=Emberdrive ^
      -DTEZLA_JUCE_SOURCE=System ^
      -DCMAKE_PREFIX_PATH=C:/dev/juce-installed
cmake --build build --config Release
```

or:

```bat
scripts\build.bat Emberdrive -juce-system
```

If JUCE is installed somewhere CMake already searches, `-DCMAKE_PREFIX_PATH` is
unnecessary. To point at one exact install, `-DJUCE_DIR=C:/dev/juce-installed/lib/cmake/JUCE-9.0.1`
also works — that is plain `find_package` behaviour, nothing special to this
project.

> Verified: this repository builds a complete, validator-passing VST3 from an
> installed JUCE, not just from a source tree.

---

### 4.5 Which versions work

| | Version |
|---|---|
| Pinned and tested | **9.0.1** |
| Minimum accepted | **8.0.0** |

The minimum is real, not cautious: the editor uses `juce::FontOptions` and the
parameters use the `AudioParameter*Attributes` API, neither of which exists in
JUCE 7. A JUCE below 8.0.0 is rejected at configure time with a message saying
so, rather than failing later as a wall of template errors from inside JUCE's
own headers.

Anything that is not exactly 9.0.1 configures with a warning — supported, but
untested here. If a build fails inside JUCE's headers, try the pinned version
before assuming the plugin is at fault.

To move the pin (for everyone, not just your machine), change
`TEZLA_JUCE_VERSION` in [`../cmake/FetchJUCE.cmake`](../cmake/FetchJUCE.cmake),
or override it for one build folder:

```bat
cmake -S . -B build -DTEZLA_JUCE_VERSION=9.0.0 -DTEZLA_PLUGINS=Emberdrive
```

---

### 4.6 Licence

JUCE is dual-licensed **AGPLv3 or commercial**, with a free tier covering
personal and small-revenue use. That is JUCE's licence and applies to you as its
user regardless of which route above you take — read `LICENSE.md` in whichever
copy you end up with. Nothing in this repository grants or restricts anything
about JUCE.

Steinberg's VST3 SDK, which JUCE bundles, has been MIT-licensed since v3.8
(October 2025), so the plugin *format* carries no obligations at all.

---

### 4.7 Building offline

Route A needs network access on the first configure of each build folder.
Routes B and C need none, which makes them the ones to use on a machine without
internet: clone or install JUCE once where you do have a connection, copy it
across, and point at it.

The DSP-only build (`-DTEZLA_PLUGINS=NONE`) never touches the network at all —
JUCE is not fetched, because it is not needed.

---

## 5. Install into FL Studio

`scripts\build.bat -install` copies the built bundles to
`C:\Program Files\Common Files\VST3\`, which is the folder FL Studio scans by
default. Writing there needs an **Administrator** prompt — right-click Command
Prompt → *Run as administrator*. The script checks and tells you if you are not
elevated rather than failing halfway through.

Without `-install`, the bundles are left in
`build\plugins\<Plugin>\<Plugin>_artefacts\<Config>\VST3\` and you can copy
them yourself ([section 3.6](#36-installing-manually)) or point FL Studio at
that folder.

Then in FL Studio: **Options → Manage plugins → Find more plugins**. Tick
*Verify plugin signatures* off if a fresh build is not picked up, and use
**Rescan previously verified plugins** after replacing an existing build.

> FL Studio caches plugin metadata. After rebuilding a plugin whose parameters
> changed, rescan it, or FL will keep showing the old parameter list.

---

## 6. Building on Linux (optional, for CI and for checking the wrapper)

The plugins target Windows, but the plugin target does build on Linux, and doing
so is a cheap way to catch wrapper mistakes before they reach the DAW — the
Steinberg validator runs there too. On Ubuntu 24.04:

```bash
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libxext-dev libxcomposite-dev libasound2-dev libfreetype6-dev \
    libfontconfig1-dev libgl1-mesa-dev

./scripts/build.sh --plugins Emberdrive
```

This is *not* a substitute for building on Windows — MSVC finds things g++ does
not — but it removes most of the round trips.

---

## 7. Build without a DAW

The DSP is deliberately independent of JUCE, so it builds and runs anywhere:

```bat
scripts\build.bat NONE -test
```

```bash
./scripts/build.sh NONE --test     # Linux/macOS, for CI or DSP work
```

Measurement tools:

```bat
build\bin\Release\tezla-measure selftest
build\bin\Release\tezla-measure clip-aliasing --fs 48000 --drive 4
build\bin\Release\tezla-measure emberdrive --freq 1000 --out emberdrive.csv
build\bin\Release\tezla-measure filter-response --freq 1000 --q 0.707 --out response.csv
build\bin\Release\tezla-measure anvil
build\bin\Release\tezla-measure sonitus
```

`selftest` verifies the analysis chain itself before you trust any number it
produces about a plugin.

### Checking the editors

`tests/` is framework-free by design, which leaves the JUCE layer with no
coverage: layout arithmetic, click wiring and component lifetimes are not
reachable from a unit test. `tezla-render` drives a plugin's real editor
offscreen instead, and `scripts/check-editors.sh` runs it across the suite.

Needs a build configured with the renderer, and an X server or `xvfb-run`:

```bash
cmake -B build-plugin -DTEZLA_BUILD_RENDER=ON
cmake --build build-plugin -j
./scripts/check-editors.sh
```

Ad hoc, one plugin at a time — `hit:` asks whether a click at a component's
centre would actually reach it, a bare id clicks it, `state:` prints one
non-parameter property, and `shot:` photographs the panel:

```bash
BIN=build-plugin/plugins/Sonitus/SonitusRender_artefacts/Release/SonitusRender
xvfb-run -a "$BIN" editor hit:tips tips state:tooltipsEnabled shot:panel.png
```

The TIPS button is why the script exists: it was wired in one plugin of six,
and from outside a button whose callback is null looks exactly like one that
works.

---

## 8. Troubleshooting

**`cmake` is not recognised**
The CMake installer's "Add to PATH" box was not ticked. Re-run the installer, or
add `C:\Program Files\CMake\bin` to PATH. Open a *new* terminal afterwards.

**`No CMAKE_CXX_COMPILER could be found`**
The Build Tools are missing the C++ workload. Re-run the Visual Studio Installer,
*Modify*, and tick **Desktop development with C++**.

**The JUCE download fails, is very slow, or you are offline**
Clone it once by hand and point the build at it:
```bat
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE C:\dev\JUCE
scripts\build.bat Emberdrive -juce C:\dev\JUCE
```
Set `JUCE_PATH` as an environment variable to avoid repeating it. Full detail,
including using a JUCE you have already installed, is in
[section 4](#4-juce-getting-it-or-using-one-you-already-have).

**`'...' is not the top of a JUCE source tree`**
`TEZLA_JUCE_PATH` must point at the folder that directly contains JUCE's
`modules\` and `extras\` directories, not at `modules\` itself. The check is
deliberately strict because `modules\` has a `CMakeLists.txt` of its own, and
pointing there configures happily and then fails with `Unknown CMake command
juce_add_modules`.

**`running scripts is disabled on this system`**
That is PowerShell refusing to run an unsigned `.ps1`. You do not need to change
anything: use `scripts\build.bat`, which is a batch file and not subject to the
execution policy, or run CMake directly as in
[section 3](#3-building-without-powershell). Do not relax your execution policy
for this repository.

**Access denied when installing**
`-install` writes to `C:\Program Files`. Use an Administrator prompt, or install
to your user VST3 folder instead — see [section 3.6](#36-installing-manually).

**FL Studio does not see the plugin**
Confirm the `.vst3` is in `C:\Program Files\Common Files\VST3\`, then rescan.
Check it is a 64-bit build — this repository builds x64 only, and a 32-bit host
will not load it.

**The plugin loads but sounds wrong at 192 kHz**
That is a bug in the plugin, not the host. Report the session sample rate; see
the sample-rate policy in [`../CLAUDE.md`](../CLAUDE.md).

**`A C compiler is required to build targets that depend on JUCE`**
The top-level `project()` must list `C` as well as `CXX` — JUCE vendors zlib,
libpng and friends as C. Already handled; noted here because the error message
points at JUCE rather than at the cause.

---

## 9. Adding a new plugin

1. `plugins/<Name>/` with `CMakeLists.txt`, `Source/`, `Dsp/`, `README.md`.
2. Claim a unique 4-character plugin code in [`../plugins/README.md`](../plugins/README.md).
3. Use `tezla_add_plugin()` from `cmake/TezlaPlugin.cmake` — it applies the
   shared vendor identity, formats and warning settings.
4. Put the DSP in `Dsp/` with no JUCE includes, and add tests in `tests/`.
5. Build it: `scripts\build.bat <Name> -test`

The top-level `CMakeLists.txt` discovers any folder under `plugins/` that has a
`CMakeLists.txt`. Nothing else needs editing.
