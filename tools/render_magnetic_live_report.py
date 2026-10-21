"""Render current shelf-free magnetic validation, CPU and listening results locally."""
import csv
import math
import json
import sys
from pathlib import Path

from render_magnetic_validation_report import canvas, save, add_chart, INK, COLORS
from reportlab.graphics.shapes import Rect, String, Line
from reportlab.lib.colors import HexColor


def rows(path):
    return list(csv.DictReader(path.open()))


def main(root):
    manifest = root / 'manifest.json'
    if manifest.exists() and json.loads(manifest.read_text()).get('model') != 'lumiere-voicing-workbench-v3':
        raise ValueError('Historical model data: keep its archived report instead of relabeling it as shelf-free.')
    validation = rows(root / 'live_validation.csv')
    cpu = rows(root / 'plugin_cpu.csv')
    listening = rows(root / 'listening.csv')
    assert len(validation) == 108 and len(cpu) == 8 and len(listening) == 9
    assert all(r['pass'] == '1' and r['solver_failures'] == '0' for r in validation)
    for row in validation:
        for key, value in row.items():
            if key != 'signal' and value:
                assert math.isfinite(float(value)), (key, value)
    rates = (44100, 48000, 96000, 192000)
    drawing = canvas(1200, 480, 'Shelf-free magnetic transformer: Release CPU',
                     'Complete PluginProcessor: compressor, magnetic stage, BA routing and meters; Amount 100%, default voicing, 256-frame blocks.')
    series = [[(float(r['rate_hz']) / 1000, float(r['realtime_percent'])) for r in cpu if int(r['channels']) == ch] for ch in (1, 2)]
    add_chart(drawing, series, 70, 140, 460, 220, 'Complete plugin: percentage of one core budget', (40, 200), (0, 100))
    drawing.add(Line(70, 360, 530, 360, strokeColor=HexColor('#a74b51'), strokeDashArray=[3, 3]))
    drawing.add(String(75, 365, '100% budget', fontSize=9, fillColor=INK))
    for x, label, color in ((70, 'Mono', COLORS[0]), (210, 'Stereo', COLORS[1])):
        drawing.add(Line(x, 103, x + 25, 103, strokeColor=color, strokeWidth=2))
        drawing.add(String(x + 32, 99, label, fontSize=10, fillColor=INK))
    drawing.add(String(230, 116, 'Host sample rate (kHz)', fontSize=10, fillColor=INK))
    drawing.add(String(650, 378, 'Fixed internal oversampling / host latency', fontSize=12, fontName='Helvetica-Bold', fillColor=INK))
    for i, rate in enumerate(rates):
        row = next(r for r in cpu if int(float(r['rate_hz'])) == rate and r['channels'] == '2')
        y = 330 - i * 52
        ms = float(row['latency']) / rate * 1000
        drawing.add(Rect(650, y - 8, ms * 80, 18, fillColor=HexColor('#d0e9df'), strokeColor=None))
        drawing.add(String(650, y + 18, f'{rate/1000:g} kHz: {row["factor"]}x, {row["latency"]} samples ({ms:.3f} ms)', fontSize=10, fillColor=INK))
    drawing.add(String(70, 40, 'Median of seven runs; stimulus generation included. Local medians are not worst-case deadline guarantees.', fontSize=10, fillColor=INK))
    save(drawing, root, 'cpu')

    names = list(dict.fromkeys(r['signal'] for r in validation))
    drawing = canvas(1200, 430, 'Shipped magnetic stage: independent high-rate validation',
                     'No emphasis shelves; Amount 100%; scalar constitutive references, analytic voltage input, independent FIR decimation.')
    for col, (rate, peak) in enumerate((r, p) for r in rates for p in (0.05, 1, 4)):
        drawing.add(String(225 + col * 77, 332, f'{rate/1000:g}k / {peak:g}', fontSize=8, fillColor=INK))
        for row, name in enumerate(names):
            record = next(r for r in validation if r['signal'] == name and int(float(r['rate_hz'])) == rate and float(r['peak']) == peak)
            x, y = 215 + col * 77, 300 - row * 26
            drawing.add(Rect(x, y - 5, 73, 23, fillColor=HexColor('#d0e9df'), strokeColor=None))
            drawing.add(String(x + 8, y + 2, f'{float(record["band_residual_dbr"]):.1f}', fontSize=9, fillColor=INK))
    for row, name in enumerate(names):
        drawing.add(String(28, 302 - row * 26, name, fontSize=10, fillColor=INK))
    drawing.add(String(28, 40, 'Cell: DC-20 kHz residual dBr. All grid cases pass, including applicable off-grid and tail checks. No hardware-fit claim.', fontSize=10, fillColor=INK))
    save(drawing, root, 'validation')

    notes = ['# Shelf-free magnetic transformer / workbench v3', '',
             'The old tanh/residual-ADAA audio stage and the LF emphasis shelf pair are removed from live and active offline processing. Shelf removal intentionally changes enabled transformer sound. The internal magnetic constitutive tanh remains; its hot evaluation uses a preparation-built polynomial table. Legacy voicing JSON migrates drive/selection/flags and ignores obsolete shelves with a visible notice. The current workbench is descriptor-driven.', '',
             '**The full shipped-stage grid passes, with zero solver failures.** It uses the shelf-free chain and Amount 100% (+12 dB drive), and compare actual float stage output with analytically driven, scalar-constitutive high-rate references.', '',
             '![Validation](validation.png)', '',
             f'Worst DC-20 kHz residual: **{max(float(r["band_residual_dbr"]) for r in validation):.2f} dBr**. Worst reference convergence residual: **{max(float(r["reference_residual_dbr"]) for r in validation):.2f} dBr**. Maximum shipped-stage solver iterations: **{max(int(r["maximum_iterations"]) for r in validation)}**. The scaled target remains 1e-13.', '',
             '## Complete-plugin Release CPU', '', '![CPU and latency](cpu.png)', '',
             '| Rate Hz | Channels | Factor | Host latency | One-core audio budget % |', '|---:|---:|---:|---:|---:|']
    for row in cpu:
        notes.append(f'| {row["rate_hz"]} | {row["channels"]} | {row["factor"]}x | {row["latency"]} | {float(row["realtime_percent"]):.1f} |')
    notes += ['', 'Seven 16,384-frame runs, median, after prepare; block size 256; low-tone input at 60/73 Hz. Includes stimulus generation, full processor, compressor, magnetic stage, BA routing and meter publication. Preparation and file output are excluded. Profiling runs do not overlap builds or tests. This is a local signal-specific measurement, not a worst-case guarantee. Release is the listening build; Debug retains debugging overhead.', '',
              '## Listening', '',
              'Three synthetic stereo fixtures, six seconds, 48 kHz float32: bass phrase, percussion and low/high tones. Each has source, raw and linked whole-file RMS-matched linear-core, magnetic-core and shared live-magnetic renders. The raw core uses drive 0 dB; the live stage uses Amount 100% (+12 dB drive). Neither uses LF emphasis shelves. These settings differ deliberately; this is not an isolated hysteresis A/B. FIR delay is removed, magnetic phase remains, and no limiting or DC subtraction is applied. RMS matching is an audition aid, not perceptual loudness matching.', '',
              '| Synthetic fixture | Source | Linear core matched | Magnetic core matched | Live magnetic matched |', '|---|---|---|---|---|']
    for name in ('synthetic_bass', 'synthetic_percussion', 'synthetic_low_high'):
        files = [f'{name}_source.wav'] + [f'{name}_{model}_matched.wav' for model in ('linear', 'magnetic16', 'live_magnetic')]
        notes.append('| ' + name + ' | ' + ' | '.join(f'[WAV]({file})' for file in files) + ' |')
    notes += ['', 'The archived tanh recordings/preference remain historical evidence only. No current renderer executes that audio model.', '',
              '## Limits and data', '',
              'This is a normalized, uncalibrated voltage-driven magnetic circuit, not a VTB1148 fit. The simplified model omits winding capacitance, leakage and eddy-current dynamics. The tested grid covers nine waveforms, four rates and input peak bounds 0.05/1/4; it does not certify arbitrary inputs or every voicing. Core voltage safety limits and the saturated-field extension remain explicit numerical definitions.', '',
              'The previous extreme-parameter trajectories converge at the unchanged 1e-13 target, with a carried-slope predictor and bounded 96-evaluation fallback. The degree-five constitutive lookup is checked against the scalar function; maximum observed value error is at most 2e-15. Model equations are unchanged by that numerical acceleration.', '',
              'The separate extended probe covers combined drive/calibration extremes and instantaneous reversals. [Probe results](solver_extremes.csv). Build/test outcomes are recorded in machine-readable provenance; Benchmarks is excluded.', '',
              '[Implementation and reproduction](../../MagneticTransformerLive.md). [Shipped-stage grid](live_validation.csv), [plugin CPU](plugin_cpu.csv), [raw-core supplementary comparisons](comparison.csv), [raw-core convergence](convergence.csv), [tone measurements](tones.csv), [listening gains](listening.csv), [provenance](manifest.json).', '']
    (root / 'README.md').write_text('\n'.join(notes))


if __name__ == '__main__':
    main(Path(sys.argv[1]))
