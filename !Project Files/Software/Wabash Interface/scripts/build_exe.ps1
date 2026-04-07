Param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (-not (Test-Path ".venv")) {
    & $Python -m venv .venv
}

& .\.venv\Scripts\python.exe -m pip install --upgrade pip
& .\.venv\Scripts\python.exe -m pip install .
& .\.venv\Scripts\python.exe -m pip install pyinstaller

$pyiArgs = @(
    "-m", "PyInstaller",
    "--noconfirm",
    "--clean",
    "WabashInterface.spec"
)

& .\.venv\Scripts\python.exe $pyiArgs

Write-Host "Build complete. Output file: dist\WabashInterface.exe" -ForegroundColor Green
