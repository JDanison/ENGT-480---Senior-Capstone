# Wabash Interface (Python Desktop App)

This folder now contains a starter desktop application for controlling and monitoring your capstone firmware over serial.

## What You Get

- Desktop UI built with CustomTkinter for a modern native look
- Serial COM connect/disconnect controls
- Quick command buttons for `d`, `z`, `m`, `s`
- Custom command send field
- Live session log viewer
- Log export to timestamped text file
- Modern dashboard layout with status badge, TX/RX counters, and theme switcher
- Wabash-inspired red and charcoal visual styling
- Theme defaults to `system` so app follows Windows dark/light preference
- PyInstaller packaging config for Windows `.exe` builds

## Project Structure

- `src/wabash_interface/main.py` - App entrypoint
- `src/wabash_interface/ui/main_window.py` - Main UI window
- `src/wabash_interface/services/serial_service.py` - Serial read/write service
- `src/wabash_interface/storage/log_export.py` - Session log export helper
- `WabashInterface.spec` - PyInstaller spec
- `scripts/run_dev.ps1` - Run app in local virtual env
- `scripts/run_dev.bat` - Run app without PowerShell script policy issues
- `scripts/build_exe.ps1` - Build executable with PyInstaller
- `scripts/build_exe.bat` - Build executable without PowerShell script policy issues

## Run In Development (Windows PowerShell)

From this folder:

```powershell
.\scripts\run_dev.ps1
```

If your machine blocks PowerShell scripts, use one of these alternatives:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_dev.ps1
```

```bat
.\scripts\run_dev.bat
```

## Build a Windows EXE

From this folder:

```powershell
.\scripts\build_exe.ps1
```

Output file is `dist\WabashInterface.exe`.

## Distribution Notes

- Build on Windows for Windows targets.
- Test on a clean machine before release.
- USB serial drivers may still be required on target computers.
- Unsigned EXE files may trigger antivirus warnings; code signing is recommended for deployment.

## Should You Use Something Else?

You can stay with Python for this project.

If you need a faster startup and harder-to-reverse binary, you can evaluate Nuitka later.
For your current stage, PyInstaller is the most practical path.
