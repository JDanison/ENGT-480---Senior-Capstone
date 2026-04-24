Param(
    [string]$Python = "python",
    [switch]$ForceDeps
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

function Test-EditableInstall {
    $packagePath = & .\.venv\Scripts\python.exe -c "import pathlib, wabash_interface; print(pathlib.Path(wabash_interface.__file__).resolve())"
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    $expectedRoot = (Resolve-Path ".\src\wabash_interface").Path
    return $packagePath.Trim().StartsWith($expectedRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

if (-not (Test-Path ".venv")) {
    & $Python -m venv .venv
}

$depsMarker = ".venv\.deps-installed"
$needsEditableRefresh = $false

if (Test-Path ".venv") {
    $needsEditableRefresh = -not (Test-EditableInstall)
}

# Only install dependencies if marker doesn't exist, -ForceDeps is specified,
# or the package is not currently resolving from the editable src tree.
if (-not (Test-Path $depsMarker) -or $ForceDeps -or $needsEditableRefresh) {
    Write-Host "Installing dependencies..." -ForegroundColor Cyan
    & .\.venv\Scripts\python.exe -m pip install --upgrade pip
    & .\.venv\Scripts\python.exe -m pip install -e .
    New-Item -ItemType File -Path $depsMarker -Force | Out-Null
    Write-Host "Dependencies installed successfully." -ForegroundColor Green
} else {
    Write-Host "Dependencies already installed. Use -ForceDeps to reinstall." -ForegroundColor Gray
}

& .\.venv\Scripts\python.exe -m wabash_interface.main
