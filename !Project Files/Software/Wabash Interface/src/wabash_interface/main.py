import platform
import sys
import webbrowser
from pathlib import Path
from typing import Callable, Literal

import customtkinter as ctk
from serial.tools import list_ports

from wabash_interface.ui.main_window import MainWindow


WABASH_BLUE = "#1676D2"
PANEL_DARK = "#0D1726"
CARD_DARK = "#162338"
CARD_LIGHT = "#F8FAFC"


def _asset(rel: str) -> Path:
    """Return path to a bundled asset whether running frozen or from source."""
    if getattr(sys, "frozen", False):
        base = Path(sys._MEIPASS)  # type: ignore[attr-defined]
    else:
        base = Path(__file__).parent.parent.parent  # project root
    return base / rel


def _show_startup_dialog(
    title: str,
    message: str,
    kind: Literal["error", "warning"] = "warning",
    primary_button_text: str = "OK",
    primary_action: Callable[[], None] | None = None,
) -> None:
    """Show a branded startup dialog without spawning shell windows."""
    ctk.set_appearance_mode("System")
    root = ctk.CTk()
    root.title("Wabash Interface")
    root.geometry("540x260")
    root.minsize(540, 260)
    root.resizable(False, False)
    root.configure(fg_color=PANEL_DARK)
    root.attributes("-topmost", True)

    try:
        root.iconbitmap(str(_asset("assets/images/icon.ico")))
    except Exception:
        pass

    panel = ctk.CTkFrame(root, corner_radius=12, fg_color=CARD_DARK)
    panel.pack(fill="both", expand=True, padx=14, pady=14)

    accent = "#F59E0B" if kind == "warning" else "#EF4444"
    header = ctk.CTkFrame(panel, fg_color="transparent")
    header.pack(fill="x", padx=16, pady=(14, 8))

    # Branded mark (uses app icon asset) with graceful fallback if imaging is unavailable.
    try:
        from PIL import Image

        logo_img = ctk.CTkImage(light_image=Image.open(_asset("assets/images/icon.ico")), size=(28, 28))
        ctk.CTkLabel(header, text="", image=logo_img).pack(side="left", padx=(0, 10))
    except Exception:
        ctk.CTkLabel(
            header,
            text="W",
            width=28,
            height=28,
            corner_radius=6,
            fg_color=WABASH_BLUE,
            text_color="#FFFFFF",
            font=ctk.CTkFont(size=14, weight="bold"),
        ).pack(side="left", padx=(0, 10))

    ctk.CTkLabel(
        header,
        text=title,
        text_color=accent,
        font=ctk.CTkFont(size=20, weight="bold"),
    ).pack(side="left", anchor="w")

    ctk.CTkLabel(
        panel,
        text=message,
        justify="left",
        wraplength=480,
        text_color=(CARD_LIGHT, "#E2E8F0"),
        font=ctk.CTkFont(size=13),
    ).pack(fill="x", padx=16, pady=(0, 16))

    button_row = ctk.CTkFrame(panel, fg_color="transparent")
    button_row.pack(fill="x", padx=16, pady=(0, 14))

    def _on_primary() -> None:
        if primary_action is not None:
            primary_action()
        root.destroy()

    ctk.CTkButton(
        button_row,
        text=primary_button_text,
        width=140,
        fg_color=WABASH_BLUE,
        hover_color="#105DA8",
        command=_on_primary,
    ).pack(side="right")

    ctk.CTkButton(
        button_row,
        text="Exit",
        width=110,
        fg_color="#334E73",
        hover_color="#293F5C",
        command=root.destroy,
    ).pack(side="right", padx=(0, 10))

    root.mainloop()


def _has_cp210x_driver_or_device() -> bool:
    """Best-effort CP210x detection with no shell calls or pop-up consoles."""
    # If a CP210x device is currently connected and enumerated, driver is present.
    for port in list_ports.comports():
        desc = (port.description or "").lower()
        manu = (port.manufacturer or "").lower()
        hwid = (port.hwid or "").lower()
        if (port.vid == 0x10C4) or ("cp210" in desc) or ("silicon labs" in manu) or ("10c4" in hwid):
            return True

    # Check known Windows service keys for Silicon Labs USB-UART drivers.
    try:
        import winreg

        candidate_keys = [
            r"SYSTEM\CurrentControlSet\Services\silabser",
            r"SYSTEM\CurrentControlSet\Services\SiUSBXp",
        ]
        for key_path in candidate_keys:
            try:
                winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path).Close()
                return True
            except OSError:
                continue
    except Exception:
        # If registry probing fails, fall through to warning path.
        pass

    return False


def _check_windows_cp2102_drivers() -> bool:
    """Check OS + CP210x driver availability using silent Python-only checks."""
    if platform.system() != "Windows":
        _show_startup_dialog(
            title="Unsupported Platform",
            message=(
                "Wabash Interface is designed for Windows machines.\n\n"
                f"Detected platform: {platform.system()}\n\n"
                "Please run this application on Windows."
            ),
            kind="error",
            primary_button_text="Exit",
        )
        return False

    if _has_cp210x_driver_or_device():
        return True

    def _open_driver_page() -> None:
        webbrowser.open("https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers")

    _show_startup_dialog(
        title="CP2102 Driver Required",
        message=(
            "The Silicon Labs CP210x USB-to-UART driver was not detected.\n\n"
            "Select Install Driver to open the official installer page, then restart Wabash Interface."
        ),
        kind="warning",
        primary_button_text="Install Driver",
        primary_action=_open_driver_page,
    )
    return False


def main() -> None:
    # Check Windows and CP2102 drivers before starting app
    if not _check_windows_cp2102_drivers():
        sys.exit(1)
    
    app = MainWindow()
    app.run()


if __name__ == "__main__":
    main()
