# Creates ui_tests\.venv and installs requirements.txt into it.
# Run once per machine, or whenever requirements.txt changes.

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
$venvPath = Join-Path $root ".venv"

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    throw "Python 3 was not found on PATH. Install Python 3.9+ and re-run this script."
}

if (-not (Test-Path $venvPath)) {
    Write-Host "Creating virtual environment at $venvPath"
    & python -m venv $venvPath
} else {
    Write-Host "Virtual environment already exists at $venvPath"
}

$venvPython = Join-Path $venvPath "Scripts\python.exe"

& $venvPython -m pip install --upgrade pip
& $venvPython -m pip install -r (Join-Path $root "requirements.txt")

Write-Host "Setup complete."
