from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from queue import Queue
from threading import Event, Thread
from typing import Optional

import serial


@dataclass
class SerialConfig:
    port: str
    baudrate: int = 115200
    timeout: float = 0.25


class SerialService:
    def __init__(self) -> None:
        self._serial: Optional[serial.Serial] = None
        self._rx_thread: Optional[Thread] = None
        self._stop_event = Event()
        self.messages: "Queue[str]" = Queue()

    @property
    def is_connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    def connect(self, config: SerialConfig) -> None:
        if self.is_connected:
            self.disconnect()

        self._serial = serial.Serial(
            port=config.port,
            baudrate=config.baudrate,
            timeout=config.timeout,
        )
        self._stop_event.clear()
        self._rx_thread = Thread(target=self._read_loop, daemon=True)
        self._rx_thread.start()
        self._emit_status(f"Connected to {config.port} @ {config.baudrate}")

    def disconnect(self) -> None:
        self._stop_event.set()

        if self._serial is not None:
            try:
                if self._serial.is_open:
                    self._serial.close()
            finally:
                self._serial = None

        self._emit_status("Disconnected")

    def send_text(self, payload: str) -> None:
        if not self.is_connected or self._serial is None:
            raise RuntimeError("Serial port is not connected.")

        self._serial.write(payload.encode("utf-8"))
        self._serial.flush()
        self._emit_status(f"TX> {payload.rstrip()}")

    def send_command(self, command: str) -> None:
        if not command:
            return
        self.send_text(command)

    def _read_loop(self) -> None:
        pending = bytearray()
        while not self._stop_event.is_set():
            if self._serial is None or not self._serial.is_open:
                break

            waiting = self._serial.in_waiting if self._serial is not None else 0
            raw = self._serial.read(waiting or 1)
            if not raw:
                continue

            pending.extend(raw)

            while True:
                newline_index = pending.find(b"\n")
                if newline_index < 0:
                    break

                line = pending[:newline_index]
                del pending[: newline_index + 1]

                message = line.decode("utf-8", errors="replace").rstrip("\r")
                if message:
                    self.messages.put(message)

    def _emit_status(self, message: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.messages.put(f"[{timestamp}] {message}")
