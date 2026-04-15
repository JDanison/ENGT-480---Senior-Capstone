from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox

import customtkinter as ctk

from serial.tools import list_ports

from wabash_interface.services.serial_service import SerialConfig, SerialService
from wabash_interface.storage.log_export import export_text_log

WABASH_BLUE        = "#1676D2"
WABASH_BLUE_HOVER  = "#105DA8"
BTN_GREY          = "#334E73"
BTN_GREY_HOVER    = "#293F5C"
PANEL_LIGHT       = "#F2F6FB"
PANEL_DARK        = "#0D1726"
CARD_LIGHT        = "#FFFFFF"
CARD_DARK         = "#162338"

# Log line colour tags (foreground only; works on both light/dark)
LOG_COLORS = {
    "tx":      "#60A5FA",   # bright blue â€” sent commands
    "rx":      "#93C5FD",   # soft blue   â€” received data
    "data":    "#34D399",   # green     â€” DATA/DATC payload rows
    "status":  "#FBBF24",   # amber     â€” status/RSP lines
    "default": "#D1D5DB",   # grey      â€” everything else
}

# Density presets: (pad_x, pad_y_btn, font_size_log, row_padding)
DENSITY = {
    "Compact":     (8,  3, 11, 4),
    "Comfortable": (16, 6, 12, 8),
}

SETUP_MASK_SENSOR_INTERVAL = 1 << 0
SETUP_MASK_THRESHOLD = 1 << 1
SETUP_MASK_SAMPLE_RATE = 1 << 2
SETUP_MASK_DURATION = 1 << 3
SETUP_MASK_TRUCK_ID = 1 << 4
SETUP_MASK_DESCRIPTION = 1 << 5
SETUP_MASK_WIFI = 1 << 6


def _asset(rel: str) -> Path:
    """Return path to a bundled asset whether running frozen or from source."""
    if getattr(sys, 'frozen', False):
        base = Path(sys._MEIPASS)  # type: ignore[attr-defined]
    else:
        base = Path(__file__).parent.parent.parent.parent  # project root
    return base / rel


class MainWindow:
    def __init__(self) -> None:
        ctk.set_appearance_mode("system")
        ctk.set_default_color_theme("blue")

        self.root = ctk.CTk()
        self.root.title("Wabash Interface")
        self.root.geometry("1240x780")
        self.root.minsize(1024, 680)

        self.serial_service = SerialService()
        self.log_lines: list[str] = []
        self.tx_count = 0
        self.rx_count = 0
        self.connected = False
        self.last_message = "No messages yet"
        self.lora_active = False
        self.session_events = 0
        self.units: dict[str, dict] = {}

        self.port_var     = tk.StringVar(value="")
        self.baud_var     = tk.StringVar(value="115200")
        self.command_var  = tk.StringVar(value="")
        self.theme_var    = tk.StringVar(value="system")
        self.density_var  = tk.StringVar(value="Comfortable")
        self.search_var      = tk.StringVar(value="")
        self.unit_filter_var = tk.StringVar(value="")
        self.scan_var        = tk.StringVar(value="No units found")
        self.text_scale_var  = tk.DoubleVar(value=1.0)
        self.search_placeholder_active = False
        self._scan_results: list[str] = []

        # Unit Setup configuration variables
        self.sensor_interval_var = tk.StringVar(value="100")
        self.event_trigger_threshold_var = tk.StringVar(value="2.0")
        self.lab_sample_rate_var = tk.StringVar(value="20")
        self.event_duration_var = tk.StringVar(value="2000")
        self.truck_id_var = tk.StringVar(value="")
        self.description_var = tk.StringVar(value="")

        self.apply_sensor_interval_var = tk.BooleanVar(value=False)
        self.apply_threshold_var = tk.BooleanVar(value=False)
        self.apply_sample_rate_var = tk.BooleanVar(value=False)
        self.apply_duration_var = tk.BooleanVar(value=False)
        self.apply_truck_id_var = tk.BooleanVar(value=False)
        self.apply_description_var = tk.BooleanVar(value=False)
        self.apply_wifi_var = tk.BooleanVar(value=False)

        # Wi-Fi credential slot (sent to receiver for Wi-Fi-first offload)
        self.wifi1_ssid_var = tk.StringVar(value="")
        self.wifi1_password_var = tk.StringVar(value="")

        self.send_config_button: ctk.CTkButton | None = None
        self._setup_apply_vars = [
            self.apply_sensor_interval_var,
            self.apply_threshold_var,
            self.apply_sample_rate_var,
            self.apply_duration_var,
            self.apply_truck_id_var,
            self.apply_description_var,
            self.apply_wifi_var,
        ]
        for var in self._setup_apply_vars:
            var.trace_add("write", self._on_setup_selection_changed)

        self.pages: dict[str, ctk.CTkFrame | ctk.CTkScrollableFrame] = {}
        self.nav_buttons: dict[str, ctk.CTkButton] = {}
        self._active_page_name: str | None = None

        # set window icon
        icon_path = _asset("assets/images/icon.ico")
        if icon_path.exists():
            self.root.iconbitmap(str(icon_path))

        self._build_ui()
        self._show_page("Dashboard")
        self._refresh_ports()
        self._pump_messages()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def run(self) -> None:
        self.root.mainloop()

    def _build_ui(self) -> None:
        self.root.configure(fg_color=(PANEL_LIGHT, PANEL_DARK))
        self.root.grid_columnconfigure(1, weight=1)
        self.root.grid_rowconfigure(0, weight=1)

        sidebar = ctk.CTkFrame(self.root, width=280, corner_radius=0, fg_color=("#E5E7EB", "#0B1220"))
        sidebar.grid(row=0, column=0, sticky="nsew")
        sidebar.grid_propagate(False)
        sidebar.grid_columnconfigure(0, weight=1)

        self.page_container = ctk.CTkFrame(self.root, corner_radius=0, fg_color="transparent")
        self.page_container.grid(row=0, column=1, sticky="nsew", padx=14, pady=14)
        self.page_container.grid_propagate(False)
        self.page_container.grid_columnconfigure(0, weight=1)
        self.page_container.grid_rowconfigure(0, weight=1)

        ctk.CTkLabel(
            sidebar,
            text="WABASH",
            font=ctk.CTkFont(size=28, weight="bold"),
            text_color=WABASH_BLUE,
        ).grid(row=0, column=0, sticky="w", padx=24, pady=(28, 4))
        ctk.CTkLabel(
            sidebar,
            text="Receiver Command Center",
            text_color=("#475569", "#9CA3AF"),
            font=ctk.CTkFont(size=14),
        ).grid(row=1, column=0, sticky="w", padx=24, pady=(0, 20))

        ctk.CTkFrame(sidebar, height=3, corner_radius=8, fg_color=WABASH_BLUE).grid(
            row=1, column=0, sticky="ew", padx=24, pady=(18, 0)
        )

        nav_card = ctk.CTkFrame(sidebar, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        nav_card.grid(row=2, column=0, sticky="ew", padx=18, pady=(0, 12))
        nav_card.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(nav_card, text="Navigation", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 8)
        )

        self.nav_buttons["Dashboard"] = ctk.CTkButton(
            nav_card,
            text="Dashboard",
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
            command=lambda: self._show_page("Dashboard"),
        )
        self.nav_buttons["Dashboard"].grid(row=1, column=0, sticky="ew", padx=16, pady=4)

        self.nav_buttons["Unit Setup"] = ctk.CTkButton(
            nav_card,
            text="Unit Setup",
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
            command=lambda: self._show_page("Unit Setup"),
        )
        self.nav_buttons["Unit Setup"].grid(row=2, column=0, sticky="ew", padx=16, pady=4)

        self.nav_buttons["Data Offload"] = ctk.CTkButton(
            nav_card,
            text="Data Offload",
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
            command=lambda: self._show_page("Data Offload"),
        )
        self.nav_buttons["Data Offload"].grid(row=3, column=0, sticky="ew", padx=16, pady=4)

        self.nav_buttons["Settings"] = ctk.CTkButton(
            nav_card,
            text="Settings",
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
            command=lambda: self._show_page("Settings"),
        )
        self.nav_buttons["Settings"].grid(row=4, column=0, sticky="ew", padx=16, pady=(4, 14))

        quick_status = ctk.CTkFrame(sidebar, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        quick_status.grid(row=3, column=0, sticky="ew", padx=18, pady=(0, 18))
        quick_status.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(quick_status, text="System Status", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 8)
        )
        self.sidebar_status = ctk.CTkLabel(
            quick_status,
            text="Disconnected",
            corner_radius=10,
            fg_color="#7F1D1D",
            text_color="#FEE2E2",
        )
        self.sidebar_status.grid(row=1, column=0, sticky="ew", padx=16, pady=(0, 14))

        self._build_dashboard_page()
        self._build_settings_page()
        self._build_live_page()
        self._build_unit_setup_page()

    def _create_stat_card(self, parent: ctk.CTkFrame, column: int, title: str, initial: str) -> ctk.CTkLabel:
        card = ctk.CTkFrame(parent, corner_radius=12, fg_color=(CARD_LIGHT, CARD_DARK))
        card.grid(row=0, column=column, sticky="ew", padx=(0 if column == 0 else 6, 0 if column == 2 else 6))
        ctk.CTkLabel(card, text=title, text_color=("#475569", "#94A3B8"), font=ctk.CTkFont(size=12)).grid(
            row=0, column=0, sticky="w", padx=14, pady=(10, 0)
        )
        value = ctk.CTkLabel(card, text=initial, font=ctk.CTkFont(size=22, weight="bold"))
        value.grid(row=1, column=0, sticky="w", padx=14, pady=(2, 10))
        return value

    def _show_page(self, page_name: str) -> None:
        if self._active_page_name == page_name:
            return

        if self._active_page_name is not None and self._active_page_name in self.pages:
            self.pages[self._active_page_name].grid_remove()

        self.pages[page_name].grid(row=0, column=0, sticky="nsew")
        self._active_page_name = page_name

        for name, button in self.nav_buttons.items():
            if name == page_name:
                button.configure(fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER)
            else:
                button.configure(fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER)

    def _build_dashboard_page(self) -> None:
        page = ctk.CTkFrame(self.page_container, corner_radius=0, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)
        page.grid_rowconfigure(2, weight=1)
        self.pages["Dashboard"] = page

        # ── Header ──────────────────────────────────────────────────────────
        header = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        header.grid(row=0, column=0, sticky="ew", pady=(0, 12))
        header.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(header, text="Fleet Dashboard", font=ctk.CTkFont(size=22, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=18, pady=14
        )
        ctk.CTkLabel(
            header, text="WABASH", text_color=WABASH_BLUE, font=ctk.CTkFont(size=14, weight="bold")
        ).grid(row=0, column=1, sticky="e", padx=18, pady=14)

        # ── System Status Cards ──────────────────────────────────────────────
        status_row = ctk.CTkFrame(page, fg_color="transparent")
        status_row.grid(row=1, column=0, sticky="ew", pady=(0, 12))
        status_row.grid_columnconfigure((0, 1, 2), weight=1)

        tx_card = ctk.CTkFrame(status_row, corner_radius=12, fg_color=(CARD_LIGHT, CARD_DARK))
        tx_card.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        tx_card.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(
            tx_card, text="Transmitter", text_color=("#64748B", "#94A3B8"), font=ctk.CTkFont(size=12)
        ).grid(row=0, column=0, sticky="w", padx=14, pady=(10, 0))
        self.db_tx_status = ctk.CTkLabel(
            tx_card, text="Not Connected",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=("#EF4444", "#EF4444"),
        )
        self.db_tx_status.grid(row=1, column=0, sticky="w", padx=14, pady=(2, 10))

        lora_card = ctk.CTkFrame(status_row, corner_radius=12, fg_color=(CARD_LIGHT, CARD_DARK))
        lora_card.grid(row=0, column=1, sticky="ew", padx=6)
        lora_card.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(
            lora_card, text="Truck / LoRa Link", text_color=("#64748B", "#94A3B8"), font=ctk.CTkFont(size=12)
        ).grid(row=0, column=0, sticky="w", padx=14, pady=(10, 0))
        self.db_lora_status = ctk.CTkLabel(
            lora_card, text="No Link",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=("#F59E0B", "#F59E0B"),
        )
        self.db_lora_status.grid(row=1, column=0, sticky="w", padx=14, pady=(2, 10))

        sess_card = ctk.CTkFrame(status_row, corner_radius=12, fg_color=(CARD_LIGHT, CARD_DARK))
        sess_card.grid(row=0, column=2, sticky="ew", padx=(6, 0))
        sess_card.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(
            sess_card, text="Events This Session", text_color=("#64748B", "#94A3B8"), font=ctk.CTkFont(size=12)
        ).grid(row=0, column=0, sticky="w", padx=14, pady=(10, 0))
        self.db_event_count = ctk.CTkLabel(sess_card, text="0", font=ctk.CTkFont(size=22, weight="bold"))
        self.db_event_count.grid(row=1, column=0, sticky="w", padx=14, pady=(2, 10))

        # ── Main row: fleet table + active unit panel ────────────────────────
        main_row = ctk.CTkFrame(page, fg_color="transparent")
        main_row.grid(row=2, column=0, sticky="nsew", pady=(0, 12))
        main_row.grid_columnconfigure(0, weight=1)
        main_row.grid_rowconfigure(0, weight=1)

        # Fleet table (left)
        fleet_card = ctk.CTkFrame(main_row, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        fleet_card.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        fleet_card.grid_columnconfigure(0, weight=1)
        fleet_card.grid_rowconfigure(2, weight=1)

        fleet_hdr = ctk.CTkFrame(fleet_card, fg_color="transparent")
        fleet_hdr.grid(row=0, column=0, sticky="ew", padx=16, pady=(14, 6))
        fleet_hdr.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(fleet_hdr, text="Known Units", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w"
        )
        self.unit_filter_entry = ctk.CTkEntry(
            fleet_hdr, textvariable=self.unit_filter_var, placeholder_text="Filter by ID...", width=180
        )
        self.unit_filter_entry.grid(row=0, column=1, sticky="e")
        self.unit_filter_var.trace_add("write", lambda *_: self._refresh_unit_list())

        thead = ctk.CTkFrame(fleet_card, fg_color=("#E5E7EB", "#1E3050"), corner_radius=8)
        thead.grid(row=1, column=0, sticky="ew", padx=16, pady=(0, 4))
        thead.grid_columnconfigure((0, 1, 2, 3), weight=1)
        for col, lbl_text in enumerate(["Truck ID", "Last Seen", "Events", "Status"]):
            ctk.CTkLabel(
                thead, text=lbl_text,
                font=ctk.CTkFont(size=11, weight="bold"),
                text_color=("#475569", "#94A3B8"),
            ).grid(row=0, column=col, sticky="w", padx=10, pady=6)

        self.fleet_scroll = ctk.CTkScrollableFrame(fleet_card, corner_radius=8, fg_color="transparent")
        self.fleet_scroll.grid(row=2, column=0, sticky="nsew", padx=16, pady=(0, 14))
        self.fleet_scroll.grid_columnconfigure((0, 1, 2, 3), weight=1)
        self.fleet_empty_label = ctk.CTkLabel(
            self.fleet_scroll,
            text="No units detected this session.\nConnect to a transmitter and offload data to populate this list.",
            text_color=("#64748B", "#6B7280"),
            font=ctk.CTkFont(size=13),
            justify="center",
        )
        self.fleet_empty_label.grid(row=0, column=0, columnspan=4, pady=30)
        self._fleet_rows: list = []

        # Active unit panel (right)
        unit_card = ctk.CTkFrame(main_row, width=260, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        unit_card.grid(row=0, column=1, sticky="nsew")
        unit_card.grid_propagate(False)
        unit_card.grid_columnconfigure(0, weight=1)
        unit_card.grid_rowconfigure(12, weight=1)

        ctk.CTkLabel(unit_card, text="Active Unit", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 8)
        )
        ctk.CTkFrame(unit_card, height=1, fg_color=("#E5E7EB", "#1E3050")).grid(
            row=1, column=0, sticky="ew", padx=16
        )
        self.db_unit_id = ctk.CTkLabel(
            unit_card, text="No unit configured",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=("#64748B", "#9CA3AF"),
            wraplength=220, justify="left",
        )
        self.db_unit_id.grid(row=2, column=0, sticky="w", padx=16, pady=(10, 2))
        self.db_unit_desc = ctk.CTkLabel(
            unit_card, text="",
            font=ctk.CTkFont(size=12),
            text_color=("#64748B", "#9CA3AF"),
            wraplength=220, justify="left",
        )
        self.db_unit_desc.grid(row=3, column=0, sticky="w", padx=16, pady=(0, 4))

        ctk.CTkFrame(unit_card, height=1, fg_color=("#E5E7EB", "#1E3050")).grid(
            row=4, column=0, sticky="ew", padx=16, pady=(8, 0)
        )
        ctk.CTkLabel(
            unit_card, text="Session Stats",
            font=ctk.CTkFont(size=13, weight="bold"),
            text_color=("#475569", "#94A3B8"),
        ).grid(row=5, column=0, sticky="w", padx=16, pady=(10, 4))

        for stat_i, (stat_label, attr_name) in enumerate([
            ("Lines Received", "db_rx_count"),
            ("Commands Sent",  "db_tx_count"),
            ("Log Lines",      "db_log_count"),
        ]):
            ctk.CTkLabel(
                unit_card, text=stat_label,
                font=ctk.CTkFont(size=11),
                text_color=("#64748B", "#9CA3AF"),
            ).grid(row=6 + stat_i * 2, column=0, sticky="w", padx=16, pady=(4, 0))
            val_lbl = ctk.CTkLabel(unit_card, text="0", font=ctk.CTkFont(size=16, weight="bold"))
            val_lbl.grid(row=7 + stat_i * 2, column=0, sticky="w", padx=16, pady=(0, 2))
            setattr(self, attr_name, val_lbl)

        ctk.CTkButton(
            unit_card, text="Open Data Offload",
            command=lambda: self._show_page("Data Offload"),
            fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER,
            font=ctk.CTkFont(size=13, weight="bold"),
        ).grid(row=13, column=0, sticky="ew", padx=16, pady=(0, 6))
        ctk.CTkButton(
            unit_card, text="Configure Unit",
            command=lambda: self._show_page("Unit Setup"),
            fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER,
            font=ctk.CTkFont(size=13),
        ).grid(row=14, column=0, sticky="ew", padx=16, pady=(0, 14))

        # ── Latest message strip ─────────────────────────────────────────────
        activity_card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        activity_card.grid(row=3, column=0, sticky="ew")
        activity_card.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(activity_card, text="Latest Message", font=ctk.CTkFont(size=14, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(12, 4)
        )
        self.db_last_message = ctk.CTkLabel(
            activity_card,
            text="No messages yet",
            wraplength=760,
            justify="left",
            anchor="w",
            text_color=("#334155", "#CBD5E1"),
            font=ctk.CTkFont(size=12),
        )
        self.db_last_message.grid(row=1, column=0, sticky="ew", padx=16, pady=(0, 12))

    def _build_settings_page(self) -> None:
        page = ctk.CTkFrame(self.page_container, corner_radius=0, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)
        self.pages["Settings"] = page

        header = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        header.grid(row=0, column=0, sticky="ew")
        ctk.CTkLabel(header, text="Settings", font=ctk.CTkFont(size=22, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=18, pady=14
        )

        card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        card.grid(row=1, column=0, sticky="ew", pady=(12, 0))
        card.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(card, text="Appearance", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 6)
        )
        ctk.CTkLabel(card, text="Theme", text_color=("#475569", "#94A3B8")).grid(
            row=1, column=0, sticky="w", padx=16
        )
        ctk.CTkSegmentedButton(
            card,
            values=["dark", "light", "system"],
            variable=self.theme_var,
            command=self._set_theme,
            selected_color=WABASH_BLUE,
            selected_hover_color=WABASH_BLUE_HOVER,
        ).grid(row=2, column=0, sticky="ew", padx=16, pady=(4, 10))

        ctk.CTkLabel(card, text="Layout Density", text_color=("#475569", "#94A3B8")).grid(
            row=3, column=0, sticky="w", padx=16
        )
        ctk.CTkSegmentedButton(
            card,
            values=["Compact", "Comfortable"],
            variable=self.density_var,
            command=self._set_density,
            selected_color=WABASH_BLUE,
            selected_hover_color=WABASH_BLUE_HOVER,
        ).grid(row=4, column=0, sticky="ew", padx=16, pady=(4, 10))

        ctk.CTkLabel(card, text="Text Size", text_color=("#475569", "#94A3B8")).grid(
            row=5, column=0, sticky="w", padx=16
        )
        self.text_scale_label = ctk.CTkLabel(card, text="100%", text_color=("#334155", "#CBD5E1"))
        self.text_scale_label.grid(row=6, column=0, sticky="e", padx=16)
        ctk.CTkSlider(
            card,
            from_=0.9,
            to=1.4,
            number_of_steps=10,
            variable=self.text_scale_var,
            button_color=WABASH_BLUE,
            button_hover_color=WABASH_BLUE_HOVER,
            progress_color=WABASH_BLUE,
            command=self._set_text_scale,
        ).grid(row=7, column=0, sticky="ew", padx=16, pady=(4, 14))

    def _build_live_page(self) -> None:
        page = ctk.CTkFrame(self.page_container, corner_radius=0, fg_color='transparent')
        page.grid_columnconfigure(0, minsize=340)
        page.grid_columnconfigure(1, weight=1)
        page.grid_rowconfigure(0, weight=1)
        self.pages['Data Offload'] = page

        # ================================================================
        # LEFT PANEL
        # ================================================================
        left = ctk.CTkFrame(page, corner_radius=0,
                            fg_color='transparent')
        left.grid(row=0, column=0, sticky='nsew', padx=(0, 12))
        left.grid_columnconfigure(0, weight=1)
        left.grid_rowconfigure(1, weight=1)

        # Section 1: Transmitter connection
        conn_card = ctk.CTkFrame(left, corner_radius=14,
                                 fg_color=(CARD_LIGHT, CARD_DARK))
        conn_card.grid(row=0, column=0, sticky='ew', padx=12, pady=(0, 8))
        conn_card.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(conn_card, text='Transmitter',
                     font=ctk.CTkFont(size=14, weight='bold')).grid(
            row=0, column=0, columnspan=2, sticky='w', padx=14, pady=(12, 6))

        ctk.CTkLabel(conn_card, text='COM Port',
                     text_color=('#475569', '#94A3B8')).grid(
            row=1, column=0, columnspan=2, sticky='w', padx=14)
        self.port_combo = ctk.CTkComboBox(conn_card, variable=self.port_var,
                                          values=['No ports found'])
        self.port_combo.grid(row=2, column=0, columnspan=2, sticky='ew',
                             padx=14, pady=(4, 6))

        ctk.CTkLabel(conn_card, text='Baud',
                     text_color=('#475569', '#94A3B8')).grid(
            row=3, column=0, columnspan=2, sticky='w', padx=14)
        ctk.CTkEntry(conn_card, textvariable=self.baud_var).grid(
            row=4, column=0, columnspan=2, sticky='ew', padx=14, pady=(4, 8))

        btn_row = ctk.CTkFrame(conn_card, fg_color='transparent')
        btn_row.grid(row=5, column=0, columnspan=2, sticky='ew',
                     padx=14, pady=(0, 12))
        btn_row.grid_columnconfigure((0, 1, 2), weight=1)
        ctk.CTkButton(btn_row, text='Refresh',
                      fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER,
                      command=self._refresh_ports).grid(
            row=0, column=0, padx=(0, 3), sticky='ew')
        self.connect_button = ctk.CTkButton(
            btn_row, text='Connect',
            fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER,
            command=self._connect)
        self.connect_button.grid(row=0, column=1, padx=3, sticky='ew')
        self.disconnect_button = ctk.CTkButton(
            btn_row, text='Disconnect',
            fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER,
            command=self._disconnect)
        self.disconnect_button.grid(row=0, column=2, padx=(3, 0), sticky='ew')

        # Section 2: Unit Discovery + table (fills remaining height)
        disc_card = ctk.CTkFrame(left, corner_radius=14,
                                 fg_color=(CARD_LIGHT, CARD_DARK))
        disc_card.grid(row=1, column=0, sticky='nsew', padx=12, pady=(0, 8))
        disc_card.grid_columnconfigure(0, weight=1)
        disc_card.grid_rowconfigure(2, weight=1)

        disc_hdr = ctk.CTkFrame(disc_card, fg_color='transparent')
        disc_hdr.grid(row=0, column=0, sticky='ew', padx=14, pady=(12, 8))
        disc_hdr.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(disc_hdr, text='Unit Discovery',
                     font=ctk.CTkFont(size=14, weight='bold')).grid(
            row=0, column=0, sticky='w')
        self.scan_button = ctk.CTkButton(
            disc_hdr, text='Unit Discover', width=110,
            fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER,
            command=self._run_connection_scan)
        self.scan_button.grid(row=0, column=1, sticky='e')

        # Table column header
        thead = ctk.CTkFrame(disc_card, fg_color=('#D1D5DB', '#1E3050'),
                             corner_radius=6)
        thead.grid(row=1, column=0, sticky='ew', padx=14, pady=(0, 2))
        thead.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(thead, text='Truck ID',
                     font=ctk.CTkFont(size=11, weight='bold'),
                     text_color=('#475569', '#94A3B8')).grid(
            row=0, column=0, sticky='w', padx=10, pady=5)
        ctk.CTkLabel(thead, text='Select',
                     font=ctk.CTkFont(size=11, weight='bold'),
                     text_color=('#475569', '#94A3B8')).grid(
            row=0, column=1, sticky='e', padx=10, pady=5)

        # Scrollable table body
        self.disc_scroll = ctk.CTkScrollableFrame(
            disc_card, corner_radius=6, fg_color='transparent')
        self.disc_scroll.grid(row=2, column=0, sticky='nsew', padx=14,
                              pady=(0, 12))
        self.disc_scroll.grid_columnconfigure(0, weight=1)
        self.disc_empty_label = ctk.CTkLabel(
            self.disc_scroll,
            text='No units found.\nPress Unit Discover to scan.',
            text_color=('#64748B', '#6B7280'),
            font=ctk.CTkFont(size=12), justify='center')
        self.disc_empty_label.grid(row=0, column=0, columnspan=2, pady=20)
        self._disc_rows: list = []

        # Section 3: Unit Actions (locked until a unit is selected)
        act_card = ctk.CTkFrame(left, corner_radius=14,
                                fg_color=(CARD_LIGHT, CARD_DARK))
        act_card.grid(row=2, column=0, sticky='ew', padx=12, pady=(0, 0))
        act_card.grid_columnconfigure((0, 1, 2), weight=1)

        ctk.CTkLabel(act_card, text='Unit Actions',
                     font=ctk.CTkFont(size=14, weight='bold')).grid(
            row=0, column=0, columnspan=3, sticky='w', padx=14, pady=(12, 8))

        self.btn_request = ctk.CTkButton(
            act_card, text='Request Data',
            fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER,
            state='disabled',
            command=lambda: self._send_quick('d'))
        self.btn_request.grid(row=1, column=0, padx=(14, 4), pady=(0, 12),
                              sticky='ew')
        self.btn_tare = ctk.CTkButton(
            act_card, text='Unit Tare',
            fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER,
            state='disabled',
            command=lambda: self._send_quick('z'))
        self.btn_tare.grid(row=1, column=1, padx=4, pady=(0, 12), sticky='ew')
        self.btn_timesync = ctk.CTkButton(
            act_card, text='Time Sync',
            fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER,
            state='disabled',
            command=lambda: self._send_quick('s'))
        self.btn_timesync.grid(row=1, column=2, padx=(4, 14), pady=(0, 12),
                               sticky='ew')

        # ================================================================
        # RIGHT PANEL
        # ================================================================
        right = ctk.CTkFrame(page, corner_radius=0, fg_color='transparent')
        right.grid(row=0, column=1, sticky='nsew')
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(2, weight=1)

        # Header bar (title + connection badge)
        hdr = ctk.CTkFrame(right, corner_radius=14,
                           fg_color=(CARD_LIGHT, CARD_DARK))
        hdr.grid(row=0, column=0, sticky='ew', pady=(0, 10))
        hdr.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(hdr, text='Data Offload',
                     font=ctk.CTkFont(size=22, weight='bold')).grid(
            row=0, column=0, sticky='w', padx=18, pady=14)
        ctk.CTkLabel(hdr, text='WABASH',
                     text_color=WABASH_BLUE,
                     font=ctk.CTkFont(size=16, weight='bold')).grid(
            row=0, column=1, sticky='e', padx=(10, 180), pady=14)
        self.connection_badge = ctk.CTkLabel(
            hdr, text='Disconnected',
            corner_radius=10,
            fg_color='#7F1D1D', text_color='#FEE2E2', width=154)
        self.connection_badge.grid(row=0, column=1, sticky='e',
                                   padx=(10, 18), pady=14)

        # Stats row
        stats = ctk.CTkFrame(right, fg_color='transparent')
        stats.grid(row=1, column=0, sticky='ew', pady=(0, 10))
        stats.grid_columnconfigure((0, 1, 2), weight=1)
        self.port_stat = self._create_stat_card(stats, 0, 'Port', '-')
        self.tx_stat   = self._create_stat_card(stats, 1, 'TX', '0')
        self.rx_stat   = self._create_stat_card(stats, 2, 'RX', '0')

        # Log card
        log_card = ctk.CTkFrame(right, corner_radius=14,
                                fg_color=(CARD_LIGHT, CARD_DARK))
        log_card.grid(row=2, column=0, sticky='nsew')
        log_card.grid_columnconfigure(0, weight=1)
        log_card.grid_rowconfigure(2, weight=1)

        log_hdr = ctk.CTkFrame(log_card, fg_color='transparent')
        log_hdr.grid(row=0, column=0, sticky='ew', padx=16, pady=(14, 0))
        log_hdr.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(log_hdr, text='Session Log',
                     font=ctk.CTkFont(size=16, weight='bold')).grid(
            row=0, column=0, sticky='w')

        search_frame = ctk.CTkFrame(log_hdr, fg_color=('#E5E7EB', '#374151'),
                                    corner_radius=8)
        search_frame.grid(row=0, column=1, sticky='e')
        search_frame.grid_columnconfigure(0, weight=1)
        self.search_entry = ctk.CTkEntry(
            search_frame,
            textvariable=self.search_var,
            border_width=0,
            fg_color=('#F8FAFC', '#374151'),
            text_color=('#0F172A', '#F3F4F6'),
            font=ctk.CTkFont(size=14),
            width=200)
        self.search_entry.grid(row=0, column=0, padx=(8, 4), pady=4,
                               sticky='ew')
        self.search_var.trace_add('write', lambda *_: self._on_search_change())
        self.search_entry.bind('<FocusIn>',
                               lambda _e: self._on_search_focus_in())
        self.search_entry.bind('<FocusOut>',
                               lambda _e: self._on_search_focus_out())
        ctk.CTkButton(
            search_frame, text='X', width=28, height=28,
            fg_color='transparent', hover_color=('#D1D5DB', '#4B5563'),
            command=self._clear_search).grid(row=0, column=1, padx=(0, 4))
        self._activate_search_placeholder()

        legend = ctk.CTkFrame(log_card, fg_color='transparent')
        legend.grid(row=1, column=0, sticky='w', padx=16, pady=(6, 4))
        for lbl, col in [('TX', LOG_COLORS['tx']), ('RX', LOG_COLORS['rx']),
                          ('Data', LOG_COLORS['data']),
                          ('Status', LOG_COLORS['status'])]:
            ctk.CTkLabel(legend, text='*', text_color=col, width=14,
                         font=ctk.CTkFont(size=9)).pack(side='left')
            ctk.CTkLabel(legend, text=lbl, text_color=('#475569', '#94A3B8'),
                         font=ctk.CTkFont(size=11)).pack(side='left',
                                                         padx=(0, 12))

        self.log_text = ctk.CTkTextbox(log_card, wrap='none', corner_radius=10,
                                       font=('Consolas', 12))
        self.log_text.grid(row=2, column=0, sticky='nsew', padx=16,
                           pady=(0, 14))
        inner: tk.Text = self.log_text._textbox  # type: ignore[attr-defined]
        for tag, colour in LOG_COLORS.items():
            inner.tag_configure(tag, foreground=colour)
        inner.tag_configure('search_hl', background=WABASH_BLUE,
                            foreground='white')

        # Bottom bar: custom command + log tools
        bottom = ctk.CTkFrame(right, corner_radius=14,
                              fg_color=(CARD_LIGHT, CARD_DARK))
        bottom.grid(row=3, column=0, sticky='ew', pady=(10, 0))
        bottom.grid_columnconfigure(0, weight=1)

        cmd_r = ctk.CTkFrame(bottom, fg_color='transparent')
        cmd_r.grid(row=0, column=0, sticky='ew', padx=14, pady=(14, 8))
        cmd_r.grid_columnconfigure(0, weight=1)
        self.command_entry = ctk.CTkEntry(
            cmd_r, textvariable=self.command_var,
            placeholder_text='Type custom command and press Send')
        self.command_entry.grid(row=0, column=0, sticky='ew')
        ctk.CTkButton(cmd_r, text='Send', width=110,
                      fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER,
                      command=self._send_custom).grid(row=0, column=1,
                                                      padx=(10, 0))

        log_btns = ctk.CTkFrame(bottom, fg_color='transparent')
        log_btns.grid(row=1, column=0, sticky='ew', padx=14, pady=(0, 14))
        log_btns.grid_columnconfigure((0, 1), weight=1)
        ctk.CTkButton(log_btns, text='Clear Log',
                      fg_color=BTN_GREY, hover_color='#64748B',
                      command=self._clear_log).grid(
            row=0, column=0, padx=(0, 6), sticky='ew')
        ctk.CTkButton(log_btns, text='Export Log',
                      fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER,
                      command=self._export_log).grid(
            row=0, column=1, padx=(6, 0), sticky='ew')

    def _set_theme(self, mode: str) -> None:
        ctk.set_appearance_mode(mode)

    def _set_text_scale(self, value: float) -> None:
        ctk.set_widget_scaling(value)
        self.text_scale_label.configure(text=f"{int(value * 100)}%")

    def _activate_search_placeholder(self) -> None:
        self.search_placeholder_active = True
        self.search_var.set("Find...")
        self.search_entry.configure(text_color=("#64748B", "#9CA3AF"))

    def _deactivate_search_placeholder(self) -> None:
        if self.search_placeholder_active:
            self.search_placeholder_active = False
            self.search_var.set("")
            self.search_entry.configure(text_color=("#0F172A", "#F3F4F6"))

    def _effective_search_term(self) -> str:
        if self.search_placeholder_active:
            return ""
        return self.search_var.get().strip()

    def _on_search_focus_in(self) -> None:
        self._deactivate_search_placeholder()

    def _on_search_focus_out(self) -> None:
        if not self.search_var.get().strip():
            self._activate_search_placeholder()

    def _set_density(self, mode: str) -> None:
        px, py, fs, rp = DENSITY[mode]
        self.log_text.configure(font=("Consolas", fs))

    def _highlight_search(self) -> None:
        # Guard against callbacks during initialization before log_text is created
        if not hasattr(self, 'log_text'):
            return
        
        inner: tk.Text = self.log_text._textbox  # type: ignore[attr-defined]
        inner.tag_remove("search_hl", "1.0", "end")
        term = self._effective_search_term()
        if not term:
            return
        start = "1.0"
        while True:
            pos = inner.search(term, start, stopindex="end", nocase=True)
            if not pos:
                break
            end = f"{pos}+{len(term)}c"
            inner.tag_add("search_hl", pos, end)
            start = end

    def _on_search_change(self) -> None:
        self._highlight_search()

    def _clear_search(self) -> None:
        self.search_var.set("")
        self.search_entry.focus_set()
        self._deactivate_search_placeholder()

    def _refresh_ports(self) -> None:
        ports = [port.device for port in list_ports.comports()]
        if not ports:
            ports = ["No ports found"]

        self.port_combo.configure(values=ports)

        current_port = self.port_var.get()
        if current_port not in ports:
            self.port_var.set(ports[0])

        if self.serial_service.is_connected:
            self.port_stat.configure(text=current_port if current_port else "-")

    def _connect(self) -> None:
        port = self.port_var.get().strip()
        if not port or port == "No ports found":
            messagebox.showwarning("Port Required", "Select a COM port before connecting.")
            return

        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            messagebox.showerror("Invalid Baud", "Baud rate must be an integer.")
            return

        try:
            self.serial_service.connect(SerialConfig(port=port, baudrate=baud))
            self._set_connected_state(True, port)
        except Exception as exc:
            self._set_connected_state(False, "-")
            messagebox.showerror("Connection Error", str(exc))

    def _disconnect(self) -> None:
        self.serial_service.disconnect()
        self._set_connected_state(False, "-")

    def _set_connected_state(self, is_connected: bool, port_text: str) -> None:
        self.connected = is_connected
        if is_connected:
            self.connection_badge.configure(text="Connected", fg_color="#14532D", text_color="#DCFCE7")
            self.connect_button.configure(fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER)
            self.disconnect_button.configure(fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER)
            self.sidebar_status.configure(text="Connected", fg_color="#14532D", text_color="#DCFCE7")
            self.db_tx_status.configure(text="Connected", text_color=("#22C55E", "#22C55E"))
        else:
            self.connection_badge.configure(text="Disconnected", fg_color="#7F1D1D", text_color="#FEE2E2")
            self.connect_button.configure(fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER)
            self.disconnect_button.configure(fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER)
            self.sidebar_status.configure(text="Disconnected", fg_color="#7F1D1D", text_color="#FEE2E2")
            self.db_tx_status.configure(text="Not Connected", text_color=("#EF4444", "#EF4444"))
            self.lora_active = False
            self.db_lora_status.configure(text="No Link", text_color=("#F59E0B", "#F59E0B"))

        self.port_stat.configure(text=port_text)
        self._update_active_unit_display()

    def _send_quick(self, command: str) -> None:
        self._send_payload(command)

    def _send_custom(self) -> None:
        payload = self.command_var.get()
        if not payload:
            return
        self._send_payload(payload)

    def _send_payload(self, payload: str) -> None:
        if not self.serial_service.is_connected:
            messagebox.showwarning("Not Connected", "Connect to a serial port first.")
            return

        try:
            wire_payload = payload if payload.endswith("\n") else f"{payload}\n"
            self.serial_service.send_text(wire_payload)
        except Exception as exc:
            messagebox.showerror("Send Error", str(exc))

    def _classify_line(self, line: str) -> str:
        if "TX>" in line:
            return "tx"
        if line.startswith("DATA:") or line.startswith("DATC:"):
            return "data"
        if line.startswith("RSP:") or line.startswith("END:") or line.startswith("["):
            return "status"
        return "rx"

    def _append_log(self, line: str) -> None:
        self.log_lines.append(line)
        self.last_message = line
        tag = self._classify_line(line)

        inner: tk.Text = self.log_text._textbox  # type: ignore[attr-defined]
        inner.insert("end", line + "\n", tag)
        self.log_text.see("end")

        # re-apply search highlight if active
        if self.search_var.get().strip():
            self._highlight_search()

        if tag == "tx":
            self.tx_count += 1
            self.tx_stat.configure(text=str(self.tx_count))
        else:
            self.rx_count += 1
            self.rx_stat.configure(text=str(self.rx_count))
            # Detect LoRa link from any receiver response
            if not self.lora_active and (
                line.startswith("RSP:") or line.startswith("DATA:")
                or line.startswith("DATC:") or line.startswith("END:")
            ):
                self.lora_active = True
                self.db_lora_status.configure(text="Link Active", text_color=("#22C55E", "#22C55E"))
            # Collect Connection Scan results
            if line.startswith("[SCAN_RESULT]:"):
                truck_id = line[14:].strip()
                if truck_id and truck_id not in self._scan_results:
                    self._scan_results.append(truck_id)

        # Each END:D marks a completed data offload from the receiver
        if line.startswith("END:D"):
            self.session_events += 1
            self.db_event_count.configure(text=str(self.session_events))
            if self.truck_id_var.get().strip():
                self._register_unit(self.truck_id_var.get().strip())

        self.db_tx_count.configure(text=str(self.tx_count))
        self.db_rx_count.configure(text=str(self.rx_count))
        self.db_log_count.configure(text=str(len(self.log_lines)))
        self.db_last_message.configure(text=self.last_message)

    def _pump_messages(self) -> None:
        while not self.serial_service.messages.empty():
            line = self.serial_service.messages.get_nowait()
            self._append_log(line)
        self._update_active_unit_display()
        self.root.after(100, self._pump_messages)

    def _clear_log(self) -> None:
        self.log_lines.clear()
        self.log_text.delete("1.0", "end")
        self.tx_count = 0
        self.rx_count = 0
        self.session_events = 0
        self.tx_stat.configure(text="0")
        self.rx_stat.configure(text="0")
        self.db_tx_count.configure(text="0")
        self.db_rx_count.configure(text="0")
        self.db_log_count.configure(text="0")
        self.db_event_count.configure(text="0")
        self.db_last_message.configure(text="No messages yet")
        self.search_var.set("")
        self._activate_search_placeholder()

    def _export_log(self) -> None:
        if not self.log_lines:
            messagebox.showinfo("No Data", "There is no log data to export.")
            return

        selected = filedialog.askdirectory(title="Choose Export Folder")
        if not selected:
            return

        output = export_text_log(self.log_lines, Path(selected))
        messagebox.showinfo("Export Complete", f"Saved log to:\n{output}")

    def _get_selected_setup_mask(self) -> int:
        mask = 0
        if self.apply_sensor_interval_var.get():
            mask |= SETUP_MASK_SENSOR_INTERVAL
        if self.apply_threshold_var.get():
            mask |= SETUP_MASK_THRESHOLD
        if self.apply_sample_rate_var.get():
            mask |= SETUP_MASK_SAMPLE_RATE
        if self.apply_duration_var.get():
            mask |= SETUP_MASK_DURATION
        if self.apply_truck_id_var.get():
            mask |= SETUP_MASK_TRUCK_ID
        if self.apply_description_var.get():
            mask |= SETUP_MASK_DESCRIPTION
        if self.apply_wifi_var.get():
            mask |= SETUP_MASK_WIFI
        return mask

    def _get_selected_setup_labels(self) -> list[str]:
        labels: list[str] = []
        if self.apply_sensor_interval_var.get():
            labels.append("Sensor Read Interval")
        if self.apply_threshold_var.get():
            labels.append("Event Trigger Threshold")
        if self.apply_sample_rate_var.get():
            labels.append("Strain Gauge Poll Rate")
        if self.apply_duration_var.get():
            labels.append("Event Capture Duration")
        if self.apply_truck_id_var.get():
            labels.append("Truck ID")
        if self.apply_description_var.get():
            labels.append("Description")
        if self.apply_wifi_var.get():
            labels.append("Wi-Fi Network")
        return labels

    def _update_send_config_button(self) -> None:
        if self.send_config_button is None:
            return

        has_selection = self._get_selected_setup_mask() != 0
        if has_selection:
            self.send_config_button.configure(
                state="normal",
                fg_color=WABASH_BLUE,
                hover_color=WABASH_BLUE_HOVER,
            )
        else:
            self.send_config_button.configure(
                state="disabled",
                fg_color=BTN_GREY,
                hover_color=BTN_GREY,
            )

    def _on_setup_selection_changed(self, *_args: object) -> None:
        self._update_send_config_button()

    def _set_all_setup_selection(self, selected: bool) -> None:
        for var in self._setup_apply_vars:
            var.set(selected)

    def _build_unit_setup_page(self) -> None:
        page = ctk.CTkScrollableFrame(
            self.page_container,
            corner_radius=0,
            fg_color="transparent",
            scrollbar_button_color=BTN_GREY,
            scrollbar_button_hover_color=BTN_GREY_HOVER,
        )
        page.grid_columnconfigure(0, weight=1)
        self.pages["Unit Setup"] = page

        header = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        header.grid(row=0, column=0, sticky="ew", pady=(0, 12))
        ctk.CTkLabel(header, text="Unit Configuration", font=ctk.CTkFont(size=22, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=18, pady=14
        )

        info_card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        info_card.grid(row=1, column=0, sticky="ew", pady=(0, 12))
        info_card.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(
            info_card,
            text="Truck Identification",
            font=ctk.CTkFont(size=16, weight="bold"),
        ).grid(row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(14, 6))

        ctk.CTkLabel(info_card, text="Truck ID", text_color=("#475569", "#94A3B8")).grid(
            row=1, column=0, sticky="w", padx=16, pady=6
        )
        ctk.CTkEntry(
            info_card,
            textvariable=self.truck_id_var,
            placeholder_text="Truck ID",
        ).grid(row=1, column=1, sticky="ew", padx=(6, 16), pady=6)

        ctk.CTkLabel(info_card, text="Description", text_color=("#475569", "#94A3B8")).grid(
            row=2, column=0, sticky="w", padx=16, pady=(6, 14)
        )
        ctk.CTkEntry(
            info_card,
            textvariable=self.description_var,
            placeholder_text="Description",
        ).grid(row=2, column=1, sticky="ew", padx=(6, 16), pady=(6, 14))

        config_card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        config_card.grid(row=2, column=0, sticky="ew", pady=(0, 12))
        config_card.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(
            config_card,
            text="Sensor Configuration",
            font=ctk.CTkFont(size=16, weight="bold"),
        ).grid(row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(14, 6))

        ctk.CTkLabel(config_card, text="Sensor Read Interval (ms)", text_color=("#475569", "#94A3B8")).grid(
            row=1, column=0, sticky="w", padx=16, pady=6
        )
        ctk.CTkOptionMenu(
            config_card,
            values=["50", "100", "200", "500"],
            variable=self.sensor_interval_var,
            fg_color=WABASH_BLUE,
            button_color=WABASH_BLUE_HOVER,
            button_hover_color=WABASH_BLUE_HOVER,
        ).grid(row=1, column=1, sticky="ew", padx=(6, 16), pady=6)

        ctk.CTkLabel(config_card, text="Strain Gauge Poll Rate (Hz)", text_color=("#475569", "#94A3B8")).grid(
            row=2, column=0, sticky="w", padx=16, pady=6
        )
        ctk.CTkOptionMenu(
            config_card,
            values=["10", "20"],
            variable=self.lab_sample_rate_var,
            fg_color=WABASH_BLUE,
            button_color=WABASH_BLUE_HOVER,
            button_hover_color=WABASH_BLUE_HOVER,
        ).grid(row=2, column=1, sticky="ew", padx=(6, 16), pady=6)

        ctk.CTkLabel(config_card, text="Event Trigger Threshold (g's)", text_color=("#475569", "#94A3B8")).grid(
            row=3, column=0, sticky="w", padx=16, pady=6
        )
        ctk.CTkEntry(
            config_card,
            textvariable=self.event_trigger_threshold_var,
            placeholder_text="Example: 2.0",
        ).grid(row=3, column=1, sticky="ew", padx=(6, 16), pady=6)

        ctk.CTkLabel(config_card, text="Event Capture Duration (ms)", text_color=("#475569", "#94A3B8")).grid(
            row=4, column=0, sticky="w", padx=16, pady=(6, 14)
        )
        ctk.CTkEntry(
            config_card,
            textvariable=self.event_duration_var,
            placeholder_text="Example: 2000",
        ).grid(row=4, column=1, sticky="ew", padx=(6, 16), pady=(6, 14))

        wifi_card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        wifi_card.grid(row=3, column=0, sticky="ew", pady=(0, 12))
        wifi_card.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(
            wifi_card,
            text="Wi-Fi Offload Network",
            font=ctk.CTkFont(size=16, weight="bold"),
        ).grid(row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(14, 6))

        ctk.CTkLabel(wifi_card, text="SSID", text_color=("#475569", "#94A3B8")).grid(
            row=1, column=0, sticky="w", padx=16, pady=6
        )
        ctk.CTkEntry(
            wifi_card,
            textvariable=self.wifi1_ssid_var,
            placeholder_text="Wi-Fi SSID",
        ).grid(row=1, column=1, sticky="ew", padx=(6, 16), pady=6)

        ctk.CTkLabel(wifi_card, text="Password", text_color=("#475569", "#94A3B8")).grid(
            row=2, column=0, sticky="w", padx=16, pady=(6, 14)
        )
        ctk.CTkEntry(
            wifi_card,
            textvariable=self.wifi1_password_var,
            placeholder_text="Wi-Fi Password",
            show="*",
        ).grid(row=2, column=1, sticky="ew", padx=(6, 16), pady=(6, 14))

        desc_card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        desc_card.grid(row=4, column=0, sticky="ew", pady=(0, 12))
        ctk.CTkLabel(
            desc_card,
            text="Select the fields to include above. Unselected values are left unchanged on the receiver. Selecting Wi-Fi with blank SSID/password clears the stored Wi-Fi network.",
            wraplength=1100,
            justify="left",
            text_color=("#334155", "#CBD5E1"),
            font=ctk.CTkFont(size=12),
        ).grid(row=0, column=0, sticky="w", padx=16, pady=12)

        update_card = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        update_card.grid(row=5, column=0, sticky="ew", pady=(0, 12))
        update_card.grid_columnconfigure((0, 1, 2), weight=1)

        ctk.CTkLabel(
            update_card,
            text="Include In This Update",
            font=ctk.CTkFont(size=16, weight="bold"),
        ).grid(row=0, column=0, columnspan=3, sticky="w", padx=16, pady=(14, 10))

        update_options = [
            ("Truck ID", self.apply_truck_id_var),
            ("Description", self.apply_description_var),
            ("Sensor Interval", self.apply_sensor_interval_var),
            ("Trigger Threshold", self.apply_threshold_var),
            ("Poll Rate", self.apply_sample_rate_var),
            ("Capture Duration", self.apply_duration_var),
            ("Wi-Fi Network", self.apply_wifi_var),
        ]
        for idx, (label, var) in enumerate(update_options):
            row = 1 + (idx // 3)
            col = idx % 3
            ctk.CTkCheckBox(
                update_card,
                text=label,
                variable=var,
                onvalue=True,
                offvalue=False,
                fg_color=WABASH_BLUE,
                hover_color=WABASH_BLUE_HOVER,
            ).grid(row=row, column=col, sticky="w", padx=16, pady=(0, 10 if idx < 6 else 14))

        button_bar = ctk.CTkFrame(update_card, fg_color="transparent")
        button_bar.grid(row=4, column=0, columnspan=3, sticky="w", padx=16, pady=(0, 14))

        ctk.CTkButton(
            button_bar,
            text="Select All",
            width=110,
            command=lambda: self._set_all_setup_selection(True),
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
        ).grid(row=0, column=0, padx=(0, 8))

        ctk.CTkButton(
            button_bar,
            text="Deselect All",
            width=110,
            command=lambda: self._set_all_setup_selection(False),
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
        ).grid(row=0, column=1)

        self.send_config_button = ctk.CTkButton(
            page,
            text="Send Configuration",
            command=self._send_unit_config,
            fg_color=WABASH_BLUE,
            hover_color=WABASH_BLUE_HOVER,
            font=ctk.CTkFont(size=14, weight="bold"),
            height=40,
            state="disabled",
        )
        self.send_config_button.grid(row=6, column=0, sticky="ew")
        self._update_send_config_button()

    def _send_unit_config(self) -> None:
        if not self.serial_service.is_connected:
            messagebox.showwarning("Not Connected", "Connect to a serial port first.")
            return

        try:
            setup_mask = self._get_selected_setup_mask()
            if setup_mask == 0:
                messagebox.showwarning("No Fields Selected", "Select at least one field to include in this update.")
                return

            interval = 0
            if self.apply_sensor_interval_var.get():
                interval = int(self.sensor_interval_var.get().strip())

            threshold = 0.0
            if self.apply_threshold_var.get():
                threshold = float(self.event_trigger_threshold_var.get().strip())

            sample_rate = 0
            if self.apply_sample_rate_var.get():
                sample_rate = int(self.lab_sample_rate_var.get().strip())

            duration = 0
            if self.apply_duration_var.get():
                duration = int(self.event_duration_var.get().strip())

            truck_id = self.truck_id_var.get().replace(";", " ").replace("=", " ").replace("\n", " ").replace("\r", " ").strip()
            description = self.description_var.get().replace(";", " ").replace("=", " ").replace("\n", " ").replace("\r", " ").strip()

            wifi_ssid = self.wifi1_ssid_var.get().replace(";", " ").replace("=", " ").replace("\n", " ").replace("\r", " ").strip()
            wifi_password = self.wifi1_password_var.get().replace(";", " ").replace("=", " ").replace("\n", " ").replace("\r", " ").strip()

            if self.apply_wifi_var.get() and wifi_password and not wifi_ssid:
                messagebox.showwarning(
                    "Wi-Fi Setup Error",
                    "Wi-Fi password is set, but the SSID is empty.",
                )
                return

            wifi_fields = [
                f"w0s={wifi_ssid}",
                f"w0p={wifi_password}",
                "w1s=",
                "w1p=",
                "w2s=",
                "w2p=",
            ]

            packet = (
                f"SETUP:m={setup_mask};si={interval};thr={threshold};sr={sample_rate};dur={duration};"
                f"tid={truck_id};desc={description};"
                + ";".join(wifi_fields)
            )

            # Transmitter forwards SETUP in one LoRa frame, so keep payload conservative.
            if len(packet.encode("utf-8")) > 220:
                messagebox.showwarning(
                    "Setup Too Large",
                    "Configuration is too large for a single LoRa setup packet. "
                    "Shorten SSID/password/description values and try again.",
                )
                return

            wire_payload = f"{packet}\n"
            self.serial_service.send_text(wire_payload)

            wifi_configured = 1 if (self.apply_wifi_var.get() and wifi_ssid) else 0
            updated_fields = ", ".join(self._get_selected_setup_labels())

            messagebox.showinfo(
                "Configuration Sent",
                f"Unit configuration sent:\n\n"
                f"Fields Updated: {updated_fields}\n"
                f"Wi-Fi Network Saved: {wifi_configured}"
            )
        except ValueError:
            messagebox.showerror("Invalid Input", "Selected numeric fields must contain valid numeric values.")
        except Exception as exc:
            messagebox.showerror("Send Error", str(exc))

    def _update_active_unit_display(self) -> None:
        """Refresh the Active Unit panel on the dashboard from Unit Setup values."""
        if self.truck_id_var.get().strip():
            tid = self.truck_id_var.get().strip()
            self.db_unit_id.configure(text=tid, text_color=("#1E293B", "#E2E8F0"))
            if self.description_var.get().strip():
                self.db_unit_desc.configure(text=self.description_var.get().strip())
            else:
                self.db_unit_desc.configure(text="No description set")
        else:
            self.db_unit_id.configure(text="No unit configured", text_color=("#64748B", "#9CA3AF"))
            self.db_unit_desc.configure(text="")

    def _register_unit(self, truck_id: str) -> None:
        """Add or update a unit in the fleet registry and refresh the table."""
        import datetime
        now = datetime.datetime.now().strftime("%H:%M:%S")
        if truck_id in self.units:
            self.units[truck_id]["last_seen"] = now
            self.units[truck_id]["events"] += 1
            self.units[truck_id]["status"] = "Active"
        else:
            self.units[truck_id] = {
                "truck_id": truck_id,
                "last_seen": now,
                "events": 1,
                "status": "Active",
            }
        self._refresh_unit_list()

    def _refresh_unit_list(self) -> None:
        """Rebuild the fleet table rows, applying the current filter."""
        for row_widgets in self._fleet_rows:
            for w in row_widgets:
                w.destroy()
        self._fleet_rows.clear()

        term = self.unit_filter_var.get().strip().lower()
        units = [u for u in self.units.values() if not term or term in u["truck_id"].lower()]

        if not units:
            self.fleet_empty_label.grid(row=0, column=0, columnspan=4, pady=30)
            return

        self.fleet_empty_label.grid_remove()
        for i, unit in enumerate(units):
            cols: list[ctk.CTkLabel] = []
            for col, text in enumerate([unit["truck_id"], unit["last_seen"], str(unit["events"]), unit["status"]]):
                lbl = ctk.CTkLabel(
                    self.fleet_scroll, text=text,
                    font=ctk.CTkFont(size=12),
                    text_color=("#1E293B", "#E2E8F0"),
                    anchor="w",
                )
                lbl.grid(row=i, column=col, sticky="ew", padx=10, pady=4)
                cols.append(lbl)
            self._fleet_rows.append(cols)

    def _run_connection_scan(self) -> None:
        if not self.serial_service.is_connected:
            messagebox.showwarning("Not Connected", "Connect to a transmitter first.")
            return
        self._scan_results.clear()
        self.scan_button.configure(text="Scanning...", state="disabled")
        self._clear_disc_table()
        self.disc_empty_label.configure(text="Scanning for units...")
        self._send_payload("SCAN")
        self.root.after(3000, self._finish_connection_scan)

    def _finish_connection_scan(self) -> None:
        self.scan_button.configure(text="Unit Discover", state="normal")
        if self._scan_results:
            self._refresh_disc_table()
        else:
            self.disc_empty_label.configure(
                text="No units found.\nPress Unit Discover to scan.")
            self.disc_empty_label.grid(row=0, column=0, columnspan=2, pady=20)

    def _clear_disc_table(self) -> None:
        for row_widgets in self._disc_rows:
            for w in row_widgets:
                w.destroy()
        self._disc_rows.clear()
        self.disc_empty_label.grid(row=0, column=0, columnspan=2, pady=20)

    def _refresh_disc_table(self) -> None:
        self._clear_disc_table()
        self.disc_empty_label.grid_forget()
        self.disc_scroll.grid_columnconfigure(0, weight=1)
        for idx, unit_id in enumerate(self._scan_results):
            bg = (CARD_LIGHT, CARD_DARK) if idx % 2 == 0 else ('#F1F5F9', '#1E3050')
            row_frame = ctk.CTkFrame(self.disc_scroll, corner_radius=6,
                                     fg_color=bg)
            row_frame.grid(row=idx, column=0, sticky='ew', pady=2)
            row_frame.grid_columnconfigure(0, weight=1)
            lbl = ctk.CTkLabel(row_frame, text=unit_id,
                               font=ctk.CTkFont(size=13))
            lbl.grid(row=0, column=0, sticky='w', padx=10, pady=6)
            sel_btn = ctk.CTkButton(
                row_frame, text='Select', width=70,
                fg_color=WABASH_BLUE, hover_color=WABASH_BLUE_HOVER,
                command=lambda uid=unit_id: self._select_scanned_unit(uid))
            sel_btn.grid(row=0, column=1, padx=(4, 10), pady=6)
            self._disc_rows.append([row_frame])

    def _select_scanned_unit(self, unit_id: str) -> None:
        self.truck_id_var.set(unit_id)
        self.apply_truck_id_var.set(True)
        self._update_active_unit_display()
        self._register_unit(unit_id)
        # Enable unit action buttons
        for btn in (self.btn_request, self.btn_tare, self.btn_timesync):
            btn.configure(state="normal",
                          fg_color=WABASH_BLUE,
                          hover_color=WABASH_BLUE_HOVER)
        self._append_log(f"[Status] Active unit set to: {unit_id}")

    def _on_close(self) -> None:
        self.serial_service.disconnect()
        self.root.destroy()

