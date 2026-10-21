"""Historical v1 artifact renderer only. Current captures use render_magnetic_live_report.py.
This script visualizes archived CSV/WAV data; it executes no audio model.
"""
import csv
import math
import sys
from pathlib import Path
import numpy as np

from render_magnetic_validation_report import canvas, save, add_chart, INK, COLORS
from reportlab.graphics.shapes import Rect, String, Line
from reportlab.lib.colors import HexColor

SIGNALS = ('low_high', 'close_high', 'six_tone', 'bandlimited_impulse', 'low_burst', 'high_burst', 'gaussian_pulse', 'bipolar_pulse', 'fast_attack_decay')
MODELS = ('linear', 'magnetic16', 'tanh_favorite')


def read(path):
    rows = list(csv.DictReader(path.open()))
    for row in rows:
        for key, value in row.items():
            if value and key not in ('signal', 'model', 'fixture'):
                assert math.isfinite(float(value)), (path, key, value)
    return rows


def main(root):
    data, tones, profile, listening = (read(root / f'{name}.csv') for name in ('comparison', 'tones', 'timing', 'listening'))
    assert len(data) in (24, 108) and len(tones) == 84 and len(profile) == 8 and len(listening) == 9
    assert len({(r['signal'], r['sample_rate_hz'], r['input_peak_linear']) for r in data}) == len(data)
    passes = sum(int(r['pass']) for r in data)
    converged = sum(int(r['converged']) for r in data)
    notes = ['# Offline transformer audio prototype v1', '',
             f'**{passes}/{len(data)} numerical comparison screens pass; {converged}/{len(data)} DC-20 kHz references converge.**', '',
             'A new normalized voltage-driven circuit couples the existing play/Preisach constitutive law to audio. It is linked only into Tests. The live plugin, saved favorite, controls, editor and meters are unchanged.', '',
             'The coupled circuit uses 16x FIR oversampling and an implicit trapezoidal solve. It does not reuse the standalone quartic field reconstruction inside feedback: that kernel requires future field samples, while this circuit must solve the field. These are fresh end-to-end measurements, not inherited H-to-M scores.', '',
             '![Comparison grid](validation.png)', '',
             '| Signal | Pass / cases | Worst band residual dBr | Worst off-grid dBr | Worst linear calibration dBr |', '|---|---:|---:|---:|---:|']
    for signal in SIGNALS:
        subset = [r for r in data if r['signal'] == signal]
        if not subset:
            continue
        off = [float(r['off_grid_dbr']) for r in subset if r['off_grid_dbr']]
        notes.append(f"| {signal} | {sum(int(r['pass']) for r in subset)}/{len(subset)} | {max(float(r['band_residual_dbr']) for r in subset):.2f} | {max(off):.2f} | {max(float(r['linear_calibration_dbr']) for r in subset):.2f} |" if off else
                     f"| {signal} | {sum(int(r['pass']) for r in subset)}/{len(subset)} | {max(float(r['band_residual_dbr']) for r in subset):.2f} | n/a | {max(float(r['linear_calibration_dbr']) for r in subset):.2f} |")
    stereo = {int(float(r['sample_rate_hz'])): float(r['realtime_percent']) for r in profile if r['channels'] == '2'}
    notes += ['', '## What this establishes', '',
              f"The default coupled model's worst DC-20 kHz residual is {max(float(r['band_residual_dbr']) for r in data):.2f} dBr against the -80 dBr engineering screen. The same grid's worst linear calibration error is {max(float(r['linear_calibration_dbr']) for r in data):.2f} dBr, so the smallest residuals should be read with that reconstruction/numerical floor in mind. These are default-setting results, not a guarantee for all tunings or audio.", '',
              f"**Keep this implementation offline.** Stereo processing consumes {stereo[48000]:.1f}% of one core's audio-time budget at 48 kHz, {stereo[96000]:.1f}% at 96 kHz and {stereo[192000]:.1f}% at 192 kHz in the stated local benchmark. The solver provides a working reference and listening prototype; optimization is required before live integration.", '',
              'The first listening comparison is about model behavior, not matching the favorite tanh tone. The circuit has its own low-frequency response and magnetic history; no attempt has been made to force its harmonics onto the favorite preset.']
    notes += ['', '## Model and calibration', '',
              'The simplified circuit represents finite source/winding resistance, a nonlinear magnetizing branch, and ideal normalized 1:1 transformer coupling with a resistive referred load absorbed into constants. Fixed insertion loss is normalized out. It omits leakage inductance, winding capacitance, eddy-current dynamics and geometry/electrical calibration. It is not a VTB1148 model or a fit to a named transformer.', '',
              '`B = H + k*M`, `D*x = H + tau*dB/dt`, `y = x - H/D`. Thus output is induced voltage, not M. Quiet input uses a controlled 5 Hz high-pass response; high-frequency small-signal gain approaches unity. Remanence may persist with zero output voltage. No extra DC blocker or DC subtraction hides it.', '',
              'Defaults: D = 40 times the drive calibration gain, k = 4, memory = 0.35, width = 1; the initial differential flux slope sets tau to give a 5 Hz small-signal pole. All new tuning and solver bounds are centralized in source/experimental/TransformerAudioSettings.h. Drive, corner and magnetic weighting are private prototype settings, not host parameters.', '',
              'The canonical constitutive law still limits its H input to +/-32. The circuit retains its linear H contribution outside that range while the M contribution saturates; this is an explicit numerical extension, not identified hardware behavior. Input is limited to +/-16 for safety; nonfinite samples become zero excitation without clearing history. Inputs above that safety range are not measurement targets.', '',
              '## Numerical method', '',
              'Trial queries read the prior play states without modifying them. A monotone scalar equation is solved with safeguarded Newton steps and a fixed bisection fallback, capped at 48 evaluations. Only the accepted field commits the canonical history. The equation residual is divided by max(1,abs(H)); the threshold is 1e-13. Diagnostics record failures and the worst iteration/residual values.', '',
              'The 108-case full grid reuses the prior nine waveform shapes at 44.1/48/96/192 kHz and nominal linear input peak bounds 0.05/1/4 (about -26/0/+12 dBFS). The input_peak_linear column records that configured scale; actual sampled peaks depend on waveform and timing. They now drive voltage x, not prescribed H. The Dirichlet impulse includes periodic sidelobes and prehistory. Each render starts demagnetized, warms for one second, then captures 16,384 samples and flushes the filters.', '',
              'Reference voltage is evaluated analytically at 32x then 64x, extended through 128x/256x/512x if necessary; every reference solves the same circuit at that rate and uses real FIR decimation. Reference input interpolation is independent of the candidate. Convergence requires DC-20 kHz residual <= -100 dBr, band maximum error <= 1e-4 and applicable tail mean error <= -100 dBr. The final convergence step is in comparison.csv and every attempt in convergence.csv.', '',
              'FIR delays are removed with the previously tested 257-tap fractional alignment filter. Circuit phase remains intact. Linear-circuit candidate/reference renders calibrate combined input reconstruction, integration and alignment (goal <= -95 dBr). The linear time-domain circuit is independently tested against its exact bilinear high-pass transfer function.', '',
              'Candidate screens: converged reference, calibration <= -95 dBr, band residual <= -80 dBr, applicable off-grid power <= -80 dBr, absolute band peak error <= 0.1 dB, applicable tail mean error <= -80 dBr, and zero solver failures. The output can physically decay while stored flux relaxes; output tail drift is diagnostic, not required to be zero as in the standalone constitutive test. Full-band residual and separate full-band agreement remain diagnostics. No full-Nyquist or arbitrary-signal alias-elimination claim follows.', '',
              '## Tone measurements', '',
              'Seven nominal frequencies (20/50/60/200/1k/5k/8k Hz), four input peaks (-60/-24/-6/+6 dBFS), and three models. Actual tones are coherent odd FFT bins and are exported. H2/H3/H5/H7 are relative to the fundamental; harmonics at/above Nyquist are blank. Measurements have no RMS matching. The favorite tanh stage uses Amount 100%, calibration -6.1 dB, emphasis +6.1 dB and corner 103 Hz, with its live implementation unchanged.', '',
              '![Gain and harmonics](tones.png)', '',
              '| Nominal frequency Hz | Magnetic gain at -60 dBFS | Linear gain at -60 dBFS |', '|---:|---:|---:|']
    for frequency in (20, 50, 60, 200, 1000, 5000, 8000):
        values = {r['model']: float(r['fundamental_gain_db']) for r in tones if float(r['requested_hz']) == frequency and float(r['input_dbfs']) == -60}
        notes.append(f"| {frequency} | {values['magnetic16']:+.5f} | {values['linear']:+.5f} |")
    notes += ['', '## Listening fixtures', '',
              'Three synthetic stereo fixtures (bass phrase, percussion and low/high tones), 48 kHz / 32-bit float / six seconds, including silence before the signal and a two-second decay tail. These are generated test material, not recordings of instruments, voices or hardware. No user recordings were supplied. Each source has linear-reference, magnetic16 and favorite-tanh renders, in raw and whole-file linked-RMS-matched versions. FIR latency is compensated, physical phase remains, and matching gains/peaks are exported. RMS matching is an audition aid, not perceptual loudness normalization.', '',
              'Comparing magnetic16 with the favorite tanh changes the complete transformer model, linear response and antialiasing method; it does not isolate hysteresis alone. Use the matched linear reference to assess the magnetic contribution in the same coupling chain. No waveform limiting or DC removal is applied to listening output.', '',
              '| Fixture | Source | Linear matched | Magnetic matched | Favorite tanh matched |', '|---|---|---|---|---|']
    for prefix in ('synthetic_bass', 'synthetic_percussion', 'synthetic_low_high'):
        files = [f'{prefix}_source.wav'] + [f'{prefix}_{model}_matched.wav' for model in MODELS]
        notes.append('| ' + prefix + ' | ' + ' | '.join(f'[WAV]({name})' for name in files) + ' |')
    notes += ['', '## Release CPU and latency', '',
              'Median of five 16,384-frame mono/stereo runs on this Mac; includes 16x FIR, circuit, copies and stimulus generation, excludes prepare/FFT/file output. A frame contains every channel. The budget percentage is elapsed processing time / audio duration on one core. The timing CSV solver_failures column is channel-0 diagnostics; elapsed timing covers all channels. This is not a worst-case deadline guarantee; it covers the stated test signal only.', '',
              '| Rate Hz | Channels | ns / base frame | us / 256 frames | Audio-time budget % |', '|---:|---:|---:|---:|---:|']
    for r in profile:
        notes.append(f"| {float(r['sample_rate_hz']):g} | {r['channels']} | {float(r['ns_per_base_frame']):.1f} | {float(r['us_per_256_frames']):.1f} | {float(r['realtime_percent']):.1f} |")
    latencies = {float(r['latency_samples']) for r in data}
    assert len(latencies) == 1
    notes += ['', f"The offline FIR chain delay is {next(iter(latencies)):.6f} base samples; it is removed from comparisons/listening files. This prototype adds no live plugin latency.", '',
              '## Impulse behavior', '', '![Impulse and reference error](impulse.png)', '',
              '## Outcomes and limits', '',
              f"Worst scaled solver residual: {max(float(r['maximum_scaled_equation_residual']) for r in data):.3g}; largest solver iteration count: {max(int(r['maximum_solver_iterations']) for r in data)}. Full-band reference agreement at the chosen convergence step holds in {sum(int(r['reference_full_agreement']) for r in data)}/{len(data)} cases; that is separate from DC-20 kHz convergence.", '',
              'The tests cover non-mutating trial queries versus the original core, committed-state circuit equations, analytic linear gain/phase, small-signal response, DC rejection with retained remanence, parameter/input extremes, mono/stereo isolation, reset and block sizes 1 through 4096. Existing plugin/audio regressions also apply.', '',
              'No numerical reference agreement proves a hardware fit or a preferred sound. Source/load values, winding geometry, realistic high-frequency parasitics, hardware calibration and listening-led tuning remain work. Solver performance and model behavior need evaluation beyond this default setting before live integration.', '',
              'Data: [comparisons](comparison.csv), [convergence](convergence.csv), [tones](tones.csv), [listening gains](listening.csv), [CPU](timing.csv), [impulse trace](impulse.csv), [provenance](manifest.json). Model derivation, primary references and reproduction commands: [TransformerAudioPrototype.md](../../TransformerAudioPrototype.md).', '']
    misses = [r for r in data if not int(r['pass'])]
    if misses:
        notes += ['## Screen misses', '', '| Signal | Rate | Peak | Residual dBr | Calibration dBr | Converged |', '|---|---:|---:|---:|---:|---:|']
        for r in misses:
            notes.append(f"| {r['signal']} | {r['sample_rate_hz']} | {r['input_peak_linear']} | {float(r['band_residual_dbr']):.2f} | {float(r['linear_calibration_dbr']):.2f} | {r['converged']} |")
    (root / 'README.md').write_text('\n'.join(notes))

    drawing = canvas(1180, 370, 'Voltage-driven transformer: fresh audio validation', f'{passes}/{len(data)} pass; {converged}/{len(data)} converged DC-20 kHz references. Offline circuit only; live plugin unchanged.')
    ordered = sorted(data, key=lambda r: (SIGNALS.index(r['signal']), float(r['sample_rate_hz']), float(r['input_peak_linear'])))
    for j, row in enumerate(ordered):
        x = 100 + j * 9
        color = '#268b77' if int(row['pass']) else '#d18137' if int(row['converged']) else '#8a939e'
        drawing.add(Rect(x, 175, 8.3, 45, fillColor=HexColor(color), strokeColor=None))
        if j % 12 == 0:
            drawing.add(String(x, 240, row['signal'].replace('bandlimited_', '')[:12], fontName='Helvetica', fontSize=8, fillColor=INK))
    drawing.add(String(100, 140, 'Each signal: 44.1/48/96/192 kHz, input peaks 0.05/1/4. Green pass; orange miss; gray reference unresolved.', fontName='Helvetica', fontSize=10, fillColor=INK))
    drawing.add(String(28, 35, 'Input voltage now drives a solved magnetic circuit. These results do not inherit the prior prescribed-field quartic scores.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, root, 'validation')

    drawing = canvas(1180, 540, 'Transformer audio: gain and odd harmonics', '48 kHz, tone near 50 Hz. Unmatched raw measurements; magnetic circuit versus its linear reference and favorite tanh.')
    for x, key, title, bounds in ((75, 'fundamental_gain_db', 'Fundamental gain dB versus input dBFS', None), (650, 'h3_dbc', 'Third harmonic dBc versus input dBFS', (-160, 0))):
        series = [[(float(r['input_dbfs']), max(-160, float(r[key]))) for r in tones if r['model'] == model and float(r['requested_hz']) == 50] for model in MODELS]
        add_chart(drawing, series, x, 140, 445, 280, title, (-60, 6), bounds)
    for i, label in enumerate(('Linear reference', 'Voltage-coupled magnetic16', 'Favorite tanh')):
        x = 75 + i * 330
        drawing.add(Line(x, 65, x + 24, 65, strokeColor=COLORS[i], strokeWidth=2))
        drawing.add(String(x + 32, 61, label, fontName='Helvetica', fontSize=11, fillColor=INK))
    drawing.add(String(28, 22, 'Different complete models, not isolated hysteresis toggles. Below-floor harmonics are limited by numerical precision.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, root, 'tones')

    trace = read(root / 'impulse.csv')
    drawing = canvas(1180, 540, 'Coupled impulse: output and reference error', '48 kHz, nominal input peak 1. Periodic Dirichlet input sidelobes are retained; field/flux relaxation can produce a physical tail.')
    center = float(max(trace, key=lambda r: abs(float(r['source'])))['time_ms'])
    zoom = [r for r in trace if center - 1 <= float(r['time_ms']) <= center + 3]
    add_chart(drawing, [[(float(r['time_ms']), float(r[k])) for r in zoom] for k in ('candidate', 'reference', 'linear')], 75, 140, 445, 280, 'Output voltage versus time (ms)')
    error = np.array([float(r['error']) for r in trace])
    bins = np.fft.rfft(error)
    bins[int(20000 * len(trace) / 48000) + 1:] = 0
    band = np.fft.irfft(bins, n=len(trace))
    add_chart(drawing, [[(float(r['time_ms']), float(band[i])) for i, r in enumerate(trace)]], 650, 140, 445, 280, 'DC-20 kHz voltage error versus time (ms)')
    for i, label in enumerate(('Magnetic16', 'Converged reference', 'Linear reference')):
        x = 75 + i * 330
        drawing.add(Line(x, 65, x + 24, 65, strokeColor=COLORS[i], strokeWidth=2))
        drawing.add(String(x + 32, 61, label, fontName='Helvetica', fontSize=11, fillColor=INK))
    drawing.add(String(28, 22, 'No DC or mean removal. Error plot uses the same finite-window DC-20 kHz mask with a double FFT; measured metrics remain in CSV.', fontName='Helvetica', fontSize=10, fillColor=INK))
    save(drawing, root, 'impulse')
    print(notes[2])


if __name__ == '__main__':
    main(Path(sys.argv[1]))
