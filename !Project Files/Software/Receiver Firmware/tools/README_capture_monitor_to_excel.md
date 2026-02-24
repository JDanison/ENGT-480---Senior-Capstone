# Continuous Monitor to Excel Export

This utility captures the `m` (continuous monitoring) serial output and creates an Excel workbook under `excel-results/`.

## 1) Install Python packages

```powershell
pip install pyserial pandas xlsxwriter
```

## 2) Upload firmware and open capture

From the workspace root:

```powershell
python .\tools\capture_monitor_to_excel.py --port COM5 --baud 115200 --duration 30
```

Optional label (used in filename + Summary cell):

```powershell
python .\tools\capture_monitor_to_excel.py --port COM5 --baud 115200 --duration 30 --label "AVVcap_100nF_ADCcap_10nF"
```

- Replace `COM5` with your board COM port.
- The script auto-sends `z` first and waits for tare completion, then sends `m` to start monitoring.
- After `--duration` seconds, it auto-sends a stop character.

## 3) Output

A file is generated at:

- `excel-results/monitor_session_YYYYMMDD_HHMMSS.xlsx`

Workbook contents:

- `Summary` sheet (first tab):
  - Top input section for your test conditions:
    - `Test Conditions Label`
    - `Filtered AVV line?` + capacitor values
    - `Filtered ADC line?` + capacitor values
    - `Filtered A+ and A- wires?`
  - Extreme high/low values and average variation metrics.
  - Chart: variation over time (Raw ADC + Strain).
- `Data` sheet (second tab):
  - Full captured monitoring table.
  - Timing columns are placed next to data with a blank spacer column in between.
  - Automatic highlight when `strain_uE` is above `+50` or below `-50`.
- `Timing` sheet (third tab):
  - Per-entry timing table (`Completion ms` and `Interval ms`).
