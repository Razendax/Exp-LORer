# Ensures ui_tests\.venv exists (creating it if needed), then runs the pytest suite.
# Any extra arguments are forwarded to pytest.
#
# Example usage:
#   .\ui_tests\run_ui_tests.ps1
#  
# Run a specific test case:
#   .\ui_tests\run_ui_tests.ps1 tests\test_configuration.py::TestConfiguration::test_some_case
#
# Filter by name:
#  .\ui_tests\run_ui_tests.ps1 -k test_some_case

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
