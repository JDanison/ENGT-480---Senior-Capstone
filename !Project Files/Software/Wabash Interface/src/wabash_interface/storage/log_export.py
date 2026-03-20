from __future__ import annotations

from datetime import datetime
from pathlib import Path
from typing import Iterable


def export_text_log(lines: Iterable[str], destination_dir: Path) -> Path:
    destination_dir.mkdir(parents=True, exist_ok=True)
    filename = f"wabash_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
    output_path = destination_dir / filename

    with output_path.open("w", encoding="utf-8") as handle:
        for line in lines:
            handle.write(f"{line}\n")

    return output_path
