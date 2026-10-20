"""Render the offline turning-point refinement capture; requires ReportLab/NumPy."""
import csv
import math
import sys
from pathlib import Path
import numpy as np

from render_magnetic_validation_report import canvas, save, add_chart, INK, COLORS
from reportlab.graphics.shapes import Line, Rect, String
from reportlab.lib.colors import HexColor

METHODS = ('point16', 'quadratic8', 'quadratic16', 'quartic8', 'quartic16', 'quartic32')
SIGNALS = ('low_high', 'close_high', 'six_tone', 'bandlimited_impulse', 'low_burst', 'high_burst', 'gaussian_pulse', 'bipolar_pulse', 'fast_attack_decay')


def read(path):
    rows = list(csv.DictReader(path.open()))
    for row in rows:
        for key, value in row.items():
            if value and key not in ('method', 'signal'):
                assert math.isfinite(float(value)), (path, key, value)
    return rows


def main(root):
    data, timings = read(root / 'comparison.csv'), read(root / 'timing.csv')
    groups = {method: [r for r in data if r['method'] == method] for method in METHODS}
    cases = len(groups[METHODS[0]])
    assert cases in (24, 108)
    assert all(len(subset) == cases for subset in groups.values())
    assert len({(r['method'], r['signal'], r['sample_rate_hz'], r['field_peak']) for r in data}) == len(data)
    assert all(float(r['reference_difference_dbr']) <= -100 and float(r['reference_band_max_error']) <= .0001 for r in data)
    notes = ['# Magnetic artifact refinement', '',
             'Offline H-to-M numerical research only. Live audio, parameters, voicing, editor, meters and latency are unchanged.', '',
             'Five-point quartic extrema use two internal samples of lookahead. The original point and three-point quadratic methods are retained as controls. Coefficients, play operators and model parameters are unchanged.', '',
             '![Comparison screens](refinement.png)', '',
             '| Method | Pass / stimuli | Worst DC-20 kHz residual dBr | Worst off-grid dBr |',
             '|---|---:|---:|---:|']
    for method, subset in groups.items():
        off = [float(r['off_grid_dbr']) for r in subset if r['off_grid_dbr']]
        notes.append(f"| {method} | {sum(int(r['pass']) for r in subset)}/{cases} | {max(float(r['band_residual_dbr']) for r in subset):.2f} | {max(off):.2f} |")
    baseline_times = {r['stimulus']: float(r['median_ns_per_base_sample']) for r in timings if r['method'] == 'point16'}
    overhead = [100 * (float(r['median_ns_per_base_sample']) / baseline_times[r['stimulus']] - 1) for r in timings if r['method'] == 'quartic16']
    if all(int(r['pass']) for r in groups['quartic16']):
        notes += ['', '## Interpretation', '',
                  f'Quartic 16x clears every applicable screen in this {cases}-stimulus grid, including both targeted artifact families. It is the next offline candidate; this does not qualify it for live deployment or establish a hardware match.', '',
                  'Quartic 8x clears all 12 impulse cases but misses four close-tone cases. Improving extrema alone does not clear that pattern at 8x. Quadratic 16x clears the close-tone cases but misses one impulse case: both reconstruction order and internal rate matter here.', '',
                  f'Quartic 16x costs {min(overhead):.1f}% to {max(overhead):.1f}% more than original point 16x in this local run. Quartic 32x also clears the screens but costs about twice original 16x. Its extra error margin is useful diagnostically; some residuals approach the reference-convergence floor, so small rankings there are uncertain.', '',
                  'This is a promising reduction of the two measured error patterns, not elimination of aliasing for arbitrary audio. Higher-frequency/full-Nyquist differences remain in the raw diagnostics.']
    notes += ['', '## Per signal', '', '| Signal | Method | Pass / 12 | Worst band residual dBr |', '|---|---|---:|---:|']
    for signal in SIGNALS:
        for method in METHODS:
            subset = [r for r in groups[method] if r['signal'] == signal]
            if subset:
                notes.append(f"| {signal} | {method} | {sum(int(r['pass']) for r in subset)}/{len(subset)} | {max(float(r['band_residual_dbr']) for r in subset):.2f} |")
    notes += ['', '## Release CPU profile', '',
              'Median of five 32,768-sample mono runs, including FIR/core/copies, excluding prepare and FFT. Sequential runs on this Mac; microbenchmark variability applies. Stimulus 0 = loud slow sine, 1 = quiet high sine, 2 = deterministic noise. Release uses -O3 and LTO.', '',
              '| Method | Slow sine ns/sample | Quiet high sine | Noise |', '|---|---:|---:|---:|']
    for method in METHODS:
        subset = sorted((r for r in timings if r['method'] == method), key=lambda r: int(r['stimulus']))
        assert len(subset) == 3
        notes.append('| ' + method + ' | ' + ' | '.join(f"{float(r['median_ns_per_base_sample']):.1f}" for r in subset) + ' |')
    notes += ['', '## Targeted impulse state error', '',
              '48 kHz, H scale 1. Late mean difference over the final 1024 samples is a retained-state diagnostic. The impulse contains periodic sidelobes; this is not a silent-tail measurement and the reference need not be zero.', '',
              '| Method | Band residual dBr | Late mean error M |', '|---|---:|---:|']
    for method in METHODS:
        row = next(r for r in groups[method] if r['signal'] == 'bandlimited_impulse' and float(r['sample_rate_hz']) == 48000 and float(r['field_peak']) == 1)
        notes.append(f"| {method} | {float(row['band_residual_dbr']):.2f} | {float(row['late_mean_error_m']):+.4e} |")
    notes += ['', '![Impulse error](impulse.png)', '', '![Close-tone spectrum](spectrum.png)', '', '## Screen misses', '',
              '| Method | Signal | Rate | H scale | Band residual dBr | Off-grid dBr |', '|---|---|---:|---:|---:|---:|']
    for row in data:
        if not int(row['pass']):
            off = f"{float(row['off_grid_dbr']):.2f}" if row['off_grid_dbr'] else 'n/a'
            notes.append(f"| {row['method']} | {row['signal']} | {float(row['sample_rate_hz']):g} | {float(row['field_peak']):g} | {float(row['band_residual_dbr']):.2f} | {off} |")
    notes += ['', '## Method and limits', '',
              'The prior validation stimuli, independent analytic high-rate references, adaptive DC-20 kHz convergence, fractional alignment and screens are unchanged. References must converge below -100 dBr with band maximum error <= 1e-4 and applicable silent-tail error below -100 dBr. Identity reconstruction must be below -95 dBr. Pass requires band residual and applicable off-grid power <= -80 dBr, absolute band peak error <= 0.1 dB, applicable tail error <= -80 dBr and tail drift <= -100 dBr.', '',
              'Full-band residual and separate full-band reference agreement remain in comparison.csv. Band convergence does not guarantee full-Nyquist accuracy. Off-grid power excludes the genuine IMD lattice but cannot reveal aliases that overlap genuine products. Numerical reference agreement does not establish audibility or hardware accuracy.', '',
              'The quartic assumes at most one stationary point per internal interval and uses 20 bounded bisection steps when endpoint derivatives change sign. It is a local polynomial, not exact bandlimited reconstruction. Actual sample startup guards prevent fabricated reset history. No DC removal, output correction, tuned model constants or stimulus-dependent DSP is used.', '',
              'Impulse plotting uses a double-precision NumPy FFT with the same finite-window DC-20 kHz mask; measured CSV metrics come from the tested C++ implementation. Raw impulse/close-tone traces and the complex spectral-error CSV remain available.', '',
              'Data: [all comparisons](comparison.csv), [timings](timing.csv), [impulse trace](bandlimited_impulse.csv), [close-tone trace](close_high.csv), [complex line errors](close_high_spectrum.csv), [provenance](manifest.json). The unchanged control outputs/final reference factors are cross-checked against [validation v1](../magnetic_validation_v1/README.md), which retains the original convergence history.', '']
    (root / 'README.md').write_text('\n'.join(notes))

    drawing = canvas(1180, 400, 'Turning-point refinement: comparison screens',
                     f'{cases} stimuli per method. Green = passes every applicable screen; orange = one or more misses.')
    x0, y0, cell = 145, 255, 9
    ordered = sorted(groups[METHODS[0]], key=lambda r: (SIGNALS.index(r['signal']), float(r['sample_rate_hz']), float(r['field_peak'])))
    for i, method in enumerate(METHODS):
        y = y0 - i * 27
        drawing.add(String(28, y + 4, method, fontName='Helvetica', fontSize=11, fillColor=INK))
        lookup = {(r['signal'], r['sample_rate_hz'], r['field_peak']): r for r in groups[method]}
        for j, row in enumerate(ordered):
            item = lookup[(row['signal'], row['sample_rate_hz'], row['field_peak'])]
            drawing.add(Rect(x0 + j * cell, y, cell - .7, 17, strokeColor=None,
                             fillColor=HexColor('#268b77' if int(item['pass']) else '#d18137')))
        drawing.add(String(x0 + cases * cell + 10, y + 4, f"{sum(int(r['pass']) for r in groups[method])}/{cases}", fontName='Helvetica', fontSize=10, fillColor=INK))
    for j in range(0, cases, 12):
        drawing.add(String(x0 + j * cell, y0 + 32, ordered[j]['signal'].replace('bandlimited_', '')[:12], fontName='Helvetica', fontSize=8, fillColor=INK))
    drawing.add(String(28, 48, 'Within each signal: 44.1/48/96/192 kHz; H scales 0.05/1/4. Same independent reference and screens as validation v1.', fontName='Helvetica', fontSize=10, fillColor=INK))
    drawing.add(String(28, 27, 'Offline research only. A pass is numerical reference agreement over DC-20 kHz, not a calibrated transformer or audibility claim.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, root, 'refinement')

    trace = read(root / 'bandlimited_impulse.csv')
    chosen = ('quadratic8', 'quartic16', 'quartic32')
    drawing = canvas(1180, 530, 'Impulse: persistent magnetic-state error', '48 kHz, H scale 1. Left: DC-20 kHz error. Right: raw late-state error. Reference is independently converged over DC-20 kHz.')
    center = max(trace, key=lambda r: abs(float(r['input'])))
    center_ms = float(center['sample']) / 48
    early = [r for r in trace if center_ms - .25 <= float(r['sample']) / 48 <= center_ms + 1]
    late = [r for r in trace if float(r['sample']) / 48 >= center_ms + 2]
    band_errors = {}
    for key in chosen:
        error = np.array([float(r[key]) - float(r['reference']) for r in trace])
        bins = np.fft.rfft(error)
        bins[int(20000 * len(trace) / 48000) + 1:] = 0
        band_errors[key] = np.fft.irfft(bins, n=len(trace))
    for subset, x, title in ((early, 75, 'DC-20 kHz impulse error M, time (ms)'), (late, 650, 'Retained-state error M, time (ms)')):
        series = [[(float(r['sample']) / 48, float(band_errors[key][int(r['sample'])]) if x == 75 else float(r[key]) - float(r['reference'])) for r in subset] for key in chosen]
        add_chart(drawing, series, x, 135, 445, 285, title)
    for i, label in enumerate(chosen):
        x = 75 + i * 330
        drawing.add(Line(x, 60, x + 24, 60, strokeColor=COLORS[i], strokeWidth=2))
        drawing.add(String(x + 32, 56, label, fontName='Helvetica', fontSize=11, fillColor=INK))
    drawing.add(String(28, 20, 'Left uses the same finite-window DC-20 kHz mask (double FFT for plotting). No mean subtraction. Full-band traces remain in CSV.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, root, 'impulse')
    spectral = [r for r in read(root / 'close_high_spectrum.csv') if 20 <= float(r['frequency_hz']) <= 20000]
    drawing = canvas(1180, 530, 'Close-tone complex spectral error', '44.1 kHz, H scale 4. Coherent inputs near 19/20 kHz; errors versus converged analytic reference.')
    series = [[(float(r['frequency_hz']) / 1000, max(-170.0, float(r[key + '_error_dbr']))) for r in spectral] for key in chosen]
    add_chart(drawing, series, 75, 135, 1020, 285, 'Line error / reference RMS (dBr) versus frequency (kHz)', (0, 20), (-170, -40))
    for i, label in enumerate(chosen):
        x = 75 + i * 330
        drawing.add(Line(x, 60, x + 24, 60, strokeColor=COLORS[i], strokeWidth=2))
        drawing.add(String(x + 32, 56, label, fontName='Helvetica', fontSize=11, fillColor=INK))
    drawing.add(String(28, 20, 'Complex error includes coincident IMD/harmonic discrepancies; it is not a pure alias-power estimate. Display floor -170 dBr.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, root, 'spectrum')
    print('\n'.join(notes[8:17]))


if __name__ == '__main__':
    main(Path(sys.argv[1]))
