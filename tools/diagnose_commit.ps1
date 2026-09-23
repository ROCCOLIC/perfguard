# PerfGuard single-commit diagnosis wrapper.
#
# Builds the tool and hands the work to `perfguard --diagnose <commit>`,
# which prepares its own worktree and build directory, measures the
# commit, and compares it against the stored baseline. The script holds
# no baseline number and no threshold.
#
# Exit code:
#   0 = no regression
#   1 = regression detected
#   2 = error, or the stored baseline cannot be compared

param(
    [Parameter(Mandatory = $true)]
    [string]$Commit
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$Exe = Join-Path $BuildDir "Debug\perfguard.exe"

Write-Host "========================================"
Write-Host "PerfGuard Commit Diagnosis"
Write-Host "========================================"

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

Write-Host "[2/2] Diagnosing $Commit..."

Push-Location $ProjectRoot

try {
    & $Exe --diagnose $Commit
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
