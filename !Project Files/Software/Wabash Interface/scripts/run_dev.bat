@echo off
setlocal
cd /d %~dp0\..

if not exist .venv (
    python -m venv .venv
)

if not exist .venv\.deps-installed (
    echo Installing dependencies...
    .venv\Scripts\python.exe -m pip install --upgrade pip
    .venv\Scripts\python.exe -m pip install -e .
    type nul > .venv\.deps-installed
    echo Dependencies installed successfully.
) else (
    echo Dependencies already installed. Delete .venv\.deps-installed to force reinstall.
)

.venv\Scripts\python.exe -m wabash_interface.main

endlocal
