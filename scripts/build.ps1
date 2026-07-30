[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string] $Type = "Release",

    [string] $BuildDir = "build",

    [ValidateRange(0, 1024)]
    [int] $Jobs = 0,

    [switch] $CleanFirst,

    [switch] $Package,

    [ValidateSet("auto", "all", "nsis", "zip")]
    [string] $PackageFormat = "auto",

    [string[]] $CMakeArgs = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($PSBoundParameters.ContainsKey("PackageFormat")) {
    $Package = $true
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string] $RequestedPath)

    $Candidate = if ([IO.Path]::IsPathRooted($RequestedPath)) {
        [IO.Path]::GetFullPath($RequestedPath)
    } else {
        [IO.Path]::GetFullPath((Join-Path $ProjectRoot $RequestedPath))
    }

    $ProjectPrefix = $ProjectRoot.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if ($Candidate -eq $ProjectRoot -or
        -not $Candidate.StartsWith(
            $ProjectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "The build directory must be inside $ProjectRoot."
    }
    return $Candidate
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter(Mandatory)][string[]] $ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

foreach ($Command in @("cmake", "ninja")) {
    if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
        throw "Required command was not found: $Command"
    }
}

$BuildPath = Resolve-ProjectPath $BuildDir
if ($CleanFirst) {
    & (Join-Path $PSScriptRoot "clean.ps1") -BuildDir $BuildPath
}

$ConfigureArguments = @(
    "-S", $ProjectRoot,
    "-B", $BuildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Type"
)

$HasQtPrefix = $CMakeArgs.Where({
    $_.StartsWith("-DCMAKE_PREFIX_PATH=", [StringComparison]::OrdinalIgnoreCase)
}).Count -gt 0
if (-not $HasQtPrefix -and $env:QTDIR) {
    $ConfigureArguments += "-DCMAKE_PREFIX_PATH=$env:QTDIR"
}

$HasToolchain = $CMakeArgs.Where({
    $_.StartsWith(
        "-DCMAKE_TOOLCHAIN_FILE=", [StringComparison]::OrdinalIgnoreCase)
}).Count -gt 0
if (-not $HasToolchain -and $env:VCPKG_ROOT) {
    $VcpkgRoot = [IO.Path]::GetFullPath($env:VCPKG_ROOT)
    $VcpkgInstalled = Join-Path $ProjectRoot "vcpkg_installed"
    $ConfigureArguments += @(
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')",
        "-DVCPKG_INSTALLED_DIR=$VcpkgInstalled",
        "-DVCPKG_TARGET_TRIPLET=x64-windows",
        "-DGEOREADER_BUNDLE_VCPKG_RUNTIME=ON",
        "-DGEOREADER_VCPKG_RUNTIME_ROOT=$(Join-Path $VcpkgInstalled 'x64-windows')"
    )
}
$ConfigureArguments += $CMakeArgs

Invoke-Checked cmake $ConfigureArguments

$BuildArguments = @("--build", $BuildPath, "--parallel")
if ($Jobs -gt 0) {
    $BuildArguments += $Jobs.ToString()
}
Invoke-Checked cmake $BuildArguments

$Architecture = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") {
    "arm64"
} else {
    "x64"
}
$RuntimeDirectory =
    Join-Path $ProjectRoot "dist\runtime\Windows-$Architecture"
if (Test-Path -LiteralPath $RuntimeDirectory) {
    Remove-Item -LiteralPath $RuntimeDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $RuntimeDirectory -Force | Out-Null
$InstallArguments = @(
    "--install", $BuildPath,
    "--config", $Type,
    "--prefix", $RuntimeDirectory
)
Invoke-Checked cmake $InstallArguments

$ExecutablePath = Join-Path $RuntimeDirectory "bin\GeoReader.exe"
Write-Host "Build completed: $(Join-Path $BuildPath 'GeoReader.exe')"
Write-Host "Runnable output preserved at: $ExecutablePath"

if (-not $Package) {
    return
}

$PackageDirectory = Join-Path $ProjectRoot "dist"
New-Item -ItemType Directory -Path $PackageDirectory -Force | Out-Null

$Generators = switch ($PackageFormat) {
    "auto" {
        if (Get-Command "makensis" -ErrorAction SilentlyContinue) {
            @("NSIS")
        } else {
            Write-Warning "NSIS was not found; creating a ZIP package instead."
            @("ZIP")
        }
    }
    "all" { @("NSIS", "ZIP") }
    "nsis" { @("NSIS") }
    "zip" { @("ZIP") }
}

foreach ($Generator in $Generators) {
    if ($Generator -eq "NSIS" -and
        -not (Get-Command "makensis" -ErrorAction SilentlyContinue)) {
        throw "NSIS packaging requires makensis (for example: choco install nsis)."
    }
    $PackageArguments = @(
        "--config", (Join-Path $BuildPath "CPackConfig.cmake"),
        "-G", $Generator,
        "-B", $PackageDirectory
    )
    Invoke-Checked cpack $PackageArguments
}

Write-Host "Windows packages completed ($($Generators -join ', ')): $PackageDirectory"
