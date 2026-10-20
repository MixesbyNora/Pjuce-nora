"""Render captured offline validation CSVs. Requires ReportLab; no network."""
import csv
import math
import statistics
import sys
from collections import Counter
from pathlib import Path

from reportlab.graphics import renderPDF, renderSVG
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.shapes import Drawing, Line, Rect, String
from reportlab.lib.colors import HexColor

SIGNALS = ('low_high', 'close_high', 'six_tone', 'bandlimited_impulse', 'low_burst', 'high_burst', 'gaussian_pulse', 'bipolar_pulse', 'fast_attack_decay')
METHODS = ('turning8', 'point16')
LABELS = {'turning8': '8x turning-point', 'point16': 'Original 16x'}
INK = HexColor('#233548')
COLORS = [HexColor('#168071'), HexColor('#315c94'), HexColor('#7a8794')]


def rows(path):
    result = list(csv.DictReader(line for line in path.read_text().splitlines() if not line.startswith('#')))
    for row in result:
        for key, value in row.items():
            if key not in ('signal', 'method', 'product') and value:
                assert math.isfinite(float(value)), (path, key, value)
    return result


def reasons(row):
    if int(row['converged']) == 0:
        return ['reference unresolved']
    if float(row['identity_residual_dbr']) > -95 or float(row['reference_identity_dbr']) > -95:
        return ['identity calibration limited']
    failures = []
    if float(row['band_residual_dbr']) > -80: failures.append('band residual')
    if row['off_grid_dbr'] and float(row['off_grid_dbr']) > -80: failures.append('off-grid power')
    if abs(float(row['band_peak_error_db'])) > 0.1: failures.append('audio-band peak error')
    if row['tail_error_dbr'] and float(row['tail_error_dbr']) > -80: failures.append('tail mean error')
    if row['tail_drift_dbr'] and float(row['tail_drift_dbr']) > -100: failures.append('tail drift')
    return failures


def canvas(width, height, title, subtitle):
    drawing = Drawing(width, height)
    drawing.add(Rect(0, 0, width, height, fillColor=HexColor('#ffffff'), strokeColor=None))
    drawing.add(String(28, height - 34, title, fontName='Helvetica-Bold', fontSize=19, fillColor=INK))
    drawing.add(String(28, height - 58, subtitle, fontName='Helvetica', fontSize=10, fillColor=INK))
    return drawing


def save(drawing, directory, stem):
    renderSVG.drawToFile(drawing, str(directory / f'{stem}.svg'))
    renderPDF.drawToFile(drawing, str(directory / f'{stem}.pdf'))


def add_chart(drawing, series, x, y, width, height, title, x_range=None, y_range=None):
    chart = LinePlot()
    chart.x, chart.y, chart.width, chart.height = x, y, width, height
    chart.data, chart.joinedLines = series, 1
    for i, color in enumerate(COLORS):
        chart.lines[i].strokeColor, chart.lines[i].strokeWidth = color, 1.2
    for axis in (chart.xValueAxis, chart.yValueAxis):
        axis.labels.fontName, axis.labels.fontSize = 'Helvetica', 9
    if x_range:
        chart.xValueAxis.valueMin, chart.xValueAxis.valueMax = x_range
    if y_range:
        chart.yValueAxis.valueMin, chart.yValueAxis.valueMax = y_range
    drawing.add(chart)
    drawing.add(String(x, y + height + 18, title, fontName='Helvetica-Bold', fontSize=11, fillColor=INK))


def main(directory):
    data, stimuli = rows(directory / 'comparison.csv'), rows(directory / 'stimuli.csv')
    convergence, products = rows(directory / 'convergence.csv'), rows(directory / 'intermodulation.csv')
    assert len(data) == 216 and len(stimuli) == 108 and len(convergence) >= 108
    assert len({(r['signal'], r['sample_rate_hz'], r['nominal_field_peak'], r['method']) for r in data}) == 216
    grouped = {method: [r for r in data if r['method'] == method] for method in METHODS}
    assert all(len(r) == 108 for r in grouped.values())
    counts = {method: Counter(reason for row in subset for reason in reasons(row)) for method, subset in grouped.items()}
    passes = {method: sum(not reasons(row) for row in subset) for method, subset in grouped.items()}
    unresolved = sum(int(row['converged']) == 0 for row in grouped['turning8'])
    paired = {(r['signal'], r['sample_rate_hz'], r['nominal_field_peak']): r for r in grouped['point16']}
    improvements = sum(float(r['band_residual_dbr']) < float(paired[(r['signal'], r['sample_rate_hz'], r['nominal_field_peak'])]['band_residual_dbr']) for r in grouped['turning8'] if int(r['converged']))
    outcome = (f"8x passes **{passes['turning8']}/108** composite screens; original 16x passes **{passes['point16']}/108**. **{unresolved}** stimuli have unresolved audio-band references.")
    notes = ['# Offline magnetic multitone and transient validation', '', outcome, '',
             'The live plugin and favorite tanh voicing remain unchanged. These normalized H-to-M experiments evaluate the existing kernels; no candidate was retuned to pass the grid.', '',
             'The 8x candidate remains offline. Its aggregate advantage does not establish a universal replacement for 16x; the quality-screen misses below remain work to resolve.', '',
             '![Validation grid](validation.png)', '', '## Results', '',
             '| Signal | Method | Pass / 12 | Worst band residual dBr | Worst off-grid dBr | Max absolute band peak error dB | Worst tail mean error dBr |',
             '|---|---|---:|---:|---:|---:|---:|']
    for signal in SIGNALS:
        for method in METHODS:
            subset = [r for r in grouped[method] if r['signal'] == signal]
            def maximum(key, absolute=False):
                values = [abs(float(r[key])) if absolute else float(r[key]) for r in subset if r[key]]
                return (f'{max(values):.6f}' if key == 'band_peak_error_db' else f'{max(values):.2f}') if values else 'n/a'
            notes.append(f"| {signal} | {LABELS[method]} | {sum(not reasons(r) for r in subset)}/12 | {maximum('band_residual_dbr')} | {maximum('off_grid_dbr')} | {maximum('band_peak_error_db', True)} | {maximum('tail_error_dbr')} |")
    notes += ['', 'Pass requires a converged reference; identity errors <= -95 dBr; band residual <= -80 dBr; applicable off-grid power <= -80 dBr; absolute audio-band peak error <= 0.1 dB; applicable tail error <= -80 dBr and drift <= -100 dBr. These are provisional engineering screens, not audibility standards. Missing metrics are inapplicable, never implied passes.', '',
              '## What the results support', '',
              f'- The 8x candidate has a lower band residual than original 16x in {improvements}/{108 - unresolved} converged stimuli. This compares errors against the high-rate reference, not one candidate against the other.',
              '- Multitone off-grid power excludes genuine intermodulation on the three-bin lattice. Complex residuals and selected product-line errors also expose differences that land on that lattice.',
              '- Transient mean-tail error compares retained magnetization with the reference. A nonzero tail is legitimate for this magnetic model; late drift is checked separately.',
              '- Input reconstruction, FIR response, aliasing and history errors all contribute to residuals. The transient residual is not a pure alias measurement.', '',
              '### Screen misses by reason', '', '| Reason | 8x turning-point | Original 16x |', '|---|---:|---:|']
    for reason in ('reference unresolved', 'identity calibration limited', 'band residual', 'off-grid power', 'audio-band peak error', 'tail mean error', 'tail drift'):
        notes.append(f"| {reason} | {counts['turning8'][reason]} | {counts['point16'][reason]} |")
    notes += ['', 'Reasons can overlap within a case. Unresolved references or identity calibration limits are separated from quality misses.', '',
              '## 8x screen misses', '',
              '| Signal | Base rate Hz | Nominal H scale | Band residual dBr | Off-grid dBr | Reason |',
              '|---|---:|---:|---:|---:|---|']
    for row in grouped['turning8']:
        failure = reasons(row)
        if failure:
            off_grid = f"{float(row['off_grid_dbr']):.2f}" if row['off_grid_dbr'] else 'n/a'
            notes.append(f"| {row['signal']} | {row['sample_rate_hz']} | {row['nominal_field_peak']} | {float(row['band_residual_dbr']):.2f} | {off_grid} | {', '.join(failure)} |")
    notes += ['',
              '## Reference and calibration', '',
              f"- Final reference factors: {dict(sorted(Counter(int(r['reference_factor']) for r in grouped['turning8']).items()))}. Raw convergence steps are in convergence.csv.",
              f"- Worst converged reference band disagreement: {max(float(r['reference_difference_dbr']) for r in grouped['turning8'] if int(r['converged'])):.2f} dBr.",
              f"- Worst reference identity error versus analytic input: {max(float(r['reference_identity_dbr']) for r in grouped['turning8']):.2f} dBr.",
              f"- Worst candidate identity error versus reference: 8x {max(float(r['identity_residual_dbr']) for r in grouped['turning8']):.2f} dBr; 16x {max(float(r['identity_residual_dbr']) for r in grouped['point16']):.2f} dBr.",
              f"- Full-band reference agreement at the chosen comparison step: {sum(int(r['reference_full_agreement']) for r in grouped['turning8'])}/108. Near-Nyquist transition-region differences remain in the raw diagnostics; audio-band convergence does not certify full-Nyquist accuracy.",
              '- Reference input is evaluated analytically at the internal rate. The original core and real FIR decimation are checked at 32x/64x, then up to 1024x when necessary. No fitted gain, fitted delay or DC removal is used.', '',
              '## Diagnostic plots', '', 'Each plot selects the worst converged case by 8x band residual. Displayed waveforms and peak errors use DC–20 kHz; full-band diagnostics remain in the CSVs.', '',
              '![Worst transient](transient.png)', '', '![Worst multitone](spectrum.png)', '',
              '## Scope and raw data', '',
              'This grid evaluates nine signals at 44.1/48/96/192 kHz and nominal field scales 0.05/1/4. It does not establish performance on all music, physical transformer voltages, circuit coupling or measured hardware. The earlier CPU comparison is unchanged and was not rerun here.', '',
              '[Method and reproduction](../../MagneticMultitoneTransientValidation.md). Raw files: [comparisons](comparison.csv), [stimuli](stimuli.csv), [convergence](convergence.csv), [IMD lines](intermodulation.csv), [transient trace](worst_transient.csv), [multitone spectrum](worst_multitone.csv), [provenance](manifest.json).', '']
    (directory / 'README.md').write_text('\n'.join(notes))

    drawing = canvas(1180, 690, 'Offline multitone and transient validation', 'Cell value: DC-20 kHz residual dBr. Color: composite screen. No live plugin changes; no audibility claim.')
    lookup = {(r['method'], r['signal'], int(float(r['sample_rate_hz'])), float(r['nominal_field_peak'])): r for r in data}
    columns = [(rate, peak) for rate in (44100, 48000, 96000, 192000) for peak in (0.05, 1, 4)]
    fills = [HexColor(c) for c in ('#d0e9df', '#f5dbc2', '#dfe4ec')]
    for panel, method in enumerate(METHODS):
        top = 570 - panel * 275
        drawing.add(String(28, top + 32, LABELS[method], fontName='Helvetica-Bold', fontSize=14, fillColor=INK))
        for col, (rate, peak) in enumerate(columns):
            drawing.add(String(215 + col * 75, top + 13, f'{rate / 1000:g}k / {peak:g}', fontName='Helvetica', fontSize=8, fillColor=INK))
        for row_index, signal in enumerate(SIGNALS):
            y = top - row_index * 24
            drawing.add(String(28, y - 13, signal, fontName='Helvetica', fontSize=10, fillColor=INK))
            for col, (rate, peak) in enumerate(columns):
                row = lookup[(method, signal, rate, peak)]
                failure = reasons(row)
                fill = fills[2 if any(r in failure for r in ('reference unresolved', 'identity calibration limited')) else 1 if failure else 0]
                x = 205 + col * 75
                drawing.add(Rect(x, y - 20, 73, 22, fillColor=fill, strokeColor=None))
                drawing.add(String(x + 12, y - 13, f"{float(row['band_residual_dbr']):.1f}", fontName='Helvetica', fontSize=9, fillColor=INK))
    for i, text in enumerate(('Pass', 'Screen miss', 'Reference / calibration limited')):
        x = 205 + i * 255
        drawing.add(Rect(x, 30, 18, 14, fillColor=fills[i], strokeColor=None))
        drawing.add(String(x + 26, 33, text, fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, directory, 'validation')

    trace = rows(directory / 'worst_transient.csv')
    annotation = (directory / 'worst_transient.csv').read_text().splitlines()[0][2:]
    drawing = canvas(1180, 540, 'Worst converged transient', annotation)
    peak_row = max(trace, key=lambda r: abs(float(r['input_h'])))
    center = float(peak_row['time_ms'])
    zoom = [r for r in trace if center - 2 <= float(r['time_ms']) <= center + 8]
    # Series order follows green candidate, blue baseline, gray reference.
    add_chart(drawing, [[(float(r['time_ms']), float(r[key])) for r in zoom] for key in ('turning8_band_m', 'point16_band_m', 'reference_band_m')], 65, 135, 460, 285, 'DC-20 kHz magnetization (M), time in ms')
    add_chart(drawing, [[(float(r['time_ms']), float(r[key])) for r in trace] for key in ('error8_band_m', 'error16_band_m')], 650, 135, 460, 285, 'DC-20 kHz error (M), time in ms')
    for i, label in enumerate(('8x turning-point', 'Original 16x', 'High-rate reference')):
        x = 65 + i * 330
        drawing.add(Line(x, 65, x + 26, 65, strokeColor=COLORS[i], strokeWidth=2))
        drawing.add(String(x + 34, 61, label, fontName='Helvetica', fontSize=11, fillColor=INK))
    drawing.add(String(28, 20, 'Tail offset is measured against reference remanence. Waveforms/errors use the same finite-window DC-20 kHz mask; full-band traces remain in CSV.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, directory, 'transient')

    spectral = [r for r in rows(directory / 'worst_multitone.csv') if 20 <= float(r['frequency_hz']) <= 20000]
    annotation = (directory / 'worst_multitone.csv').read_text().splitlines()[0][2:]
    drawing = canvas(1180, 540, 'Worst converged multitone', annotation)
    add_chart(drawing, [[(float(r['frequency_hz']) / 1000, max(-170.0, min(10.0, float(r[key])))) for r in spectral] for key in ('turning8_dbr', 'point16_dbr', 'reference_dbr')], 65, 135, 460, 285, 'Output line RMS / reference RMS (dBr)', (0, 20), (-170, 10))
    add_chart(drawing, [[(float(r['frequency_hz']) / 1000, max(-170.0, min(10.0, float(r[key])))) for r in spectral] for key in ('error8_dbr', 'error16_dbr')], 650, 135, 460, 285, 'Complex line error / reference RMS (dBr)', (0, 20), (-170, 10))
    for x in (65, 650): drawing.add(String(x + 170, 93, 'Frequency (kHz)', fontName='Helvetica', fontSize=10, fillColor=INK))
    for i, label in enumerate(('8x turning-point', 'Original 16x', 'High-rate reference')):
        x = 65 + i * 330
        drawing.add(Line(x, 57, x + 26, 57, strokeColor=COLORS[i], strokeWidth=2))
        drawing.add(String(x + 34, 53, label, fontName='Helvetica', fontSize=11, fillColor=INK))
    drawing.add(String(28, 20, 'Genuine IMD and overlapping aliases can share bins. Complex residuals supplement off-grid power. Display floor: -170 dBr.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, directory, 'spectrum')
    print(outcome)


if __name__ == '__main__':
    main(Path(sys.argv[1]))
