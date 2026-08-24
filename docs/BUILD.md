# Build guide

Everything here is free. No IDE is required, no SDK has to be downloaded by
hand, and a clean clone builds with one command.

---

## 1. Install the tools (Windows 11)

| Tool | Why | Where |
|---|---|---|
| **Visual Studio 2022 Build Tools** | The C++ compiler (MSVC). The command-line-only package; the full Visual Studio Community edition works too if you prefer an IDE. | <https://visualstudio.microsoft.com/downloads/> → *Tools for Visual Studio* → *Build Tools for Visual Studio 2022* |
| **CMake** ≥ 3.22 | The build system. Tick **"Add CMake to the system PATH"** in the installer. | <https://cmake.org/download/> |
| **Git** | Fetches the repository and JUCE. | <https://git-scm.com/download/win> |
| **Ninja** *(optional)* | Noticeably faster builds. CMake ships with a copy, or install standalone. | <https://github.com/ninja-build/ninja/releases> |

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
git clone https://github.com/wingit33/tezla.tech.git
cd tezla.tech

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
scripts\build.bat -install                 :: copy to the system VST3 folder
scripts\build.bat -test                    :: run the DSP tests afterwards
scripts\build.bat -clean                   :: wipe build\ first
scripts\build.bat NONE -test               :: DSP + tests only; skips JUCE entirely
scripts\build.bat -vs                      :: force the Visual Studio generator
scripts\build.bat -ninja                   :: force Ninja (needs a developer prompt)
```

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
| `-DTEZLA_JUCE_PATH=C:\dev\JUCE` | Use a local JUCE checkout instead of downloading |

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
:: from an Administrator prompt
xcopy /E /I /Y ^
  "build\plugins\Emberdrive\Emberdrive_artefacts\Release\VST3\Tezla Emberdrive.vst3" ^
  "%CommonProgramFiles%\VST3\Tezla Emberdrive.vst3"
```

Or skip the elevation entirely by putting it in your user VST3 folder and
pointing FL Studio at that instead:

```bat
xcopy /E /I /Y ^
  "build\plugins\Emberdrive\Emberdrive_artefacts\Release\VST3\Tezla Emberdrive.vst3" ^
  "%LOCALAPPDATA%\Programs\Common\VST3\Tezla Emberdrive.vst3"
```

Then in FL Studio, **Options → Manage plugins → Plugin search paths** → add that
folder.

---

## 4. Install into FL Studio

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

## 5. Building on Linux (optional, for CI and for checking the wrapper)

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

## 6. Build without a DAW

The DSP is deliberately independent of JUCE, so it builds and runs anywhere:

```bat
scripts\build.bat NONE -test
```

```bash
./scripts/build.sh --test          # Linux/macOS, for CI or DSP work
```

Measurement tools:

```bat
build\bin\Release\tezla-measure selftest
build\bin\Release\tezla-measure clip-aliasing --fs 48000 --drive 4
build\bin\Release\tezla-measure emberdrive --freq 1000 --out emberdrive.csv
build\bin\Release\tezla-measure filter-response --freq 1000 --q 0.707 --out response.csv
```

`selftest` verifies the analysis chain itself before you trust any number it
produces about a plugin.

---

## 7. Troubleshooting

**`cmake` is not recognised**
The CMake installer's "Add to PATH" box was not ticked. Re-run the installer, or
add `C:\Program Files\CMake\bin` to PATH. Open a *new* terminal afterwards.

**`No CMAKE_CXX_COMPILER could be found`**
The Build Tools are missing the C++ workload. Re-run the Visual Studio Installer,
*Modify*, and tick **Desktop development with C++**.

**The JUCE download fails or is very slow**
Clone it once by hand and point the build at it:
```bat
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE C:\dev\JUCE
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DTEZLA_JUCE_PATH=C:\dev\JUCE -DTEZLA_PLUGINS=Emberdrive
cmake --build build --config Release
```

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

## 8. Adding a new plugin

1. `plugins/<Name>/` with `CMakeLists.txt`, `Source/`, `Dsp/`, `README.md`.
2. Claim a unique 4-character plugin code in [`../plugins/README.md`](../plugins/README.md).
3. Use `tezla_add_plugin()` from `cmake/TezlaPlugin.cmake` — it applies the
   shared vendor identity, formats and warning settings.
4. Put the DSP in `Dsp/` with no JUCE includes, and add tests in `tests/`.
5. Build it: `scripts\build.bat <Name> -test`

The top-level `CMakeLists.txt` discovers any folder under `plugins/` that has a
`CMakeLists.txt`. Nothing else needs editing.
