import sys
import tkinter.messagebox as messagebox
import platform

from wabash_interface.ui.main_window import MainWindow


def _check_windows_cp2102_drivers() -> bool:
    """
    Check if running on Windows and CP2102 drivers are installed.
    Returns True if all checks pass, False if on non-Windows or drivers missing.
    Shows appropriate popup messages.
    """
    # Check if on Windows
    if platform.system() != "Windows":
        messagebox.showerror(
            "Unsupported Platform",
            "Wabash Interface is designed for Windows only.\n\n"
            "Your current platform is: " + platform.system() + "\n\n"
            "Please run this application on a Windows machine."
        )
        return False
    
    # Try to detect CP2102 driver on Windows
    try:
        import subprocess
        # Check device manager for CP2102 via WMI
        result = subprocess.run(
            [
                "wmic", "logicaldisk", "get", "name"
            ],
            capture_output=True,
            text=True,
            timeout=5
        )
        # If wmic works, Windows/COM support is likely available
        # Do a more specific check for CP210x driver
        result = subprocess.run(
            [
                "powershell", "-Command",
                "Get-WmiObject Win32_SerialPort | Where-Object {$_.Name -like '*CP210x*' -or $_.Description -like '*CP210x*'} | Select-Object -First 1"
            ],
            capture_output=True,
            text=True,
            timeout=5
        )
        
        if result.stdout.strip():
            # Driver found
            return True
        
        # Check registry for Silicon Labs driver
        result = subprocess.run(
            [
                "reg", "query", 
                "HKLM\\SYSTEM\\CurrentControlSet\\Services\\usbser",
                "/v", "DisplayName"
            ],
            capture_output=True,
            text=True,
            timeout=5
        )
        
        if "Silicon" in result.stdout or "CP210" in result.stdout:
            return True
        
        # Also check if any COM ports are available as indication driver may be present
        result = subprocess.run(
            [
                "powershell", "-Command",
                "Get-WmiObject Win32_SerialPort | Measure-Object | Select-Object -ExpandProperty Count"
            ],
            capture_output=True,
            text=True,
            timeout=5
        )
        
        try:
            count = int(result.stdout.strip())
            if count > 0:
                # COM ports detected, likely have drivers
                return True
        except (ValueError, AttributeError):
            pass
        
        # Driver not detected - show popup
        messagebox.showwarning(
            "CP2102 Drivers Not Found",
            "The CP2102 USB-to-Serial drivers do not appear to be installed.\n\n"
            "Please download and install the Silicon Labs CP210x drivers:\n"
            "https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers\n\n"
            "After installation, please restart this application."
        )
        return False
        
    except Exception as e:
        # If we can't verify, warn but allow continuation
        print(f"Driver check warning: {e}", file=sys.stderr)
        return True


def main() -> None:
    # Check Windows and CP2102 drivers before starting app
    if not _check_windows_cp2102_drivers():
        sys.exit(1)
    
    app = MainWindow()
    app.run()


if __name__ == "__main__":
    main()
