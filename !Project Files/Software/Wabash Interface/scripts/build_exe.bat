@echo off
setlocal
cd /d %~dp0\..

if not exist .venv (
    python -m venv .venv
)

.venv\Scripts\python.exe -m pip install --upgrade pip
.venv\Scripts\python.exe -m pip install -r requirements.txt
.venv\Scripts\python.exe -m PyInstaller --noconfirm --clean WabashInterface.spec

echo Build complete. Output file: dist\WabashInterface.exe
endlocal
