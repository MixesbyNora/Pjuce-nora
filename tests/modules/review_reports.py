#!/usr/bin/env python3
"""Local, dependency-free audit of the isolated spike's exported measurements.

Run only after all exports finish. Does not render, benchmark or modify DSP.
Numeric output is measurement analysis, not a listening or architecture verdict.
"""
import argparse
import array
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import platform
import statistics
import struct
import sys


def rows(root, name):
    with (root / name).open() as stream:
        result = list(csv.DictReader(stream))
    assert result and all(row['kind'] == 'measurement' for row in result), name
    return result


def wav_measurement(path):
    data = path.read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', path
    offset = 12
    fmt = payload = None
    while offset + 8 <= len(data):
        tag, length = struct.unpack_from('<4sI', data, offset)
        chunk = data[offset + 8:offset + 8 + length]
        if tag == b'fmt ': fmt = chunk
        if tag == b'data': payload = chunk
        offset += 8 + length + length % 2
    assert fmt and payload, path
    encoding, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', fmt)
    assert bits == 32 and (encoding == 3 or encoding == 65534 and fmt[24:28] == b'\x03\x00\x00\x00'), path
    samples = array.array('f')
    samples.frombytes(payload)
    if sys.byteorder != 'little': samples.byteswap()
    assert all(math.isfinite(x) for x in samples), path
    rms = math.sqrt(math.fsum(x * x for x in samples) / len(samples))
    return rate, channels, len(samples) // channels, rms, max(map(abs, samples))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--cpu-model', default='not recorded')
    args = parser.parse_args()
    root = args.directory
    validation = rows(root, 'validation.csv')
    actual = {(r['variant'], r['topology'], float(r['rate_hz']), int(r['channels']), r['signal'], float(r['peak']), int(r['factor'])) for r in validation}
    expected = set(itertools.product(('A', 'B'), ('shunt', 'series'), (44100., 48000., 96000., 192000.), (1, 2),
        ('low_high', 'close_high', 'six_tone', 'bandlimited_impulse', 'low_burst', 'high_burst', 'gaussian_pulse', 'bipolar_pulse', 'fast_attack_decay'),
        (0.05, 1., 4.), (1, 2, 4, 8)))
    assert actual == expected and len(actual) == len(validation), 'incomplete or duplicated validation matrix'
    assert all(r['solver_failures'] == '0' and r['stereo_exact'] == '1' for r in validation)
    summary = {'kind': 'measurement-analysis', 'validation_coverage_complete': True, 'models': {}}
    for variant, topology in itertools.product(('A', 'B'), ('shunt', 'series')):
        group = [r for r in validation if r['variant'] == variant and r['topology'] == topology]
        passing = [f for f in (1, 2, 4, 8) if all(r['pass'] == '1' for r in group if int(r['factor']) == f)]
        summary['models'][f'{variant}_{topology}'] = {
            'lowest_certified_factor': min(passing) if passing else None,
            'all_references_converged': all(r['reference_converged'] == '1' for r in group),
            'worst_reference_band_dbr': max(float(r['reference_band_dbr']) for r in group),
            'factor_screens': {str(f): {'all_pass': all(r['pass'] == '1' for r in group if int(r['factor']) == f),
                'worst_band_dbr': max(float(r['band_dbr']) for r in group if int(r['factor']) == f)} for f in (1, 2, 4, 8)},
            'maximum_iterations': max(int(r['maximum_iterations']) for r in group),
        }
    coupling = rows(root, 'coupling.csv')
    actual = {(r['variant'], r['topology'], float(r['source_ohms']), float(r['load_ohms']), float(r['input_peak']), r['mode']) for r in coupling}
    expected = set(itertools.product(('A', 'B'), ('shunt', 'series'), (10., 100., 1000., 10000., 100000.), (1000., 10000., 100000.), (0.05, 0.7, 4.), ('1', '2', '2b', '3-offline')))
    assert actual == expected and len(actual) == len(coupling), 'incomplete coupling sweep'
    # The optional offline oracle has an explicit effort cap. Its failed rows
    # must be invalidated, never silently treated as accuracy measurements.
    assert all(r['solver_failures'] == '0' or r['mode'] == '3-offline' and r['reference_valid'] == '0' for r in coupling)
    summary['invalid_coupling_reference_settings'] = [r for r in coupling if r['mode'] == '3-offline' and r['reference_valid'] == '0']
    summary['coupling'] = {}
    for mode in ('1', '2', '2b'):
        group = [r for r in coupling if r['mode'] == mode]
        valid = [r for r in group if r['reference_valid'] == '1']
        values = [float(r['difference_dbr']) for r in valid]
        summary['coupling'][mode] = {
            'all_references_converged': all(r['reference_valid'] == '1' for r in group),
            'valid_reference_error_dbr_min_median_max': [min(values), statistics.median(values), max(values)] if values else None,
            'minimum_error_setting': min(valid, key=lambda r: float(r['difference_dbr'])) if valid else None,
            'maximum_error_setting': max(valid, key=lambda r: float(r['difference_dbr'])) if valid else None,
        }
    # A diagnostic onset bracket, not a hardware calibration or pass criterion.
    with (root / 'onset.csv').open('w') as stream:
        output = csv.writer(stream)
        output.writerow(('kind', 'variant', 'topology', 'source_ohms', 'load_ohms', 'mode', 'diagnostic_h3_threshold_dbc', 'last_below_peak', 'first_at_or_above_peak', 'status', 'reference_grid_valid'))
        for v, t, resistance, load, mode in itertools.product(('A', 'B'), ('shunt', 'series'), (10., 100., 1000., 10000., 100000.), (1000., 10000., 100000.), ('1', '2', '2b', '3-offline')):
            group = sorted((r for r in coupling if r['variant'] == v and r['topology'] == t and float(r['source_ohms']) == resistance and float(r['load_ohms']) == load and r['mode'] == mode), key=lambda r: float(r['input_peak']))
            valid = all(r['reference_valid'] == '1' for r in group)
            if mode == '3-offline' and not valid:
                output.writerow(('measurement-analysis', v, t, resistance, load, mode, -40, '', '', 'invalid_reference_grid', False))
                continue
            previous = ''
            for row in group:
                if float(row['h3_dbc']) >= -40:
                    output.writerow(('measurement-analysis', v, t, resistance, load, mode, -40, previous, row['input_peak'], 'bracketed' if previous else 'at_or_below_minimum', valid))
                    break
                previous = row['input_peak']
            else:
                output.writerow(('measurement-analysis', v, t, resistance, load, mode, -40, previous, '', 'not_reached', valid))
    latency = rows(root, 'latency.csv')
    summary['latency_maximum_error_samples'] = max(abs(float(r['reported_samples']) - float(r['identity_centroid_samples'])) for r in latency)
    assert summary['latency_maximum_error_samples'] < 1e-7 and all(r['off_exact'] == '1' for r in latency)
    swaps = rows(root, 'swaps.csv')
    summary['swap_maximum_excess_step_by_event'] = {str(event): max(float(r['maximum_excess_step']) for r in swaps if int(r['event']) == event) for event in range(5)}
    cpu = rows(root, 'cpu.csv')
    assert all(r['solver_failures'] == '0' for r in cpu)
    summary['stereo_neutral_cpu'] = [r for r in cpu if r['channels'] == '2' and r['workload'] == '0' and r['coupling'] in ('transformer', 'none')]
    shared = rows(root, 'shared.csv')
    savings = []
    for v, t, rate in itertools.product(('A', 'B'), ('shunt', 'series'), (48000., 96000., 192000.)):
        group = {r['configuration']: r for r in shared if r['variant'] == v and r['topology'] == t and float(r['rate_hz']) == rate}
        separate, one = group['separate'], group['shared']
        savings.append(100 * (1 - float(one['median_block_ns']) / float(separate['median_block_ns'])))
    summary['shared_cpu_savings_percent_range'] = [min(savings), max(savings)]
    summary['shared_latency_samples'] = sorted({(float(r['latency_samples']), r['configuration']) for r in shared})
    renders = rows(root, 'renders.csv')
    matching = []
    with (root / 'renders_verified.csv').open('w') as stream:
        output = csv.writer(stream)
        output.writerow(('kind', 'file', 'measured_rms', 'measured_peak', 'rms_match_error_db'))
        for row in renders:
            rate, channels, frames, rms, peak = wav_measurement(root / row['file'])
            assert (rate, channels, frames) == (48000, 2, 48000)
            error = 20 * math.log10(rms / float(row['matched_rms']))
            assert abs(error) < .001 and peak < .901
            matching.append(abs(error))
            output.writerow(('measurement', row['file'], rms, peak, error))
    summary['render_maximum_rms_match_error_db'] = max(matching)
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    protected = {}
    if args.baseline:
        protected = json.loads(args.baseline.read_text())
        assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in protected.items()), 'protected source changed'
    provenance = {'kind': 'measurement-provenance', 'date_local': '2026-10-22', 'machine': platform.machine(), 'cpu_model': args.cpu_model, 'os': platform.mac_ver()[0],
        'protected_source_verified': bool(protected), 'protected_source_digests': protected,
        'research_source_digests': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('source/modules', 'tests/modules') for p in Path(folder).rglob('*') if p.is_file()},
        'method': {'cpu_block_frames': 256, 'cpu_timed_blocks': 1024, 'cpu_warm_blocks': 32, 'cpu_p999_rank': 1023,
            'cpu_scope': 'process plus diagnostic reset; shipped row also includes float/double copies; input generation excluded',
            'validation_window_frames': 16384, 'periodic_warm_frames': 16384, 'transient_warm_frames': 1024,
            'reference_factors': [32, 64, 128, 256, 512], 'coupling_reference_factors': [32, 64, 128, 256],
            'coupling_rate_hz': 48000, 'coupling_bass_hz': round(60 * 16384 / 48000) * 48000 / 16384,
            'coupling_probe_hz': round(2000 * 16384 / 48000) * 48000 / 16384,
            'coupling_onset_diagnostic_h3_dbc': -40,
            'waveforms': 'deterministic synthetic material, not recordings; no listening verdict', 'curve_impedance': 'unimplemented; scalar only',
            'reference_limit': 'Failed/unconverged offline coupling rows are invalid; no null accuracy claim for those settings.'}}
    (root / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(json.dumps({key: value for key, value in summary.items() if key not in ('stereo_neutral_cpu', 'coupling')}, indent=2))


if __name__ == '__main__':
    main()
