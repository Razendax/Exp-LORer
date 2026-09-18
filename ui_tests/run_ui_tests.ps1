# Ensures ui_tests\.venv exists (creating it if needed), then runs the pytest suite.
# Any extra arguments are forwarded to pytest.

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
$venvPath = Join-Path $root ".venv"
$venvPython = Join-Path $venvPath "Scripts\python.exe"

if (-not (Test-Path $venvPython)) {
    Write-Host "Virtual environment not found, running setup_env.ps1 first."
    & (Join-Path $root "setup_env.ps1")
}

Push-Location $root
try {
    & $venvPython -m pytest @args
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
