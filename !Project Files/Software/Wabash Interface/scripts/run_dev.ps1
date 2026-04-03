Param(
    [string]$Python = "python",
    [switch]$ForceDeps
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (-not (Test-Path ".venv")) {
    & $Python -m venv .venv
}

$depsMarker = ".venv\.deps-installed"

# Only install dependencies if marker doesn't exist or -ForceDeps is specified
if (-not (Test-Path $depsMarker) -or $ForceDeps) {
    Write-Host "Installing dependencies..." -ForegroundColor Cyan
    & .\.venv\Scripts\python.exe -m pip install --upgrade pip
    & .\.venv\Scripts\python.exe -m pip install -e .
    New-Item -ItemType File -Path $depsMarker -Force | Out-Null
    Write-Host "Dependencies installed successfully." -ForegroundColor Green
} else {
    Write-Host "Dependencies already installed. Use -ForceDeps to reinstall." -ForegroundColor Gray
}

& .\.venv\Scripts\python.exe -m wabash_interface.main
