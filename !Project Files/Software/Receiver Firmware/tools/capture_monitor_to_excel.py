import argparse
import sys
import time
from datetime import datetime
from pathlib import Path
import re

import pandas as pd
import serial


ROW_PATTERN = re.compile(
    r"^\s*([0-9]+(?:\.[0-9]+)?)\s*,\s*([0-9]+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+(?:\.\d+)?)"
)


def build_output_path(workspace_root: Path) -> Path:
    return build_output_path_with_label(workspace_root, None)


def sanitize_label(label: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_-]+", "-", label.strip())
    cleaned = re.sub(r"-+", "-", cleaned).strip("-")
    return cleaned[:60] if cleaned else "session"


def build_output_path_with_label(workspace_root: Path, label: str | None) -> Path:
    output_dir = workspace_root / "excel-results"
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    if label:
        safe = sanitize_label(label)
        return output_dir / f"monitor_session_{stamp}_{safe}.xlsx"
    return output_dir / f"monitor_session_{stamp}.xlsx"


def parse_row(line: str):
    match = ROW_PATTERN.match(line)
    if not match:
        return None

    return {
        "elapsed_s": float(match.group(1)),
        "sample_ms": int(match.group(2)),
        "raw_adc": int(match.group(3)),
        "avg_20": int(match.group(4)),
        "filtered_20": int(match.group(5)),
        "zeroed_adc": int(match.group(6)),
        "strain_uE": float(match.group(7)),
    }


def run_tare_before_monitor(ser: serial.Serial, tare_timeout_s: float = 60.0):
    print("Sending 'z' command to tare strain gauge before monitoring...\n")
    ser.write(b"z")
    ser.flush()

    tare_start = time.time()
    saw_tare_banner = False

    while True:
        if (time.time() - tare_start) > tare_timeout_s:
            raise RuntimeError("Tare timed out before completion.")

        raw = ser.readline()
        if not raw:
            continue

        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue

        print(line)

        if "=== TARING STRAIN GAUGE ===" in line:
            saw_tare_banner = True

        if "Strain gauge zeroed successfully!" in line:
            print("\nTare complete. Starting continuous monitor...\n")
            return

        if "Failed to zero strain gauge!" in line:
            raise RuntimeError("Firmware reported tare failure.")

        if saw_tare_banner and "===========================" in line:
            raise RuntimeError("Tare ended without a success message.")


def capture_monitor_session(port: str, baud: int, duration: float | None, timeout: float):
    rows = []
    collecting = False
    sent_stop = False
    monitor_start_wall = None

    with serial.Serial(port=port, baudrate=baud, timeout=0.5) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        run_tare_before_monitor(ser)

        while ser.in_waiting:
            ser.read()

        ser.write(b"m")
        ser.flush()
        print("Sent 'm' command to firmware. Waiting for monitoring output...\n")

        start_wall = time.time()

        while True:
            if timeout > 0 and (time.time() - start_wall) > timeout:
                if collecting and not sent_stop:
                    ser.write(b"x\n")
                    ser.flush()
                    sent_stop = True
                    print("Timeout reached. Sent stop command.")
                else:
                    break

            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            print(line)

            if "[M_SESSION_START]" in line:
                collecting = True
                monitor_start_wall = time.time()

            if collecting:
                parsed = parse_row(line)
                if parsed:
                    rows.append(parsed)

                if duration is not None and not sent_stop and monitor_start_wall is not None:
                    if (time.time() - monitor_start_wall) >= duration:
                        ser.write(b"x\n")
                        ser.flush()
                        sent_stop = True
                        print("Duration reached. Sent stop command.")

                if "[M_SESSION_END]" in line or "Monitoring stopped. Collected" in line:
                    break

    if not rows:
        raise RuntimeError(
            "No monitoring samples captured. Verify COM port/baud and that firmware includes monitor output."
        )

    df = pd.DataFrame(rows)
    df.insert(0, "sample", range(1, len(df) + 1))
    df["entry_interval_ms"] = (df["elapsed_s"].diff() * 1000.0).fillna(df["sample_ms"])

    return df


def compute_summary(df: pd.DataFrame):
    raw_step_mean = df["raw_adc"].diff().abs().iloc[1:].mean() if len(df) > 1 else 0.0
    strain_step_mean = df["strain_uE"].diff().abs().iloc[1:].mean() if len(df) > 1 else 0.0

    return {
        "samples": int(len(df)),
        "sampling_total_s": float(df["elapsed_s"].iloc[-1]),
        "sample_ms_avg": float(df["sample_ms"].mean()),
        "sample_ms_min": int(df["sample_ms"].min()),
        "sample_ms_max": int(df["sample_ms"].max()),
        "raw_high": int(df["raw_adc"].max()),
        "raw_low": int(df["raw_adc"].min()),
        "strain_high_uE": float(df["strain_uE"].max()),
        "strain_low_uE": float(df["strain_uE"].min()),
        "avg_variation_raw_step": float(raw_step_mean),
        "avg_variation_strain_step_uE": float(strain_step_mean),
    }


def write_excel(df: pd.DataFrame, output_file: Path, run_label: str | None):
    summary = compute_summary(df)
    generated_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    data_export = df[
        [
            "sample",
            "elapsed_s",
            "raw_adc",
            "avg_20",
            "filtered_20",
            "zeroed_adc",
            "strain_uE",
            "sample_ms",
            "entry_interval_ms",
        ]
    ].copy()
    data_export.insert(7, " ", "")

    with pd.ExcelWriter(output_file, engine="xlsxwriter") as writer:
        workbook = writer.book
        ws_summary = workbook.add_worksheet("Summary")
        writer.sheets["Summary"] = ws_summary

        data_export.to_excel(writer, sheet_name="Data", index=False, startrow=1)
        ws_data = writer.sheets["Data"]

        timing_df = df[["sample", "sample_ms"]].copy()
        timing_df.rename(
            columns={
                "sample": "Sample",
                "sample_ms": "Completion ms",
            },
            inplace=True,
        )
        timing_df.to_excel(writer, sheet_name="Timing", index=False, startrow=1)
        ws_timing = writer.sheets["Timing"]

        title_fmt = workbook.add_format({"bold": True, "font_size": 13, "align": "left", "valign": "vcenter"})
        head_fmt = workbook.add_format({"bold": True, "bg_color": "#D9E1F2", "border": 1, "align": "center", "valign": "vcenter"})
        input_head_fmt = workbook.add_format({"bold": True, "bg_color": "#FFF2CC", "border": 1, "align": "left", "valign": "vcenter"})
        input_cell_fmt = workbook.add_format({"border": 1, "bg_color": "#FFFBE6", "align": "left", "valign": "vcenter"})
        high_strain_fmt = workbook.add_format({"bg_color": "#FCE4D6", "font_color": "#9C0006"})
        center_cell_fmt = workbook.add_format({"align": "center", "valign": "vcenter"})

        ws_summary.write(0, 0, "Test Conditions Label", input_head_fmt)
        ws_summary.merge_range(0, 1, 0, 5, run_label if run_label else "", input_cell_fmt)
        ws_summary.write(1, 0, "General Notes", input_head_fmt)
        ws_summary.merge_range(1, 1, 1, 5, f"Generated At: {generated_at}", input_cell_fmt)
        ws_summary.write(2, 0, "Filtered AVV line?", input_head_fmt)
        ws_summary.write(2, 1, "", input_cell_fmt)
        ws_summary.write(2, 2, "AVV capacitor values", input_head_fmt)
        ws_summary.merge_range(2, 3, 2, 5, "", input_cell_fmt)
        ws_summary.write(3, 0, "Filtered ADC line?", input_head_fmt)
        ws_summary.write(3, 1, "", input_cell_fmt)
        ws_summary.write(3, 2, "ADC capacitor values", input_head_fmt)
        ws_summary.merge_range(3, 3, 3, 5, "", input_cell_fmt)
        ws_summary.write(4, 0, "Filtered A+ and A- wires?", input_head_fmt)
        ws_summary.write(4, 1, "", input_cell_fmt)

        ws_summary.write(6, 0, "Continuous Monitoring Summary", title_fmt)

        meta_rows = [
            ("Total Samples", summary["samples"]),
            ("Total Time (s)", round(summary["sampling_total_s"], 3)),
        ]

        stats_start = 8
        section_fmt = workbook.add_format({"bold": True, "bg_color": "#4F81BD", "font_color": "#FFFFFF", "align": "center", "valign": "vcenter", "border": 1})
        label_fmt = workbook.add_format({"bold": True, "bg_color": "#D9E1F2", "align": "center", "valign": "vcenter", "border": 1})
        value_fmt = workbook.add_format({"align": "center", "valign": "vcenter", "border": 1})
        avg_value_fmt = workbook.add_format({"bold": True, "bg_color": "#FFF2CC", "align": "center", "valign": "vcenter", "border": 1})

        # ADC Variation & Extremes
        ws_summary.merge_range(stats_start, 0, stats_start, 2, "ADC Variation & Extremes", section_fmt)
        ws_summary.write(stats_start + 1, 0, "Raw ADC High", label_fmt)
        ws_summary.write(stats_start + 1, 1, "Raw ADC Low", label_fmt)
        ws_summary.write(stats_start + 1, 2, "Avg ADC Variation", label_fmt)
        ws_summary.write(stats_start + 2, 0, summary["raw_high"], value_fmt)
        ws_summary.write(stats_start + 2, 1, summary["raw_low"], value_fmt)
        ws_summary.write(stats_start + 2, 2, summary["avg_variation_raw_step"], avg_value_fmt)

        # Strain Variation & Extremes
        ws_summary.merge_range(stats_start + 4, 0, stats_start + 4, 2, "Strain Variation & Extremes", section_fmt)
        ws_summary.write(stats_start + 5, 0, "Strain High (uE)", label_fmt)
        ws_summary.write(stats_start + 5, 1, "Strain Low (uE)", label_fmt)
        ws_summary.write(stats_start + 5, 2, "Avg Variation Strain Step (uE)", label_fmt)
        ws_summary.write(stats_start + 6, 0, summary["strain_high_uE"], value_fmt)
        ws_summary.write(stats_start + 6, 1, summary["strain_low_uE"], value_fmt)
        ws_summary.write(stats_start + 6, 2, summary["avg_variation_strain_step_uE"], avg_value_fmt)

        # Sample Time Variation & Extremes
        ws_summary.merge_range(stats_start + 8, 0, stats_start + 8, 2, "Sample Time Variation & Extremes", section_fmt)
        ws_summary.write(stats_start + 9, 0, "Sample Time High (ms)", label_fmt)
        ws_summary.write(stats_start + 9, 1, "Sample Time Low (ms)", label_fmt)
        ws_summary.write(stats_start + 9, 2, "Avg Sample Time (ms)", label_fmt)
        ws_summary.write(stats_start + 10, 0, summary["sample_ms_max"], value_fmt)
        ws_summary.write(stats_start + 10, 1, summary["sample_ms_min"], value_fmt)
        ws_summary.write(stats_start + 10, 2, summary["sample_ms_avg"], avg_value_fmt)

        ws_summary.write(stats_start + 12, 0, "Timing details are available on the Timing tab.")

        mrow = stats_start + 9
        for key, value in meta_rows:
            ws_summary.write(mrow, 4, key, label_fmt)
            ws_summary.write(mrow, 5, value, value_fmt)
            mrow += 1

        chart = workbook.add_chart({"type": "line"})
        data_first = 2
        data_last = len(data_export) + 1

        chart.add_series(
            {
                "name": "Raw ADC",
                "categories": ["Data", data_first, 0, data_last, 0],
                "values": ["Data", data_first, 2, data_last, 2],
            }
        )
        chart.add_series(
            {
                "name": "Strain (uE)",
                "categories": ["Data", data_first, 0, data_last, 0],
                "values": ["Data", data_first, 6, data_last, 6],
                "y2_axis": True,
            }
        )

        chart.set_title({"name": "Variation Over Time"})
        chart.set_x_axis({"name": "Sample #"})
        chart.set_y_axis({"name": "Raw ADC"})
        chart.set_y2_axis({"name": "Strain (uE)"})

        ws_summary.insert_chart(2, 7, chart, {"x_scale": 1.4, "y_scale": 1.4})

        ws_summary.set_column_pixels("A:C", 200)
        ws_summary.set_column_pixels("E:F", 100)

        ws_data.write(0, 0, "Continuous Monitor Data")
        ws_data.set_column_pixels("A:C", 200, center_cell_fmt)
        ws_data.set_column("D:G", 14, center_cell_fmt)
        ws_data.set_column("H:H", 3, center_cell_fmt)
        ws_data.set_column("I:J", 16, center_cell_fmt)
        ws_data.write(0, 8, "Highlight Rule: |strain_uE| > 50")

        data_first_row = 2
        data_last_row = len(df) + 1
        ws_data.conditional_format(
            data_first_row,
            0,
            data_last_row,
            7,
            {
                "type": "formula",
                "criteria": "=OR($G2>50,$G2<-50)",
                "format": high_strain_fmt,
            },
        )

        ws_timing.write(0, 0, "Per-Entry Timing")
        ws_timing.set_column_pixels("A:C", 200, center_cell_fmt)

        ws_timing.write(2, 4, "Timing Summary", head_fmt)
        ws_timing.write(3, 4, "Total Samples", head_fmt)
        ws_timing.write(3, 5, summary["samples"], value_fmt)
        ws_timing.write(4, 4, "Total Time (s)", head_fmt)
        ws_timing.write(4, 5, round(summary["sampling_total_s"], 3), value_fmt)
        ws_timing.write(5, 4, "Completion ms Avg", head_fmt)
        ws_timing.write(5, 5, round(summary["sample_ms_avg"], 2), avg_value_fmt)
        ws_timing.write(6, 4, "Completion ms Min", head_fmt)
        ws_timing.write(6, 5, summary["sample_ms_min"], value_fmt)
        ws_timing.write(7, 4, "Completion ms Max", head_fmt)
        ws_timing.write(7, 5, summary["sample_ms_max"], value_fmt)
        ws_timing.set_column_pixels("E:F", 100)


def main():
    parser = argparse.ArgumentParser(
        description="Capture firmware continuous monitor output and export to Excel."
    )
    parser.add_argument("--port", required=True, help="Serial port (example: COM5)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument(
        "--duration",
        type=float,
        default=20.0,
        help="Monitoring duration in seconds before auto-stop (default: 20)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=90.0,
        help="Hard timeout in seconds for the capture session (default: 90)",
    )
    parser.add_argument(
        "--label",
        type=str,
        default="",
        help="Optional test-condition label included in filename and Summary sheet.",
    )

    args = parser.parse_args()

    workspace_root = Path(__file__).resolve().parents[1]
    run_label = args.label.strip() if args.label else ""
    output_file = build_output_path_with_label(workspace_root, run_label if run_label else None)

    try:
        df = capture_monitor_session(args.port, args.baud, args.duration, args.timeout)
        write_excel(df, output_file, run_label if run_label else None)
    except Exception as exc:
        print(f"ERROR: {exc}")
        sys.exit(1)

    print("\nExcel export complete:")
    print(output_file)


if __name__ == "__main__":
    main()
