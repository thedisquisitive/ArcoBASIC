param(
    [string]$BuildDir = "",
    [string]$Configuration = "Release",
    [string]$InstallPrefix = "",
    [switch]$SkipDependencyInstall,
    [switch]$RunTests,
    [switch]$Install
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Test-Command {
    param([string]$Name)
    $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE"
    }
}

function Install-WingetPackage {
    param(
        [string]$Id,
        [string]$Name,
        [string[]]$ExtraArgs = @()
    )

    if (-not (Test-Command "winget")) {
        throw "$Name is missing and winget is not available. Install $Name manually, then rerun with -SkipDependencyInstall."
    }

    Write-Step "Installing $Name"
    $args = @(
        "install",
        "--id", $Id,
        "--exact",
        "--source", "winget",
        "--accept-package-agreements",
        "--accept-source-agreements"
    ) + $ExtraArgs
    Invoke-Native "winget" $args
}

function Test-VisualStudioInstall {
    $candidates = @(
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $true
        }
    }
    return $false
}

function Get-DefaultGenerator {
    if ($env:ARCO_CMAKE_GENERATOR) {
        return $env:ARCO_CMAKE_GENERATOR
    }

    if (Test-VisualStudioInstall) {
        return "Visual Studio 17 2022"
    }

    if ((Test-Command "ninja") -and ((Test-Command "cl") -or (Test-Command "clang") -or (Test-Command "g++"))) {
        return "Ninja"
    }

    return ""
}

$RunningOnWindows =
    ($PSVersionTable.PSEdition -eq "Desktop") -or
    ($PSVersionTable.ContainsKey("Platform") -and $PSVersionTable.Platform -eq "Win32NT") -or
    ($IsWindows -eq $true)

if (-not $RunningOnWindows) {
    throw "scripts/build-windows.ps1 must be run on Windows PowerShell or PowerShell 7 on Windows."
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = Resolve-Path (Join-Path $ScriptDir "..")
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $SourceDir "build-windows"
}
if ([string]::IsNullOrWhiteSpace($InstallPrefix)) {
    $InstallPrefix = Join-Path $SourceDir "dist\windows-install"
}

if (-not $SkipDependencyInstall) {
    if (-not (Test-Command "cmake")) {
        Install-WingetPackage "Kitware.CMake" "CMake"
    }

    if (-not (Test-Command "ninja")) {
        Install-WingetPackage "Ninja-build.Ninja" "Ninja"
    }

    if (-not (Test-VisualStudioInstall)) {
        Install-WingetPackage `
            "Microsoft.VisualStudio.2022.BuildTools" `
            "Visual Studio 2022 Build Tools" `
            @("--override", "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended")
        Write-Host ""
        Write-Host "Visual Studio Build Tools were installed. If CMake cannot find the compiler, close and reopen Windows Terminal, then rerun this script." -ForegroundColor Yellow
    }
}

if (-not (Test-Command "cmake")) {
    throw "cmake is not on PATH. Install CMake or rerun without -SkipDependencyInstall."
}

$generator = Get-DefaultGenerator
$configureArgs = @("-S", $SourceDir, "-B", $BuildDir)
if (-not [string]::IsNullOrWhiteSpace($generator)) {
    $configureArgs += @("-G", $generator)
}
if ($generator -like "Visual Studio*") {
    $configureArgs += @("-A", "x64")
}

Write-Step "Configuring ArcoBASIC"
Write-Host "Source: $SourceDir"
Write-Host "Build:  $BuildDir"
if (-not [string]::IsNullOrWhiteSpace($generator)) {
    Write-Host "Generator: $generator"
}
Invoke-Native "cmake" $configureArgs

Write-Step "Building ArcoBASIC and ArcoSH"
Invoke-Native "cmake" @("--build", $BuildDir, "--config", $Configuration)

if ($RunTests) {
    Write-Step "Running tests"
    Invoke-Native "ctest" @("--test-dir", $BuildDir, "-C", $Configuration, "--output-on-failure")
}

if ($Install) {
    Write-Step "Installing staged Windows build"
    Invoke-Native "cmake" @("--install", $BuildDir, "--config", $Configuration, "--prefix", $InstallPrefix)
    Write-Host "Installed to: $InstallPrefix" -ForegroundColor Green
}

$exeDir = if ($generator -like "Visual Studio*") {
    Join-Path $BuildDir $Configuration
} else {
    $BuildDir
}

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
Write-Host "ArcoSH:    $(Join-Path $exeDir 'arcosh.exe')"
Write-Host "ArcoBASIC: $(Join-Path $exeDir 'arco_cli.exe')"
Write-Host ""
Write-Host "Example:"
Write-Host "  $(Join-Path $exeDir 'arcosh.exe') --version"
