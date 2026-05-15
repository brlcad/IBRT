# Copyright (c) 2026 BRL-CAD Visualizer contributors
# SPDX-License-Identifier: MIT

param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\..\build\package-release"),
    [string]$OutputDir = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Get-CacheValue {
    param(
        [string[]]$Lines,
        [string]$Name
    )

    $prefix = "${Name}:"
    foreach ($line in $Lines) {
        if ($line.StartsWith($prefix)) {
            $parts = $line.Split("=", 2)
            if ($parts.Length -eq 2) {
                return $parts[1].Trim()
            }
        }
    }

    throw "Missing CMake cache entry: $Name"
}

function Find-TargetFile {
    param(
        [string]$SearchRoot,
        [string]$Name
    )

    $match = Get-ChildItem -Path $SearchRoot -Recurse -File -Filter $Name -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $match) {
        throw "Could not find $Name under $SearchRoot"
    }
    return $match
}

$cachePath = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $cachePath)) {
    throw "Build directory is not configured: $BuildDir"
}

$cacheLines = Get-Content $cachePath
$brlcadPrefix = Get-CacheValue $cacheLines "BRLCAD_PREFIX"

if (-not $SkipBuild) {
    & cmake --build $BuildDir --config Release --target ospray_module_brl_cad IBRT IBRTRenderWorker
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed."
    }
}

$viewerPath = Find-TargetFile -SearchRoot $BuildDir -Name "IBRT.exe"
$viewerDir = Split-Path -Parent $viewerPath
$outputRoot = if ($OutputDir) { $OutputDir } else { Join-Path $BuildDir "package\IBRT" }
$outputRoot = [System.IO.Path]::GetFullPath($outputRoot)
$modelsDir = Join-Path $outputRoot "models"

if (Test-Path $outputRoot) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null
Copy-Item -LiteralPath (Join-Path $viewerDir "*") -Destination $outputRoot -Recurse -Force

$dbDir = Join-Path $brlcadPrefix "share\db"
foreach ($demoName in @("moss.g", "havoc.g", "axis.g")) {
    $demoSource = Join-Path $dbDir $demoName
    if (Test-Path $demoSource) {
        Copy-Item -LiteralPath $demoSource -Destination (Join-Path $modelsDir $demoName) -Force
    }
}

Write-Host "Packaged IBRT runtime at $outputRoot"
