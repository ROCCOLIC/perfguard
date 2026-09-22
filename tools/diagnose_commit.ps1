param(
    [Parameter(Mandatory = $true)]
    [string]$Commit
)

$ErrorActionPreference = "Stop"

$ProjectRoot = "C:\perfguard"
$DiagnoseRoot = "C:\perfguard-diagnose"
$BuildDir = "C:\perfguard-diagnose-build"
$Exe = "$BuildDir\Debug\perfguard.exe"

$BaselineUs = 62738.5
$Threshold = 10.0

Write-Host "========================================"
Write-Host "PerfGuard Commit Diagnosis"
Write-Host "========================================"

# ------------------------------------------------------------
# 1. Clean old diagnostic worktree
# ------------------------------------------------------------

Write-Host "[1/5] Preparing diagnostic worktree..."

if (Test-Path $DiagnoseRoot) {
    git -C $ProjectRoot worktree remove `
        --force `
        $DiagnoseRoot 2>$null

    if (Test-Path $DiagnoseRoot) {
        Remove-Item `
            -Recurse `
            -Force `
            $DiagnoseRoot
    }
}

# ------------------------------------------------------------
# 2. Create worktree at target commit
# ------------------------------------------------------------

Write-Host "[2/5] Checking out commit $Commit..."

git -C $ProjectRoot worktree add `
    --detach `
    $DiagnoseRoot `
    $Commit

if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to create diagnostic worktree."
    exit 125
}

# ------------------------------------------------------------
# 3. Build target commit
# ------------------------------------------------------------

Write-Host "[3/5] Preparing clean build directory..."

if (Test-Path $BuildDir) {
    Remove-Item `
        -Recurse `
        -Force `
        $BuildDir
}

Write-Host "[3/5] Building target commit..."

cmake `
    -S $DiagnoseRoot `
    -B $BuildDir `
    -G "Visual Studio 17 2022"

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed."
    git -C $ProjectRoot worktree remove --force $DiagnoseRoot
    exit 125
}

cmake `
    --build $BuildDir `
    --config Debug

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed."
    git -C $ProjectRoot worktree remove --force $DiagnoseRoot
    exit 125
}

if (!(Test-Path $Exe)) {
    Write-Host "Executable not found:"
    Write-Host $Exe

    git -C $ProjectRoot worktree remove --force $DiagnoseRoot

    exit 125
}

# ------------------------------------------------------------
# 4. Run benchmark
# ------------------------------------------------------------

Write-Host "[4/5] Running target commit benchmark..."

$output = & $Exe 2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "Benchmark failed."

    git -C $ProjectRoot worktree remove --force $DiagnoseRoot

    exit 125
}

$averageLine = $output |
    Select-String "Average:"

if ($null -eq $averageLine) {
    Write-Host "Could not find benchmark average."

    git -C $ProjectRoot worktree remove --force $DiagnoseRoot

    exit 125
}

$averageText = $averageLine.ToString()

if ($averageText -match "Average:\s+([0-9.]+)\s+us") {
    $CurrentUs = [double]$matches[1]
}
else {
    Write-Host "Could not parse benchmark result."

    git -C $ProjectRoot worktree remove --force $DiagnoseRoot

    exit 125
}

$Change =
    (($CurrentUs - $BaselineUs) / $BaselineUs) * 100.0

# ------------------------------------------------------------
# 5. Cleanup
# ------------------------------------------------------------

Write-Host "[5/5] Cleaning diagnostic worktree..."

git -C $ProjectRoot worktree remove `
    --force `
    $DiagnoseRoot

Write-Host ""
Write-Host "========================================"
Write-Host "Diagnosis Result"
Write-Host "========================================"

Write-Host "Commit:   $Commit"
Write-Host "Baseline: $BaselineUs us"
Write-Host "Current:  $CurrentUs us"
Write-Host ("Change:   {0:N2}%" -f $Change)
Write-Host "Threshold: $Threshold%"

if ($Change -gt $Threshold) {
    Write-Host "Result: REGRESSION"
}
else {
    Write-Host "Result: OK"
}

exit 0