#!/usr/bin/env python3
"""Audit Step 1 exports; no DSP edits, resynthesis, dependencies or listening claims."""
import argparse
import array
import struct
import sys
import csv
import cmath
import hashlib
import itertools
import json
import math
from pathlib import Path
import platform
import runpy
import statistics


def rows(root, name):
    with (root / name).open() as stream:
        result = list(csv.DictReader(stream))
    assert result and all(r['kind'] == 'measurement' for r in result), name
    return result


def span(values):
    return [min(values), max(values)] if values else None


def coherent_floor(bin_index, size=65536):
    values = [complex(.8 * math.sin(2 * math.pi * bin_index * n / size)) for n in range(size)]
    j = 0
    for i in range(1, size):
        bit = size >> 1
        while j & bit: j ^= bit; bit >>= 1
        j ^= bit
        if i < j: values[i], values[j] = values[j], values[i]
    width = 2
    while width <= size:
        root = cmath.exp(-2j * math.pi / width)
        for start in range(0, size, width):
            phase = 1+0j
            for k in range(width // 2):
                a, b = values[start+k], phase * values[start+k+width//2]
                values[start+k], values[start+k+width//2] = a+b, a-b
                phase *= root
        width <<= 1
    result = []
    for low, high in zip((20., 250., 2000., 8000.), (250., 2000., 8000., 20000.)):
        power = math.fsum(2 * abs(values[k] / size)**2 for k in range(1, size//2) if low <= k * 48000 / size < high and k % bin_index != 0)
        result.append((low, high, 10 * math.log10(max(power, 1e-30))))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--baseline', type=Path, required=True)
    args = parser.parse_args()
    root = args.directory
    sines = rows(root, 'sines.csv')
    actual = {(r['model'], float(r['drive_db']), float(r['requested_hz']), int(r['factor']), float(r['band_low_hz'])) for r in sines}
    expected = set(itertools.product(('A', 'B'), (0., 6.), (55., 110., 440., 1000., 7001., 13001., 19001.), (4, 8, 16, 32), (20., 250., 2000., 8000.)))
    expected |= set(itertools.product(('shipped',), (0., 6.), (55., 110., 440., 1000., 7001., 13001., 19001.), (16,), (20., 250., 2000., 8000.)))
    assert actual == expected and len(sines) == len(actual), 'sine coverage'
    assert all(r['solver_failures'] == '0' and r['limited_inputs'] == '0' and r['invalid_inputs'] == '0' for r in sines)
    summary = {'kind': 'measurement-analysis', 'models': {}}
    with (root / 'input_numerical_floor.csv').open('w') as stream:
        output = csv.writer(stream)
        output.writerow(('kind', 'actual_hz', 'fundamental_bin', 'band_low_hz', 'band_high_hz', 'nonharmonic_dbfs'))
        for bin_index in sorted({int(r['fundamental_bin']) for r in sines}):
            for low, high, value in coherent_floor(bin_index):
                output.writerow(('measurement', bin_index * 48000 / 65536, bin_index, low, high, value))
    with (root / 'sine_summary.csv').open('w') as stream:
        out = csv.writer(stream)
        out.writerow(('kind', 'model', 'drive_db', 'factor', 'band_low_hz', 'band_high_hz', 'harmonic_dbfs_min', 'harmonic_dbfs_max', 'nonharmonic_dbfs_min', 'nonharmonic_dbfs_max', 'worst_residual_reference_dbfs', 'literal_all_nonharmonic_below_harmonic'))
        for model in ('A', 'B', 'shipped'):
            for drive, factor, low in itertools.product((0., 6.), (4, 8, 16, 32) if model != 'shipped' else (16,), (20., 250., 2000., 8000.)):
                group = [r for r in sines if r['model'] == model and float(r['drive_db']) == drive and int(r['factor']) == factor and float(r['band_low_hz']) == low]
                harmonic = [float(r['harmonic_dbfs']) for r in group]
                nonharmonic = [float(r['nonharmonic_dbfs']) for r in group]
                out.writerow(('measurement', model, drive, factor, low, group[0]['band_high_hz'], min(harmonic), max(harmonic), min(nonharmonic), max(nonharmonic), max(float(r['residual_to_reference_dbfs']) for r in group), all(float(r['nonharmonic_to_harmonic_db']) < 0 for r in group)))
            group = [r for r in sines if r['model'] == model]
            passing = [f for f in (4, 8, 16, 32) if model != 'shipped' and all(float(r['nonharmonic_to_harmonic_db']) < 0 for r in group if int(r['factor']) == f)]
            summary['models'][model] = {
                'literal_lowest_factor_below_harmonics_every_band': min(passing) if passing else None,
                'reference_64_128_dbr_range': span([float(r['reference_64_128_dbr']) for r in group]) if model != 'shipped' else None,
                'worst_period_difference_dbr': max(float(r['period_difference_dbr']) for r in group),
                'factor_worst_nonharmonic_dbfs': {str(f): max(float(r['nonharmonic_dbfs']) for r in group if int(r['factor']) == f) for f in (4, 8, 16, 32) if model != 'shipped' or f == 16},
                'ratio_limit': 'Literal band comparison includes bands with no measurable upper harmonic; null is not proof of audible aliasing.'}
    grid = rows(root, 'off_grid.csv')
    prior = rows(root.parent / 'module_spike', 'validation.csv')
    failed = {(r['variant'], r['signal'], float(r['peak'])) for r in prior if r['topology'] == 'shunt' and r['rate_hz'] == '192000' and r['factor'] == '8' and r['signal'] in ('low_high', 'close_high', 'six_tone') and float(r['off_grid_dbr']) > -80}
    actual = {(r['variant'], r['signal'], float(r['peak']), int(r['factor']), int(r['warm_frames'])) for r in grid}
    expected = {(*g, f, w) for g in failed for f in (1, 2, 4, 8) for w in (16384, 192000, 384000)}
    assert actual == expected and len(actual) == len(grid), 'off-grid coverage'
    assert all(r['solver_failures'] == '0' for r in grid)
    summary['conditioning'] = {str(w): {
        'off_grid_dbr_range': span([float(r['off_grid_dbr']) for r in grid if int(r['warm_frames']) == w]),
        'reference_off_grid_dbr_range': span([float(r['reference_off_grid_dbr']) for r in grid if int(r['warm_frames']) == w]),
        'all_original_off_grid_screens_pass': all(r['original_screen_pass'] == '1' for r in grid if int(r['warm_frames']) == w),
        'worst_reference_convergence_dbr': max(float(r['reference_64_128_dbr']) for r in grid if int(r['warm_frames']) == w)} for w in (16384, 192000, 384000)}
    renders = rows(root, 'renders.csv')
    expected = set(itertools.product(('bass', 'drums', 'vocal-like', 'log-sweep'), (0., 6.), ('A', 'B'), (4, 8, 16, 32)))
    expected |= set(itertools.product(('bass', 'drums', 'vocal-like', 'log-sweep'), (0., 6.), ('shipped',), (16,)))
    expected |= set(itertools.product(('bass', 'drums', 'vocal-like', 'log-sweep'), (0., 6.), ('input',), (0,)))
    actual = {(r['material'], float(r['drive_db']), r['model'], int(r['factor'])) for r in renders}
    assert actual == expected and len(actual) == len(renders), 'render coverage'
    assert all(r['solver_failures'] == '0' and r['invalid_inputs'] == '0' and r['limited_inputs'] == '0' for r in renders)
    wav_measurement = runpy.run_path(str(Path(__file__).with_name('review_reports.py')))['wav_measurement']
    errors = []
    with (root / 'renders_verified.csv').open('w') as stream:
        out = csv.writer(stream); out.writerow(('kind', 'file', 'measured_rms', 'measured_peak', 'rms_match_error_db'))
        for row in renders:
            fs, channels, frames, rms, peak = wav_measurement(root / row['file'])
            assert (fs, channels, frames) == (48000, 2, 384000 if row['material'] == 'log-sweep' else 48000)
            error = 20 * math.log10(rms / float(row['matched_rms']))
            assert abs(error) < .001 and peak < .901
            errors.append(abs(error)); out.writerow(('measurement', row['file'], rms, peak, error))
    summary['maximum_wav_rms_match_error_db'] = max(errors)
    original = next(r for r in renders if r['material'] == 'bass' and r['model'] == 'input' and r['drive_db'] == '0')
    data = (root / original['file']).read_bytes(); offset = 12; samples = array.array('f')
    while offset + 8 <= len(data):
        tag, length = struct.unpack_from('<4sI', data, offset)
        if tag == b'data': samples.frombytes(data[offset + 8:offset + 8 + length]); break
        offset += 8 + length + length % 2
    if sys.byteorder != 'little': samples.byteswap()
    summary['bass_input_note_boundary_steps'] = [
        {'kind': 'measurement', 'time_s': n / 48000, 'raw_left_step': (samples[2*n] - samples[2*n-2]) / float(original['applied_gain'])}
        for n in (12000, 24000, 36000)]
    convergence = rows(root, 'render_convergence.csv')
    summary['material_reference_64_128_dbr'] = convergence
    band_checks = rows(root, 'reference_band_check.csv')
    assert len(band_checks) == 16 and all(r['solver_failures'] == '0' for r in band_checks)
    summary['material_reference_band_dbr_range'] = span([float(r['reference_64_128_band_dbr']) for r in band_checks])
    summary['all_material_band_rms_references_below_minus100'] = all(float(r['reference_64_128_band_dbr']) <= -100 for r in band_checks)
    summary['reference_scope'] = 'Band RMS convergence only; no peak-error or general aliasing certification inferred.'
    tracks = rows(root, 'sweep_tracks.csv')
    corroborated = []
    # Single negative-slope STFT segments can be peak-association errors. Retain
    # them all but require five consecutive segments of the same predicted
    # internal-fold branch before describing a sustained ridge candidate.
    with (root / 'sweep_ridges.csv').open('w') as stream:
        out = csv.writer(stream)
        out.writerow(('kind', 'model', 'drive_db', 'factor', 'fold_harmonic', 'fold_image', 'start_s', 'end_s', 'start_hz', 'end_hz', 'peak_dbfs', 'minimum_excess_over_reference_db', 'segments'))
        keys = sorted({(r['model'], r['drive_db'], r['factor'], r['fold_harmonic'], r['fold_image']) for r in tracks if r['fold_candidate'] == '1'})
        for key in keys:
            group = sorted((r for r in tracks if r['fold_candidate'] == '1' and (r['model'], r['drive_db'], r['factor'], r['fold_harmonic'], r['fold_image']) == key), key=lambda r: float(r['time_s']))
            run = []
            def flush():
                if len(run) < 5: return
                row = ('measurement', *key, run[0]['time_s'], run[-1]['time_s'], run[0]['frequency_hz'], run[-1]['frequency_hz'], max(float(r['level_dbfs']) for r in run), min(float(r['level_dbfs']) - float(r['reference_level_dbfs']) for r in run), len(run))
                out.writerow(row); corroborated.append(row)
            for row in group:
                if run and (float(row['time_s']) - float(run[-1]['time_s']) > 1.5 * 512 / 48000 or float(row['frequency_hz']) >= float(run[-1]['frequency_hz'])):
                    flush(); run = []
                run.append(row)
            flush()
    summary['sustained_fold_ridge_candidates'] = corroborated
    bands = rows(root, 'material_bands.csv')
    summary['bass_high_band_raw_dbfs'] = [r for r in bands if r['material'] == 'bass' and r['band_low_hz'] == '8000']
    baseline = json.loads(args.baseline.read_text())
    assert all(Path(path).is_file() and hashlib.sha256(Path(path).read_bytes()).hexdigest() == sha for path, sha in baseline.items()), 'protected source changed'
    archive = Path('docs/measurements/module_spike')
    protected = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in archive.rglob('*') if p.is_file()}
    provenance = {'kind': 'measurement-provenance', 'date': '2026-10-22', 'platform': platform.platform(), 'scope': 'Step 1 shunt only; no model or shipped changes',
        'sample_rate_hz': 48000, 'sine_fft_frames': 65536, 'sine_peak': .8, 'sine_warm_frames': 65536,
        'drives_db': [0, 6], 'shipped_mapping': 'Amount 100; calibration -12/-6 dB; other voicing defaults; level match off',
        'grid_rate_hz': 192000, 'screen_db': -80, 'reference_screen_db': -100,
        'stft_frames': 4096, 'stft_hop': 512, 'stft_window': 'Hann', 'fold_track_floor_dbfs': -110, 'fold_reference_excess_db': 10,
        'fold_reference_note': 'Negative ridge candidates only, not an exhaustive alias detector; 64/128 full-waveform convergence reported separately',
        'verified_unchanged_source': baseline, 'preserved_spike_exports': protected,
        'research_files': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(Path('tests/modules').iterdir()) if p.is_file() and p.suffix in ('.h', '.cpp', '.py')},
        'benchmarks_built': False, 'release_plugins_deployed': False, 'subjective_listening_performed': False}
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (root / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__': main()
