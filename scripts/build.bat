@echo off
setlocal EnableDelayedExpansion
rem ============================================================================
rem  tezla.tech build script -- native cmd.exe, no PowerShell involved.
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
rem    build.bat -install                 also copy to the system VST3 folder
rem    build.bat -clean                   wipe the build folder first
rem    build.bat -list                    show available plugins
rem    build.bat -help
rem ============================================================================

set "REPO=%~dp0.."
set "PLUGINS=ALL"
set "CONFIG=Release"
set "BUILDDIR=%REPO%\build"
set "DO_INSTALL=0"
set "DO_TEST=0"
set "DO_CLEAN=0"
set "GENERATOR="

:parse
if "%~1"=="" goto after_parse
if /I "%~1"=="-help"    goto usage
if /I "%~1"=="--help"   goto usage
if /I "%~1"=="/?"       goto usage
if /I "%~1"=="-list"    goto opt_list
if /I "%~1"=="-clean"   goto opt_clean
if /I "%~1"=="-install" goto opt_install
if /I "%~1"=="-test"    goto opt_test
if /I "%~1"=="-config"  goto opt_config
if /I "%~1"=="-plugins" goto opt_plugins
if /I "%~1"=="-builddir" goto opt_builddir
if /I "%~1"=="-ninja"   goto opt_ninja
if /I "%~1"=="-vs"      goto opt_vs
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
:opt_ninja
set "GENERATOR=Ninja Multi-Config"
shift
goto parse
:opt_vs
set "GENERATOR=Visual Studio 17 2022"
shift
goto parse

:after_parse

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
cmake -S "%REPO%" -B "%BUILDDIR%" %GENARGS% -DTEZLA_PLUGINS=%PLUGINS% -DCMAKE_BUILD_TYPE=%CONFIG%
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
set "FOUND=0"
echo.
echo Built VST3 bundles:
for /d /r "%BUILDDIR%" %%D in (*.vst3) do (
    echo    %%D
    set "FOUND=1"
)
if "%FOUND%"=="0" echo    ^(none -- no plugin targets were selected^)

if "%DO_INSTALL%"=="1" goto do_install
if "%FOUND%"=="1" (
    echo.
    echo Re-run with -install from an Administrator prompt to copy these to
    echo "%CommonProgramFiles%\VST3".
)
goto done

:do_install
net session >nul 2>&1
if errorlevel 1 (
    echo.
    echo ERROR: -install writes to "%CommonProgramFiles%\VST3", which needs
    echo        an elevated prompt. Right-click Command Prompt, "Run as
    echo        administrator", and run this again.
    exit /b 1
)
echo.
echo Installing to "%CommonProgramFiles%\VST3"...
for /d /r "%BUILDDIR%" %%D in (*.vst3) do (
    echo    %%~nxD
    xcopy /E /I /Y /Q "%%D" "%CommonProgramFiles%\VST3\%%~nxD" >nul
    if errorlevel 1 (
        echo ERROR: failed to copy %%~nxD
        exit /b 1
    )
)
echo Done. In FL Studio: Options ^> Manage plugins ^> Find more plugins.

:done
echo.
exit /b 0

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
echo tezla.tech build script
echo.
echo   build.bat                       all plugins, Release
echo   build.bat Emberdrive            one plugin
echo   build.bat Emberdrive,Foo        a list of plugins
echo   build.bat NONE -test            DSP core + tests only ^(no JUCE, seconds^)
echo.
echo Options:
echo   -config ^<cfg^>   Debug ^| Release ^| RelWithDebInfo ^| MinSizeRel
echo   -install        copy the built .vst3 bundles to the system VST3 folder
echo                   ^(needs an Administrator prompt^)
echo   -test           run the DSP unit tests after building
echo   -clean          delete the build folder first
echo   -builddir ^<d^>   use a different build folder
echo   -ninja          force the Ninja generator ^(needs a VS developer prompt^)
echo   -vs             force the Visual Studio 2022 generator
echo   -list           show available plugin names
echo   -help           this message
echo.
echo No PowerShell required. See docs\BUILD.md for a fully manual CMake recipe.
exit /b 0
