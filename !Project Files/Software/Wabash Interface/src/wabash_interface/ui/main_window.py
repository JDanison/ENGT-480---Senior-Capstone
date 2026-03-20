from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox

import customtkinter as ctk

from serial.tools import list_ports

from wabash_interface.services.serial_service import SerialConfig, SerialService
from wabash_interface.storage.log_export import export_text_log

WABASH_RED        = "#C8102E"
WABASH_RED_HOVER  = "#A30D26"
BTN_GREY          = "#475569"
BTN_GREY_HOVER    = "#334155"
PANEL_LIGHT       = "#F3F4F6"
PANEL_DARK        = "#111827"
CARD_LIGHT        = "#FFFFFF"
CARD_DARK         = "#1F2937"

# Log line colour tags (foreground only; works on both light/dark)
LOG_COLORS = {
    "tx":      "#F87171",   # soft red  — sent commands
    "rx":      "#60A5FA",   # soft blue — received data
    "data":    "#34D399",   # green     — DATA/DATC payload rows
    "status":  "#FBBF24",   # amber     — status/RSP lines
    "default": "#D1D5DB",   # grey      — everything else
}

# Density presets: (pad_x, pad_y_btn, font_size_log, row_padding)
DENSITY = {
    "Compact":     (8,  3, 11, 4),
    "Comfortable": (16, 6, 12, 8),
}


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

        self.port_var     = tk.StringVar(value="")
        self.baud_var     = tk.StringVar(value="115200")
        self.command_var  = tk.StringVar(value="")
        self.theme_var    = tk.StringVar(value="system")
        self.density_var  = tk.StringVar(value="Comfortable")
        self.search_var   = tk.StringVar(value="")
        self.text_scale_var = tk.DoubleVar(value=1.0)
        self.search_placeholder_active = False

        self.pages: dict[str, ctk.CTkFrame] = {}
        self.nav_buttons: dict[str, ctk.CTkButton] = {}

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

        sidebar = ctk.CTkFrame(self.root, width=320, corner_radius=0, fg_color=("#E5E7EB", "#0B1220"))
        sidebar.grid(row=0, column=0, sticky="nsew")
        sidebar.grid_propagate(False)
        sidebar.grid_columnconfigure(0, weight=1)

        self.page_container = ctk.CTkFrame(self.root, corner_radius=0, fg_color="transparent")
        self.page_container.grid(row=0, column=1, sticky="nsew", padx=14, pady=14)
        self.page_container.grid_columnconfigure(0, weight=1)
        self.page_container.grid_rowconfigure(0, weight=1)

        ctk.CTkLabel(
            sidebar,
            text="WABASH NATIONAL",
            font=ctk.CTkFont(size=30, weight="bold"),
            text_color=WABASH_RED,
        ).grid(row=0, column=0, sticky="w", padx=24, pady=(28, 4))
        ctk.CTkLabel(
            sidebar,
            text="Receiver Command Center",
            text_color=("#475569", "#9CA3AF"),
            font=ctk.CTkFont(size=14),
        ).grid(row=1, column=0, sticky="w", padx=24, pady=(0, 20))

        ctk.CTkFrame(sidebar, height=3, corner_radius=8, fg_color=WABASH_RED).grid(
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

        self.nav_buttons["Settings"] = ctk.CTkButton(
            nav_card,
            text="Settings",
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
            command=lambda: self._show_page("Settings"),
        )
        self.nav_buttons["Settings"].grid(row=2, column=0, sticky="ew", padx=16, pady=4)

        self.nav_buttons["Live Session"] = ctk.CTkButton(
            nav_card,
            text="Live Session",
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
            command=lambda: self._show_page("Live Session"),
        )
        self.nav_buttons["Live Session"].grid(row=3, column=0, sticky="ew", padx=16, pady=(4, 14))

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
        for name, frame in self.pages.items():
            if name == page_name:
                frame.grid(row=0, column=0, sticky="nsew")
            else:
                frame.grid_remove()

        for name, button in self.nav_buttons.items():
            if name == page_name:
                button.configure(fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER)
            else:
                button.configure(fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER)

    def _build_dashboard_page(self) -> None:
        page = ctk.CTkFrame(self.page_container, corner_radius=0, fg_color="transparent")
        page.grid_columnconfigure(0, weight=1)
        page.grid_rowconfigure(2, weight=1)
        self.pages["Dashboard"] = page

        header = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        header.grid(row=0, column=0, sticky="ew")
        header.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(header, text="System Dashboard", font=ctk.CTkFont(size=22, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=18, pady=14
        )
        ctk.CTkLabel(header, text="WABASH", text_color=WABASH_RED, font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=1, sticky="e", padx=18, pady=14
        )

        cards = ctk.CTkFrame(page, fg_color="transparent")
        cards.grid(row=1, column=0, sticky="ew", pady=(12, 12))
        cards.grid_columnconfigure((0, 1, 2), weight=1)

        self.db_connection = self._create_stat_card(cards, 0, "Connection", "Disconnected")
        self.db_ports = self._create_stat_card(cards, 1, "Ports Detected", "0")
        self.db_logs = self._create_stat_card(cards, 2, "Log Lines", "0")

        cards2 = ctk.CTkFrame(page, fg_color="transparent")
        cards2.grid(row=2, column=0, sticky="new")
        cards2.grid_columnconfigure((0, 1, 2), weight=1)

        self.db_tx = self._create_stat_card(cards2, 0, "TX Count", "0")
        self.db_rx = self._create_stat_card(cards2, 1, "RX Count", "0")
        self.db_uptime = self._create_stat_card(cards2, 2, "Session Health", "Live")

        detail = ctk.CTkFrame(page, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        detail.grid(row=3, column=0, sticky="ew")
        detail.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(detail, text="Latest Message", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(12, 4)
        )
        self.db_last_message = ctk.CTkLabel(
            detail,
            text=self.last_message,
            wraplength=760,
            justify="left",
            anchor="w",
            text_color=("#334155", "#CBD5E1"),
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
            selected_color=WABASH_RED,
            selected_hover_color=WABASH_RED_HOVER,
        ).grid(row=2, column=0, sticky="ew", padx=16, pady=(4, 10))

        ctk.CTkLabel(card, text="Layout Density", text_color=("#475569", "#94A3B8")).grid(
            row=3, column=0, sticky="w", padx=16
        )
        ctk.CTkSegmentedButton(
            card,
            values=["Compact", "Comfortable"],
            variable=self.density_var,
            command=self._set_density,
            selected_color=WABASH_RED,
            selected_hover_color=WABASH_RED_HOVER,
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
            button_color=WABASH_RED,
            button_hover_color=WABASH_RED_HOVER,
            progress_color=WABASH_RED,
            command=self._set_text_scale,
        ).grid(row=7, column=0, sticky="ew", padx=16, pady=(4, 14))

    def _build_live_page(self) -> None:
        page = ctk.CTkFrame(self.page_container, corner_radius=0, fg_color="transparent")
        page.grid_columnconfigure(1, weight=1)
        page.grid_rowconfigure(0, weight=1)
        self.pages["Live Session"] = page

        left_panel = ctk.CTkFrame(page, width=320, corner_radius=0, fg_color=("#E5E7EB", "#0B1220"))
        left_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 14))
        left_panel.grid_propagate(False)
        left_panel.grid_columnconfigure(0, weight=1)

        content = ctk.CTkFrame(page, corner_radius=0, fg_color="transparent")
        content.grid(row=0, column=1, sticky="nsew")
        content.grid_columnconfigure(0, weight=1)
        content.grid_rowconfigure(2, weight=1)

        connection_card = ctk.CTkFrame(left_panel, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        connection_card.grid(row=0, column=0, sticky="ew", padx=18, pady=(18, 12))
        connection_card.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(connection_card, text="Connection", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=16, pady=(14, 6)
        )

        ctk.CTkLabel(connection_card, text="COM Port", text_color=("#475569", "#94A3B8")).grid(
            row=1, column=0, sticky="w", padx=16
        )
        self.port_combo = ctk.CTkComboBox(connection_card, variable=self.port_var, values=["No ports found"])
        self.port_combo.grid(row=2, column=0, sticky="ew", padx=16, pady=(4, 10))

        ctk.CTkLabel(connection_card, text="Baud", text_color=("#475569", "#94A3B8")).grid(
            row=3, column=0, sticky="w", padx=16
        )
        ctk.CTkEntry(connection_card, textvariable=self.baud_var).grid(row=4, column=0, sticky="ew", padx=16, pady=(4, 12))

        action_row = ctk.CTkFrame(connection_card, fg_color="transparent")
        action_row.grid(row=5, column=0, sticky="ew", padx=16, pady=(0, 12))
        action_row.grid_columnconfigure((0, 1, 2), weight=1)

        ctk.CTkButton(action_row, text="Refresh", command=self._refresh_ports, fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER).grid(row=0, column=0, padx=(0, 6), sticky="ew")
        self.connect_button = ctk.CTkButton(
            action_row,
            text="Connect",
            command=self._connect,
            fg_color=WABASH_RED,
            hover_color=WABASH_RED_HOVER,
        )
        self.connect_button.grid(row=0, column=1, padx=3, sticky="ew")
        self.disconnect_button = ctk.CTkButton(
            action_row,
            text="Disconnect",
            command=self._disconnect,
            fg_color=BTN_GREY,
            hover_color=BTN_GREY_HOVER,
        )
        self.disconnect_button.grid(row=0, column=2, padx=(6, 0), sticky="ew")

        quick_card = ctk.CTkFrame(left_panel, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        quick_card.grid(row=1, column=0, sticky="ew", padx=18, pady=(0, 12))
        quick_card.grid_columnconfigure((0, 1), weight=1)

        ctk.CTkLabel(quick_card, text="Quick Commands", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, columnspan=2, sticky="w", padx=16, pady=(14, 8)
        )
        ctk.CTkButton(quick_card, text="Request Data  d", command=lambda: self._send_quick("d"), fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER).grid(
            row=1, column=0, padx=(16, 6), pady=5, sticky="ew"
        )
        ctk.CTkButton(quick_card, text="Tare  z", command=lambda: self._send_quick("z"), fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER).grid(
            row=1, column=1, padx=(6, 16), pady=5, sticky="ew"
        )
        ctk.CTkButton(quick_card, text="Monitor  m", command=lambda: self._send_quick("m"), fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER).grid(
            row=2, column=0, padx=(16, 6), pady=5, sticky="ew"
        )
        ctk.CTkButton(quick_card, text="Time Sync  s", command=lambda: self._send_quick("s"), fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER).grid(
            row=2, column=1, padx=(6, 16), pady=5, sticky="ew"
        )

        top_bar = ctk.CTkFrame(content, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        top_bar.grid(row=0, column=0, sticky="ew")
        top_bar.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(top_bar, text="Live Session", font=ctk.CTkFont(size=20, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=18, pady=14
        )

        ctk.CTkLabel(top_bar, text="WABASH", text_color=WABASH_RED, font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=1, sticky="e", padx=(10, 170), pady=14
        )

        self.connection_badge = ctk.CTkLabel(
            top_bar,
            text="Disconnected",
            corner_radius=10,
            fg_color="#7F1D1D",
            text_color="#FEE2E2",
            width=140,
        )
        self.connection_badge.grid(row=0, column=1, sticky="e", padx=(10, 18), pady=14)

        stats = ctk.CTkFrame(content, fg_color="transparent")
        stats.grid(row=1, column=0, sticky="ew", pady=(12, 12))
        stats.grid_columnconfigure((0, 1, 2), weight=1)

        self.port_stat = self._create_stat_card(stats, 0, "Port", "-")
        self.tx_stat = self._create_stat_card(stats, 1, "TX", "0")
        self.rx_stat = self._create_stat_card(stats, 2, "RX", "0")

        log_card = ctk.CTkFrame(content, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        log_card.grid(row=2, column=0, sticky="nsew")
        log_card.grid_columnconfigure(0, weight=1)
        log_card.grid_rowconfigure(2, weight=1)

        log_header = ctk.CTkFrame(log_card, fg_color="transparent")
        log_header.grid(row=0, column=0, sticky="ew", padx=16, pady=(14, 0))
        log_header.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(log_header, text="Session Log", font=ctk.CTkFont(size=16, weight="bold")).grid(
            row=0, column=0, sticky="w"
        )

        search_frame = ctk.CTkFrame(log_header, fg_color=("#E5E7EB", "#374151"), corner_radius=8)
        search_frame.grid(row=0, column=1, sticky="e")
        search_frame.grid_columnconfigure(0, weight=1)

        self.search_entry = ctk.CTkEntry(
            search_frame,
            textvariable=self.search_var,
            border_width=0,
            fg_color=("#F8FAFC", "#374151"),
            text_color=("#0F172A", "#F3F4F6"),
            font=ctk.CTkFont(size=14),
            width=220,
        )
        self.search_entry.grid(row=0, column=0, padx=(8, 4), pady=4, sticky="ew")

        self.search_var.trace_add("write", lambda *_: self._on_search_change())
        self.search_entry.bind("<FocusIn>", lambda _event: self._on_search_focus_in())
        self.search_entry.bind("<FocusOut>", lambda _event: self._on_search_focus_out())

        ctk.CTkButton(
            search_frame, text="✕", width=28, height=28,
            fg_color="transparent", hover_color=("#D1D5DB", "#4B5563"),
            command=self._clear_search,
        ).grid(row=0, column=1, padx=(0, 4))

        self._activate_search_placeholder()

        legend = ctk.CTkFrame(log_card, fg_color="transparent")
        legend.grid(row=1, column=0, sticky="w", padx=16, pady=(6, 4))
        for label, colour in [("TX", LOG_COLORS["tx"]), ("RX", LOG_COLORS["rx"]),
                              ("Data", LOG_COLORS["data"]), ("Status", LOG_COLORS["status"])]:
            dot = ctk.CTkLabel(legend, text="●", text_color=colour, width=18, font=ctk.CTkFont(size=10))
            dot.pack(side="left")
            ctk.CTkLabel(legend, text=label, text_color=("#475569", "#94A3B8"), font=ctk.CTkFont(size=11)).pack(side="left", padx=(0, 12))

        self.log_text = ctk.CTkTextbox(log_card, wrap="none", corner_radius=10, font=("Consolas", 12))
        self.log_text.grid(row=2, column=0, sticky="nsew", padx=16, pady=(0, 14))

        inner: tk.Text = self.log_text._textbox  # type: ignore[attr-defined]
        for tag, colour in LOG_COLORS.items():
            inner.tag_configure(tag, foreground=colour)
        inner.tag_configure("search_hl", background=WABASH_RED, foreground="white")

        bottom = ctk.CTkFrame(content, corner_radius=14, fg_color=(CARD_LIGHT, CARD_DARK))
        bottom.grid(row=3, column=0, sticky="ew", pady=(12, 0))
        bottom.grid_columnconfigure(0, weight=1)

        command_row = ctk.CTkFrame(bottom, fg_color="transparent")
        command_row.grid(row=0, column=0, sticky="ew", padx=14, pady=(14, 8))
        command_row.grid_columnconfigure(0, weight=1)

        self.command_entry = ctk.CTkEntry(command_row, textvariable=self.command_var, placeholder_text="Type custom command and press Send")
        self.command_entry.grid(row=0, column=0, sticky="ew")
        ctk.CTkButton(command_row, text="Send", width=110, command=self._send_custom, fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER).grid(row=0, column=1, padx=(10, 0))

        button_row = ctk.CTkFrame(bottom, fg_color="transparent")
        button_row.grid(row=1, column=0, sticky="ew", padx=14, pady=(0, 14))
        button_row.grid_columnconfigure((0, 1), weight=1)
        ctk.CTkButton(button_row, text="Clear Log", fg_color=BTN_GREY, hover_color="#64748B", command=self._clear_log).grid(
            row=0, column=0, padx=(0, 6), sticky="ew"
        )
        ctk.CTkButton(button_row, text="Export Log", fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER, command=self._export_log).grid(
            row=0, column=1, padx=(6, 0), sticky="ew"
        )

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

        self.db_ports.configure(text=str(0 if ports == ["No ports found"] else len(ports)))

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
            self.disconnect_button.configure(fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER)
            self.sidebar_status.configure(text="Connected", fg_color="#14532D", text_color="#DCFCE7")
        else:
            self.connection_badge.configure(text="Disconnected", fg_color="#7F1D1D", text_color="#FEE2E2")
            self.connect_button.configure(fg_color=WABASH_RED, hover_color=WABASH_RED_HOVER)
            self.disconnect_button.configure(fg_color=BTN_GREY, hover_color=BTN_GREY_HOVER)
            self.sidebar_status.configure(text="Disconnected", fg_color="#7F1D1D", text_color="#FEE2E2")

        self.port_stat.configure(text=port_text)
        self.db_connection.configure(text="Connected" if is_connected else "Disconnected")

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

        self.db_tx.configure(text=str(self.tx_count))
        self.db_rx.configure(text=str(self.rx_count))
        self.db_logs.configure(text=str(len(self.log_lines)))
        self.db_last_message.configure(text=self.last_message)

    def _pump_messages(self) -> None:
        while not self.serial_service.messages.empty():
            line = self.serial_service.messages.get_nowait()
            self._append_log(line)
        self.db_uptime.configure(text="Active" if self.connected else "Standby")
        self.root.after(100, self._pump_messages)

    def _clear_log(self) -> None:
        self.log_lines.clear()
        self.log_text.delete("1.0", "end")
        self.tx_count = 0
        self.rx_count = 0
        self.tx_stat.configure(text="0")
        self.rx_stat.configure(text="0")
        self.db_tx.configure(text="0")
        self.db_rx.configure(text="0")
        self.db_logs.configure(text="0")
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

    def _on_close(self) -> None:
        self.serial_service.disconnect()
        self.root.destroy()
