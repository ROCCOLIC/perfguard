# PerfGuard check wrapper.
#
# Builds the tool and hands the comparison to `perfguard --check`, which
# measures the stored baseline commit and HEAD in this same session and
# decides the verdict from the medians. The script holds no baseline
# number and no threshold: both live in .perfguard/baseline.json and the
# CLI, so they cannot drift from what the tool actually uses.
#
# Exit code:
#   0 = no regression
#   1 = regression detected
#   2 = error, or the stored baseline cannot be compared

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$Exe = Join-Path $BuildDir "Debug\perfguard.exe"

Write-Host "================================"
Write-Host "PerfGuard Check"
Write-Host "================================"

Write-Host "[1/2] Building..."

cmake --build $BuildDir --config Debug

if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED"
    exit 2
}

if (!(Test-Path $Exe)) {
    Write-Host "Executable not found:"
    Write-Host $Exe
    exit 2
}

Write-Host "[2/2] Checking..."

Push-Location $ProjectRoot

try {
    # Any extra arguments go to the CLI, e.g. -Threshold / --json.
    & $Exe --check @args
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
