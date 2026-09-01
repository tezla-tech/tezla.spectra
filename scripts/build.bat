@echo off
:: Copyright (c) 2026 The Tezla <thetezla@proton.me>
:: Created by The Tezla -- https://github.com/wingit33/tezla.tech
:: Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
:: Built with development assistance from Claude (Anthropic).
:: SPDX-License-Identifier: AGPL-3.0-only
:: GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

setlocal EnableDelayedExpansion
rem ============================================================================
rem  tezla.spectra build script -- native cmd.exe, no PowerShell involved.
rem
rem  Deliberately not a wrapper around build.ps1. Running an unsigned .ps1 means
rem  either relaxing the machine's execution policy or passing -ExecutionPolicy
rem  Bypass on every invocation, and neither should be a prerequisite for
rem  building a plugin. This script needs nothing but cmake, git and MSVC.
rem
rem    build.bat                          all plugins, Release
rem    build.bat Emberdrive               one plugin
rem    build.bat Emberdrive,Foo           a list
rem    build.bat NONE -test               DSP core + tests only, no JUCE
rem    build.bat -config Debug            Debug build
rem    build.bat --install                also copy to the system VST3 folder
rem    build.bat --installbuild           copy an existing build, skip building
rem    build.bat -clean                   wipe the build folder first
rem    build.bat -juce C:\dev\JUCE        use a JUCE you already have
rem    build.bat -list                    show available plugins
rem    build.bat -help
rem ============================================================================

set "REPO=%~dp0.."
set "PLUGINS=ALL"
set "CONFIG=Release"
set "BUILDDIR=%REPO%\build"
set "DO_INSTALL=0"
set "INSTALL_ONLY=0"
set "DO_TEST=0"
set "DO_CLEAN=0"
set "GENERATOR="
set "JUCEARGS="
set "LTOARGS="

:parse
if "%~1"=="" goto after_parse

rem One leading dash or two, both work.
rem
rem Every other tool takes --install, and typing it here used to fall through
rem to the line at the end of this block that treats an unrecognised argument
rem as the plugin list -- so "build.bat --install" quietly configured a build of
rem a plugin named "--install" and never mentioned the option at all.
set "ARG=%~1"
if "!ARG:~0,2!"=="--" set "ARG=!ARG:~1!"

if /I "!ARG!"=="-help"    goto usage
if /I "!ARG!"=="/?"       goto usage
if /I "!ARG!"=="-list"    goto opt_list
if /I "!ARG!"=="-clean"   goto opt_clean
if /I "!ARG!"=="-install" goto opt_install
if /I "!ARG!"=="-installbuild" goto opt_install_only
if /I "!ARG!"=="-test"    goto opt_test
if /I "!ARG!"=="-config"  goto opt_config
if /I "!ARG!"=="-plugins" goto opt_plugins
if /I "!ARG!"=="-builddir" goto opt_builddir
if /I "!ARG!"=="-juce"        goto opt_juce
if /I "!ARG!"=="-juce-system" goto opt_juce_system
if /I "!ARG!"=="-lto"     goto opt_lto
if /I "!ARG!"=="-ninja"   goto opt_ninja
if /I "!ARG!"=="-vs"      goto opt_vs
rem Anything else is taken as the plugin list, so "build.bat Emberdrive" works.
set "PLUGINS=%~1"
shift
goto parse

:opt_list
call :list_plugins
exit /b 0
:opt_clean
set "DO_CLEAN=1"
shift
goto parse
:opt_install
set "DO_INSTALL=1"
shift
goto parse
:opt_install_only
rem Copy what is already in the build folder, and do nothing else.
rem
rem For the common case: you have just built by hand and want the bundles where
rem FL Studio will look for them. Going through CMake again to be told there is
rem nothing to do is a wait for no reason.
set "DO_INSTALL=1"
set "INSTALL_ONLY=1"
shift
goto parse
:opt_test
set "DO_TEST=1"
shift
goto parse
:opt_config
if "%~2"=="" (
    echo ERROR: -config needs a value, e.g. -config Debug
    exit /b 1
)
set "CONFIG=%~2"
shift
shift
goto parse
:opt_plugins
if "%~2"=="" (
    echo ERROR: -plugins needs a value, e.g. -plugins Emberdrive
    exit /b 1
)
set "PLUGINS=%~2"
shift
shift
goto parse
:opt_builddir
if "%~2"=="" (
    echo ERROR: -builddir needs a value
    exit /b 1
)
set "BUILDDIR=%~2"
shift
shift
goto parse
:opt_juce
if "%~2"=="" (
    echo ERROR: -juce needs the path to your JUCE folder,
    echo        e.g. -juce C:\dev\JUCE
    exit /b 1
)
set "JUCEARGS=-DTEZLA_JUCE_PATH=%~2"
shift
shift
goto parse
:opt_juce_system
set "JUCEARGS=-DTEZLA_JUCE_SOURCE=System"
shift
goto parse
:opt_lto
rem Link-time optimisation. Off by default: measured to make no runtime
rem difference to the DSP (see the TEZLA_LTO comment in CMakeLists.txt) and
rem it makes every link a multiple slower. Here for a release build, and for
rem anyone who wants to measure it on their own machine.
set "LTOARGS=-DTEZLA_LTO=ON"
shift
goto parse
:opt_ninja
set "GENERATOR=Ninja Multi-Config"
shift
goto parse
:opt_vs
set "GENERATOR=Visual Studio 17 2022"
shift
goto parse

:after_parse

rem --installbuild does no configuring, no building and no tool checks: the
rem build folder either has bundles in it or it does not, and cmake has no part
rem in answering that.
if "%INSTALL_ONLY%"=="1" goto collect

rem ---------------------------------------------------------------- tools ----
rem Probed by running them rather than by asking `where`: this also catches a
rem tool that is on PATH but broken, and does not depend on `where` being
rem present, which it is not on every Windows install.
cmake --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake was not found, or failed to run.
    echo        Install it from https://cmake.org/download/ and tick
    echo        "Add CMake to the system PATH" in the installer.
    echo        Then open a NEW Command Prompt. See docs\BUILD.md.
    exit /b 1
)
git --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: git was not found, or failed to run.
    echo        CMake needs it to fetch JUCE.
    echo        Install it from https://git-scm.com/download/win
    exit /b 1
)

rem Ninja is a lot faster, but only usable when the MSVC environment is already
rem loaded -- that is, from a "x64 Native Tools Command Prompt for VS 2022".
rem From a plain cmd.exe the Visual Studio generator is the right choice: it
rem locates the toolchain itself, so no Developer Command Prompt is needed.
rem
rem The Visual Studio generator is the default because it works from anywhere.
rem Ninja is only chosen when both it and the compiler are demonstrably present,
rem and -ninja / -vs override the guess either way.
if not "%GENERATOR%"=="" goto have_generator
ninja --version >nul 2>&1
if errorlevel 1 goto use_vs
cl >nul 2>&1
if errorlevel 9009 goto use_vs
set "GENERATOR=Ninja Multi-Config"
echo Using Ninja Multi-Config ^(MSVC environment detected^).
goto have_generator
:use_vs
set "GENERATOR=Visual Studio 17 2022"
echo Using the Visual Studio 2022 generator.
:have_generator

if "%DO_CLEAN%"=="1" (
    echo Removing "%BUILDDIR%"
    if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
)

rem ------------------------------------------------------------ configure ----
set "GENARGS=-G "%GENERATOR%""
if /I "%GENERATOR%"=="Visual Studio 17 2022" set "GENARGS=-G "%GENERATOR%" -A x64"

echo.
echo Configuring ^(%CONFIG%, plugins: %PLUGINS%^)...
cmake -S "%REPO%" -B "%BUILDDIR%" %GENARGS% -DTEZLA_PLUGINS=%PLUGINS% -DCMAKE_BUILD_TYPE=%CONFIG% %JUCEARGS% %LTOARGS%
if errorlevel 1 (
    echo.
    echo ERROR: CMake configure failed. See docs\BUILD.md, section "Troubleshooting".
    exit /b 1
)

rem ---------------------------------------------------------------- build ----
echo.
echo Building...
cmake --build "%BUILDDIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    exit /b 1
)

rem ----------------------------------------------------------------- test ----
if "%DO_TEST%"=="1" (
    echo.
    echo Running DSP tests...
    set "TESTEXE="
    for /r "%BUILDDIR%" %%F in (tezla-tests.exe) do if exist "%%F" set "TESTEXE=%%F"
    if "!TESTEXE!"=="" (
        echo ERROR: tezla-tests.exe was not built.
        exit /b 1
    )
    "!TESTEXE!"
    if errorlevel 1 (
        echo ERROR: DSP tests failed.
        exit /b 1
    )
)

rem -------------------------------------------------------------- install ----
:collect
set "FOUND=0"
echo.
echo Built VST3 bundles:
for /d /r "%BUILDDIR%" %%D in (*.vst3) do (
    echo    %%D
    set "FOUND=1"
)
if "%FOUND%"=="1" goto have_bundles
if "%INSTALL_ONLY%"=="1" goto nothing_built
echo    ^(none -- no plugin targets were selected^)
goto after_found

:nothing_built
echo    ^(none^)
echo.
echo ERROR: --installbuild found no .vst3 bundles in
echo        "%BUILDDIR%".
echo        There is nothing built to install. Build first, or point at a
echo        different folder with -builddir ^<dir^>.
exit /b 1

:have_bundles
:after_found

if "%DO_INSTALL%"=="1" goto do_install
if "%FOUND%"=="1" (
    echo.
    echo Re-run with --install to copy these to "%CommonProgramFiles%\VST3",
    echo or --installbuild to copy them without building again.
)
goto done

:do_install
echo.
echo Installing to "%CommonProgramFiles%\VST3"...

rem **No elevation check before trying.** The VST3 folder is writable by an
rem ordinary account once somebody has granted it Modify, which is the sane way
rem to set a machine up -- and a script that refuses up front makes having done
rem that pointless. So: attempt the copy, and explain the fix only if it fails.
if not exist "%CommonProgramFiles%\VST3" mkdir "%CommonProgramFiles%\VST3" 2>nul

rem A flag rather than errorlevel, because `if errorlevel` after the loop only
rem sees the *last* call: with one bundle failing and the next succeeding it
rem would read zero and the script would say "Done" having not installed one of
rem them. Every bundle is attempted either way, so one locked file does not hide
rem the rest.
set "INSTALL_FAILED=0"
for /d /r "%BUILDDIR%" %%D in (*.vst3) do call :install_one "%%D" "%%~nxD"
if "!INSTALL_FAILED!"=="1" exit /b 1

echo Done. In FL Studio: Options ^> Manage plugins ^> Find more plugins.

:done
echo.
exit /b 0

:install_one
xcopy /E /I /Y /Q %1 "%CommonProgramFiles%\VST3\%~2" >nul
if errorlevel 1 goto install_failed
echo    %~2
exit /b 0

:install_failed
set "INSTALL_FAILED=1"
echo.
echo ERROR: could not copy %~2 into "%CommonProgramFiles%\VST3".
echo.
echo        Usually the folder is not writable by this account. Either grant
echo        your user Modify on it once (Properties, Security), or run this
echo        from an Administrator prompt.
echo.
echo        The other cause is a DAW holding the old bundle open -- close it
echo        and try again.
exit /b 1

:list_plugins
rem Quoting the wildcard -- for /d %%D in ("%REPO%\plugins\*") -- silently
rem matches nothing, so this pushd's into the folder (quoted, so a path with
rem spaces in it still works) and expands a bare wildcard instead.
echo Available plugins:
set "ANY=0"
pushd "%REPO%\plugins" 2>nul
if errorlevel 1 (
    echo    ^(no plugins folder^)
    exit /b 0
)
for /d %%D in (*) do (
    if exist "%%D\CMakeLists.txt" (
        echo    %%~nxD
        set "ANY=1"
    )
)
popd
if "!ANY!"=="0" echo    ^(none yet^)
exit /b 0

:usage
echo tezla.spectra build script
echo.
echo   build.bat                       all plugins, Release
echo   build.bat Emberdrive            one plugin
echo   build.bat Emberdrive,Foo        a list of plugins
echo   build.bat NONE -test            DSP core + tests only ^(no JUCE, seconds^)
echo   build.bat --installbuild        install what is already built, no rebuild
echo.
echo Options take one dash or two: -install and --install are the same.
echo.
echo Options:
echo   -config ^<cfg^>   Debug ^| Release ^| RelWithDebInfo ^| MinSizeRel
echo   --install       copy the built .vst3 bundles to the system VST3 folder
echo   --installbuild  copy an existing build and skip building entirely
echo   -test           run the DSP unit tests after building
echo   -clean          delete the build folder first
echo   -builddir ^<d^>   use a different build folder
echo   -juce ^<path^>    use a JUCE source tree you already have
echo                   ^(or set the JUCE_PATH environment variable once^)
echo   -juce-system    use a JUCE installed with "cmake --install"
echo   -lto            link-time optimisation ^(release builds; slow link,
echo                   measured no runtime gain -- see docs\BUILD.md^)
echo   -ninja          force the Ninja generator ^(needs a VS developer prompt^)
echo   -vs             force the Visual Studio 2022 generator
echo   -list           show available plugin names
echo   -help           this message
echo.
echo No PowerShell required. See docs\BUILD.md for a fully manual CMake recipe.
exit /b 0
