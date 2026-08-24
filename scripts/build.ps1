<#
.SYNOPSIS
    Builds the tezla.tech plugins on Windows.

.DESCRIPTION
    Configures and builds with CMake. Needs only free tools: Visual Studio 2022
    Build Tools, CMake and Git. See docs/BUILD.md.

.PARAMETER Plugins
    Which plugins to build. Omit for all of them, or pass names:
        -Plugins Foo
        -Plugins Foo,Bar
    Use -List to see what is available. Pass NONE to build only the DSP core,
    its tests and the measurement tools -- that path does not need JUCE at all,
    so it is the fastest way to check a DSP change.

.PARAMETER Config
    Debug, Release, RelWithDebInfo or MinSizeRel. Defaults to Release.
    Never judge CPU cost or aliasing from a Debug build.

.PARAMETER Install
    Also copy the built .vst3 bundles into C:\Program Files\Common Files\VST3,
    where FL Studio scans. Needs an Administrator shell.

.PARAMETER Test
    Run the DSP unit tests after building.

.PARAMETER Clean
    Delete the build folder first.

.PARAMETER List
    List the available plugin names and exit.

.EXAMPLE
    .\scripts\build.ps1
.EXAMPLE
    .\scripts\build.ps1 -Plugins Foo -Config Debug -Install
.EXAMPLE
    .\scripts\build.ps1 -Plugins NONE -Test
#>

[CmdletBinding()]
param(
    [string[]] $Plugins = @('ALL'),
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]   $Config = 'Release',
    [string]   $BuildDir = '',
    [string]   $Generator = '',
    [switch]   $Install,
    [switch]   $Test,
    [switch]   $Clean,
    [switch]   $List
)

$ErrorActionPreference = 'Stop'

$repoRoot  = Split-Path -Parent $PSScriptRoot
$pluginDir = Join-Path $repoRoot 'plugins'

function Get-AvailablePlugins {
    if (-not (Test-Path $pluginDir)) { return @() }
    Get-ChildItem -Path $pluginDir -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'CMakeLists.txt') } |
        Select-Object -ExpandProperty Name |
        Sort-Object
}

if ($List) {
    $available = Get-AvailablePlugins
    if ($available.Count -eq 0) {
        Write-Host 'No plugins in plugins/ yet.'
    } else {
        Write-Host 'Available plugins:'
        $available | ForEach-Object { Write-Host "  $_" }
    }
    exit 0
}

# --- tool check ------------------------------------------------------------
foreach ($tool in @('cmake', 'git')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Error "$tool was not found on PATH. See docs/BUILD.md for where to get it."
    }
}

if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build' }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

# --- plugin selection ------------------------------------------------------
$selection = ($Plugins -join ';')
if ($Plugins.Count -eq 1 -and $Plugins[0] -match '^(ALL|NONE)$') {
    $selection = $Plugins[0].ToUpper()
} else {
    $available = Get-AvailablePlugins
    foreach ($name in $Plugins) {
        if ($available -notcontains $name) {
            Write-Error "Unknown plugin '$name'. Available: $($available -join ', ')  (use -List)"
        }
    }
}

# --- configure -------------------------------------------------------------
$cmakeArgs = @(
    '-S', $repoRoot,
    '-B', $BuildDir,
    "-DTEZLA_PLUGINS=$selection",
    "-DCMAKE_BUILD_TYPE=$Config"
)

if ($Generator) {
    $cmakeArgs += @('-G', $Generator)
} elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    # Ninja is a multi-minute saving on a JUCE build. Use it when it is there.
    $cmakeArgs += @('-G', 'Ninja Multi-Config')
}

if ($Install) {
    $cmakeArgs += '-DTEZLA_COPY_AFTER_BUILD=ON'
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Warning 'Not running as Administrator: copying to C:\Program Files\Common Files\VST3 will fail. Re-run this shell as Administrator.'
    }
}

Write-Host "Configuring ($Config, plugins: $selection)..." -ForegroundColor Cyan
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Write-Error 'CMake configure failed.' }

# --- build -----------------------------------------------------------------
Write-Host "Building..." -ForegroundColor Cyan
& cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { Write-Error 'Build failed.' }

# --- test ------------------------------------------------------------------
if ($Test) {
    Write-Host 'Running DSP tests...' -ForegroundColor Cyan
    $testExe = Get-ChildItem -Path $BuildDir -Recurse -Filter 'tezla-tests.exe' -ErrorAction SilentlyContinue |
               Select-Object -First 1
    if ($null -eq $testExe) { Write-Error 'tezla-tests.exe was not built.' }
    & $testExe.FullName
    if ($LASTEXITCODE -ne 0) { Write-Error 'DSP tests failed.' }
}

# --- report ----------------------------------------------------------------
Write-Host ''
$bundles = Get-ChildItem -Path $BuildDir -Recurse -Directory -Filter '*.vst3' -ErrorAction SilentlyContinue
if ($bundles) {
    Write-Host 'Built VST3 bundles:' -ForegroundColor Green
    $bundles | ForEach-Object { Write-Host "  $($_.FullName)" }
    if (-not $Install) {
        Write-Host ''
        Write-Host 'Re-run with -Install (as Administrator) to copy these to C:\Program Files\Common Files\VST3.'
    }
} else {
    Write-Host 'Done (no plugin targets were selected).' -ForegroundColor Green
}
