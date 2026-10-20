"""Render and summarize captured offline oversampling CSVs with ReportLab."""

import csv
import math
import statistics
import sys
from pathlib import Path

from reportlab.graphics import renderSVG
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.shapes import Drawing, Line, String
from reportlab.lib.colors import HexColor


def read_csv(path):
    rows = list(csv.DictReader(line for line in path.read_text().splitlines() if not line.startswith("#")))
    for row in rows:
        for value in row.values():
            assert math.isfinite(float(value)), (path, row)
    return rows


def main():
    directory = Path(sys.argv[1])
    comparisons = read_csv(directory / "comparison.csv")
    filters = read_csv(directory / "filters.csv")
    costs = read_csv(directory / "cost.csv")
    assert (len(comparisons), len(filters), len(costs)) == (240, 160, 5)
    factors = [1, 2, 4, 8, 16]
    grouped = {factor: [r for r in comparisons if int(r["factor"]) == factor] for factor in factors}
    cost_for = {int(row["factor"]): row for row in costs}
    assert all(len(rows) == 48 for rows in grouped.values())
    build_note = (directory / "cost.csv").read_text().splitlines()[0][2:]
    table = ["| Factor | Worst full-band dBr | Worst 20 Hz–20 kHz dBr | Median 20 Hz–20 kHz dBr | Band cases <= -80 dBr | Local ns/base mono sample | Latency at 48 kHz |",
             "|---:|---:|---:|---:|---:|---:|---:|"]
    eligible = []
    for factor in factors:
        full_aliases = [float(row["alias_dbr"]) for row in grouped[factor]]
        aliases = [float(row["audio_band_alias_dbr"]) for row in grouped[factor]]
        passed = sum(value <= -80 for value in aliases)
        if passed == 48:
            eligible.append(factor)
        cost = cost_for[factor]
        table.append(f"| {factor}x | {max(full_aliases):.2f} | {max(aliases):.2f} | {statistics.median(aliases):.2f} | {passed}/48 | {float(cost['ns_per_base_mono_sample']):.1f} | {float(cost['latency_samples']) / 48:.3f} ms |")
    selected = [row for row in comparisons if float(row["sample_rate_hz"]) == 48000 and float(row["field_peak"]) == 1]
    detail = ["| Factor | ~50 Hz | ~1 kHz | ~7 kHz | ~10 kHz |", "|---:|---:|---:|---:|---:|"]
    for factor in factors:
        series = sorted((r for r in selected if int(r["factor"]) == factor), key=lambda r: float(r["requested_frequency_hz"]))
        detail.append("| " + str(factor) + "x | " + " | ".join(f"{float(row['audio_band_alias_dbr']):.2f}" for row in series) + " |")
    initial_convergence = max(float(row["reference_32v64_dbr"]) for row in comparisons)
    convergence = max(float(row["reference_last_dbr"]) for row in comparisons)
    extended = len({(r['sample_rate_hz'], r['frequency_hz'], r['field_peak']) for r in comparisons if int(r['reference_factor']) == 128})
    worst16 = max(grouped[16], key=lambda r: float(r['alias_dbr']))
    worst_note = (f"- The 16x worst full-band value is {float(worst16['alias_dbr']):.2f} dBr at base rate {float(worst16['sample_rate_hz']):g} Hz, nominal tone {float(worst16['requested_frequency_hz']):g} Hz and field peak {float(worst16['field_peak']):g}. Its strongest nonharmonic component is at {float(worst16['peak_alias_hz']):.2f} Hz. Both full-band and 20 Hz–20 kHz results are retained.")
    maximum_gain = max(abs(float(row["gain_db"])) for row in filters)
    maximum_phase = max(abs(float(row["delay_removed_phase_deg"])) for row in filters)
    decision = (f"The lowest tested factor meeting the provisional -80 dBr nonharmonic screen over **20 Hz–20 kHz** in all 48 cases is **{eligible[0]}x**."
                if eligible else "**No tested factor meets the provisional -80 dBr nonharmonic screen over 20 Hz–20 kHz in all 48 stress cases.**")
    notes = ["# Offline magnetic oversampling comparison v1", "",
             "Same core: `preisach-play-core-v1`, memory 0.35 / width 1.0. All results describe normalized H/M, not transformer audio or measured VTB1148 hardware.", "",
             "![Oversampling comparison](comparison.png)", "", "## Results", "", decision, "",
             "The -80 dBr screen is a comparison goal, not an audibility standard. Fundamental/harmonic accuracy and reference uncertainty must also be considered. No live plugin setting has changed.", "", *table, "",
             "The cost column includes FIR interpolation, all core updates, FIR decimation, input copies and a checksum. Preparation and FFT are excluded. Timings are local build measurements, not realtime deadline guarantees.", "", build_note, "",
             f"16x measured {float(cost_for[16]['ns_per_base_mono_sample']) / float(cost_for[1]['ns_per_base_mono_sample']):.1f} times the 1x cost in this run. A factor that meets the numerical screen is not automatically a practical production setting.", "",
             "## Aliases at 48 kHz, normalized field peak 1", "",
             "Values integrate nonharmonic power over 20 Hz–20 kHz, relative to the final reference's full in-band RMS magnetization. Nominal tone labels below are snapped to coherent frequencies recorded in the CSV.", "", *detail, "",
             "## Calibration and interpretation", "",
             f"- Worst initial 32x/64x reference disagreement: {initial_convergence:.2f} dBr. {extended} stimuli required a 64x/128x check; worst final disagreement: {convergence:.2f} dBr. The reference is approximate and very low errors are FFT-precision limited.",
             f"- Maximum absolute identity round-trip gain error over the measured 20 Hz–20 kHz probes: {maximum_gain:.6f} dB.",
             f"- Maximum identity phase error after removing reported FIR latency: {maximum_phase:.6f} degrees.",
             "- Full and passband residuals also include filter response and history/extremum sampling differences. They are not pure alias measurements.",
             "- Remaining aliases can overlap genuine harmonics; the nonharmonic-bin metric alone cannot certify an alias-free result.",
             worst_note,
             "- Nonharmonic artifacts can include sidebands from sampled turning-point history as well as folded harmonics.",
             "- Release profiling, voltage/flux/circuit coupling, and richer stimuli remain necessary before any production choice.", "",
             "[Method, tests and reproduction commands](../../MagneticOversamplingComparison.md).", ""]
    (directory / "README.md").write_text("\n".join(notes))

    ink = HexColor("#202e38")
    colors = [HexColor(c) for c in ("#147eaa", "#bd641b", "#278357", "#8056a7")]
    drawing = Drawing(1100, 520)
    drawing.add(String(28, 489, "Offline magnetic core: filtered oversampling", fontName="Helvetica-Bold", fontSize=20, fillColor=ink))
    drawing.add(String(28, 465, "48 kHz base | normalized field peak 1 | linear-phase FIR, -120 dB stopband target | live plugin unchanged", fontSize=11, fillColor=ink))
    alias = LinePlot()
    alias.x, alias.y, alias.width, alias.height = 80, 160, 385, 245
    alias.data = [[(math.log2(int(r["factor"])), float(r["audio_band_alias_dbr"])) for r in sorted(
        (row for row in selected if float(row["requested_frequency_hz"]) == frequency), key=lambda r: int(r["factor"]))]
        for frequency in (50, 1000, 7000, 10000)]
    alias.yValueAxis.valueMin, alias.yValueAxis.valueMax, alias.yValueAxis.valueStep = -150, 0, 30
    for i, color in enumerate(colors):
        alias.lines[i].strokeColor = color
        alias.lines[i].strokeWidth = 1.8
    threshold_y = alias.y + (70 / 150) * alias.height
    drawing.add(Line(alias.x, threshold_y, alias.x + alias.width, threshold_y, strokeColor=HexColor("#bfc7ce"), strokeDashArray=[4, 3]))
    drawing.add(String(alias.x + 5, threshold_y + 4, "-80 dBr comparison line", fontSize=9, fillColor=ink))
    drawing.add(String(80, 433, "20 Hz–20 kHz nonharmonic power (dBr)", fontName="Helvetica-Bold", fontSize=13, fillColor=ink))
    cpu = LinePlot()
    cpu.x, cpu.y, cpu.width, cpu.height = 635, 160, 385, 245
    cpu.data = [[(math.log2(factor), float(cost_for[factor]["ns_per_base_mono_sample"]) / 1000) for factor in factors]]
    ceiling = math.ceil(max(y for _, y in cpu.data[0]) / 5) * 5
    cpu.yValueAxis.valueMin, cpu.yValueAxis.valueMax, cpu.yValueAxis.valueStep = 0, max(5, ceiling), max(1, ceiling / 5)
    cpu.lines[0].strokeColor, cpu.lines[0].strokeWidth = ink, 1.8
    drawing.add(String(635, 433, "Local CPU cost (us/base mono sample)", fontName="Helvetica-Bold", fontSize=14, fillColor=ink))
    for chart in (alias, cpu):
        chart.joinedLines = 1
        chart.xValueAxis.valueMin, chart.xValueAxis.valueMax, chart.xValueAxis.valueStep = 0, 4, 1
        chart.xValueAxis.labelTextFormat = lambda value: f"{2 ** int(value)}x"
        for axis in (chart.xValueAxis, chart.yValueAxis):
            axis.labels.fontName, axis.labels.fontSize, axis.strokeColor = "Helvetica", 11, ink
        drawing.add(chart)
        drawing.add(String(chart.x + 110, 124, "Oversampling factor", fontSize=12, fillColor=ink))
    for i, (label, color) in enumerate(zip(("~50 Hz", "~1 kHz", "~7 kHz", "~10 kHz"), colors)):
        x = 80 + (i % 2) * 190
        y = 92 - (i // 2) * 23
        drawing.add(Line(x, y, x + 25, y, strokeColor=color, strokeWidth=2))
        drawing.add(String(x + 33, y - 4, label, fontSize=11, fillColor=ink))
    drawing.add(String(635, 88, "Median of five local runs; includes filters and core.", fontSize=11, fillColor=ink))
    drawing.add(String(635, 65, "Build/CPU details and latency are in the report table.", fontSize=11, fillColor=ink))
    drawing.add(String(28, 24, "Normalized magnetization proxy. References checked at 32x/64x, extended to 128x when needed; full-band results retained.", fontSize=11, fillColor=ink))
    output = directory / "comparison.svg"
    renderSVG.drawToFile(drawing, str(output))
    print(output)


if __name__ == "__main__":
    main()
