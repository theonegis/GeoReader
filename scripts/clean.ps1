[CmdletBinding(PositionalBinding = $false)]
param(
    [string] $BuildDir = "",
    [switch] $StageOnly,
    [switch] $All,
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

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
        throw "Cleanup targets must be inside $ProjectRoot."
    }
    return $Candidate
}

if ($BuildDir -and $StageOnly) {
    throw "-BuildDir and -StageOnly cannot be used together."
}
if ($All -and ($BuildDir -or $StageOnly)) {
    throw "-All cannot be combined with -BuildDir or -StageOnly."
}

$Targets = [System.Collections.Generic.List[string]]::new()
$BuildTargets = [System.Collections.Generic.List[string]]::new()

function Add-TargetIfPresent {
    param(
        [Parameter(Mandatory)][string] $Path,
        [switch] $IsBuild
    )
    if (Test-Path -LiteralPath $Path) {
        $Resolved = Resolve-ProjectPath $Path
        if (-not $Targets.Contains($Resolved)) {
            $Targets.Add($Resolved)
        }
        if ($IsBuild -and -not $BuildTargets.Contains($Resolved)) {
            $BuildTargets.Add($Resolved)
        }
    }
}

if ($BuildDir) {
    Add-TargetIfPresent (Resolve-ProjectPath $BuildDir) -IsBuild
} elseif ($StageOnly) {
    Add-TargetIfPresent (Join-Path $ProjectRoot "stage")
} else {
    Add-TargetIfPresent (Join-Path $ProjectRoot "build") -IsBuild
    Add-TargetIfPresent (Join-Path $ProjectRoot "stage")

    foreach ($Pattern in @("build-*", "stage-*", "dist-check*", "dist-*check*")) {
        foreach ($Item in Get-ChildItem -Path $ProjectRoot -Filter $Pattern -Force) {
            $IsBuildTarget = $Item.Name.StartsWith("build-")
            Add-TargetIfPresent $Item.FullName -IsBuild:$IsBuildTarget
        }
    }

    if ($All) {
        Add-TargetIfPresent (Join-Path $ProjectRoot "dist")
        foreach ($Item in Get-ChildItem -Path $ProjectRoot -Filter "dist-*" -Force) {
            Add-TargetIfPresent $Item.FullName
        }
    }
}

if ($Targets.Count -eq 0) {
    Write-Host "Nothing to clean."
    return
}

Write-Host "Cleanup targets:"
foreach ($Target in $Targets) {
    Write-Host "  $Target"
}

if ($DryRun) {
    Write-Host "Dry run only; nothing was removed."
    return
}

if (-not $All) {
    $Architecture = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") {
        "arm64"
    } else {
        "x64"
    }
    $RuntimeDirectory =
        Join-Path $ProjectRoot "dist\runtime\Windows-$Architecture"
    New-Item -ItemType Directory -Path $RuntimeDirectory -Force | Out-Null

    $DeployedExecutable =
        Join-Path $RuntimeDirectory "bin\GeoReader.exe"
    if (-not (Test-Path -LiteralPath $DeployedExecutable)) {
        foreach ($BuildTarget in $BuildTargets) {
            $Executable = Join-Path $BuildTarget "GeoReader.exe"
            if (Test-Path -LiteralPath $Executable) {
                Copy-Item -LiteralPath $Executable -Destination (
                    Join-Path $RuntimeDirectory "GeoReader.exe") -Force
                Write-Host "Preserved runnable executable: $RuntimeDirectory\GeoReader.exe"
                break
            }
        }
    }
}

foreach ($Target in $Targets) {
    Remove-Item -LiteralPath $Target -Recurse -Force
}

if ($All) {
    Write-Host "Build files and generated installers were removed."
} else {
    foreach ($Pattern in @("dylibbundler-*.log", ".DS_Store")) {
        Get-ChildItem -Path $ProjectRoot -Filter $Pattern -File -Recurse -Force |
            Remove-Item -Force
    }
    Write-Host "Temporary files were removed; runnable outputs and installers were preserved."
}
