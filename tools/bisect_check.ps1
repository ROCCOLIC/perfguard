# PerfGuard automatic bisect test
# Exit code:
#   0 = performance is OK
#   1 = performance regression detected
#   2 = test/build error

$ErrorActionPreference = "Stop"

$ProjectRoot = "C:\perfguard"
$BuildDir = "$ProjectRoot\build"
$Exe = "$BuildDir\Debug\perfguard.exe"

# Baseline measured from the original commit.
$BaselineUs = 62738.5

# Regression threshold.
$Threshold = 10.0

Write-Host "================================"
Write-Host "PerfGuard Bisect Check"
Write-Host "================================"

# --------------------------------
# 1. Build current commit
# --------------------------------

Write-Host "[1/3] Building..."

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

# --------------------------------
# 2. Run benchmark
# --------------------------------

Write-Host "[2/3] Running benchmark..."

$output = & $Exe 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "BENCHMARK FAILED"
    exit 2
}

# --------------------------------
# 3. Extract average
# --------------------------------

$averageLine = $output |
    Select-String "Average:"

if ($null -eq $averageLine) {
    Write-Host "Could not find Average in benchmark output."
    exit 2
}

$averageText = $averageLine.ToString()

if ($averageText -match "Average:\s+([0-9.]+)\s+us") {
    $CurrentUs = [double]$matches[1]
}
else {
    Write-Host "Could not parse benchmark result."
    exit 2
}

$Change = (($CurrentUs - $BaselineUs) / $BaselineUs) * 100.0

Write-Host ""
Write-Host "Baseline: $BaselineUs us"
Write-Host "Current:  $CurrentUs us"
Write-Host ("Change:   {0:N2}%" -f $Change)
Write-Host "Threshold: $Threshold%"

# --------------------------------
# Result
# --------------------------------

if ($Change -gt $Threshold) {
    Write-Host "Result: REGRESSION"
    exit 1
}

Write-Host "Result: OK"
exit 0