"""Summarize Tests-only Release antialiasing exports; requires ReportLab."""

import csv
import math
import statistics
import sys
from pathlib import Path

from reportlab.graphics import renderPDF, renderSVG
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.shapes import Drawing, Line, Rect, String
from reportlab.lib.colors import HexColor

FACTORS = (1, 2, 4, 8, 16)
METHODS = ("point", "interval", "turning")
LABELS = {"point": "Original point", "interval": "Interval mean", "turning": "Quadratic extrema"}


def read_csv(path):
    rows = list(csv.DictReader(line for line in path.read_text().splitlines() if not line.startswith("#")))
    assert rows, path
    assert all(math.isfinite(float(value)) for row in rows for value in row.values()), path
    return rows


def main(directory):
    data = {method: read_csv(directory / method / "comparison.csv") for method in METHODS}
    filters = {method: read_csv(directory / method / "filters.csv") for method in METHODS}
    costs = {method: {int(row["factor"]): row for row in read_csv(directory / method / "cost.csv")} for method in METHODS}
    profiling = read_csv(directory / "profiling.csv")
    assert len(profiling) == 135
    profiles = {}
    for index, method in enumerate(METHODS):
        profiles[method] = {}
        for stimulus in range(3):
            profiles[method][stimulus] = {}
            for factor in FACTORS:
                rows = [r for r in profiling if int(r['method_index']) == index and int(r['stimulus_index']) == stimulus and int(r['factor']) == factor]
                assert len(rows) == 3 and {int(r['sweep']) for r in rows} == {0, 1, 2}
                profiles[method][stimulus][factor] = statistics.median(float(r['ns_per_base_mono_sample']) for r in rows)
                if stimulus == 0:
                    costs[method][factor]['ns_per_base_mono_sample'] = profiles[method][stimulus][factor]
    grouped = {}
    keys = None
    for method in METHODS:
        assert len(data[method]) == 240 and len(filters[method]) == 160 and len(costs[method]) == 5
        current = {(r['sample_rate_hz'], r['frequency_hz'], r['field_peak'], r['factor']) for r in data[method]}
        assert len(current) == 240 and (keys is None or keys == current)
        keys = current
        grouped[method] = {f: [r for r in data[method] if int(r["factor"]) == f] for f in FACTORS}
        assert all(len(rows) == 48 for rows in grouped[method].values())

    def worst(method, factor, key="audio_band_alias_dbr"):
        return max(float(r[key]) for r in grouped[method][factor])

    def passes(method, factor):
        return sum(float(r["audio_band_alias_dbr"]) <= -80 for r in grouped[method][factor])

    table = ["| Method | Factor | Worst 20 Hz–20 kHz dBr | Cases <= -80 | Worst passband residual dBr | Max abs fundamental gain error | Max abs phase error | Median local ns/mono sample | Latency at 48 kHz |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for method in METHODS:
        for factor in FACTORS:
            rows = grouped[method][factor]
            gain = max(abs(float(r['gain_error_db'])) for r in rows)
            phase = max(abs(float(r['phase_error_deg'])) for r in rows)
            cost = costs[method][factor]
            table.append(f"| {LABELS[method]} | {factor}x | {worst(method, factor):.2f} | {passes(method, factor)}/48 | {worst(method, factor, 'passband_residual_dbr'):.2f} | {gain:.6f} dB | {phase:.6f} deg | {float(cost['ns_per_base_mono_sample']):.1f} | {float(cost['latency_samples']) / 48:.4f} ms |")

    eligible = [(method, factor) for method in METHODS for factor in FACTORS if passes(method, factor) == 48]
    fastest = min(eligible, key=lambda pair: float(costs[pair[0]][pair[1]]['ns_per_base_mono_sample']))
    recommended = f"The cheapest tested all-grid screen pass in this run is **{LABELS[fastest[0]]} at {fastest[1]}x**. This is a research result, not a live-plugin selection."
    baseline16 = float(costs['point'][16]['ns_per_base_mono_sample'])
    notes = ["# Magnetic antialiasing follow-up: Release", "",
             "Normalized H-to-M research, Apple M2 Pro / arm64 / Release (-O3, LTO). Live plugin, tanh favorite, parameters, UI and latency unchanged.", "",
             "![Release comparison](comparison.png)", "", "## Results", "", recommended, "", *table, "",
             "The -80 dBr screen integrates nonharmonic power over 20 Hz–20 kHz, relative to high-rate reference RMS below base Nyquist. It is not an audibility standard; genuine-harmonic overlap is excluded by this metric, and very low values approach float FFT precision. Full-band aliases, residuals and reference checks remain in each CSV.", "",
             "## Interpretation", "",
             f"- Original 16x measured {baseline16 / 1000:.3f} us per mono input sample. It remains the unchanged numerical baseline.",
             f"- Interval 8x measured {float(costs['interval'][8]['ns_per_base_mono_sample']) / baseline16:.2f} times the original 16x cost and passed {passes('interval', 8)}/48 cases. Averaging's treble loss remains in the gain/residual results; no compensation was used.",
             f"- Quadratic-extrema 8x passed {passes('turning', 8)}/48 cases; its worst band result was {worst('turning', 8):.2f} dBr. Its cost was {float(costs['turning'][8]['ns_per_base_mono_sample']) / baseline16:.2f} times original 16x.",
             f"- Worst passband complex residual: {worst('turning', 8, 'passband_residual_dbr'):.2f} dBr at quadratic-extrema 8x versus {worst('point', 16, 'passband_residual_dbr'):.2f} dBr at original 16x. This includes DC/harmonic error, filter response and history differences, rather than only nonharmonic bins.",
             "- Turning-point reconstruction estimates missed extrema with a local parabola. It changes sampled history and is not exact for arbitrary audio. Improvements on coherent tones need multitone/transient validation before any integration.",
             "- Interval integration preserves endpoint history but cannot restore unsampled reversals. It tests averaging separately from reversal reconstruction.",
             "- Summary timings above use the median of three matched-binary sweeps, each itself a median of five x 32768 mono base samples, H peak 4, repeated 256-sample sine. They include filtering/core/copy/checksum, exclude prepare/FFT, and do not establish host deadline safety or workload-independent speed. Factor order alternates and method order rotates across sweeps.", "",
             "## Matched-binary cost stress checks", "",
             "The profiling.csv export was run sequentially after builds/tests, with no other experiment running. The per-method cost.csv files retain the initial grid-export timings; the table/plot use profiling.csv. Values below are ns per mono input sample, median across the three sweeps.", "",
             "| Stimulus | Original 8x | Original 16x | Interval 8x | Quadratic extrema 8x | Cost saving vs original 16x |",
             "|---|---:|---:|---:|---:|---:|"]
    for stimulus, label in enumerate(('H peak 4, slow sine (187.5 Hz at 48k)', 'H peak 0.05, fast sine (9937.5 Hz at 48k)', 'H peak 4, seeded repeating noise')):
        base = profiles['point'][stimulus][16]
        refined = profiles['turning'][stimulus][8]
        notes.append(f"| {label} | {profiles['point'][stimulus][8]:.1f} | {base:.1f} | {profiles['interval'][stimulus][8]:.1f} | {refined:.1f} | {100 * (1 - refined / base):.1f}% |")
    notes += ["",
             "## The six original 8x failures", "",
             "Values below use the same 8x filters. Normalized H peak is not audio dBFS.", "",
             "| Base rate | Actual tone | H peak | Original dBr | Interval dBr | Quadratic extrema dBr |",
             "|---:|---:|---:|---:|---:|---:|"]
    lookup = {method: {(r['sample_rate_hz'], r['frequency_hz'], r['field_peak'], r['factor']): r for r in data[method]} for method in METHODS}
    for row in grouped['point'][8]:
        if float(row['audio_band_alias_dbr']) <= -80:
            continue
        key = (row['sample_rate_hz'], row['frequency_hz'], row['field_peak'], row['factor'])
        values = [float(lookup[m][key]['audio_band_alias_dbr']) for m in METHODS]
        notes.append(f"| {float(row['sample_rate_hz']):g} | {float(row['frequency_hz']):.2f} | {float(row['field_peak']):g} | " + ' | '.join(f'{v:.2f}' for v in values) + ' |')
    convergence = max(float(row['reference_last_dbr']) for rows in data.values() for row in rows)
    notes += ["", "## Calibration and limits", "",
              f"Worst final analytic-reference convergence disagreement: {convergence:.2f} dBr. References remain 32x/64x with adaptive 128x as in the original experiment.", ""]
    for method in METHODS:
        gain = max(abs(float(row['gain_db'])) for row in filters[method])
        explanation = " Includes its expected two-tap response across all factors, including 1x." if method == 'interval' else ""
        notes.append(f"- {LABELS[method]}: maximum absolute identity gain error over the measured 20 Hz–20 kHz probes: {gain:.6f} dB.{explanation}")
    notes += ["", "At elevated sample rates, ultrasonic transition-band aliases can dominate full-band totals; the screen above does not include those frequencies. Magnetic constitutive behavior alone is not a transformer voltage model or a hardware fit.", "",
              "[Method, derivation, limitations and reproduction](../../MagneticAntialiasingFollowup.md). Raw exports are in point/, interval/ and turning/. The original Debug report is preserved in ../magnetic_oversampling_v1/.", ""]
    (directory / 'README.md').write_text('\n'.join(notes))

    ink = HexColor('#253343')
    colors = [HexColor(c) for c in ('#315c94', '#b76623', '#168071')]
    drawing = Drawing(1120, 500)
    drawing.add(Rect(0, 0, 1120, 500, fillColor=HexColor('#ffffff'), strokeColor=None))
    drawing.add(String(28, 468, 'Magnetic core: Release antialiasing experiments', fontName='Helvetica-Bold', fontSize=20, fillColor=ink))
    drawing.add(String(28, 444, '48 stimuli | normalized H/M | Apple M2 Pro | Release -O3, LTO | live plugin unchanged', fontSize=11, fillColor=ink))
    for panel, (title, y_min, y_max, y_step) in enumerate((('Worst 20 Hz-20 kHz nonharmonic power (dBr)', -150, 0, 30), ('Local cost (us / mono input sample)', 0, max(1, math.ceil(max(float(r['ns_per_base_mono_sample']) / 1000 for c in costs.values() for r in c.values()))), 1))):
        chart = LinePlot()
        chart.x, chart.y, chart.width, chart.height = 82 + panel * 550, 142, 380, 230
        chart.data = [[(math.log2(f), worst(m, f) if panel == 0 else float(costs[m][f]['ns_per_base_mono_sample']) / 1000) for f in FACTORS] for m in METHODS]
        chart.joinedLines = 1
        chart.xValueAxis.valueMin, chart.xValueAxis.valueMax, chart.xValueAxis.valueStep = 0, 4, 1
        chart.xValueAxis.labelTextFormat = lambda v: f'{2 ** int(v)}x'
        chart.yValueAxis.valueMin, chart.yValueAxis.valueMax, chart.yValueAxis.valueStep = y_min, y_max, y_step
        for axis in (chart.xValueAxis, chart.yValueAxis):
            axis.labels.fontName, axis.labels.fontSize = 'Helvetica', 11
        for i, color in enumerate(colors):
            chart.lines[i].strokeColor, chart.lines[i].strokeWidth = color, 2
        drawing.add(chart)
        drawing.add(String(chart.x - 25, 400, title, fontName='Helvetica-Bold', fontSize=13, fillColor=ink))
        drawing.add(String(chart.x + 120, 105, 'Oversampling factor', fontSize=11, fillColor=ink))
        if panel == 0:
            y = chart.y + 70 / 150 * chart.height
            drawing.add(Line(chart.x, y, chart.x + chart.width, y, strokeColor=HexColor('#87939e'), strokeDashArray=[4, 4]))
            drawing.add(String(chart.x + 8, y + 5, '-80 dBr provisional screen', fontSize=10, fillColor=ink))
    for i, method in enumerate(METHODS):
        x = 82 + i * 310
        drawing.add(Line(x, 67, x + 26, 67, strokeColor=colors[i], strokeWidth=2))
        drawing.add(String(x + 34, 63, LABELS[method], fontSize=12, fillColor=ink))
    drawing.add(String(28, 22, 'Numerical comparison, not an audibility or hardware-realism claim. Full residual, phase, latency and CSV results accompany this plot.', fontSize=10, fillColor=ink))
    renderSVG.drawToFile(drawing, str(directory / 'comparison.svg'))
    renderPDF.drawToFile(drawing, str(directory / 'comparison.pdf'))
    print(recommended)


if __name__ == '__main__':
    main(Path(sys.argv[1]))
